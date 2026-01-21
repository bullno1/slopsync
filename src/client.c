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
#include "base64.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

struct ssync_ctx_s {
	ssync_t* ssync;
	ssync_mode_t mode;

	ssync_net_id_t obj_id;
	ssync_obj_t* obj;
	ssync_obj_t* next_obj;
	const ssync_prop_t* prev_props;
	const ssync_prop_t* next_props;
	int prop_group_index;
	int prop_index;
	float interpolant;
};

struct ssync_s {
	ssync_config_t config;
	ssync_endpoint_config_t endpoint_config;

	ssync_obj_schema_t schema;
	size_t schema_size;

	ssync_init_record_t init_record;

	uint16_t next_obj_id;

	ssync_timestamp_t current_time_ms;
	ssync_timestamp_t interpolation_delay;
	double current_time_s;
	double simulation_time_s;
	double logic_tick_accumulator;
	double net_tick_accumulator;
	double logic_tick_interval;
	double net_tick_interval;

	ssync_snapshot_pool_t snapshot_pool;
	ssync_endpoint_t endpoint;

	barray(ssync_obj_create_record_t) created_objects;
	barray(ssync_obj_destroy_record_t) destroyed_objects;
	void* outgoing_packet_buf;
	void* record_buf;

	BHASH_TABLE(ssync_net_id_t, ssync_obj_info_t) local_objects;
	BHASH_TABLE(ssync_net_id_t, ssync_obj_info_t) remote_objects;
};

// Allocator {{{

void*
ssync_host_realloc(void* ptr, size_t size, void* ctx) {
	ssync_t* ssync = ctx;
	return ssync->config.realloc(ssync->config.userdata, ptr, size);
}

static void*
ssync_blib_realloc(void* ptr, size_t size, void* ctx) {
	return ssync_host_realloc(ptr, size, ctx);
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
		ctx->ssync->schema.prop_groups[ctx->prop_group_index - 1].num_props = ctx->prop_index;
	}
}

static void
ssync_reflect_add_prop(ssync_ctx_t* ctx, ssync_prop_type_t type, int precision, ssync_prop_flags_t flags) {
	ctx->ssync->schema.prop_groups[ctx->prop_group_index - 1].props[ctx->prop_index] = (ssync_prop_schema_t){
		.type = type,
		.precision = precision,
		.flags = flags,
	};
}

// }}}

// Message records {{{

static bool
ssync_process_init_record(ssync_t* ssync, bsv_ctx_t* ctx) {
	ssync_init_record_t init_record = { 0 };
	if (bsv_ssync_init_record(ctx, &init_record) != BSV_OK) { return false; }

	if (ssync->init_record.net_tick_rate != 0) {
		BLOG_WARN("Server sends duplicated init record");
		return false;
	}

	ssync->init_record = init_record;
	ssync->logic_tick_interval = 1.0 / (double)init_record.logic_tick_rate;
	ssync->net_tick_interval = 1.0 / (double)init_record.net_tick_rate;
	ssync->current_time_ms = init_record.current_time;
	ssync->current_time_s = (double)ssync->current_time_ms / 1000.0;
	ssync->interpolation_delay = (ssync_timestamp_t)(ssync->config.interpolation_ratio / (double)init_record.net_tick_rate * 1000.0);
	ssync->logic_tick_accumulator = 0.0;
	ssync->net_tick_accumulator = 0.0;
	BLOG_INFO(
		"Received init record: logic_tick_rate = %d, net_tick_rate = %d, current_time_s = %f, player_id = %d, obj_id_bin = %d",
		init_record.logic_tick_rate,
		init_record.net_tick_rate,
		ssync->current_time_s,
		init_record.player_id,
		init_record.obj_id_bin
	);
	return true;
}

// }}}

// Math {{{

static uint16_t
ssync_rad_to_u16(float radians) {
	// Normalize to [0, 2π) range
	float normalized = fmodf(radians, 2.f * M_PI);
	if (normalized < 0.f) {
		normalized += 2.f * M_PI;
	}

	// Map [0, 2 * M_PI) to [0, 65536)
	return (uint16_t)(normalized * (65536.f) / (2.f * M_PI));
}

static float
ssync_u16_to_rad(uint16_t angle) {
    return (float)angle * (2.0f * M_PI / 65536.0f);
}

static float
ssync_lerp_angle16(uint16_t from, uint16_t to, float t) {
	// Calculate signed difference (automatically wraps)
	int16_t diff = (int16_t)(to - from);

	// Interpolate (diff is already the shortest path due to signed arithmetic)
	return ssync_u16_to_rad(from + (uint16_t)((int16_t)((float)diff * t)));
}

static ssync_prop_t
ssync_float_to_fixed(float f, int precision) {
	return (ssync_prop_t)(f * (float)(1 << precision));
}

static float
ssync_fixed_to_float(ssync_prop_t f, int precision) {
	return (float)f / (float)(1 << precision);
}

static inline float
ssync_lerp(float from, float to, float interpolant) {
	return from + (to - from) * interpolant;
}

// }}}

static inline void
ssync_do_reinit(ssync_t* ssync, const ssync_config_t* config) {
	ssync->config = *config;

	bhash_config_t hconfig = bhash_config_default();
	hconfig.memctx = ssync;
	bhash_reinit(&ssync->local_objects, hconfig);
	bhash_reinit(&ssync->remote_objects, hconfig);

	ssync->outgoing_packet_buf = ssync_realloc(config, ssync->outgoing_packet_buf, config->max_message_size);
	ssync->record_buf = ssync_realloc(config, ssync->record_buf, config->max_message_size);

	ssync_reinit_snapshot_pool(&ssync->snapshot_pool, ssync);
	ssync->endpoint_config = (ssync_endpoint_config_t){
		.max_message_size = config->max_message_size,
		.memctx = ssync,
		.record_buf = ssync->record_buf,
		.schema = &ssync->schema,
		.snapshot_pool = &ssync->snapshot_pool
	};
	ssync_reinit_endpoint(&ssync->endpoint, &ssync->endpoint_config);

	// Use reflection to extract schema
	ssync_ctx_t sync_ctx = {
		.mode = SSYNC_MODE_REFLECT,
		.ssync = ssync,
	};
	config->sync(config->userdata, &sync_ctx, (ssync_net_id_t){ 0 });
	ssync_finalize_prop_group(&sync_ctx);
	ssync->schema.num_prop_groups = sync_ctx.prop_group_index;

	ssync_bsv_count_t count_stream;
	bsv_ctx_t ctx = { .out = ssync_init_bsv_count(&count_stream) };
	bsv_ssync_obj_schema(&ctx, &ssync->schema);
	ssync->schema_size = base64_encoded_size(count_stream.count);
}

ssync_t*
ssync_init(const ssync_config_t* config) {
	ssync_t* ssync = ssync_malloc(config, sizeof(ssync_t));
	*ssync = (ssync_t){ 0 };
	ssync_do_reinit(ssync, config);
	return ssync;
}

void
ssync_reinit(ssync_t** ssync_ptr, const ssync_config_t* config) {
	ssync_t* ssync = *ssync_ptr;
	if (ssync == NULL) {
		ssync = ssync_init(config);
		*ssync_ptr = ssync;
	} else {
		ssync_do_reinit(*ssync_ptr, config);
	}
}

void
ssync_cleanup(ssync_t* ssync) {
	bhash_cleanup(&ssync->local_objects);
	bhash_cleanup(&ssync->remote_objects);

	ssync_cleanup_endpoint(&ssync->endpoint);
	ssync_cleanup_snapshot_pool(&ssync->snapshot_pool, ssync);

	barray_free(ssync->created_objects, ssync);
	barray_free(ssync->destroyed_objects, ssync);

	ssync_free(&ssync->config, ssync->outgoing_packet_buf);
	ssync_free(&ssync->config, ssync->record_buf);

	ssync_free(&ssync->config, ssync);
}

void
ssync_write_schema(ssync_t* ssync, void* out) {
	size_t raw_size = base64_decoded_size(ssync->schema_size);
	void* tmp_buf = ssync_malloc(&ssync->config, raw_size);
	bitstream_out_t out_stream = { .data = tmp_buf, .num_bytes = raw_size };
	ssync_bsv_out_t bsv_out;
	bsv_ctx_t ctx = { .out = ssync_init_bsv_out(&bsv_out, &out_stream) };
	bsv_ssync_obj_schema(&ctx, &ssync->schema);

	base64_encode(tmp_buf, raw_size, out);
	ssync_free(&ssync->config, tmp_buf);
}

ssync_info_t
ssync_info(ssync_t* ssync) {
	ssync_timestamp_t server_time = 0;
	if (ssync->endpoint.incoming_archive.next != NULL) {
		server_time = ssync->endpoint.incoming_archive.next->timestamp;
	}

	return (ssync_info_t){
		.player_id = ssync->init_record.player_id,
		.net_tick_rate = ssync->init_record.net_tick_rate,
		.logic_tick_rate = ssync->init_record.logic_tick_rate,
		.client_time = ssync->current_time_ms,
		.interp_time = (ssync_timestamp_t)(ssync->simulation_time_s * 1000.0),
		.server_time = server_time,
		.schema_size = ssync->schema_size,
		.num_incoming_snapshots = ssync->endpoint.incoming_archive.count,
		.num_outgoing_snapshots = ssync->endpoint.outgoing_archive.count,
	};
}

const ssync_obj_info_t*
ssync_obj_info(ssync_t* ssync, ssync_net_id_t obj_id) {
	const ssync_obj_info_t* local = bhash_get_value(&ssync->local_objects, obj_id);
	if (local != NULL) { return local; }

	const ssync_obj_info_t* remote = bhash_get_value(&ssync->remote_objects, obj_id);
	return remote;
}

void
ssync_process_message(ssync_t* ssync, ssync_blob_t msg) {
	bitstream_in_t packet_stream = {
		.data = msg.data,
		.num_bytes = msg.size,
	};
	ssync_bsv_in_t bsv_in;
	bsv_ctx_t packet_bsv = { .in = ssync_init_bsv_in(&bsv_in, &packet_stream) };

	ssync_incoming_packet_ctx_t ctx;
	ssync_begin_incoming_packet(&ctx, &ssync->endpoint, &packet_stream, &packet_bsv);

	while (true) {
		ssync_record_type_t record_type;
		if (!ssync_read_record_type(&packet_stream, &record_type)) {
			break;
		}

		switch ((ssync_record_type_t)record_type) {
			case SSYNC_RECORD_TYPE_END:
				goto end;
			case SSYNC_RECORD_TYPE_INIT: {
				if (!ssync_process_init_record(ssync, &packet_bsv)) {
					ssync_discard_incoming_packet(&ctx);
					goto end;
				}
			} break;
			case SSYNC_RECORD_TYPE_SNAPSHOT_INFO: {
				if (!ssync_process_snapshot_info_record(&ctx)) {
					goto end;
				}
			} break;
			case SSYNC_RECORD_TYPE_OBJ_CREATE: {
				ssync_obj_create_record_t record;
				const ssync_obj_t* obj;
				if (!ssync_process_process_object_create_record(&ctx, &record, &obj)) {
					goto end;
				}

				barray_push(ssync->created_objects, record, ssync);
			} break;
			case SSYNC_RECORD_TYPE_OBJ_DESTROY: {
				ssync_obj_destroy_record_t record;
				if (!ssync_process_process_object_destroy_record(&ctx, &record)) {
					goto end;
				}

				barray_push(ssync->destroyed_objects, record, ssync);
			} break;
			case SSYNC_RECORD_TYPE_OBJ_UPDATE: {
				if (!ssync_process_object_update_record(&ctx, NULL, NULL)) {
					goto end;
				}
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
ssync_update(ssync_t* ssync, double dt) {
	if (ssync->init_record.net_tick_rate == 0) { return; }

	ssync->logic_tick_accumulator += dt;

	// Interpolate remote objects
	while (ssync->logic_tick_accumulator >= ssync->logic_tick_interval) {
		ssync->logic_tick_accumulator -= ssync->logic_tick_interval;
		ssync->current_time_s += ssync->logic_tick_interval;
		ssync->current_time_ms = (ssync_timestamp_t)(ssync->current_time_s * 1000.0);

		const ssync_snapshot_t* next_snapshot = NULL;
		ssync_timestamp_t simulation_time_ms;
		if (ssync->simulation_time_s == 0.0) {
			if (
				ssync->endpoint.incoming_archive.next != NULL
				&&
				ssync->endpoint.incoming_archive.next->timestamp > ssync->interpolation_delay
			) {
				simulation_time_ms = ssync->endpoint.incoming_archive.next->timestamp - ssync->interpolation_delay;
				next_snapshot = ssync_find_snapshot_pair(&ssync->endpoint.incoming_archive, simulation_time_ms);
				if (next_snapshot != NULL) {
					ssync->simulation_time_s = (double)simulation_time_ms / 1000.0;
					BLOG_DEBUG("Started interpolation at %dms (current time: %dms)", simulation_time_ms, ssync->current_time_ms);
				}
			}
		} else {
			ssync->simulation_time_s += ssync->logic_tick_interval;
			simulation_time_ms = (ssync_timestamp_t)(ssync->simulation_time_s * 1000.0);
			next_snapshot = ssync_find_snapshot_pair(&ssync->endpoint.incoming_archive, simulation_time_ms);
		}

		if (next_snapshot == NULL) { continue; }
		ssync_snapshot_t* prev_snapshot = next_snapshot->next;

		// Execute queued creations
		int num_created_objects = (int)barray_len(ssync->created_objects);
		int num_delayed_creations = 0;
		for (int i = 0; i < num_created_objects; ++i) {
			ssync_obj_create_record_t* record = &ssync->created_objects[i];
			if (record->timestamp <= simulation_time_ms) {
				ssync_obj_info_t info = {
					.created_at = record->timestamp,
					.flags = record->flags,
					.is_local = false,
				};

				bhash_alloc_result_t alloc_result = bhash_alloc(&ssync->remote_objects, record->id);
				if (alloc_result.is_new) {
					ssync->remote_objects.keys[alloc_result.index] = record->id;
					ssync->remote_objects.values[alloc_result.index] = info;
					ssync->config.create_obj(ssync->config.userdata, record->id);
				}
			} else {
				ssync->created_objects[num_delayed_creations++] = *record;
			}
		}
		barray_resize(ssync->created_objects, num_delayed_creations, ssync);

		// Execute queued destructions
		int num_destroyed_objects = (int)barray_len(ssync->destroyed_objects);
		int num_delayed_destructions = 0;
		for (int i = 0; i < num_destroyed_objects; ++i) {
			ssync_obj_destroy_record_t* record = &ssync->destroyed_objects[i];
			if (record->timestamp <= simulation_time_ms) {
				if (bhash_has(&ssync->remote_objects, record->id)) {
					ssync->config.destroy_obj(ssync->config.userdata, record->id);
					bhash_remove(&ssync->remote_objects, record->id);
				}
			} else {
				ssync->destroyed_objects[num_delayed_destructions++] = *record;
			}
		}
		barray_resize(ssync->destroyed_objects, num_delayed_destructions, ssync);

		// Update existing objects
		float interpolant =
			  (float)(simulation_time_ms - prev_snapshot->timestamp)
			/ (float)(next_snapshot->timestamp - prev_snapshot->timestamp);
		bhash_index_t num_remote_objects = bhash_len(&ssync->remote_objects);
		for (bhash_index_t i = 0; i < num_remote_objects; ++i) {
			ssync_net_id_t id = ssync->remote_objects.keys[i];
			ssync_obj_t* from_obj = bhash_get_value(&prev_snapshot->objects, id);
			ssync_obj_t* to_obj = bhash_get_value(&next_snapshot->objects, id);

			if (from_obj == NULL) { continue; }

			ssync_obj_info_t* obj_info = &ssync->remote_objects.values[i];
			obj_info->updated_at = prev_snapshot->timestamp;
			obj_info->simulated_at = simulation_time_ms;

			ssync_ctx_t ctx = {
				.mode = SSYNC_MODE_READ,
				.ssync = ssync,
				.obj_id = id,
				.obj = from_obj,
				.next_obj = to_obj,
				.prev_props = from_obj->props,
				.next_props = to_obj != NULL ? to_obj->props : NULL,
				.interpolant = interpolant,
			};
			ssync->config.sync(ssync->config.userdata, &ctx, id);
		}

		// Prune old snapshots
		if (
			ssync->endpoint.last_acked_snapshot != NULL
			&&
			ssync->endpoint.last_acked_snapshot->remote != NULL
		) {
			// A snapshot is old enough if:
			//
			// * It is older than the base snapshot used for delta compression
			// * It is older than the snapshot used for interpolation
			const ssync_snapshot_t* base_snapshot = ssync->endpoint.last_acked_snapshot->remote;
			if (prev_snapshot->timestamp <= base_snapshot->timestamp) {
				ssync_release_after(
					&ssync->snapshot_pool,
					&ssync->endpoint.incoming_archive,
					prev_snapshot
				);
			}
		}
	}

	// Send local objects
	ssync->net_tick_accumulator += dt;
	if (ssync->net_tick_accumulator >= ssync->net_tick_interval) {
		ssync->net_tick_accumulator = fmod(ssync->net_tick_accumulator, ssync->net_tick_interval);

		bitstream_out_t packet_stream = {
			.data = ssync->outgoing_packet_buf,
			.num_bytes = ssync->config.max_message_size,
		};
		ssync_bsv_out_t bsv_packet_out;
		bsv_ctx_t packet_bsv = {
			.out = ssync_init_bsv_out(&bsv_packet_out, &packet_stream)
		};

		ssync_outgoing_snapshot_ctx_t snapshot_ctx;
		ssync_begin_outgoing_snapshot(
			&snapshot_ctx,
			&ssync->endpoint,
			ssync->current_time_ms,
			&packet_stream, &packet_bsv
		);
		ssync_obj_t tmp_obj = { 0 };

		bhash_index_t num_local_objects = bhash_len(&ssync->local_objects);
		for (bhash_index_t i = 0; i < num_local_objects; ++i) {
			ssync_net_id_t id = ssync->local_objects.keys[i];
			const ssync_obj_info_t* info = &ssync->local_objects.values[i];

			if (snapshot_ctx.has_space) {
				ssync_reset_obj(&tmp_obj);

				ssync_ctx_t sync_ctx = {
					.mode = SSYNC_MODE_WRITE,
					.ssync = ssync,
					.obj_id = id,
					.obj = &tmp_obj,
				};
				ssync->config.sync(ssync->config.userdata, &sync_ctx, id);

				ssync_add_outgoing_obj(
					&snapshot_ctx,
					info->created_at, info->flags,
					id, &tmp_obj
				);
			}
		}

		ssync_cleanup_obj(&tmp_obj, ssync);
		ssync_end_outgoing_snapshot(&snapshot_ctx);

		ssync_end_packet(&packet_stream);
		ssync_blob_t msg = {
			.data = ssync->outgoing_packet_buf,
			.size = (packet_stream.bit_pos + 7) / 8,
		};
		ssync->config.send_msg(ssync->config.userdata, msg, false);
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
		.created_at = ssync->current_time_ms,
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

void*
ssync_ctx_userdata(ssync_ctx_t* ctx) {
	return ctx->ssync->config.userdata;
}

bool
ssync_prop_group(ssync_ctx_t* ctx, ssync_local_id_t prop_group_id) {
	ssync_t* ssync = ctx->ssync;
	switch (ctx->mode) {
		case SSYNC_MODE_REFLECT:
			ssync_finalize_prop_group(ctx);
			++ctx->prop_group_index;
			ctx->prop_index = 0;
			return true;
		case SSYNC_MODE_WRITE: {
			bool has_prop_group = ssync->config.has_prop_group(
				ssync->config.userdata, ctx->obj_id, prop_group_id
			);
			if (has_prop_group) {
				ctx->obj->prop_group_mask |= 1 << ctx->prop_group_index;
			}
			ctx->prop_group_index += 1;
			return has_prop_group;
		}
		case SSYNC_MODE_READ: {
			if (ctx->prop_group_index) {
				int group_index = ctx->prop_group_index - 1;
				int num_props = ssync->schema.prop_groups[group_index].num_props;
				if (ssync_obj_has_prop_group(ctx->obj, group_index)) {
					ctx->prev_props += num_props;
				}
				if (
					ctx->next_obj != NULL
					&&
					ssync_obj_has_prop_group(ctx->next_obj, group_index)
				) {
					ctx->next_props += num_props;
				}
			}

			ctx->prop_group_index += 1;
			ctx->prop_index = 0;

			bool snapshot_has_prop_group = ssync_obj_has_prop_group(ctx->obj, ctx->prop_index - 1);
			bool proxy_has_prop_group = ssync->config.has_prop_group(
				ssync->config.userdata, ctx->obj_id, prop_group_id
			);

			if (snapshot_has_prop_group && !proxy_has_prop_group) {
				ssync->config.add_prop_group(ssync->config.userdata, ctx->obj_id, prop_group_id);
			} else if (!snapshot_has_prop_group && proxy_has_prop_group) {
				ssync->config.rem_prop_group(ssync->config.userdata, ctx->obj_id, prop_group_id);
			}

			return snapshot_has_prop_group;
		}
		default:
			return false;
	}
}

void
ssync_prop_int(ssync_ctx_t* ctx, int64_t* value, ssync_prop_flags_t flags) {
	ssync_t* ssync = ctx->ssync;
	switch (ctx->mode) {
		case SSYNC_MODE_REFLECT:
			ssync_reflect_add_prop(ctx, SSYNC_PROP_TYPE_INT, 0, flags);
			++ctx->prop_index;
			break;
		case SSYNC_MODE_WRITE:
			barray_push(ctx->obj->props, *value, ssync);
			break;
		case SSYNC_MODE_READ:
			if (
				(flags & SSYNC_PROP_INTERPOLATE)
				&&
				ctx->next_obj != NULL
				&&
				ssync_obj_has_prop_group(ctx->next_obj, ctx->prop_group_index - 1)
			) {  // Interpolate when enabled and next snapshot has the same property group
				float prev_value = ctx->prev_props[ctx->prop_index];
				float next_value = ctx->next_props[ctx->prop_index];
				*value = (int64_t)ssync_lerp((float)prev_value, (float)next_value, ctx->interpolant);
			} else {  // Otherwise, snap
				*value = ctx->prev_props[ctx->prop_index];
			}
			ctx->prop_index += 1;
			break;
	}
}

void
ssync_prop_float(ssync_ctx_t* ctx, float* value, int precision, ssync_prop_flags_t flags) {
	ssync_t* ssync = ctx->ssync;
	switch (ctx->mode) {
		case SSYNC_MODE_REFLECT:
			ssync_reflect_add_prop(ctx, SSYNC_PROP_TYPE_FLOAT, precision, flags);
			++ctx->prop_index;
			break;
		case SSYNC_MODE_WRITE:
			if (flags & SSYNC_PROP_ROTATION) {
				uint16_t fixed_point_angle = ssync_rad_to_u16(*value);
				barray_push(ctx->obj->props, (int64_t)fixed_point_angle, ssync);
			} else {
				int64_t fixed_point = ssync_float_to_fixed(*value, precision);
				barray_push(ctx->obj->props, fixed_point, ssync);
			}
			break;
		case SSYNC_MODE_READ:
			if (
				(flags & SSYNC_PROP_INTERPOLATE)
				&&
				ctx->next_obj != NULL
				&&
				ssync_obj_has_prop_group(ctx->next_obj, ctx->prop_group_index - 1)
			) {  // Interpolate when enabled and next snapshot has the same property group
				if (flags & SSYNC_PROP_ROTATION) {  // Rotation
					uint16_t prev_value = ctx->prev_props[ctx->prop_index];
					uint16_t next_value = ctx->next_props[ctx->prop_index];
					*value = ssync_lerp_angle16(prev_value, next_value, ctx->interpolant);
				} else {  // Flat value
					float prev_value = ssync_fixed_to_float(ctx->prev_props[ctx->prop_index], precision);
					float next_value = ssync_fixed_to_float(ctx->next_props[ctx->prop_index], precision);
					*value = ssync_lerp(prev_value, next_value, ctx->interpolant);
				}
			} else {  // Otherwise, snap
				if (flags & SSYNC_PROP_ROTATION) {
					*value = ssync_u16_to_rad(ctx->prev_props[ctx->prop_index]);
				} else {
					*value = ssync_fixed_to_float(ctx->prev_props[ctx->prop_index], precision);
				}
			}
			ctx->prop_index += 1;
			break;
	}
}

void
ssync_prop_u8(ssync_ctx_t* ctx, uint8_t* value, ssync_prop_flags_t flags) {
	int64_t temp;
	ssync_mode_t mode = ssync_mode(ctx);
	if (mode == SSYNC_MODE_WRITE) {
		temp = *value;
	}
	ssync_prop_int(ctx, &temp, flags);
	if (mode == SSYNC_MODE_READ) {
		*value = (uint8_t)temp;
	}
}

void
ssync_prop_s8(ssync_ctx_t* ctx, int8_t* value, ssync_prop_flags_t flags) {
	int64_t temp;
	ssync_mode_t mode = ssync_mode(ctx);
	if (mode == SSYNC_MODE_WRITE) {
		temp = *value;
	}
	ssync_prop_int(ctx, &temp, flags);
	if (mode == SSYNC_MODE_READ) {
		*value = (int8_t)temp;
	}
}

void
ssync_prop_u16(ssync_ctx_t* ctx, uint16_t* value, ssync_prop_flags_t flags) {
	int64_t temp;
	ssync_mode_t mode = ssync_mode(ctx);
	if (mode == SSYNC_MODE_WRITE) {
		temp = *value;
	}
	ssync_prop_int(ctx, &temp, flags);
	if (mode == SSYNC_MODE_READ) {
		*value = (uint16_t)temp;
	}
}

void
ssync_prop_s16(ssync_ctx_t* ctx, int16_t* value, ssync_prop_flags_t flags) {
	int64_t temp;
	ssync_mode_t mode = ssync_mode(ctx);
	if (mode == SSYNC_MODE_WRITE) {
		temp = *value;
	}
	ssync_prop_int(ctx, &temp, flags);
	if (mode == SSYNC_MODE_READ) {
		*value = (int16_t)temp;
	}
}

void
ssync_prop_u32(ssync_ctx_t* ctx, uint32_t* value, ssync_prop_flags_t flags) {
	int64_t temp;
	ssync_mode_t mode = ssync_mode(ctx);
	if (mode == SSYNC_MODE_WRITE) {
		temp = *value;
	}
	ssync_prop_int(ctx, &temp, flags);
	if (mode == SSYNC_MODE_READ) {
		*value = (uint32_t)temp;
	}
}

void
ssync_prop_s32(ssync_ctx_t* ctx, int32_t* value, ssync_prop_flags_t flags) {
	int64_t temp;
	ssync_mode_t mode = ssync_mode(ctx);
	if (mode == SSYNC_MODE_WRITE) {
		temp = *value;
	}
	ssync_prop_int(ctx, &temp, flags);
	if (mode == SSYNC_MODE_READ) {
		*value = (int32_t)temp;
	}
}

void
ssync_prop_u64(ssync_ctx_t* ctx, uint64_t* value, ssync_prop_flags_t flags) {
	int64_t temp;
	ssync_mode_t mode = ssync_mode(ctx);
	if (mode == SSYNC_MODE_WRITE) {
		temp = *value;
	}
	ssync_prop_int(ctx, &temp, flags);
	if (mode == SSYNC_MODE_READ) {
		*value = (uint64_t)temp;
	}
}

#define BLIB_REALLOC ssync_blib_realloc
#define BLIB_IMPLEMENTATION
#include <bsv.h>
#include <barray.h>
#include <bhash.h>
