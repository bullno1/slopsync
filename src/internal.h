#ifndef SLOPSYNC_INTERNAL_H
#define SLOPSYNC_INTERNAL_H

#include <slopsync/shared.h>
#include <bsv.h>
#include <limits.h>

typedef enum {
	SSYNC_RECORD_TYPE_NONE  = 0,  // error
	SSYNC_RECORD_TYPE_INIT  = 1,
	SSYNC_RECORD_TYPE_EVENT = 2,
	SSYNC_RECORD_TYPE_SYNC  = 3,
} ssync_record_type_t;

typedef struct {
	ssync_tick_t current_tick;
	ssync_tick_t last_receive;
} ssync_msg_header_t;
typedef struct {
	ssync_player_id_t player_id;
	ssync_tick_t net_tick_rate;
	ssync_tick_t logic_tick_rate;
} ssync_player_init_record_t;

typedef enum {
	SSYNC_PROP_TYPE_INT,
	SSYNC_PROP_TYPE_FLOAT,
	SSYNC_PROP_TYPE_BINARY,
} ssync_prop_type_t;

typedef struct {
	ssync_prop_type_t type;
	union {
		int64_t int_value;
		float float_value;
	};
} ssync_prop_t;

typedef struct {
	int id;
	ssync_prop_t props[];
} ssync_prop_group_t;

typedef struct {
	ssync_obj_update_mask_t prop_group_mask;
	ssync_prop_group_t* prop_groups;
} ssync_obj_t;

typedef struct {
	ssync_prop_type_t type;
	int precision;
	ssync_prop_flags_t flags;
} ssync_prop_schema_t;

typedef struct {
	int num_props;
	ssync_prop_schema_t props[sizeof(ssync_prop_group_update_mask_t) * CHAR_BIT];
} ssync_prop_group_schema_t;

typedef struct {
	int num_prop_groups;
	ssync_prop_group_schema_t prop_groups[sizeof(ssync_obj_update_mask_t) * CHAR_BIT];
} ssync_obj_schema_t;

typedef struct {
	ssync_obj_schema_t obj_schema;
} ssync_server_init_record_t;

typedef struct {
	uint16_t num_events;
	uint16_t num_objects;
} ssync_sync_record_t;

typedef enum {
	SSYNC_OBJ_OP_CREATE,
	SSYNC_OBJ_OP_UPDATE,
	SSYNC_OBJ_OP_DESTROY,
} ssync_obj_op_type_t;

typedef struct {
	ssync_net_id_t id;
	ssync_obj_op_type_t op;
	ssync_obj_update_mask_t update_mask;
	ssync_tick_t timestamp;
} ssync_obj_header_t;

typedef struct {
	ssync_obj_op_type_t op;
	ssync_prop_group_update_mask_t update_mask;
	ssync_prop_group_update_mask_t delta_mask;
} ssync_prop_group_header_t;

typedef struct {
	char* current;
	char* const begin;
	char* const end;
} ssync_write_buf_t;

typedef struct {
	bsv_in_t bsv;
	const char* pos;
	ssync_blob_t blob;
} ssync_bsv_in_t;

typedef struct {
	bsv_out_t bsv;
	ssync_write_buf_t* buf;
} ssync_bsv_out_t;

bsv_in_t*
ssync_init_bsv_in(ssync_bsv_in_t* bsv_in, ssync_blob_t blob);

bool
ssync_bsv_in_eof(ssync_bsv_in_t* bsv_in);

bsv_out_t*
ssync_init_bsv_out(ssync_bsv_out_t* bsv_out, ssync_write_buf_t* buf);

static inline bsv_status_t
bsv_ssync_msg_header(bsv_ctx_t* ctx, ssync_msg_header_t* header) {
	BSV_CHECK_STATUS(bsv_auto(ctx, &header->current_tick));
	BSV_CHECK_STATUS(bsv_auto(ctx, &header->last_receive));
	return bsv_status(ctx);
}

static inline bsv_status_t
bsv_ssync_player_init_record(bsv_ctx_t* ctx, ssync_player_init_record_t* rec) {
	BSV_CHECK_STATUS(bsv_auto(ctx, &rec->player_id));
	BSV_CHECK_STATUS(bsv_auto(ctx, &rec->net_tick_rate));
	BSV_CHECK_STATUS(bsv_auto(ctx, &rec->logic_tick_rate));
	return bsv_status(ctx);
}

static inline bsv_status_t
bsv_ssync_prop_schema(bsv_ctx_t* ctx, ssync_prop_schema_t* schema) {
	BSV_BLK(ctx, 0) {
		BSV_REV(0) {
			BSV_ADD(&schema->type);
			BSV_ADD(&schema->precision);
			BSV_ADD(&schema->flags);
		}
	}

	return bsv_status(ctx);
}

static inline bsv_status_t
bsv_ssync_prop_group_schema(bsv_ctx_t* ctx, ssync_prop_group_schema_t* schema) {
	BSV_BLK(ctx, 0) {
		BSV_REV(0) {
			bsv_len_t num_props = schema->num_props;
			BSV_ARRAY(ctx, &num_props) {
				schema->num_props = (int)num_props;
				for (bsv_len_t i = 0; i < num_props; ++i) {
					bsv_ssync_prop_schema(ctx, &schema->props[i]);
				}
			}
		}
	}

	return bsv_status(ctx);
}

static inline bsv_status_t
bsv_ssync_obj_schema(bsv_ctx_t* ctx, ssync_obj_schema_t* schema) {
	// This is exchanged once on game creation, we can afford to use a bit more
	// data and version it
	BSV_BLK(ctx, 0) {
		BSV_REV(0) {
			bsv_len_t num_prop_groups = schema->num_prop_groups;
			BSV_ARRAY(ctx, &num_prop_groups);
			for (bsv_len_t i = 0; i < num_prop_groups; ++i) {
				bsv_ssync_prop_group_schema(ctx, &schema->prop_groups[i]);
			}
			schema->num_prop_groups = (int)num_prop_groups;
		}
	}

	return bsv_status(ctx);
}

#endif
