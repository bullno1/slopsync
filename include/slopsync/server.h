#ifndef SLOPSYNC_SERVER_H
#define SLOPSYNC_SERVER_H

#include "shared.h"

typedef struct ssyncd_s ssyncd_t;

typedef struct {
	ssync_tick_t current_tick;
	ssync_tick_t net_tick_rate;
	ssync_tick_t logic_tick_rate;
} ssyncd_info_t;

typedef struct {
	size_t max_message_size;
	ssync_tick_t net_tick_rate;
	ssync_tick_t logic_tick_rate;

	ssync_realloc_fn_t realloc;
	ssync_send_msg_fn_t send_msg;

	void* userdata;
} ssyncd_config_t;

ssyncd_t*
ssyncd_init(const ssyncd_config_t* config);

void
ssyncd_cleanup(ssyncd_t* ssyncd);

void
ssyncd_process_message(ssyncd_t* ssyncd, ssync_blob_t msg);

void
ssyncd_update(ssyncd_t* ssyncd, double dt);

const ssyncd_info_t*
ssyncd_info(ssyncd_t* ssyncd);

#endif
