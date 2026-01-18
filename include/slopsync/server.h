#ifndef SLOPSYNC_SERVER_H
#define SLOPSYNC_SERVER_H

#include "shared.h"

typedef struct ssyncd_s ssyncd_t;

typedef struct {
	ssync_timestamp_t current_time;
	ssync_timestamp_t net_tick_rate;
	ssync_timestamp_t logic_tick_rate;
} ssyncd_info_t;

typedef void (*ssyncd_send_msg_fn_t)(
	void* userdata,
	int receiver,
	ssync_blob_t message,
	bool reliable
);

typedef struct {
	size_t max_message_size;
	int max_num_players;

	ssync_timestamp_t net_tick_rate;
	ssync_timestamp_t logic_tick_rate;

	ssync_realloc_fn_t realloc;
	ssyncd_send_msg_fn_t send_msg;

	ssync_blob_t obj_schema;

	void* userdata;
} ssyncd_config_t;

ssyncd_t*
ssyncd_init(const ssyncd_config_t* config);

void
ssyncd_cleanup(ssyncd_t* ssyncd);

void
ssyncd_process_message(ssyncd_t* ssyncd, ssync_blob_t msg, int sender);

void
ssyncd_add_player(ssyncd_t* ssyncd, ssync_blob_t msg, int id, const char* username);

void
ssyncd_remove_player(ssyncd_t* ssyncd, ssync_blob_t msg, int id);

void
ssyncd_update(ssyncd_t* ssyncd, double dt);

const ssyncd_info_t*
ssyncd_info(ssyncd_t* ssyncd);

#endif
