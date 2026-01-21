#ifndef SLOPSYNC_CLIENT_H
#define SLOPSYNC_CLIENT_H

#include "shared.h"

#define ssync_prop_any_int(CTX, PTR, FLAGS) \
	_Generic(*(PTR), \
		uint8_t: ssync_prop_u8, \
		int8_t: ssync_prop_s8, \
		uint16_t: ssync_prop_u16, \
		int16_t: ssync_prop_s16, \
		uint32_t: ssync_prop_u32, \
		int32_t: ssync_prop_s32, \
		uint64_t: ssync_prop_u64, \
		int64_t: ssync_prop_int \
	)(CTX, PTR, FLAGS)

#define SSYNC_PROP_DEFAULT (ssync_prop_flags_t)(0)

typedef struct ssync_s ssync_t;
typedef uint64_t ssync_local_id_t;
typedef struct ssync_ctx_s ssync_ctx_t;

typedef void (*ssync_sync_fn_t)(
	void* userdata,
	ssync_ctx_t* ctx,
	ssync_net_id_t obj_id
);

typedef void (*ssync_prop_group_fn_t)(
	void* userdata,
	ssync_net_id_t obj_id, ssync_local_id_t prop_group_id
);

typedef bool (*ssync_check_prop_group_fn_t)(
	void* userdata,
	ssync_net_id_t obj_id,
	ssync_local_id_t prop_group_id
);

typedef void (*ssync_obj_fn_t)(
	void* userdata,
	ssync_net_id_t obj_id
);

typedef void (*ssync_control_fn_t)(
	void* userdata,
	ssync_player_id_t sender,
	ssync_net_id_t obj_id,
	ssync_blob_t command
);

typedef void (*ssync_send_msg_fn_t)(
	void* userdata,
	ssync_blob_t message,
	bool reliable
);

typedef struct {
	ssync_player_id_t player_id;
	ssync_timestamp_t client_time;
	ssync_timestamp_t server_time;
	ssync_timestamp_t interp_time;
	ssync_timestamp_t net_tick_rate;
	ssync_timestamp_t logic_tick_rate;
	int num_incoming_snapshots;
	int num_outgoing_snapshots;
	size_t schema_size;
} ssync_info_t;

typedef enum {
	SSYNC_MODE_REFLECT,
	SSYNC_MODE_WRITE,
	SSYNC_MODE_READ,
} ssync_mode_t;

typedef struct {
	ssync_timestamp_t created_at;
	ssync_timestamp_t updated_at;
	ssync_timestamp_t simulated_at;
	ssync_obj_flags_t flags;
	bool is_local;
} ssync_obj_info_t;

typedef struct {
	size_t max_message_size;

	double interpolation_ratio;

	ssync_realloc_fn_t realloc;
	ssync_obj_fn_t create_obj;
	ssync_obj_fn_t destroy_obj;
	ssync_prop_group_fn_t add_prop_group;
	ssync_prop_group_fn_t rem_prop_group;
	ssync_check_prop_group_fn_t has_prop_group;
	ssync_sync_fn_t sync;
	ssync_control_fn_t control;
	ssync_send_msg_fn_t send_msg;

	void* userdata;
} ssync_config_t;

ssync_t*
ssync_init(const ssync_config_t* config);

void
ssync_reinit(ssync_t** ssync, const ssync_config_t* config);

void
ssync_cleanup(ssync_t* ssync);

void
ssync_write_schema(ssync_t* ssync, void* out);

ssync_info_t
ssync_info(ssync_t* ssync);

const ssync_obj_info_t*
ssync_obj_info(ssync_t* ssync, ssync_net_id_t obj_id);

void
ssync_process_message(ssync_t* ssync, ssync_blob_t msg);

void
ssync_update(ssync_t* ssync, double dt);

ssync_net_id_t
ssync_create(ssync_t* ssync, ssync_obj_flags_t flags);

void
ssync_destroy(ssync_t* ssync, ssync_net_id_t obj_id);

ssync_mode_t
ssync_mode(ssync_ctx_t* ctx);

void*
ssync_ctx_userdata(ssync_ctx_t* ctx);

void
ssync_handover(ssync_t* ssync, ssync_net_id_t obj_id, ssync_player_id_t player);

void
ssync_control(ssync_t* ssync, ssync_net_id_t obj, ssync_blob_t command);

bool
ssync_prop_group(ssync_ctx_t* ctx, ssync_local_id_t id);

void
ssync_prop_int(ssync_ctx_t* ctx, int64_t* value, ssync_prop_flags_t flags);

void
ssync_prop_float(ssync_ctx_t* ctx, float* value, int precision, ssync_prop_flags_t flags);

void
ssync_prop_u8(ssync_ctx_t* ctx, uint8_t* value, ssync_prop_flags_t flags);

void
ssync_prop_s8(ssync_ctx_t* ctx, int8_t* value, ssync_prop_flags_t flags);

void
ssync_prop_u16(ssync_ctx_t* ctx, uint16_t* value, ssync_prop_flags_t flags);

void
ssync_prop_s16(ssync_ctx_t* ctx, int16_t* value, ssync_prop_flags_t flags);

void
ssync_prop_u32(ssync_ctx_t* ctx, uint32_t* value, ssync_prop_flags_t flags);

void
ssync_prop_s32(ssync_ctx_t* ctx, int32_t* value, ssync_prop_flags_t flags);

void
ssync_prop_u64(ssync_ctx_t* ctx, uint64_t* value, ssync_prop_flags_t flags);

#endif
