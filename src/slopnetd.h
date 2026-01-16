#ifndef SLOPNETD_H
#define SLOPNETD_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

typedef struct snetd_env_s snetd_env_t;

struct snetd_env_s {
	void* (*realloc)(snetd_env_t* env, void* ptr, size_t size);
	void (*log)(snetd_env_t* env, const char* message);

	void (*allow_join)(snetd_env_t* env);
	void (*forbid_join)(snetd_env_t* env, const char* reason);

	void (*send)(snetd_env_t* env, int player_index, const void* data, size_t size, bool reliable);
	void (*kick)(snetd_env_t* env, int player_index);
	void (*terminate)(snetd_env_t* env);
};

typedef enum {
	SNETD_EVENT_TICK,
	SNETD_EVENT_BROADCAST,
	SNETD_EVENT_PLAYER_JOINING,
	SNETD_EVENT_PLAYER_JOINED,
	SNETD_EVENT_PLAYER_LEFT,
	SNETD_EVENT_MESSAGE,
} snetd_event_type_t;

typedef struct {
	uint64_t tick;
} snetd_tick_t;

typedef struct {
	const char* username;
	int player_index;
} snetd_player_joined_t;

typedef struct {
	int player_index;
} snetd_player_left_t;

typedef struct {
	const char* username;
} snetd_player_joining_t;

typedef struct {
	const void* data;
	size_t size;
	int sender;
} snetd_message_t;

typedef struct {
	const char* created_by;
	const char* creation_data;
	const char* extra_data;
	int max_num_players;
} snetd_game_options_t;

typedef struct {
	snetd_event_type_t type;
	union {
		snetd_tick_t tick;
		snetd_player_joining_t player_joining;
		snetd_player_joined_t player_joined;
		snetd_player_left_t player_left;
		snetd_message_t message;
	};
} snetd_event_t;

typedef struct {
	void* (*init)(snetd_env_t* env, const snetd_game_options_t* options);
	void (*cleanup)(void* ctx);
	void (*event)(void* ctx, const snetd_event_t* event);
} snetd_t;

static inline void*
snetd_realloc(snetd_env_t* env, void* ptr, size_t size) {
	return env->realloc(env, ptr, size);
}

static inline void*
snetd_malloc(snetd_env_t* env, size_t size) {
	return snetd_realloc(env, NULL, size);
}

static inline void
snetd_free(snetd_env_t* env, void* ptr) {
	snetd_realloc(env, ptr, 0);
}

static inline void
snetd_log(snetd_env_t* env, const char* message) {
	env->log(env, message);
}

static inline void
snetd_send(snetd_env_t* env, int player_index, const void* data, size_t size, bool reliable) {
	env->send(env, player_index, data, size, reliable);
}

static inline void
snetd_allow_join(snetd_env_t* env) {
	env->allow_join(env);
}

static inline void
snetd_forbid_join(snetd_env_t* env, const char* reason) {
	env->forbid_join(env, reason);
}

static inline void
snetd_kick(snetd_env_t* env, int player) {
	env->kick(env, player);
}

static inline void
snetd_terminate(snetd_env_t* env) {
	env->terminate(env);
}

#define SNETD_ENTRY(ENTRY) \
	__attribute__((visibility("default"))) \
	snetd_t* snetd_entry(void) { return &ENTRY; }

#endif
