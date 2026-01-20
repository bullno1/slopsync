// vim: set foldmethod=marker foldlevel=0:
#include <slopsync/server.h>
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
	ssync_timestamp_t last_incoming_snapshot_time;
	ssync_timestamp_t last_outgoing_snapshot_time;
	ssync_snapshot_t* last_acked_snapshot;
	ssync_snapshot_pool_t incoming_archive;
	ssync_snapshot_pool_t outgoing_archive;
} ssyncd_player_info_t;

struct ssyncd_s {
	ssyncd_config_t config;

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
	bitstream_out_t* packet_out_stream, bsv_ctx_t* bsv_packet_out
) {
	ssync_timestamp_t current_time_ms = (ssync_timestamp_t)(ssyncd->current_time_s * 1000.0);

	ssync_snapshot_info_record_t snapshot_info = {
		.current_time = current_time_ms,
	};
	const ssync_snapshot_t* remote_snapshot = player->incoming_archive.next;
	if (remote_snapshot != NULL) {
		snapshot_info.last_received = remote_snapshot->timestamp;
	}
	ssync_write_record_type(packet_out_stream, SSYNC_RECORD_TYPE_SNAPSHOT_INFO);
	bsv_ssync_snapshot_info_record(bsv_packet_out, &snapshot_info);

	bool has_space = true;
	ssync_snapshot_t* snapshot = ssync_acquire_snapshot(&ssyncd->snapshot_pool, current_time_ms, ssyncd);
	snapshot->remote = remote_snapshot;

	const ssync_snapshot_t* base_snapshot = player->last_acked_snapshot;
	ssync_snapshot_t* tmp_snapshot = NULL;
	if (base_snapshot == NULL) {
		base_snapshot = tmp_snapshot = ssync_acquire_snapshot(&ssyncd->snapshot_pool, 0, ssyncd);
	}

	// Created objects since the last snapshot
	bhash_index_t num_objects = bhash_len(&ssyncd->objects);
	for (bhash_index_t i = 0; has_space && i < num_objects; ++i) {
		ssync_net_id_t obj_id = ssyncd->objects.keys[i];
		if (bhash_has(&base_snapshot->objects, obj_id)) { continue; }

		const ssyncd_obj_info_t* obj = &ssyncd->objects.values[i];
		if (obj->authority == player_id) { continue; }  // Don't send to owner

		// Write to a temp buffer first
		bitstream_out_t record_out_stream = {
			.data = ssyncd->record_buf,
			.num_bytes = ssyncd->config.max_message_size,
		};
		ssync_bsv_out_t bsv_record_out;
		bsv_ctx_t bsv_record_ctx = { .out = ssync_init_bsv_out(&bsv_record_out, &record_out_stream) };
		ssync_write_record_type(&record_out_stream, SSYNC_RECORD_TYPE_OBJ_CREATE);
		ssync_obj_create_record_t record = {
			.id = obj_id,
			.timestamp = obj->created_at,
			.flags = obj->flags,
		};
		bsv_ssync_obj_create_record(&bsv_record_ctx, &record);

		// Try appending to the packet
		bool written = bitstream_append(packet_out_stream, &record_out_stream);
		if (written) {
			// Put an empty object into the snapshot so when this snapshot is
			// ack-ed, we stop trying to resend object creation data
			ssync_obj_t empty_obj = { 0 };
			bhash_put(&snapshot->objects, obj_id, empty_obj);
		}
		has_space &= written;
	}

	// Destroyed objects since the last snapshot
	bhash_index_t num_snapshotted_objects = bhash_len(&base_snapshot->objects);
	for (bhash_index_t i = 0; i < num_snapshotted_objects; ++i) {
		ssync_net_id_t id = base_snapshot->objects.keys[i];
		if (bhash_has(&ssyncd->objects, id)) { continue; }

		// Because we don't send an object to its owner, the fact that it appears
		// in a previous snapshot means this user does not own it
		if (has_space) {
			// Write to a temp buffer first
			bitstream_out_t record_out_stream = {
				.data = ssyncd->record_buf,
				.num_bytes = ssyncd->config.max_message_size,
			};
			ssync_bsv_out_t bsv_record_out;
			bsv_ctx_t bsv_record_ctx = { .out = ssync_init_bsv_out(&bsv_record_out, &record_out_stream) };
			ssync_write_record_type(&record_out_stream, SSYNC_RECORD_TYPE_OBJ_DESTROY);
			ssync_obj_destroy_record_t record = {
				.id = id,
				.timestamp = current_time_ms,
			};
			bsv_ssync_obj_destroy_record(&bsv_record_ctx, &record);

			// Try appending to the packet
			has_space &= bitstream_append(packet_out_stream, &record_out_stream);
		}

		if (!has_space) {
			// Put an empty object into the snapshot if not written so we
			// will try resending destruction data in the next snapshot
			ssync_obj_t empty_obj = { 0 };
			bhash_put(&snapshot->objects, id, empty_obj);
		}
	}

	// State change since the last snapshot
	ssync_obj_t empty_obj = { 0 };
	for (bhash_index_t i = 0; i < num_objects; ++i) {
		ssync_net_id_t id = ssyncd->objects.keys[i];
		const ssyncd_obj_info_t* obj_info = &ssyncd->objects.values[i];
		if (obj_info->authority == player_id) { continue; }  // Don't send to owner

		const ssync_obj_t* current_obj = &obj_info->data;
		const ssync_obj_t* previous_obj = bhash_get_value(&base_snapshot->objects, id);

		if (has_space) {
			const ssync_obj_t* diff_target = previous_obj != NULL ? previous_obj : &empty_obj;

			// TODO: This comparison can be skipped if the local copy of the snapshot
			// stores the update timestamp
			if (!ssync_obj_equal(&ssyncd->schema, current_obj, diff_target)) {
				// Write to a temp buffer first
				bitstream_out_t record_out_stream = {
					.data = ssyncd->record_buf,
					.num_bytes = ssyncd->config.max_message_size,
				};
				ssync_bsv_out_t bsv_record_out;
				bsv_ctx_t bsv_record_ctx = { .out = ssync_init_bsv_out(&bsv_record_out, &record_out_stream) };

				ssync_write_record_type(&record_out_stream, SSYNC_RECORD_TYPE_OBJ_UPDATE);
				bsv_ssync_net_id(&bsv_record_ctx, &id);
				ssync_write_obj_update(
					&bsv_record_ctx,
					&record_out_stream,
					&ssyncd->schema,
					current_obj, diff_target
				);

				// Try appending to the packet
				has_space &= bitstream_append(packet_out_stream, &record_out_stream);
			}
		}

		// Store the effective object for later diff
		ssync_obj_t copy = { 0 };
		if (has_space) {
			ssync_copy_obj(&copy, current_obj, ssyncd);
		} else if (previous_obj != NULL) {
			ssync_copy_obj(&copy, previous_obj, ssyncd);
		}
		bhash_put(&snapshot->objects, id, copy);
	}

	if (tmp_snapshot != NULL) {
		ssync_release_snapshot(&ssyncd->snapshot_pool, tmp_snapshot);
	}
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
		return NULL;
	}

	ssync_obj_schema_t schema = { 0 };
	bitstream_in_t in_stream = { .data = raw_data, .num_bytes = raw_size };
	ssync_bsv_in_t bsv_in;
	bsv_ctx_t ctx = { .in = ssync_init_bsv_in(&bsv_in, &in_stream) };
	if (bsv_ssync_obj_schema(&ctx, &schema) != BSV_OK) {
		ssyncd_free(config, raw_data);
		return NULL;
	}
	ssyncd_free(config, raw_data);

	ssyncd_t* ssd = ssyncd_malloc(config, sizeof(ssyncd_t*));
	*ssd = (ssyncd_t){
		.config = *config,
		.players = ssyncd_malloc(config, sizeof(ssyncd_player_info_t) * config->max_num_players),
		.outgoing_packet_buf = ssyncd_malloc(config, config->max_message_size),
		.record_buf = ssyncd_malloc(config, config->max_message_size),
		.schema = schema,
	};
	memset(ssd->players, 0, sizeof(ssyncd_player_info_t) * config->max_num_players);

	bhash_config_t hconfig = bhash_config_default();
	hconfig.memctx = ssd;
	bhash_init(&ssd->objects, hconfig);

	return ssd;
}

void
ssyncd_cleanup(ssyncd_t* ssyncd) {
	for (int i = 0; i < ssyncd->config.max_num_players; ++i) {
		ssync_cleanup_snapshot_pool(&ssyncd->players[i].incoming_archive, ssyncd);
		ssync_cleanup_snapshot_pool(&ssyncd->players[i].outgoing_archive, ssyncd);
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
	player->username = NULL;
	ssync_release_archive(&ssyncd->snapshot_pool, &player->incoming_archive);
	ssync_release_archive(&ssyncd->snapshot_pool, &player->outgoing_archive);
	player->last_incoming_snapshot_time = player->last_outgoing_snapshot_time = 0;
	player->last_acked_snapshot = NULL;
	// TODO: destroy observers
	// TODO: handover non-observers
}

void
ssyncd_process_message(ssyncd_t* ssyncd, ssync_blob_t msg, int sender) {
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
