#ifndef SLOPSYNC_TEST_SHARED_H
#define SLOPSYNC_TEST_SHARED_H

#include <btest.h>
#include <barena.h>

typedef struct {
	barena_pool_t pool;
	barena_t arena;
} fixture_t;

static fixture_t fixture;

static inline void
init_per_suite(void) {
	barena_pool_init(&fixture.pool, 1);
}

static inline void
cleanup_per_suite(void) {
	barena_pool_cleanup(&fixture.pool);
}

static inline void
init_per_test(void) {
	barena_init(&fixture.arena, &fixture.pool);
}

static inline void
cleanup_per_test(void) {
	barena_reset(&fixture.arena);
}

#endif
