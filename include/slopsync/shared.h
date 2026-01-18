#ifndef SLOPSYNC_SHARED_H
#define SLOPSYNC_SHARED_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef uint8_t ssync_player_id_t;
typedef uint32_t ssync_timestamp_t;

typedef enum {
	SSYNC_OBJ_OBSERVER = 1 << 0,
	SSYNC_OBJ_GLOBAL   = 1 << 1,
	SSYNC_OBJ_ONESHOT  = 1 << 2,
} ssync_obj_flag_t;

typedef int ssync_obj_flags_t;

typedef enum {
	SSYNC_PROP_INTERPOLATE = 1 << 0,
	SSYNC_PROP_EXTRAPOLATE = 1 << 1,
	SSYNC_PROP_POSITION_X  = 1 << 2,
	SSYNC_PROP_POSITION_Y  = 1 << 3,
	SSYNC_PROP_POSITION_Z  = 1 << 4,
	SSYNC_PROP_ROTATION    = 1 << 5,
	SSYNC_PROP_RADIUS      = 1 << 6,
} ssync_prop_flag_t;

typedef int ssync_prop_flags_t;

typedef struct {
	uint16_t bin;
	uint16_t index;
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

#endif
