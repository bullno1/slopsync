// vim: set foldmethod=marker foldlevel=0:
#define BSV_API static inline
#define BARRAY_API static inline
#define BHASH_API static inline
#include <slopsync/client.h>
#include <blog.h>
#include <bmacro.h>
#include <barray.h>
#include <bhash.h>
#include <math.h>
#include "internal.h"

struct ssync_ctx_s {
	ssync_t* ssync;
	ssync_mode_t mode;

	ssync_obj_schema_t* schema;

	ssync_obj_t* obj;
	int prop_group_index;
	int prop_index;
};

struct ssync_s {
	ssync_config_t config;
	ssync_obj_schema_t schema;

	ssync_init_record_t init_record;

	uint16_t next_obj_id;

	ssync_msg_header_t last_server_header;

	ssync_tick_t current_tick;
	double current_time;
	double logic_tick_accumulator;
	double net_tick_accumulator;
	double logic_tick_interval;
	double net_tick_interval;

	ssync_snapshot_t* last_snapshot;
	void* outgoing_packet_buf;
	void* record_buf;

	BHASH_TABLE(ssync_net_id_t, ssync_obj_info_t) local_objects;
	BHASH_TABLE(ssync_net_id_t, ssync_obj_info_t) remote_objects;
};

// Allocator {{{

static void*
ssync_blib_realloc(void* ptr, size_t size, void* ctx) {
	ssync_t* ssync = ctx;
	return ssync->config.realloc(ssync->config.userdata, ptr, size);
}

static void*
ssync_realloc(const ssync_config_t* config, void* ptr, size_t size) {
	return config->realloc(config->userdata, ptr, size);
}

static void*
ssync_malloc(const ssync_config_t* config, size_t size) {
	return ssync_realloc(config, NULL, size);
}

static void*
ssync_free(const ssync_config_t* config, void* ptr) {
	return ssync_realloc(config, ptr, 0);
}

// }}}

// Schema {{{

static void
ssync_finalize_prop_group(ssync_ctx_t* ctx) {
	if (ctx->prop_group_index) {
		ctx->schema->prop_groups[ctx->prop_group_index - 1].num_props = ctx->prop_index;
	}
}

static void
ssync_write_schema_impl(ssync_sync_fn_t sync, void* userdata, bsv_ctx_t* ctx) {
	// Use reflection to extract schema
	ssync_obj_schema_t schema = { 0 };
	ssync_ctx_t sync_ctx = {
		.mode = SSYNC_MODE_REFLECT,
		.schema = &schema,
	};
	sync(userdata, &sync_ctx, (ssync_net_id_t){ 0 });
	ssync_finalize_prop_group(&sync_ctx);
	schema.num_prop_groups = sync_ctx.prop_group_index;

	// Encode schema
	bsv_ssync_obj_schema(ctx, &schema);
}

// }}}

// Snapshot {{{

static void
ssync_write_obj(ssync_t* ssync, ssync_net_id_t id, ssync_obj_t* out) {
	ssync_ctx_t ctx = {
		.mode = SSYNC_MODE_WRITE,
		.ssync = ssync,
		.obj = out,
	};
	ssync->config.sync(ssync->config.userdata, &ctx, id);
}

static ssync_snapshot_t*
ssync_acquire_snapshot(ssync_t* ssync) {
	return NULL;
}

/*static void*/
/*ssync_release_snapshot(ssync_t* ssync, ssync_snapshot_t* snapshot) {*/
/*}*/

static void
ssync_archive_snapshot(ssync_t* ssync, ssync_snapshot_t* snapshot) {
}

static void
ssync_cleanup_obj(ssync_t* sync, ssync_obj_t* obj) {
}

static void
ssync_copy_obj(ssync_t* sync, ssync_obj_t* dst, const ssync_obj_t* src) {
}

// }}}

// Message records {{{

static bool
ssync_process_init_record(ssync_t* ssync, bsv_ctx_t* ctx) {
	ssync_init_record_t init_record = { 0 };
	if (bsv_ssync_player_init_record(ctx, &init_record) != BSV_OK) { return false; }

	if (ssync->init_record.net_tick_rate != 0) {
		BLOG_WARN("Server sends duplicated init record");
		return false;
	}

	ssync->init_record = init_record;
	ssync->logic_tick_interval = 1.0 / (double)init_record.logic_tick_rate;
	ssync->net_tick_interval = 1.0 / (double)init_record.net_tick_rate;
	ssync->current_tick = ssync->last_server_header.current_tick;
	ssync->logic_tick_accumulator = 0.0;
	ssync->net_tick_accumulator = 0.0;
	return true;
}

// }}}

size_t
ssync_schema_size(ssync_sync_fn_t sync, void* userdata) {
	ssync_bsv_count_t count_stream;
	bsv_ctx_t ctx = { .out = ssync_init_bsv_count(&count_stream) };
	ssync_write_schema_impl(sync, userdata, &ctx);
	return count_stream.count;
}

void
ssync_write_schema(ssync_sync_fn_t sync, void* userdata, void* out, size_t out_size) {
	bitstream_out_t out_stream = { .data = out, .num_bytes = out_size };
	ssync_bsv_out_t bsv_out;
	bsv_ctx_t ctx = { .out = ssync_init_bsv_out(&bsv_out, &out_stream) };
	ssync_write_schema_impl(sync, userdata, &ctx);
}

ssync_t*
ssync_init(const ssync_config_t* config) {
	ssync_t* ssync = ssync_malloc(config, sizeof(ssync_t));
	*ssync = (ssync_t){ 0 };
	ssync_reinit(&ssync, config);
	return ssync;
}

void
ssync_reinit(ssync_t** ssync_ptr, const ssync_config_t* config) {
	ssync_t* ssync = *ssync_ptr;
	if (ssync == NULL) {
		ssync = ssync_init(config);
		*ssync_ptr = ssync;
		return;
	}
	ssync->config = *config;

	bhash_config_t hconfig = bhash_config_default();
	hconfig.memctx = ssync;
	bhash_reinit(&ssync->local_objects, hconfig);
	bhash_reinit(&ssync->remote_objects, hconfig);

	ssync_realloc(config, ssync->outgoing_packet_buf, config->max_message_size);
	ssync_realloc(config, ssync->record_buf, config->max_message_size);
}

void
ssync_cleanup(ssync_t* ssync) {
	bhash_cleanup(&ssync->local_objects);
	bhash_cleanup(&ssync->remote_objects);
	ssync_free(&ssync->config, ssync->outgoing_packet_buf);
	ssync_free(&ssync->config, ssync->record_buf);
	ssync_free(&ssync->config, ssync);
}

const ssync_info_t*
ssync_info(ssync_t* ssync);

const ssync_obj_info_t*
ssync_obj_info(ssync_t* ssync, ssync_net_id_t obj_id) {
	const ssync_obj_info_t* local = bhash_get_value(&ssync->local_objects, obj_id);
	if (local != NULL) { return local; }

	const ssync_obj_info_t* remote = bhash_get_value(&ssync->remote_objects, obj_id);
	return remote;
}

void
ssync_process_message(ssync_t* ssync, ssync_blob_t msg) {
	bitstream_in_t msg_stream = {
		.data = msg.data,
		.num_bytes = msg.size,
	};
	ssync_bsv_in_t bsv_in;
	bsv_ctx_t ctx = { .in = ssync_init_bsv_in(&bsv_in, &msg_stream) };
	ssync_msg_header_t header = { 0 };
	if (bsv_ssync_msg_header(&ctx, &header) != BSV_OK) { return; }

	// TODO: maybe drop to 16 bits and do some wrap around math
	if (header.current_tick < ssync->last_server_header.current_tick) {
		return;  // Out of order message
	}

	ssync->last_server_header = header;

	while (true) {
		ssync_record_type_t record_type;
		if (!ssync_read_record_type(&msg_stream, &record_type)) {
			break;
		}

		switch ((ssync_record_type_t)record_type) {
			case SSYNC_RECORD_TYPE_INIT:
				if (!ssync_process_init_record(ssync, &ctx)) { return; }
				break;
			default: return;
		}
	}
}

void
ssync_update(ssync_t* ssync, double dt) {
	if (ssync->init_record.net_tick_rate == 0) { return; }

	ssync->current_time += dt;
	ssync->logic_tick_accumulator += dt;

	// Interpolate remote objects
	while (ssync->logic_tick_accumulator >= ssync->logic_tick_interval) {
		ssync->logic_tick_accumulator -= ssync->logic_tick_interval;
		ssync->current_tick += 1;
		// TODO: execute creation and destruction

		bhash_index_t num_remote_objects = bhash_len(&ssync->remote_objects);
		for (bhash_index_t i = 0; i < num_remote_objects; ++i) {
			ssync_ctx_t ctx = {
				.mode = SSYNC_MODE_READ,
				.ssync = ssync,
			};
			ssync->config.sync(ssync->config.userdata, &ctx, ssync->remote_objects.keys[i]);
		}
	}

	// Send local objects
	ssync->net_tick_accumulator += dt;
	if (ssync->net_tick_accumulator > ssync->net_tick_interval) {
		ssync->net_tick_accumulator = fmod(ssync->net_tick_accumulator, ssync->net_tick_interval);

		bitstream_out_t packet_out_stream = {
			.data = ssync->outgoing_packet_buf,
			.num_bytes = ssync->config.max_message_size,
		};
		ssync_bsv_out_t bsv_packet_out;
		bsv_ctx_t bsv_packet_ctx = { .out = ssync_init_bsv_out(&bsv_packet_out, &packet_out_stream) };

		ssync_msg_header_t header = {
			.current_tick = ssync->current_tick,
			.last_receive = ssync->last_server_header.current_tick,
		};
		bsv_ssync_msg_header(&bsv_packet_ctx, &header);

		bool has_space = true;
		ssync_snapshot_t* snapshot = ssync_acquire_snapshot(ssync);
		const ssync_snapshot_t* base_snapshot = ssync->last_snapshot;
		bhash_index_t num_local_objects = bhash_len(&ssync->local_objects);

		// Created objects since the last snapshot
		for (bhash_index_t i = 0; has_space && i < num_local_objects; ++i) {
			ssync_net_id_t id = ssync->local_objects.keys[i];
			if (bhash_has(&base_snapshot->objects, id)) { continue; }

			ssync_obj_info_t data = ssync->local_objects.values[i];

			// Write to a temp buffer first
			bitstream_out_t record_out_stream = {
				.data = ssync->record_buf,
				.num_bytes = ssync->config.max_message_size,
			};
			ssync_bsv_out_t bsv_record_out;
			bsv_ctx_t bsv_record_ctx = { .out = ssync_init_bsv_out(&bsv_record_out, &record_out_stream) };
			ssync_write_record_type(&record_out_stream, SSYNC_RECORD_TYPE_OBJ_CREATE);
			ssync_obj_create_record_t record = {
				.id = id,
				.timestamp = data.created_at,
				.flags = data.flags,
			};
			bsv_ssync_obj_create_record(&bsv_record_ctx, &record);

			// Try appending to the packet
			bool written = bitstream_append(&packet_out_stream, &record_out_stream);
			if (written) {
				// Put an empty object into the snapshot so when this snapshot is
				// ack-ed, we stop trying to resend object creation data
				ssync_obj_t empty_obj = { 0 };
				bhash_put(&snapshot->objects, id, empty_obj);
			}
			has_space &= written;
		}

		// Destroyed objects since the last snapshot
		bhash_index_t num_snapshotted_objects = bhash_len(&base_snapshot->objects);
		for (bhash_index_t i = 0; i < num_snapshotted_objects; ++i) {
			ssync_net_id_t id = base_snapshot->objects.keys[i];
			if (bhash_has(&ssync->local_objects, id)) { continue; }

			if (has_space) {
				// Write to a temp buffer first
				bitstream_out_t record_out_stream = {
					.data = ssync->record_buf,
					.num_bytes = ssync->config.max_message_size,
				};
				ssync_bsv_out_t bsv_record_out;
				bsv_ctx_t bsv_record_ctx = { .out = ssync_init_bsv_out(&bsv_record_out, &record_out_stream) };
				ssync_write_record_type(&record_out_stream, SSYNC_RECORD_TYPE_OBJ_DESTROY);
				ssync_obj_destroy_record_t record = {
					.id = id,
					// TODO: this might not be accurate and we may need to record
					// destruction time separately
					.timestamp = ssync->current_tick,
				};
				bsv_ssync_obj_destroy_record(&bsv_record_ctx, &record);

				// Try appending to the packet
				has_space &= bitstream_append(&packet_out_stream, &record_out_stream);
			}

			if (!has_space) {
				// Put an empty object into the snapshot if not written so we
				// can try resending destruction data
				ssync_obj_t empty_obj = { 0 };
				bhash_put(&snapshot->objects, id, empty_obj);
			}
		}

		// State change since the last snapshot
		for (bhash_index_t i = 0; i < num_local_objects; ++i) {
			ssync_net_id_t id = ssync->local_objects.keys[i];

			ssync_obj_t empty_obj = { 0 };
			const ssync_obj_t* previous_obj = &empty_obj;
			bhash_index_t index = bhash_find(&base_snapshot->objects, id);
			if (bhash_is_valid(index)) {
				previous_obj = &base_snapshot->objects.values[index];
			}

			ssync_obj_t current_obj = { 0 };
			if (has_space) {
				// Most of the time, an object has the same size
				barray_reserve(current_obj.props, barray_len(previous_obj->props), ssync);
				ssync_write_obj(ssync, id, &current_obj);

				if (!ssync_obj_equal(&ssync->schema, &current_obj, previous_obj)) {
					// Write to a temp buffer first
					bitstream_out_t record_out_stream = {
						.data = ssync->record_buf,
						.num_bytes = ssync->config.max_message_size,
					};
					ssync_write_record_type(&record_out_stream, SSYNC_RECORD_TYPE_OBJ_UPDATE);
					ssync_write_obj_update(&record_out_stream, &ssync->schema, &current_obj, previous_obj);

					// Try appending to the packet
					has_space &= bitstream_append(&packet_out_stream, &record_out_stream);
				}
			}

			// Store the effective object for later diff
			if (has_space) {
				bhash_put(&snapshot->objects, id, current_obj);
			} else {
				ssync_cleanup_obj(ssync, &current_obj);
				ssync_obj_t copy = { 0 };
				ssync_copy_obj(ssync, &copy, previous_obj);
				bhash_put(&snapshot->objects, id, copy);
			}
		}

		// Send
		ssync_blob_t msg = {
			.data = ssync->outgoing_packet_buf,
			.size = (packet_out_stream.bit_pos + 7) / 8,
		};
		ssync->config.send_msg(ssync->config.userdata, msg, false);

		ssync_archive_snapshot(ssync, snapshot);
	}
}

ssync_net_id_t
ssync_create(ssync_t* ssync, ssync_obj_flags_t flags) {
	ssync_net_id_t net_id = { .bin = ssync->init_record.obj_id_bin };
	bhash_alloc_result_t alloc_result;

	while (true) {
		net_id.index = ssync->next_obj_id++;

		alloc_result = bhash_alloc(&ssync->local_objects, net_id);
		if (alloc_result.is_new) { break; }
	}

	ssync_obj_info_t data = {
		.created_at = ssync->current_tick,
		.flags = flags,
		.is_local = true,
	};
	ssync->local_objects.keys[alloc_result.index] = net_id;
	ssync->local_objects.values[alloc_result.index] = data;

	return net_id;
}

void
ssync_destroy(ssync_t* ssync, ssync_net_id_t obj_id) {
	bhash_remove(&ssync->local_objects, obj_id);
}

void
ssync_handover(ssync_t* ssync, ssync_net_id_t obj_id, ssync_player_id_t player);

void
ssync_control(ssync_t* ssync, ssync_net_id_t obj, ssync_blob_t command);

ssync_mode_t
ssync_mode(ssync_ctx_t* ctx) {
	return ctx->mode;
}

bool
ssync_prop_group(ssync_ctx_t* ctx, ssync_local_id_t id) {
	switch (ctx->mode) {
		case SSYNC_MODE_REFLECT:
			ssync_finalize_prop_group(ctx);
			++ctx->prop_group_index;
			ctx->prop_index = 0;
			return true;
		default:
			return false;
	}
}

static void
ssync_reflect_add_prop(ssync_ctx_t* ctx, ssync_prop_type_t type, int precision, ssync_prop_flags_t flags) {
	ctx->schema->prop_groups[ctx->prop_group_index - 1].props[ctx->prop_index] = (ssync_prop_schema_t){
		.type = type,
		.precision = precision,
		.flags = flags,
	};
}

bool
ssync_prop_int(ssync_ctx_t* ctx, int64_t* value, ssync_prop_flags_t flags) {
	switch (ctx->mode) {
		case SSYNC_MODE_REFLECT:
			ssync_reflect_add_prop(ctx, SSYNC_PROP_TYPE_INT, 0, flags);
			++ctx->prop_index;
			return false;
		default:
			return false;
	}
}

bool
ssync_prop_float(ssync_ctx_t* ctx, float* value, int precision, ssync_prop_flags_t flags) {
	switch (ctx->mode) {
		case SSYNC_MODE_REFLECT:
			ssync_reflect_add_prop(ctx, SSYNC_PROP_TYPE_FLOAT, precision, flags);
			++ctx->prop_index;
			return false;
		default:
			return false;
	}
}

bool
ssync_prop_binary(ssync_ctx_t* ctx, ssync_blob_t* content) {
	switch (ctx->mode) {
		case SSYNC_MODE_REFLECT:
			++ctx->prop_index;
			return false;
		default:
			return false;
	}
}

bool
ssync_prop_u8(ssync_ctx_t* ctx, uint8_t* value, ssync_prop_flags_t flags) {
	int64_t temp;
	ssync_mode_t mode = ssync_mode(ctx);
	if (mode == SSYNC_MODE_WRITE) {
		temp = *value;
	}
	bool result = ssync_prop_int(ctx, &temp, flags);
	if (mode == SSYNC_MODE_READ && result) {
		*value = (uint8_t)temp;
	}

	return result;
}

bool
ssync_prop_s8(ssync_ctx_t* ctx, int8_t* value, ssync_prop_flags_t flags) {
	int64_t temp;
	ssync_mode_t mode = ssync_mode(ctx);
	if (mode == SSYNC_MODE_WRITE) {
		temp = *value;
	}
	bool result = ssync_prop_int(ctx, &temp, flags);
	if (mode == SSYNC_MODE_READ && result) {
		*value = (int8_t)temp;
	}

	return result;
}

bool
ssync_prop_u16(ssync_ctx_t* ctx, uint16_t* value, ssync_prop_flags_t flags) {
	int64_t temp;
	ssync_mode_t mode = ssync_mode(ctx);
	if (mode == SSYNC_MODE_WRITE) {
		temp = *value;
	}
	bool result = ssync_prop_int(ctx, &temp, flags);
	if (mode == SSYNC_MODE_READ && result) {
		*value = (uint16_t)temp;
	}

	return result;
}

bool
ssync_prop_s16(ssync_ctx_t* ctx, int16_t* value, ssync_prop_flags_t flags) {
	int64_t temp;
	ssync_mode_t mode = ssync_mode(ctx);
	if (mode == SSYNC_MODE_WRITE) {
		temp = *value;
	}
	bool result = ssync_prop_int(ctx, &temp, flags);
	if (mode == SSYNC_MODE_READ && result) {
		*value = (int16_t)temp;
	}

	return result;
}

bool
ssync_prop_u32(ssync_ctx_t* ctx, uint32_t* value, ssync_prop_flags_t flags) {
	int64_t temp;
	ssync_mode_t mode = ssync_mode(ctx);
	if (mode == SSYNC_MODE_WRITE) {
		temp = *value;
	}
	bool result = ssync_prop_int(ctx, &temp, flags);
	if (mode == SSYNC_MODE_READ && result) {
		*value = (uint32_t)temp;
	}

	return result;
}

bool
ssync_prop_s32(ssync_ctx_t* ctx, int32_t* value, ssync_prop_flags_t flags) {
	int64_t temp;
	ssync_mode_t mode = ssync_mode(ctx);
	if (mode == SSYNC_MODE_WRITE) {
		temp = *value;
	}
	bool result = ssync_prop_int(ctx, &temp, flags);
	if (mode == SSYNC_MODE_READ && result) {
		*value = (int32_t)temp;
	}

	return result;
}

bool
ssync_prop_u64(ssync_ctx_t* ctx, uint64_t* value, ssync_prop_flags_t flags) {
	int64_t temp;
	ssync_mode_t mode = ssync_mode(ctx);
	if (mode == SSYNC_MODE_WRITE) {
		temp = *value;
	}
	bool result = ssync_prop_int(ctx, &temp, flags);
	if (mode == SSYNC_MODE_READ && result) {
		*value = (uint64_t)temp;
	}

	return result;
}

#define BLIB_REALLOC ssync_blib_realloc
#define BLIB_IMPLEMENTATION
#include <bsv.h>
#include <barray.h>
#include <bhash.h>
