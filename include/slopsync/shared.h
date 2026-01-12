#ifndef SLOPSYNC_SHARED_H
#define SLOPSYNC_SHARED_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifndef SSYNC_OBJ_UPDATE_MASK_TYPE
#define SSYNC_OBJ_UPDATE_MASK_TYPE uint16_t
#endif

#ifndef SSYNC_PROP_GROUP_UPDATE_MASK_TYPE
#define SSYNC_PROP_GROUP_UPDATE_MASK_TYPE uint16_t
#endif

typedef uint8_t ssync_player_id_t;
typedef uint32_t ssync_tick_t;
typedef SSYNC_OBJ_UPDATE_MASK_TYPE ssync_obj_update_mask_t;
typedef SSYNC_PROP_GROUP_UPDATE_MASK_TYPE ssync_prop_group_update_mask_t;
typedef int ssync_prop_flags_t;

typedef struct {
	uint16_t index;
	ssync_player_id_t source;
	uint8_t gen;
} ssync_net_id_t;

typedef struct {
	const void* data;
	size_t size;
} ssync_blob_t;

typedef void* (*ssync_realloc_fn_t)(
	void* userdata,
	void* ptr,
	size_t size
);

typedef void (*ssync_send_msg_fn_t)(
	void* userdata,
	ssync_blob_t message,
	bool reliable
);

#endif
