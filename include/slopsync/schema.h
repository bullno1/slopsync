#ifndef SSYNC_STATIC_SCHEMA_H
#define SSYNC_STATIC_SCHEMA_H

#include <stdint.h>
#include <stddef.h>

struct ssync_s;

typedef struct {
	const char* filename;
	uint64_t hash;
	size_t size;
	const char* content;
} ssync_static_schema_t;

void
ssync_sync_static_schema(struct ssync_s* ssync, const ssync_static_schema_t* schema);

#endif
