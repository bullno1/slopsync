#include "slopnetd.h"
#include <slopsync/server.h>

typedef struct {
	snetd_env_t* env;
	ssyncd_t* ssd;
	double dt;
} slopsyncd_t;

static void*
ssyncd_realloc(void* userdata, void* ptr, size_t size) {
	slopsyncd_t* ssd = userdata;
	return snetd_realloc(ssd->env, ptr, size);
}

static void
ssyncd_send(void* userdata, int receiver, ssync_blob_t message, bool reliable) {
	slopsyncd_t* ssd = userdata;
	snetd_send(ssd->env, receiver, message.data, message.size, reliable);
}

static void*
init(snetd_env_t* env, const snetd_game_options_t* options) {
	slopsyncd_t* ssd = snetd_malloc(env, sizeof(slopsyncd_t));
	ssyncd_config_t config = {
		.max_num_players = options->max_num_players,
		.max_message_size = 1100 * 4,
		.net_tick_rate = 20,  // TODO: pass this from options
		.logic_tick_rate = 30,
		.realloc = ssyncd_realloc,
		.send_msg = ssyncd_send,
		.userdata = ssd,
	};
	*ssd = (slopsyncd_t){
		.env = env,
		.ssd = ssyncd_init(&config),
		.dt = 1.0 / (double)config.logic_tick_rate,
	};
	if (ssd->ssd == NULL) { snetd_terminate(env); }

	return ssd;
}

static void
cleanup(void* ctx) {
	slopsyncd_t* ssd = ctx;
	if (ssd->ssd != NULL) { ssyncd_cleanup(ssd->ssd); }

	snetd_free(ssd->env, ssd);
}

static void
event(void* ctx, const snetd_event_t* event) {
	slopsyncd_t* ssd = ctx;
	switch (event->type) {
		case SNETD_EVENT_TICK:
			ssyncd_update(ssd->ssd, ssd->dt);
			break;
		case SNETD_EVENT_BROADCAST:
			ssyncd_broadcast(ssd->ssd);
			break;
		case SNETD_EVENT_PLAYER_JOINING:
			snetd_allow_join(ssd->env);
			break;
		case SNETD_EVENT_PLAYER_JOINED:
			ssyncd_add_player(ssd->ssd, event->player_joined.player_index, event->player_joined.username);
			break;
		case SNETD_EVENT_PLAYER_LEFT:
			ssyncd_remove_player(ssd->ssd, event->player_joined.player_index);
			break;
		case SNETD_EVENT_MESSAGE:
			ssyncd_process_message(
				ssd->ssd,
				(ssync_blob_t){
					.data = event->message.data,
					.size = event->message.size,
				},
				event->message.sender
			);
			break;
	}
}

static snetd_t ssyncd = {
	.init = init,
	.cleanup = cleanup,
	.event = event,
};

SNETD_ENTRY(ssyncd)
