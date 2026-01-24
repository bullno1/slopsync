// vim: set foldmethod=marker foldlevel=0:
#ifndef SLOPSYNC_INTERNAL_H
#define SLOPSYNC_INTERNAL_H

#include <slopsync/shared.h>
#include <bsv.h>
#include <bhash.h>
#include <barray.h>
#include <limits.h>
#include <stdlib.h>
#include <blog.h>
#include "bitstream.h"
#include "jtckdint.h"

#ifndef SSYNC_PROP_GROUP_MASK_TYPE
#define SSYNC_PROP_GROUP_MASK_TYPE uint32_t
#endif

#ifndef SSYNC_PROP_MASK_TYPE
#define SSYNC_PROP_MASK_TYPE uint16_t
#endif

typedef SSYNC_PROP_GROUP_MASK_TYPE ssync_prop_group_mask_t;
typedef SSYNC_PROP_MASK_TYPE ssync_prop_mask_t;

typedef enum {
	SSYNC_RECORD_TYPE_END = 0,
	SSYNC_RECORD_TYPE_INIT,
	SSYNC_RECORD_TYPE_SNAPSHOT_INFO,
	SSYNC_RECORD_TYPE_OBJ_CREATE,
	SSYNC_RECORD_TYPE_OBJ_UPDATE,
	SSYNC_RECORD_TYPE_OBJ_DESTROY,
	SSYNC_RECORD_TYPE_OBJ_CONTROL,
	SSYNC_RECORD_TYPE_OBJ_HANDOVER,

	SSYNC_RECORD_TYPE_COUNT,
} ssync_record_type_t;

typedef struct {
	ssync_player_id_t player_id;
	ssync_timestamp_t logic_tick_rate;
	ssync_timestamp_t net_tick_rate;
	ssync_timestamp_t current_time;
	uint16_t obj_id_bin;
} ssync_init_record_t;

typedef struct {
	ssync_timestamp_t current_time;
	ssync_timestamp_t last_received;
} ssync_snapshot_info_record_t;

typedef int64_t ssync_prop_t;

typedef struct {
	ssync_prop_group_mask_t prop_group_mask;
	barray(ssync_prop_t) props;
} ssync_obj_t;

typedef struct {
	ssync_net_id_t id;
	ssync_timestamp_t timestamp;
	ssync_obj_flags_t flags;
} ssync_obj_create_record_t;

typedef struct {
	ssync_net_id_t id;
	ssync_timestamp_t timestamp;
} ssync_obj_destroy_record_t;

typedef enum {
	SSYNC_PROP_GROUP_OP_ADD = 0,
	SSYNC_PROP_GROUP_OP_UPDATE,
	SSYNC_PROP_GROUP_OP_REMOVE,
} ssync_prop_group_op_t;

typedef struct ssync_snapshot_s ssync_snapshot_t;

struct ssync_snapshot_s {
	ssync_snapshot_t* next;
	ssync_snapshot_t* remote;
	ssync_timestamp_t timestamp;

	BHASH_TABLE(ssync_net_id_t, ssync_obj_t) objects;
};

typedef struct {
	ssync_snapshot_t* next;
	int count;
} ssync_snapshot_pool_t;

typedef enum {
	SSYNC_PROP_TYPE_INT,
	SSYNC_PROP_TYPE_FLOAT,
	SSYNC_PROP_TYPE_BINARY,
} ssync_prop_type_t;

typedef struct {
	ssync_prop_type_t type;
	int precision;
	ssync_prop_flags_t flags;
} ssync_prop_schema_t;

typedef struct {
	int num_props;
	ssync_prop_schema_t props[sizeof(ssync_prop_mask_t) * CHAR_BIT];
} ssync_prop_group_schema_t;

typedef struct {
	int num_prop_groups;
	ssync_prop_group_schema_t prop_groups[sizeof(ssync_prop_group_mask_t) * CHAR_BIT];
} ssync_obj_schema_t;

typedef struct {
	bsv_in_t bsv;
	bitstream_in_t* stream;
} ssync_bsv_in_t;

typedef struct {
	bsv_out_t bsv;
	bitstream_out_t* stream;
} ssync_bsv_out_t;

typedef struct {
	bsv_out_t bsv;
	size_t count;
} ssync_bsv_count_t;

typedef struct {
	ssync_snapshot_pool_t* snapshot_pool;
	size_t max_message_size;
	const ssync_obj_schema_t* schema;
	void* record_buf;
	void* memctx;
} ssync_endpoint_config_t;

typedef struct {
	const ssync_endpoint_config_t* config;

	ssync_snapshot_t* last_acked_snapshot;
	ssync_snapshot_pool_t incoming_archive;
	ssync_snapshot_pool_t outgoing_archive;
} ssync_endpoint_t;

typedef struct {
	ssync_endpoint_t* endpoint;

	ssync_snapshot_t* snapshot;
	const ssync_snapshot_t* base_snapshot;
	ssync_snapshot_t* tmp_snapshot;

	bitstream_out_t* packet_stream;

	ssync_timestamp_t current_time_ms;

	bool has_space;
} ssync_outgoing_snapshot_ctx_t;

typedef struct {
	ssync_endpoint_t* endpoint;

	bitstream_in_t* packet_stream;
	bsv_ctx_t* packet_bsv;

	ssync_snapshot_t* incoming_snapshot;
	bool can_read;
} ssync_incoming_packet_ctx_t;

typedef struct {
	bitstream_out_t stream;
	bsv_ctx_t bsv;
	ssync_bsv_out_t bsv_out;
} ssync_record_ctx_t;

extern void*
ssync_host_realloc(void* ptr, size_t size, void* ctx);

// Object {{{

static inline void
ssync_cleanup_obj(ssync_obj_t* obj, void* memctx) {
	barray_free(obj->props, memctx);
}

static inline void
ssync_copy_obj(ssync_obj_t* dst, const ssync_obj_t* src, void* memctx) {
	dst->prop_group_mask = src->prop_group_mask;
	size_t num_props = barray_len(src->props);
	barray_resize(dst->props, num_props, memctx);
	memcpy(dst->props, src->props, num_props * sizeof(ssync_prop_t));
}

static inline bool
ssync_obj_has_prop_group(const ssync_obj_t* obj, int index) {
	return obj->prop_group_mask & (1 << index);
}

// }}}

// Snapshot {{{

static inline void
ssync_reinit_snapshot(ssync_snapshot_t* snapshot, void* memctx) {
	bhash_config_t config = bhash_config_default();
	config.memctx = memctx;
	config.removable = false;
	bhash_reinit(&snapshot->objects, config);
}

static inline void
ssync_reinit_snapshot_pool(ssync_snapshot_pool_t* pool, void* memctx) {
	for (ssync_snapshot_t* itr = pool->next; itr != NULL; itr = itr->next) {
		ssync_reinit_snapshot(itr, memctx);
	}
}

static inline void
ssync_clear_snapshot(ssync_snapshot_t* snapshot, void* memctx) {
	for (bhash_index_t i = 0; i < bhash_len(&snapshot->objects); ++i) {
		ssync_cleanup_obj(&snapshot->objects.values[i], memctx);
	}
}

static inline void
ssync_destroy_snapshot(ssync_snapshot_t* snapshot, void* memctx) {
	ssync_clear_snapshot(snapshot, memctx);
	bhash_cleanup(&snapshot->objects);
	ssync_host_realloc(snapshot, 0, memctx);
}

static inline void
ssync_cleanup_snapshot_pool(ssync_snapshot_pool_t* pool, void* memctx) {
	for (ssync_snapshot_t* itr = pool->next; itr != NULL;) {
		ssync_snapshot_t* next = itr->next;
		ssync_destroy_snapshot(itr, memctx);
		itr = next;
	}
	pool->count = 0;
}

static inline ssync_snapshot_t*
ssync_acquire_snapshot(ssync_snapshot_pool_t* pool, ssync_timestamp_t timestamp, void* memctx) {
	ssync_snapshot_t* snapshot;
	if (pool->next != NULL) {
		snapshot = pool->next;
		pool->next = snapshot->next;
		pool->count -= 1;

		ssync_clear_snapshot(snapshot, memctx);
		bhash_clear(&snapshot->objects);
	} else {
		snapshot = ssync_host_realloc(NULL, sizeof(ssync_snapshot_t), memctx);
		*snapshot = (ssync_snapshot_t){ 0 };
		ssync_reinit_snapshot(snapshot, memctx);
	}

	snapshot->timestamp = timestamp;
	snapshot->remote = NULL;
	return snapshot;
}

static inline void
ssync_release_snapshot(ssync_snapshot_pool_t* pool, ssync_snapshot_t* snapshot) {
	snapshot->next = pool->next;
	pool->next = snapshot;
	pool->count += 1;
}

static inline void
ssync_release_archive(ssync_snapshot_pool_t* pool, ssync_snapshot_pool_t* archive) {
	ssync_snapshot_t* itr = archive->next;

	while (itr != NULL) {
		ssync_snapshot_t* to_release = itr;
		itr = itr->next;
		ssync_release_snapshot(pool, to_release);
	}

	archive->next = NULL;
	archive->count = 0;
}

static inline bool
ssync_archive_snapshot(ssync_snapshot_pool_t* archive, ssync_snapshot_t* snapshot) {
	ssync_snapshot_t** itr = &archive->next;

	// Traverse the list to find the insertion point
	while (*itr != NULL) {
		if ((*itr)->timestamp == snapshot->timestamp) {
			// Duplicate timestamp found, do not insert
			return false;
		}

		if ((*itr)->timestamp < snapshot->timestamp) {
			break;
		}
		itr = &(*itr)->next;
	}

	// Insert the snapshot
	snapshot->next = *itr;
	*itr = snapshot;
	archive->count += 1;
	return true;
}

static inline void
ssync_release_after(ssync_snapshot_pool_t* pool, ssync_snapshot_pool_t* from, ssync_snapshot_t* snapshot) {
    ssync_snapshot_t* itr = snapshot->next;
    while (itr != NULL) {
        ssync_snapshot_t* to_release = itr;
        itr = itr->next;
        ssync_release_snapshot(pool, to_release);
		from->count -= 1;
    }
    snapshot->next = NULL;
}

static inline ssync_snapshot_t*
ssync_ack_snapshot(
	ssync_snapshot_pool_t* archive,
	ssync_snapshot_pool_t* pool,
	ssync_timestamp_t timestamp
) {
	ssync_snapshot_t* itr = archive->next;
	while (itr != NULL) {
		if (itr->timestamp == timestamp) {
			ssync_release_after(pool, archive, itr);
			return itr;
		}

		if (itr->timestamp < timestamp) {
			// Since list is in descending order, no match exists
			return NULL;
		}

		itr = itr->next;
	}

	return NULL;
}

static inline const ssync_snapshot_t*
ssync_find_snapshot_pair(const ssync_snapshot_pool_t* archive, ssync_timestamp_t timestamp) {
	const ssync_snapshot_t* itr = archive->next;

	while (itr != NULL && itr->next != NULL) {
		const ssync_snapshot_t* a = itr;
		const ssync_snapshot_t* b = itr->next;

		if (b->timestamp <= timestamp && timestamp <= a->timestamp) {
			return a;
		}

		if (timestamp > a->timestamp) {
			return NULL;
		}

		itr = itr->next;
	}

	return NULL;
}

// }}}

// bsv {{{

bsv_in_t*
ssync_init_bsv_in(ssync_bsv_in_t* bsv_in, bitstream_in_t* stream);

bsv_out_t*
ssync_init_bsv_out(ssync_bsv_out_t* bsv_out, bitstream_out_t* stream);

bsv_out_t*
ssync_init_bsv_count(ssync_bsv_count_t* bsv_count);

// }}}

// Records {{{

#if defined(__GNUC__) || defined(__clang__)
#define BITS_USE_CLZ
#elif defined(_MSC_VER)
#include <intrin.h>
#pragma intrinsic(__lzcnt64)
#define BITS_USE_LZCNT
#endif

static inline size_t
bits_required(size_t count) {
	if (count <= 1) { return 0; }

	unsigned long long val = (unsigned long long)count - 1;

#if defined(BITS_USE_CLZ)
	return (sizeof(unsigned long long) * 8) - __builtin_clzll(val);
#elif defined(BITS_USE_LZCNT)
	return 64 - __lzcnt64(val);
#else
	size_t bits = 0;
	while (val != 0) {
		bits++;
		val >>= 1;
	}
	return bits;
#endif
}

static inline bool
ssync_write_record_type(bitstream_out_t* stream, ssync_record_type_t type) {
	size_t record_type_bits = bits_required(SSYNC_RECORD_TYPE_COUNT);

	uint8_t data = (uint8_t)type;
	return bitstream_write(stream, &data, record_type_bits);
}

static inline bool
ssync_read_record_type(bitstream_in_t* stream, ssync_record_type_t* type) {
	size_t record_type_bits = bits_required(SSYNC_RECORD_TYPE_COUNT);
	uint8_t data = 0;
	bool result = bitstream_read(stream, &data, record_type_bits);
	*type = (ssync_record_type_t)data;
	return result;
}

static inline bsv_status_t
bsv_u16_fixed(bsv_ctx_t* ctx, uint16_t* u16) {
	uint8_t data[2];

	if (bsv_mode(ctx) == BSV_MODE_WRITE) {
		data[0] = (uint8_t)((*u16 >> 0) & 0xFF);
		data[1] = (uint8_t)((*u16 >> 8) & 0xFF);
	}

	BSV_CHECK_STATUS(bsv_raw(ctx, data, sizeof(data)));

	if (bsv_mode(ctx) == BSV_MODE_READ) {
		*u16 = ((uint16_t)data[0]) << 0
			 | ((uint16_t)data[1]) << 8;
	}

	return bsv_status(ctx);
}

static inline bsv_status_t
bsv_u32_fixed(bsv_ctx_t* ctx, uint32_t* u32) {
	uint8_t data[4];

	if (bsv_mode(ctx) == BSV_MODE_WRITE) {
		data[0] = (uint8_t)((*u32 >>  0) & 0xFF);
		data[1] = (uint8_t)((*u32 >>  8) & 0xFF);
		data[2] = (uint8_t)((*u32 >> 16) & 0xFF);
		data[3] = (uint8_t)((*u32 >> 24) & 0xFF);
	}

	BSV_CHECK_STATUS(bsv_raw(ctx, data, sizeof(data)));

	if (bsv_mode(ctx) == BSV_MODE_READ) {
		*u32 = ((uint32_t)data[0]) <<  0
			 | ((uint32_t)data[1]) <<  8
			 | ((uint32_t)data[2]) << 16
			 | ((uint32_t)data[3]) << 24;
	}

	return bsv_status(ctx);
}

static inline bsv_status_t
bsv_ssync_net_id(bsv_ctx_t* ctx, ssync_net_id_t* id) {
	_Static_assert(sizeof(id->bin) == sizeof(uint16_t), "Type mismatch");
	BSV_CHECK_STATUS(bsv_u16_fixed(ctx, &id->bin));
	BSV_CHECK_STATUS(bsv_u16_fixed(ctx, &id->index));
	return bsv_status(ctx);
}

static inline bsv_status_t
bsv_ssync_timestamp(bsv_ctx_t* ctx, ssync_timestamp_t* timestamp) {
	_Static_assert(sizeof(*timestamp) == sizeof(uint32_t), "Type mismatch");
	BSV_CHECK_STATUS(bsv_u32_fixed(ctx, timestamp));
	return bsv_status(ctx);
}

static inline bsv_status_t
bsv_ssync_snapshot_info_record(bsv_ctx_t* ctx, ssync_snapshot_info_record_t* rec) {
	BSV_CHECK_STATUS(bsv_ssync_timestamp(ctx, &rec->current_time));
	BSV_CHECK_STATUS(bsv_ssync_timestamp(ctx, &rec->last_received));
	return bsv_status(ctx);
}

static inline bsv_status_t
bsv_ssync_init_record(bsv_ctx_t* ctx, ssync_init_record_t* rec) {
	BSV_CHECK_STATUS(bsv_ssync_timestamp(ctx, &rec->current_time));
	BSV_CHECK_STATUS(bsv_auto(ctx, &rec->player_id));
	BSV_CHECK_STATUS(bsv_auto(ctx, &rec->net_tick_rate));
	BSV_CHECK_STATUS(bsv_auto(ctx, &rec->logic_tick_rate));
	BSV_CHECK_STATUS(bsv_auto(ctx, &rec->obj_id_bin));
	return bsv_status(ctx);
}

static inline bsv_status_t
bsv_ssync_obj_create_record(bsv_ctx_t* ctx, ssync_obj_create_record_t* rec) {
	BSV_CHECK_STATUS(bsv_ssync_net_id(ctx, &rec->id));
	BSV_CHECK_STATUS(bsv_ssync_timestamp(ctx, &rec->timestamp));
	BSV_CHECK_STATUS(bsv_auto(ctx, &rec->flags));  // Maybe shrink to bit size
	return bsv_status(ctx);
}

static inline bsv_status_t
bsv_ssync_obj_destroy_record(bsv_ctx_t* ctx, ssync_obj_destroy_record_t* rec) {
	BSV_CHECK_STATUS(bsv_ssync_net_id(ctx, &rec->id));
	BSV_CHECK_STATUS(bsv_ssync_timestamp(ctx, &rec->timestamp));
	return bsv_status(ctx);
}

static inline bsv_status_t
bsv_ssync_prop_schema(bsv_ctx_t* ctx, ssync_prop_schema_t* schema) {
	BSV_BLK(ctx, 0) {
		BSV_REV(0) {
			BSV_ADD(&schema->type);
			BSV_ADD(&schema->precision);
			BSV_ADD(&schema->flags);
		}
	}

	return bsv_status(ctx);
}

static inline bsv_status_t
bsv_ssync_prop_group_schema(bsv_ctx_t* ctx, ssync_prop_group_schema_t* schema) {
	BSV_BLK(ctx, 0) {
		BSV_REV(0) {
			bsv_len_t num_props = schema->num_props;
			BSV_ARRAY(ctx, &num_props) {
				schema->num_props = (int)num_props;
				for (bsv_len_t i = 0; i < num_props; ++i) {
					bsv_ssync_prop_schema(ctx, &schema->props[i]);
				}
			}
		}
	}

	return bsv_status(ctx);
}

static inline bsv_status_t
bsv_ssync_obj_schema(bsv_ctx_t* ctx, ssync_obj_schema_t* schema) {
	// This is exchanged once on game creation, we can afford to use a bit more
	// data and version it
	BSV_BLK(ctx, 0) {
		BSV_REV(0) {
			bsv_len_t num_prop_groups = schema->num_prop_groups;
			BSV_ARRAY(ctx, &num_prop_groups) {
				schema->num_prop_groups = (int)num_prop_groups;
				for (bsv_len_t i = 0; i < num_prop_groups; ++i) {
					bsv_ssync_prop_group_schema(ctx, &schema->prop_groups[i]);
				}
			}
		}
	}

	return bsv_status(ctx);
}

static inline bool
ssync_obj_equal(
	const ssync_obj_schema_t* schema,
	const ssync_obj_t* lhs,
	const ssync_obj_t* rhs
) {
	return lhs->prop_group_mask != rhs->prop_group_mask
		&& memcmp(lhs->props, rhs->props, barray_len(lhs->props) * sizeof(ssync_prop_t)) == 0;
}

static inline bool
ssync_write_bitmask(bitstream_out_t* bitstream, uint32_t mask, size_t num_bits) {
	_Static_assert(sizeof(ssync_prop_group_mask_t) <= sizeof(mask), "Mask type is too small");
	_Static_assert(sizeof(ssync_prop_mask_t) <= sizeof(mask), "Mask type is too small");

	uint8_t bytes[] = {
		(mask >>  0) & 0xFF,
		(mask >>  8) & 0xFF,
		(mask >> 16) & 0xFF,
		(mask >> 24) & 0xFF,
	};

	return bitstream_write(bitstream, bytes, num_bits);
}

static inline bool
ssync_read_bitmask(bitstream_in_t* bitstream, uint32_t* mask, size_t num_bits) {
	_Static_assert(sizeof(ssync_prop_group_mask_t) <= sizeof(mask), "Mask type is too small");
	_Static_assert(sizeof(ssync_prop_mask_t) <= sizeof(mask), "Mask type is too small");

	uint8_t bytes[4] = { 0 };
	bool result = bitstream_read(bitstream, bytes, num_bits);

	*mask = (uint32_t)bytes[0] <<  0
		  | (uint32_t)bytes[1] <<  8
		  | (uint32_t)bytes[2] << 16
		  | (uint32_t)bytes[3] << 24;

	return result;
}

static inline void
ssync_write_obj_update(
	bsv_ctx_t* bsv,
	bitstream_out_t* bitstream,
	const ssync_obj_schema_t* schema,
	const ssync_obj_t* current_obj,
	const ssync_obj_t* previous_obj
) {
	// Gather the update mask
	ssync_prop_group_mask_t update_mask = 0;
	const ssync_prop_t* current_props = current_obj->props;
	const ssync_prop_t* previous_props = previous_obj->props;
	for (int prop_group_index = 0; prop_group_index < schema->num_prop_groups; ++prop_group_index) {
		ssync_prop_group_mask_t mask = 1 << prop_group_index;
		const ssync_prop_group_schema_t* prop_group = &schema->prop_groups[prop_group_index];
		int num_props = prop_group->num_props;
		bool current_has_prop_group = (current_obj->prop_group_mask & mask) != 0;
		bool previous_has_prop_group = (previous_obj->prop_group_mask & mask) != 0;

		if (current_has_prop_group) {
			if (previous_has_prop_group) { // Both have this prop group, compare values
				if (memcmp(current_props, previous_props, num_props * sizeof(ssync_prop_t)) != 0) {
					update_mask |= mask;
				}
			} else {  // Only current has it
				update_mask |= mask;
			}
		} else {
			if (previous_has_prop_group) {  // Only previous has it
				update_mask |= mask;
			}
		}

		if (current_has_prop_group) { current_props += num_props; }
		if (previous_has_prop_group) { previous_props += num_props; }
	}

	ssync_write_bitmask(bitstream, update_mask, schema->num_prop_groups);

	// Write each update
	current_props = current_obj->props;
	previous_props = previous_obj->props;
	for (int prop_group_index = 0; prop_group_index < schema->num_prop_groups; ++prop_group_index) {
		ssync_prop_group_mask_t mask = 1 << prop_group_index;
		const ssync_prop_group_schema_t* prop_group = &schema->prop_groups[prop_group_index];
		int num_props = prop_group->num_props;
		bool current_has_prop_group = (current_obj->prop_group_mask & mask) != 0;
		bool previous_has_prop_group = (previous_obj->prop_group_mask & mask) != 0;

		if ((update_mask & mask) != 0) {
			if (current_has_prop_group) {
				if (previous_has_prop_group) {  // Write diff for each prop
					bitstream_write(bitstream, &(uint8_t){ SSYNC_PROP_GROUP_OP_UPDATE }, 2);

					// Write update for each prop
					for (int prop_index = 0; prop_index < num_props; ++prop_index) {
						ssync_prop_t current_value = current_props[prop_index];
						ssync_prop_t previous_value = previous_props[prop_index];
						if (current_value != previous_value) {
							bitstream_write(bitstream, &(uint8_t){ 1 }, 1);

							// Pick between delta and full value encoding
							int64_t out_value;

							// Diff for delta
							int64_t diff;
							if (!ckd_sub(&diff, current_value, previous_value)) {
								diff = INT64_MAX;
							}

							uint8_t method;
							if (llabs(diff) < llabs(current_value)) {  // Delta
								method = 1;
								out_value = diff;
							} else {  // Full value
								method = 0;
								out_value = current_value;
							}

							bitstream_write(bitstream, &method, 1);
							bsv_auto(bsv, &out_value);
						} else {
							bitstream_write(bitstream, &(uint8_t){ 0 }, 1);
						}
					}
				} else {  // Fully write all props
					bitstream_write(bitstream, &(uint8_t){ SSYNC_PROP_GROUP_OP_ADD }, 2);

					for (int prop_index = 0; prop_index < num_props; ++prop_index) {
						ssync_prop_t value = current_props[prop_index];
						bsv_auto(bsv, &value);
					}
				}
			} else {
				if (previous_has_prop_group) {  // Remove
					bitstream_write(bitstream, &(uint8_t){ SSYNC_PROP_GROUP_OP_REMOVE }, 2);
				}
			}
		}

		if (current_has_prop_group) { current_props += num_props; }
		if (previous_has_prop_group) { previous_props += num_props; }
	}
}

static inline void
ssync_reset_obj(ssync_obj_t* obj) {
	obj->prop_group_mask = 0;
	barray_clear(obj->props);
}

static inline bool
ssync_read_obj_update(
	bsv_ctx_t* bsv,
	bitstream_in_t* bitstream,
	void* memctx,
	const ssync_obj_schema_t* schema,
	const ssync_obj_t* previous_obj,
	ssync_obj_t* current_obj
) {
	ssync_prop_group_mask_t update_mask;
	if (!ssync_read_bitmask(bitstream, &update_mask, schema->num_prop_groups)) {
		return false;
	}

	ssync_reset_obj(current_obj);

	const ssync_prop_t* previous_props = previous_obj->props;

	for (int prop_group_index = 0; prop_group_index < schema->num_prop_groups; ++prop_group_index) {
		ssync_prop_group_mask_t mask = 1 << prop_group_index;
		const ssync_prop_group_schema_t* prop_group = &schema->prop_groups[prop_group_index];
		int num_props = prop_group->num_props;
		bool previous_has_prop_group = (previous_obj->prop_group_mask & mask) != 0;

		if (update_mask & mask) {  // change
			uint8_t op;
			if (!bitstream_read(bitstream, &op, 2)) { return false; }

			switch ((ssync_prop_group_op_t)op) {
				case SSYNC_PROP_GROUP_OP_ADD:
					current_obj->prop_group_mask |= mask;
					for (int prop_index = 0; prop_index < num_props; ++prop_index) {
						ssync_prop_t value;
						if (bsv_auto(bsv, &value) != BSV_OK) { return false; }

						barray_push(current_obj->props, value, memctx);
					}
					break;
				case SSYNC_PROP_GROUP_OP_REMOVE:
					// Do nothing
					break;
				case SSYNC_PROP_GROUP_OP_UPDATE:
					if (!previous_has_prop_group) { return false; }

					current_obj->prop_group_mask |= mask;
					for (int prop_index = 0; prop_index < num_props; ++prop_index) {
						uint8_t changed;
						if (!bitstream_read(bitstream, &changed, 1)) { return false; }
						if (!changed) { continue; }

						uint8_t delta;
						if (!bitstream_read(bitstream, &delta, 1)) { return false; }
						int64_t value;
						if (bsv_auto(bsv, &value) != BSV_OK) { return false; }

						int64_t new_value = delta ? previous_props[prop_index] + value : value;
						barray_push(current_obj->props, new_value, memctx);
					}
					break;
				default:
					return false;
			}
		} else {  // No change
			if (previous_has_prop_group) {
				for (int prop_index = 0; prop_index < num_props; ++prop_index) {
					barray_push(current_obj->props, previous_props[prop_index], memctx);
				}
				current_obj->prop_group_mask |= mask;
			}
		}

		if (previous_has_prop_group) { previous_props += num_props; }
	}

	return true;
}

static inline void
ssync_end_packet(bitstream_out_t* packet_stream) {
	if (packet_stream->bit_pos % 8 != 0) {
		ssync_write_record_type(packet_stream, SSYNC_RECORD_TYPE_END);
	}
}

// }}}

// Endpoint {{{

static inline void
ssync_reinit_endpoint(ssync_endpoint_t* endpoint, const ssync_endpoint_config_t* config) {
	endpoint->config = config;
	ssync_reinit_snapshot_pool(&endpoint->incoming_archive, config->memctx);
	ssync_reinit_snapshot_pool(&endpoint->outgoing_archive, config->memctx);
}

static inline void
ssync_cleanup_endpoint(ssync_endpoint_t* endpoint) {
	ssync_release_archive(endpoint->config->snapshot_pool, &endpoint->incoming_archive);
	ssync_release_archive(endpoint->config->snapshot_pool, &endpoint->outgoing_archive);
	endpoint->last_acked_snapshot = NULL;
}

static inline void
ssync_prepare_record_buf(
	ssync_record_ctx_t* record,
	ssync_outgoing_snapshot_ctx_t* ctx
) {
	record->stream = (bitstream_out_t){
		.data = ctx->endpoint->config->record_buf,
		.num_bytes = ctx->endpoint->config->max_message_size,
	};
	record->bsv = (bsv_ctx_t){
		.out = ssync_init_bsv_out(&record->bsv_out, &record->stream),
	};
}

static inline void
ssync_begin_outgoing_snapshot(
	ssync_outgoing_snapshot_ctx_t* ctx,
	ssync_endpoint_t* endpoint,
	ssync_timestamp_t current_time_ms,
	bitstream_out_t* out_stream,
	bsv_ctx_t* out_bsv
) {
	ssync_snapshot_info_record_t snapshot_info = {
		.current_time = current_time_ms,
	};
	ssync_snapshot_t* remote_snapshot = endpoint->incoming_archive.next;
	if (remote_snapshot != NULL) {
		snapshot_info.last_received = remote_snapshot->timestamp;
	}
	ssync_write_record_type(out_stream, SSYNC_RECORD_TYPE_SNAPSHOT_INFO);
	bsv_ssync_snapshot_info_record(out_bsv, &snapshot_info);

	ssync_snapshot_t* snapshot = ssync_acquire_snapshot(
		endpoint->config->snapshot_pool,
		current_time_ms,
		endpoint->config->memctx
	);
	snapshot->remote = remote_snapshot;

	const ssync_snapshot_t* base_snapshot = endpoint->last_acked_snapshot;
	ssync_snapshot_t* tmp_snapshot = NULL;
	if (base_snapshot == NULL) {
		base_snapshot = tmp_snapshot = ssync_acquire_snapshot(
			endpoint->config->snapshot_pool,
			0, endpoint->config->memctx
		);
	}

	*ctx = (ssync_outgoing_snapshot_ctx_t){
		.endpoint = endpoint,

		.packet_stream = out_stream,

		.has_space = true,
		.current_time_ms = current_time_ms,
		.snapshot = snapshot,
		.base_snapshot = base_snapshot,
		.tmp_snapshot = tmp_snapshot,
	};
}

static inline void
ssync_end_outgoing_snapshot(ssync_outgoing_snapshot_ctx_t* ctx) {
	// Find all destroyed (omitted) objects
	bhash_index_t num_snapshotted_objects = bhash_len(&ctx->base_snapshot->objects);
	for (bhash_index_t i = 0; i < num_snapshotted_objects; ++i) {
		ssync_net_id_t id = ctx->base_snapshot->objects.keys[i];
		if (bhash_has(&ctx->snapshot->objects, id)) { continue; }

		if (ctx->has_space) {
			ssync_record_ctx_t buf;
			ssync_prepare_record_buf(&buf, ctx);

			ssync_write_record_type(&buf.stream, SSYNC_RECORD_TYPE_OBJ_DESTROY);
			ssync_obj_destroy_record_t record = {
				.id = id,
				.timestamp = ctx->current_time_ms,
			};
			bsv_ssync_obj_destroy_record(&buf.bsv, &record);

			// Try appending to the packet
			ctx->has_space &= bitstream_append(ctx->packet_stream, &buf.stream);
		}

		if (!ctx->has_space) {
			// Put an empty object into the snapshot if not written so we
			// will try resending destruction data in the next snapshot
			ssync_obj_t empty_obj = { 0 };
			bhash_put(&ctx->snapshot->objects, id, empty_obj);
		}
	}

	ssync_endpoint_t* endpoint = ctx->endpoint;
	if (!ssync_archive_snapshot(&endpoint->outgoing_archive, ctx->snapshot)) {
		ssync_release_snapshot(endpoint->config->snapshot_pool, ctx->snapshot);
	}

	if (ctx->tmp_snapshot != NULL) {
		ssync_release_snapshot(endpoint->config->snapshot_pool, ctx->tmp_snapshot);
	}
}

static inline void
ssync_add_outgoing_obj(
	ssync_outgoing_snapshot_ctx_t* ctx,
	ssync_timestamp_t created_at,
	ssync_obj_flags_t flags,
	ssync_net_id_t obj_id,
	const ssync_obj_t* obj
) {
	ssync_endpoint_t* endpoint = ctx->endpoint;
	const ssync_endpoint_config_t* config = endpoint->config;
	const ssync_obj_t* base_obj = bhash_get_value(&ctx->base_snapshot->objects, obj_id);

	if (base_obj == NULL) {  // New obj
		if (ctx->has_space) {
			ssync_record_ctx_t buf;
			ssync_prepare_record_buf(&buf, ctx);

			ssync_write_record_type(&buf.stream, SSYNC_RECORD_TYPE_OBJ_CREATE);
			ssync_obj_create_record_t record = {
				.id = obj_id,
				.timestamp = created_at,
				.flags = flags,
			};
			bsv_ssync_obj_create_record(&buf.bsv, &record);
			ssync_write_bitmask(
				&buf.stream,
				obj->prop_group_mask,
				config->schema->num_prop_groups
			);
			int num_props = (int)barray_len(obj->props);
			for (int i = 0; i < num_props; ++i) {
				bsv_auto(&buf.bsv, &obj->props[i]);
			}

			ctx->has_space &= bitstream_append(ctx->packet_stream, &buf.stream);
		}

		if (ctx->has_space) {
			ssync_obj_t copy = { 0 };
			ssync_copy_obj(&copy, obj, config->memctx);
			bhash_put(&ctx->snapshot->objects, obj_id, copy);
		}
	} else {  // Existing obj
		if (
			ctx->has_space
			&&
			!ssync_obj_equal(config->schema, obj, base_obj)
		) {
			ssync_record_ctx_t buf;
			ssync_prepare_record_buf(&buf, ctx);

			ssync_write_record_type(&buf.stream, SSYNC_RECORD_TYPE_OBJ_UPDATE);
			bsv_ssync_net_id(&buf.bsv, &obj_id);
			ssync_write_obj_update(
				&buf.bsv, &buf.stream,
				config->schema, obj, base_obj
			);

			ctx->has_space &= bitstream_append(ctx->packet_stream, &buf.stream);
		}

		ssync_obj_t copy = { 0 };
		if (ctx->has_space) {
			ssync_copy_obj(&copy, obj, config->memctx);
		} else {
			ssync_copy_obj(&copy, base_obj, config->memctx);
		}
		bhash_put(&ctx->snapshot->objects, obj_id, copy);
	}
}

static inline void
ssync_begin_incoming_packet(
	ssync_incoming_packet_ctx_t* ctx,
	ssync_endpoint_t* endpoint,
	bitstream_in_t* in_stream,
	bsv_ctx_t* in_bsv
) {
	*ctx = (ssync_incoming_packet_ctx_t){
		.endpoint = endpoint,

		.packet_stream = in_stream,
		.packet_bsv = in_bsv,

		.can_read = true,
	};
}

static inline void
ssync_end_incoming_packet(ssync_incoming_packet_ctx_t* ctx) {
	ssync_endpoint_t* endpoint = ctx->endpoint;
	if (ctx->incoming_snapshot != NULL) {
		if (ctx->can_read) {
			if (!ssync_archive_snapshot(&endpoint->incoming_archive, ctx->incoming_snapshot)) {
				ssync_release_snapshot(endpoint->config->snapshot_pool, ctx->incoming_snapshot);
			}
		} else {
			ssync_release_snapshot(endpoint->config->snapshot_pool, ctx->incoming_snapshot);
		}
	}
}

static inline bool
ssync_discard_incoming_packet(ssync_incoming_packet_ctx_t* ctx) {
	return ctx->can_read = false;
}

static inline bool
ssync_process_snapshot_info_record(ssync_incoming_packet_ctx_t* ctx) {
	ssync_endpoint_t* endpoint = ctx->endpoint;
	const ssync_endpoint_config_t* config = endpoint->config;

	ssync_snapshot_info_record_t record;
	if (bsv_ssync_snapshot_info_record(ctx->packet_bsv, &record) != BSV_OK) {
		return ssync_discard_incoming_packet(ctx);
	}

	ssync_timestamp_t last_snapshot_time = 0;
	if (endpoint->incoming_archive.next != NULL) {
		last_snapshot_time = endpoint->incoming_archive.next->timestamp;
	}
	if (record.current_time <= last_snapshot_time) {
		return ssync_discard_incoming_packet(ctx);
	}

	if (ctx->incoming_snapshot == NULL) {
		ctx->incoming_snapshot = ssync_acquire_snapshot(
			config->snapshot_pool,
			record.current_time,
			config->memctx
		);
	} else {
		return ssync_discard_incoming_packet(ctx);
	}

	ssync_snapshot_t* last_acked_snapshot = ssync_ack_snapshot(
		&endpoint->outgoing_archive, config->snapshot_pool, record.last_received
	);
	if (last_acked_snapshot != NULL && last_acked_snapshot->remote != NULL) {
		// Make copies of every existing object so that those without updates
		// appear unchanged
		const ssync_snapshot_t* base_snapshot = last_acked_snapshot->remote;
		for (bhash_index_t i = 0; i < bhash_len(&base_snapshot->objects); ++i) {
			ssync_net_id_t id = base_snapshot->objects.keys[i];
			const ssync_obj_t* obj = &base_snapshot->objects.values[i];
			ssync_obj_t copy = { 0 };
			ssync_copy_obj(&copy, obj, config->memctx);
			bhash_put(&ctx->incoming_snapshot->objects, id, copy);
		}
	}
	if (last_acked_snapshot == NULL && record.last_received != 0) {
		return ssync_discard_incoming_packet(ctx);
	}
	endpoint->last_acked_snapshot = last_acked_snapshot;

	return true;
}

static inline bool
ssync_process_process_object_create_record(
	ssync_incoming_packet_ctx_t* ctx,
	ssync_obj_create_record_t* record_out,
	const ssync_obj_t** data_out
) {
	if (ctx->incoming_snapshot == NULL) {
		return ssync_discard_incoming_packet(ctx);
	}

	ssync_obj_create_record_t record;
	if (bsv_ssync_obj_create_record(ctx->packet_bsv, &record) != BSV_OK) {
		return ssync_discard_incoming_packet(ctx);
	}

	bhash_alloc_result_t alloc_result = bhash_alloc(&ctx->incoming_snapshot->objects, record.id);
	if (!alloc_result.is_new) {  // Duplicated entry
		return ssync_discard_incoming_packet(ctx);
	}

	// Write a dummy entry first so there is no problem during cleanup
	ctx->incoming_snapshot->objects.keys[alloc_result.index] = record.id;
	ctx->incoming_snapshot->objects.values[alloc_result.index] = (ssync_obj_t){ 0 };

	ssync_endpoint_t* endpoint = ctx->endpoint;
	const ssync_endpoint_config_t* config = endpoint->config;
	const ssync_obj_schema_t* schema = config->schema;

	ssync_obj_t obj = { 0 };
	int num_prop_groups = schema->num_prop_groups;
	if (!ssync_read_bitmask(
		ctx->packet_stream,
		&obj.prop_group_mask,
		num_prop_groups
	)) {
		return ssync_discard_incoming_packet(ctx);
	}

	for (
		int prop_group_index = 0;
		prop_group_index < num_prop_groups;
		++prop_group_index
	) {
		if (!ssync_obj_has_prop_group(&obj, prop_group_index)) { continue; }

		int num_props = schema->prop_groups[prop_group_index].num_props;
		for (int prop_index = 0; prop_index < num_props; ++prop_index) {
			ssync_prop_t prop;
			if (bsv_auto(ctx->packet_bsv, &prop) != BSV_OK) {
				ssync_cleanup_obj(&obj, config->memctx);
				return ssync_discard_incoming_packet(ctx);
			}

			barray_push(obj.props, prop, config->memctx);
		}
	}

	ctx->incoming_snapshot->objects.values[alloc_result.index] = obj;

	if (record_out != NULL) { *record_out = record; }
	if (data_out != NULL) {
		*data_out = &ctx->incoming_snapshot->objects.values[alloc_result.index];
	}

	return true;
}

static inline bool
ssync_process_process_object_destroy_record(
	ssync_incoming_packet_ctx_t* ctx,
	ssync_obj_destroy_record_t* record_out
) {
	if (ctx->incoming_snapshot == NULL) {
		return ssync_discard_incoming_packet(ctx);
	}

	ssync_obj_destroy_record_t record;
	if (bsv_ssync_obj_destroy_record(ctx->packet_bsv, &record) != BSV_OK) {
		return ssync_discard_incoming_packet(ctx);
	}

	ssync_endpoint_t* endpoint = ctx->endpoint;
	const ssync_endpoint_config_t* config = endpoint->config;

	bhash_index_t index = bhash_remove(&ctx->incoming_snapshot->objects, record.id);
	if (!bhash_is_valid(index)) {
		return ssync_discard_incoming_packet(ctx);
	}
	ssync_cleanup_obj(&ctx->incoming_snapshot->objects.values[index], config->memctx);

	if (record_out != NULL) { *record_out = record; }

	return true;
}

static inline bool
ssync_process_object_update_record(
	ssync_incoming_packet_ctx_t* ctx,
	ssync_net_id_t* id_out,
	ssync_obj_t** data_out
) {
	if (ctx->incoming_snapshot == NULL) {
		return ssync_discard_incoming_packet(ctx);
	}

	ssync_net_id_t id;
	if (bsv_ssync_net_id(ctx->packet_bsv, &id) != BSV_OK) {
		return ssync_discard_incoming_packet(ctx);
	}

	ssync_endpoint_t* endpoint = ctx->endpoint;
	const ssync_endpoint_config_t* config = endpoint->config;

	const ssync_obj_t* base_obj = NULL;
	if (
		endpoint->last_acked_snapshot != NULL
		&&
		endpoint->last_acked_snapshot->remote != NULL
	) {
		base_obj = bhash_get_value(&endpoint->last_acked_snapshot->remote->objects, id);
	} else {
		return ssync_discard_incoming_packet(ctx);
	}

	ssync_obj_t* updated_obj = bhash_get_value(&ctx->incoming_snapshot->objects, id);
	if (
		updated_obj == NULL
		||
		!ssync_read_obj_update(
			ctx->packet_bsv,
			ctx->packet_stream,
			config->memctx, config->schema,
			base_obj, updated_obj
		)
	) {
		return ssync_discard_incoming_packet(ctx);
	}

	if (id_out != NULL) { *id_out = id; }
	if (data_out != NULL) { *data_out = updated_obj; }

	return true;
}

// }}}

#endif
