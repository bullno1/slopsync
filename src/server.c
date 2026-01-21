// vim: set foldmethod=marker foldlevel=0:
#include <slopsync/server.h>
#include <blog.h>
#include "internal.h"
#include "base64.h"

typedef struct {
	ssync_timestamp_t created_at;
	ssync_obj_flags_t flags;
	int authority;
	ssync_obj_t data;
} ssyncd_obj_info_t;

typedef struct {
	const char* username;
	uint16_t obj_id_bin;
	ssync_endpoint_t endpoint;
} ssyncd_player_info_t;

struct ssyncd_s {
	ssyncd_config_t config;
	ssync_endpoint_config_t endpoint_config;

	double current_time_s;

	uint16_t obj_id_bin;
	ssyncd_player_info_t* players;

	ssync_snapshot_pool_t snapshot_pool;
	void* outgoing_packet_buf;
	void* record_buf;

	ssync_obj_schema_t schema;

	BHASH_TABLE(ssync_net_id_t, ssyncd_obj_info_t) objects;
};

// Allocator {{{

void*
ssync_host_realloc(void* ptr, size_t size, void* ctx) {
	ssyncd_t* ssync = ctx;
	return ssync->config.realloc(ssync->config.userdata, ptr, size);
}

static void*
ssyncd_blib_realloc(void* ptr, size_t size, void* ctx) {
	return ssync_host_realloc(ptr, size, ctx);
}

static void*
ssyncd_realloc(const ssyncd_config_t* config, void* ptr, size_t size) {
	return config->realloc(config->userdata, ptr, size);
}

static void*
ssyncd_malloc(const ssyncd_config_t* config, size_t size) {
	return ssyncd_realloc(config, NULL, size);
}

static void*
ssyncd_free(const ssyncd_config_t* config, void* ptr) {
	return ssyncd_realloc(config, ptr, 0);
}

// }}}

// Snapshot {{{

static void
ssyncd_write_snapshot(
	ssyncd_t* ssyncd,
	int player_id, ssyncd_player_info_t* player,
	bitstream_out_t* packet_out_stream, bsv_ctx_t* packet_out_bsv
) {
	ssync_timestamp_t current_time_ms = (ssync_timestamp_t)(ssyncd->current_time_s * 1000.0);

	ssync_outgoing_snapshot_ctx_t ctx;
	ssync_begin_outgoing_snapshot(
		&ctx,
		&player->endpoint,
		current_time_ms,
		packet_out_stream, packet_out_bsv
	);

	bhash_index_t num_objs = bhash_len(&ssyncd->objects);
	for (bhash_index_t i = 0; i < num_objs; ++i) {
		ssync_net_id_t id = ssyncd->objects.keys[i];
		const ssyncd_obj_info_t* info = &ssyncd->objects.values[i];
		if (info->authority == player_id) { continue; }

		ssync_add_outgoing_obj(
			&ctx,
			info->created_at, info->flags,
			id, &info->data
		);
	}

	ssync_end_outgoing_snapshot(&ctx);
}

// }}}

ssyncd_t*
ssyncd_init(const ssyncd_config_t* config) {
	// Decode schema
	size_t raw_size = base64_decoded_size(config->obj_schema.size);
	if (raw_size == 0) { return NULL; }
	void* raw_data = ssyncd_malloc(config, raw_size);
	if (!base64_decode(config->obj_schema.data, config->obj_schema.size, raw_data)) {
		ssyncd_free(config, raw_data);
		BLOG_ERROR("Base64 decoding failed");
		return NULL;
	}

	ssync_obj_schema_t schema = { 0 };
	bitstream_in_t in_stream = { .data = raw_data, .num_bytes = raw_size };
	ssync_bsv_in_t bsv_in;
	bsv_ctx_t ctx = { .in = ssync_init_bsv_in(&bsv_in, &in_stream) };
	if (bsv_ssync_obj_schema(&ctx, &schema) != BSV_OK) {
		ssyncd_free(config, raw_data);
		BLOG_ERROR("Schema decoding failed");
		return NULL;
	}
	ssyncd_free(config, raw_data);

	ssyncd_t* ssd = ssyncd_malloc(config, sizeof(ssyncd_t));
	*ssd = (ssyncd_t){
		.config = *config,
		.players = ssyncd_malloc(config, sizeof(ssyncd_player_info_t) * config->max_num_players),
		.outgoing_packet_buf = ssyncd_malloc(config, config->max_message_size),
		.record_buf = ssyncd_malloc(config, config->max_message_size),
		.schema = schema,
	};
	memset(ssd->players, 0, sizeof(ssyncd_player_info_t) * config->max_num_players);
	ssd->endpoint_config = (ssync_endpoint_config_t){
		.max_message_size = config->max_message_size,
		.memctx = ssd,
		.record_buf = ssd->record_buf,
		.schema = &ssd->schema,
		.snapshot_pool = &ssd->snapshot_pool
	};

	ssync_reinit_snapshot_pool(&ssd->snapshot_pool, ssd);

	bhash_config_t hconfig = bhash_config_default();
	hconfig.memctx = ssd;
	bhash_init(&ssd->objects, hconfig);

	return ssd;
}

void
ssyncd_cleanup(ssyncd_t* ssyncd) {
	for (int i = 0; i < ssyncd->config.max_num_players; ++i) {
		ssync_cleanup_endpoint(&ssyncd->players[i].endpoint);
	}
	ssync_cleanup_snapshot_pool(&ssyncd->snapshot_pool, ssyncd);

	bhash_cleanup(&ssyncd->objects);

	ssyncd_free(&ssyncd->config, ssyncd->players);
	ssyncd_free(&ssyncd->config, ssyncd->outgoing_packet_buf);
	ssyncd_free(&ssyncd->config, ssyncd->record_buf);
	ssyncd_free(&ssyncd->config, ssyncd);
}

void
ssyncd_add_player(ssyncd_t* ssyncd, int id, const char* username) {
	// TODO: what if the bin counter overflow?
	ssyncd_player_info_t* player = &ssyncd->players[id];
	player->obj_id_bin = ++ssyncd->obj_id_bin;
	player->username = username;
	ssync_reinit_endpoint(&player->endpoint, &ssyncd->endpoint_config);

	BLOG_INFO("%s joined with id: %d and obj bin: %d", username, id, player->obj_id_bin);

	bitstream_out_t packet_out_stream = {
		.data = ssyncd->outgoing_packet_buf,
		.num_bytes = ssyncd->config.max_message_size,
	};
	ssync_bsv_out_t bsv_packet_out;
	bsv_ctx_t bsv_packet_ctx = {
		.out = ssync_init_bsv_out(&bsv_packet_out, &packet_out_stream)
	};

	ssync_init_record_t record = {
		.current_time = (ssync_timestamp_t)(ssyncd->current_time_s * 1000.0),
		.logic_tick_rate = ssyncd->config.logic_tick_rate,
		.net_tick_rate = ssyncd->config.net_tick_rate,
		.obj_id_bin = ssyncd->players[id].obj_id_bin,
		.player_id = id,
	};
	ssync_write_record_type(&packet_out_stream, SSYNC_RECORD_TYPE_INIT);
	bsv_ssync_init_record(&bsv_packet_ctx, &record);

	ssyncd_write_snapshot(ssyncd, id, player, &packet_out_stream, &bsv_packet_ctx);

	ssync_end_packet(&packet_out_stream);
	ssync_blob_t msg = {
		.data = ssyncd->outgoing_packet_buf,
		.size = (packet_out_stream.bit_pos + 7) / 8,
	};
	// The init message should be reliable
	ssyncd->config.send_msg(ssyncd->config.userdata, id, msg, true);
}

void
ssyncd_remove_player(ssyncd_t* ssyncd, int id) {
	ssyncd_player_info_t* player = &ssyncd->players[id];
	BLOG_INFO("%s left", player->username);

	player->username = NULL;
	ssync_cleanup_endpoint(&player->endpoint);
	// TODO: destroy observers
	// TODO: handover non-observers
}

void
ssyncd_process_message(ssyncd_t* ssyncd, ssync_blob_t msg, int player_id) {
	bitstream_in_t packet_stream = {
		.data = msg.data,
		.num_bytes = msg.size,
	};
	ssync_bsv_in_t bsv_in;
	bsv_ctx_t packet_bsv = { .in = ssync_init_bsv_in(&bsv_in, &packet_stream) };
	ssyncd_player_info_t* player = &ssyncd->players[player_id];

	ssync_incoming_packet_ctx_t ctx;
	ssync_begin_incoming_packet(&ctx, &player->endpoint, &packet_stream, &packet_bsv);

	while (true) {
		ssync_record_type_t record_type;
		if (!ssync_read_record_type(&packet_stream, &record_type)) {
			break;
		}

		switch ((ssync_record_type_t)record_type) {
			case SSYNC_RECORD_TYPE_END:
				goto end;
			case SSYNC_RECORD_TYPE_SNAPSHOT_INFO: {
				if (!ssync_process_snapshot_info_record(&ctx)) {
					goto end;
				}

				if (
					player->endpoint.last_acked_snapshot != NULL
					&&
					player->endpoint.last_acked_snapshot->remote != NULL
				) {
					ssync_release_after(
						&ssyncd->snapshot_pool,
						player->endpoint.last_acked_snapshot->remote
					);
				}
			} break;
			case SSYNC_RECORD_TYPE_OBJ_CREATE: {
				ssync_obj_create_record_t record;
				const ssync_obj_t* obj;
				if (!ssync_process_process_object_create_record(&ctx, &record, &obj)) {
					goto end;
				}

				if (record.id.bin != player->obj_id_bin) {
					ssync_discard_incoming_packet(&ctx);
					goto end;
				}

				bhash_alloc_result_t alloc_result = bhash_alloc(&ssyncd->objects, record.id);
				if (alloc_result.is_new) {
					ssyncd->objects.keys[alloc_result.index] = record.id;
					ssyncd->objects.values[alloc_result.index] = (ssyncd_obj_info_t){
						.authority = player_id,
						.created_at = (ssync_timestamp_t)(ssyncd->current_time_s * 1000.0),
						.flags = record.flags,
					};
					ssync_copy_obj(
						&ssyncd->objects.values[alloc_result.index].data,
						obj,
						ssyncd
					);
				}
			} break;
			case SSYNC_RECORD_TYPE_OBJ_DESTROY: {
				ssync_obj_destroy_record_t record;
				if (!ssync_process_process_object_destroy_record(&ctx, &record)) {
					goto end;
				}

				ssyncd_obj_info_t* obj_info = bhash_get_value(&ssyncd->objects, record.id);
				if (obj_info != NULL) {
					if (obj_info->authority == player_id) {
						ssync_cleanup_obj(&obj_info->data, ssyncd);
						bhash_remove(&ssyncd->objects, record.id);
					} else {
						ssync_discard_incoming_packet(&ctx);
						goto end;
					}
				}
			} break;
			case SSYNC_RECORD_TYPE_OBJ_UPDATE: {
				ssync_net_id_t id;
				ssync_obj_t* data;
				if (!ssync_process_object_update_record(&ctx, &id, &data)) {
					goto end;
				}

				ssyncd_obj_info_t* obj_info = bhash_get_value(&ssyncd->objects, id);
				if (obj_info == NULL || obj_info->authority != player_id) {
						ssync_discard_incoming_packet(&ctx);
						goto end;
				}
				ssync_copy_obj(&obj_info->data, data, ssyncd);
			} break;
			default:
				ssync_discard_incoming_packet(&ctx);
				goto end;
		}
	}
end:

	ssync_end_incoming_packet(&ctx);
}

void
ssyncd_update(ssyncd_t* ssyncd, double dt) {
	ssyncd->current_time_s += dt;
}

void
ssyncd_broadcast(ssyncd_t* ssyncd) {
	for (int i = 0; i < ssyncd->config.max_num_players; ++i) {
		ssyncd_player_info_t* player = &ssyncd->players[i];
		if (player->username == NULL) { continue; }

		bitstream_out_t packet_out_stream = {
			.data = ssyncd->outgoing_packet_buf,
			.num_bytes = ssyncd->config.max_message_size,
		};
		ssync_bsv_out_t bsv_packet_out;
		bsv_ctx_t bsv_packet_ctx = {
			.out = ssync_init_bsv_out(&bsv_packet_out, &packet_out_stream)
		};
		ssyncd_write_snapshot(ssyncd, i, player, &packet_out_stream, &bsv_packet_ctx);

		ssync_end_packet(&packet_out_stream);
		ssync_blob_t msg = {
			.data = ssyncd->outgoing_packet_buf,
			.size = (packet_out_stream.bit_pos + 7) / 8,
		};
		ssyncd->config.send_msg(ssyncd->config.userdata, i, msg, false);
	}
}

#define BLIB_REALLOC ssyncd_blib_realloc
#define BLIB_IMPLEMENTATION
#include <bsv.h>
#include <barray.h>
#include <bhash.h>
