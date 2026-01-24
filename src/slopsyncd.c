#include "slopnetd.h"
#include <slopsync/server.h>
#include <blog.h>
#include <string.h>

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

static void
blog_snetd(const blog_ctx_t* ctx, blog_str_t msg, void* userdata) {
	char buf[2048];
	const char* level = "";
	switch (ctx->level) {
		case BLOG_LEVEL_TRACE: level = "TRACE"; break;
		case BLOG_LEVEL_DEBUG: level = "DEBUG"; break;
		case BLOG_LEVEL_INFO: level = "INFO"; break;
		case BLOG_LEVEL_WARN: level = "WARN"; break;
		case BLOG_LEVEL_ERROR: level = "ERROR"; break;
		case BLOG_LEVEL_FATAL: level = "FATAL";  break;
	}
	snprintf(
		buf, sizeof(buf),
		"%s:%.*s:%d: %.*s",
		level,
		ctx->file.len, ctx->file.data,
		ctx->line,
		msg.len, msg.data
	);

	snetd_log(userdata, buf);
}

static void*
init(snetd_env_t* env, const snetd_game_options_t* options) {
	blog_init(&(blog_options_t){
		.current_filename = __FILE__,
		.current_depth_in_project = 2,
	});
	blog_add_logger(BLOG_LEVEL_TRACE, blog_snetd, env);

	slopsyncd_t* ssd = snetd_malloc(env, sizeof(slopsyncd_t));
	ssyncd_config_t config = {
		.max_num_players = options->max_num_players,
		.max_message_size = 1100 * 4,
		.net_tick_rate = 20,  // TODO: pass this from options
		.logic_tick_rate = 30,
		.realloc = ssyncd_realloc,
		.send_msg = ssyncd_send,
		.obj_schema = {
			.data = options->creation_data,
			.size = options->creation_data ? strlen(options->creation_data) : 0,
		},
		.userdata = ssd,
	};
	*ssd = (slopsyncd_t){
		.env = env,
		.dt = 1.0 / (double)config.logic_tick_rate,
	};
	ssd->ssd = ssyncd_init(&config);
	if (ssd->ssd == NULL) {
		BLOG_ERROR("Failed to initialize");
		snetd_terminate(env);
	}

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
			ssyncd_add_player(
				ssd->ssd,
				event->player_joined.player_index,
				event->player_joined.username
			);
			break;
		case SNETD_EVENT_PLAYER_LEFT:
			ssyncd_remove_player(ssd->ssd, event->player_left.player_index);
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

#define BLIB_IMPLEMENTATION
#include <blog.h>
