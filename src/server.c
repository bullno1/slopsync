// vim: set foldmethod=marker foldlevel=0:
#include <slopsync/server.h>
#include "internal.h"

typedef struct {
	ssync_timestamp_t created_at;
	ssync_timestamp_t updated_at;
	ssync_obj_flags_t flags;
	int authority;
	ssync_obj_t data;
} ssyncd_obj_info_t;

typedef struct {
	const char* username;
	uint16_t obj_id_bin;
	ssync_snapshot_pool_t incoming_archive;
	ssync_snapshot_pool_t outgoing_archive;
} ssyncd_player_info_t;

struct ssyncd_s {
	ssyncd_config_t config;

	uint16_t obj_id_bin;

	ssync_snapshot_pool_t snapshot_pool;
	void* outgoing_packet_buf;
	void* record_buf;

	BHASH_TABLE(ssync_net_id_t, ssyncd_obj_info_t) objects;
};

// Allocator {{{

void*
ssyncd_host_realloc(void* ptr, size_t size, void* ctx) {
	ssyncd_t* ssync = ctx;
	return ssync->config.realloc(ssync->config.userdata, ptr, size);
}

static void*
ssyncd_blib_realloc(void* ptr, size_t size, void* ctx) {
	return ssyncd_host_realloc(ptr, size, ctx);
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

ssyncd_t*
ssyncd_init(const ssyncd_config_t* config) {
	ssyncd_t* ssd = ssyncd_malloc(config, sizeof(ssyncd_t*));
	*ssd = (ssyncd_t){
		.config = *config,
	};
	return ssd;
}

void
ssyncd_cleanup(ssyncd_t* ssyncd) {
	ssyncd_free(&ssyncd->config, ssyncd);
}

void
ssyncd_process_message(ssyncd_t* ssyncd, ssync_blob_t msg, int sender) {
}

void
ssyncd_add_player(ssyncd_t* ssyncd, int id, const char* username) {
}

void
ssyncd_remove_player(ssyncd_t* ssyncd, int id) {
}

void
ssyncd_update(ssyncd_t* ssyncd, double dt) {
}

#define BLIB_REALLOC ssyncd_blib_realloc
#define BLIB_IMPLEMENTATION
#include <bsv.h>
#include <barray.h>
#include <bhash.h>
