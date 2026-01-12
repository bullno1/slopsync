#define BSV_API static inline
#include <slopsync/client.h>
#include <blog.h>
#include <bmacro.h>
#include "internal.h"

typedef struct {
	bsv_out_t bsv;
	size_t count;
} ssync_bsv_count_t;

struct ssync_ctx_s {
	ssync_t* ssync;
	ssync_mode_t mode;
	ssync_obj_schema_t* schema;
	int prop_group_index;
	int prop_index;
};

struct ssync_s {
	ssync_config_t config;
	ssync_msg_header_t latest_header;
	ssync_player_init_record_t init_record;

	double current_time;
	double net_interval;
	double net_accumulator;
};

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

static size_t
ssync_bsv_count_write(struct bsv_out_s* out, const void* buf, size_t size) {
	ssync_bsv_count_t* ssync_bsv_count = BCONTAINER_OF(out, ssync_bsv_count_t, bsv);
	ssync_bsv_count->count += size;
	return size;
}

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

size_t
ssync_schema_size(ssync_sync_fn_t sync, void* userdata) {
	ssync_bsv_count_t count_stream = { .bsv.write = ssync_bsv_count_write };
	bsv_ctx_t ctx = { .out = &count_stream.bsv };
	ssync_write_schema_impl(sync, userdata, &ctx);
	return count_stream.count;
}

void
ssync_write_schema(ssync_sync_fn_t sync, void* userdata, void* out, size_t out_size) {
	ssync_write_buf_t buf = {
		.begin = out,
		.current = out,
		.end = (char*)out + out_size,
	};
	ssync_bsv_out_t bsv_out;
	bsv_ctx_t ctx = { .out = ssync_init_bsv_out(&bsv_out, &buf) };
	ssync_write_schema_impl(sync, userdata, &ctx);
}

ssync_t*
ssync_init(const ssync_config_t* config) {
	ssync_t* ssync = ssync_malloc(config, sizeof(ssync_t));
	*ssync = (ssync_t){
		.config = *config,
	};
	return ssync;
}

void
ssync_cleanup(ssync_t* ssync) {
	ssync_free(&ssync->config, ssync);
}

const ssync_info_t*
ssync_info(ssync_t* ssync);

const ssync_obj_info_t*
ssync_obj_info(ssync_t* ssync, ssync_net_id_t obj_id);

static bool
ssync_process_init_record(ssync_t* ssync, bsv_ctx_t* ctx) {
	ssync_player_init_record_t record = { 0 };
	if (bsv_ssync_player_init_record(ctx, &record) != BSV_OK) { return false; }

	if (ssync->init_record.net_tick_rate != 0) {
		BLOG_WARN("Server sends duplicated init record");
		return false;
	}

	ssync->init_record = record;
	return true;
}

void
ssync_process_message(ssync_t* ssync, ssync_blob_t msg) {
	ssync_bsv_in_t bsv_in;
	bsv_ctx_t ctx = { .in = ssync_init_bsv_in(&bsv_in, msg)  };
	ssync_msg_header_t header = { 0 };
	if (bsv_ssync_msg_header(&ctx, &header) != BSV_OK) { return; }
	ssync->latest_header = header;

	while (!ssync_bsv_in_eof(&bsv_in)) {
		uint8_t record_type = SSYNC_RECORD_TYPE_NONE;
		if (bsv_auto(&ctx, &record_type) != BSV_OK) { return; }

		switch ((ssync_record_type_t)record_type) {
			case SSYNC_RECORD_TYPE_INIT:
				if (!ssync_process_init_record(ssync, &ctx)) {
					break;
				}
				break;
			default: return;
		}
	}
}

void
ssync_update(ssync_t* ssync, double dt) {
	ssync->current_time += dt;
}

ssync_net_id_t
ssync_create(ssync_t* ssync, ssync_obj_flags_t flags);

bool
ssync_destroy(ssync_t* ssync, ssync_net_id_t obj_id);

void
ssync_sync(ssync_t* ssync, ssync_net_id_t obj_id);

ssync_mode_t
ssync_mode(ssync_ctx_t* ctx);

bool
ssync_position(
	ssync_ctx_t* ctx,
	float* x, float* y, float* radius
);

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

bool
ssync_prop_s64(ssync_ctx_t* ctx, int64_t* value, ssync_prop_flags_t flags) {
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

#define BLIB_IMPLEMENTATION
#include <bsv.h>
