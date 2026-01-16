// vim: set foldmethod=marker foldlevel=0:
#ifndef SLOPSYNC_INTERNAL_H
#define SLOPSYNC_INTERNAL_H

#include <slopsync/shared.h>
#include <bsv.h>
#include <bhash.h>
#include <barray.h>
#include <limits.h>
#include <stdlib.h>
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
	SSYNC_RECORD_TYPE_INIT = 0,
	SSYNC_RECORD_TYPE_OBJ_CREATE,
	SSYNC_RECORD_TYPE_OBJ_UPDATE,
	SSYNC_RECORD_TYPE_OBJ_DESTROY,
	SSYNC_RECORD_TYPE_OBJ_CONTROL,
	SSYNC_RECORD_TYPE_OBJ_HANDOVER,

	SSYNC_RECORD_TYPE_COUNT,
} ssync_record_type_t;

typedef struct {
	ssync_tick_t current_tick;
	ssync_tick_t last_receive;
} ssync_msg_header_t;

typedef struct {
	ssync_player_id_t player_id;
	ssync_tick_t logic_tick_rate;
	ssync_tick_t net_tick_rate;
	uint16_t obj_id_bin;
} ssync_init_record_t;

typedef int64_t ssync_prop_t;

typedef struct {
	ssync_prop_group_mask_t prop_group_mask;
	barray(ssync_prop_t) props;
} ssync_obj_t;

typedef struct {
	ssync_net_id_t id;
	ssync_tick_t timestamp;
	ssync_obj_flags_t flags;
} ssync_obj_create_record_t;

typedef struct {
	ssync_net_id_t id;
	ssync_tick_t timestamp;
} ssync_obj_destroy_record_t;

typedef enum {
	SSYNC_PROP_GROUP_OP_ADD = 0,
	SSYNC_PROP_GROUP_OP_UPDATE,
	SSYNC_PROP_GROUP_OP_REMOVE,

	SSYNC_PROP_GROUP_OP_COUNT,
} ssync_prop_group_op_t;

typedef struct ssync_snapshot_s ssync_snapshot_t;

struct ssync_snapshot_s {
	ssync_snapshot_t* next;
	ssync_tick_t tick;

	BHASH_TABLE(ssync_net_id_t, ssync_obj_t) objects;
};

typedef struct {
	ssync_snapshot_t* next;
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

extern void*
ssync_host_realloc(void* ptr, size_t size, void* ctx);

// Object {{{

static inline void
ssync_cleanup_obj(ssync_obj_t* obj, void* memctx) {
	barray_free(obj->props, memctx);
	obj->props = NULL;
}

static inline void
ssync_copy_obj(ssync_obj_t* dst, const ssync_obj_t* src, void* memctx) {
	size_t num_props = barray_len(src->props);
	barray_resize(dst->props, num_props, memctx);
	memcpy(dst->props, src->props, num_props * sizeof(ssync_prop_t));
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
ssync_cleanup_snapshot_pool(ssync_snapshot_pool_t* pool, void* memctx) {
	for (ssync_snapshot_t* itr = pool->next; itr != NULL; itr = itr->next) {
		for (bhash_index_t i = 0; i < bhash_len(&itr->objects); ++i) {
			ssync_cleanup_obj(&itr->objects.values[i], memctx);
		}
		bhash_cleanup(&itr->objects);
	}
}

static inline ssync_snapshot_t*
ssync_acquire_snapshot(ssync_snapshot_pool_t* pool, ssync_tick_t tick, void* memctx) {
	ssync_snapshot_t* snapshot;
	if (pool->next != NULL) {
		snapshot = pool->next;
		pool->next = snapshot->next;

		for (bhash_index_t i = 0; i < bhash_len(&snapshot->objects); ++i) {
			ssync_cleanup_obj(&snapshot->objects.values[i], memctx);
		}
		bhash_clear(&snapshot->objects);
	} else {
		snapshot = ssync_host_realloc(NULL, sizeof(ssync_snapshot_t), memctx);
		*snapshot = (ssync_snapshot_t){ 0 };
		ssync_reinit_snapshot(snapshot, memctx);
	}

	snapshot->tick = tick;
	return snapshot;
}

static inline void
ssync_release_snapshot(ssync_snapshot_pool_t* pool, ssync_snapshot_t* snapshot) {
	snapshot->next = pool->next;
	pool->next = snapshot->next;
}

static inline bool
ssync_archive_snapshot(ssync_snapshot_pool_t* archive, ssync_snapshot_t* snapshot) {
	ssync_snapshot_t** itr = &archive->next;

	// Traverse the list to find the insertion point
	while (*itr != NULL) {
		if ((*itr)->tick == snapshot->tick) {
			// Duplicate tick found, do not insert
			return false;
		}

		if ((*itr)->tick < snapshot->tick) {
			break;
		}
		itr = &(*itr)->next;
	}

	// Insert the snapshot
	snapshot->next = *itr;
	*itr = snapshot;
	return true;
}

static inline void
ssync_release_after(ssync_snapshot_pool_t* pool, ssync_snapshot_t* snapshot) {
    ssync_snapshot_t* itr = snapshot->next;
    while (itr != NULL) {
        ssync_snapshot_t* to_release = itr;
        itr = itr->next;
        ssync_release_snapshot(pool, to_release);
    }
    snapshot->next = NULL;
}

static inline ssync_snapshot_t*
ssync_ack_snapshot(
	ssync_snapshot_pool_t* archive,
	ssync_snapshot_pool_t* pool,
	ssync_tick_t tick
) {
	ssync_snapshot_t* itr = archive->next;
	while (itr != NULL) {
		if (itr->tick == tick) {
			ssync_release_after(pool, itr);
			return itr;
		}

		if (itr->tick < tick) {
			// Since list is in descending order, no match exists
			return NULL;
		}

		itr = itr->next;
	}

	return NULL;
}

static inline const ssync_snapshot_t*
ssync_find_snapshot_pair(const ssync_snapshot_pool_t* archive, ssync_tick_t tick) {
	const ssync_snapshot_t* itr = archive->next;

	while (itr != NULL && itr->next != NULL) {
		const ssync_snapshot_t* a = itr;
		const ssync_snapshot_t* b = itr->next;

		if (b->tick <= tick && tick < a->tick) {
			return a;
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

#if defined(_MSC_VER)
#include <intrin.h>
#endif

static inline size_t
bits_required(size_t count) {
    if (count <= 1) { return 0; }

    size_t v = count - 1;

#if defined(_MSC_VER)

    unsigned long index;

    #if defined(_M_X64) || defined(_M_ARM64)
        _BitScanReverse64(&index, (unsigned __int64)v);
        return (size_t)index + 1;
    #else
        _BitScanReverse(&index, (unsigned long)v);
        return (size_t)index + 1;
    #endif

#elif defined(__GNUC__) || defined(__clang__)

    return (sizeof(size_t) * 8) - __builtin_clz(v);

#else

    size_t bits = 0;
	while (v) {
		bits++;
		v >>= 1;
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
bsv_ssync_tick(bsv_ctx_t* ctx, ssync_tick_t* tick) {
	_Static_assert(sizeof(*tick) == sizeof(uint32_t), "Type mismatch");
	BSV_CHECK_STATUS(bsv_u32_fixed(ctx, tick));
	return bsv_status(ctx);
}

static inline bsv_status_t
bsv_ssync_msg_header(bsv_ctx_t* ctx, ssync_msg_header_t* header) {
	BSV_CHECK_STATUS(bsv_ssync_tick(ctx, &header->current_tick));
	BSV_CHECK_STATUS(bsv_ssync_tick(ctx, &header->last_receive));
	return bsv_status(ctx);
}

static inline bsv_status_t
bsv_ssync_player_init_record(bsv_ctx_t* ctx, ssync_init_record_t* rec) {
	BSV_CHECK_STATUS(bsv_auto(ctx, &rec->player_id));
	BSV_CHECK_STATUS(bsv_auto(ctx, &rec->net_tick_rate));
	BSV_CHECK_STATUS(bsv_auto(ctx, &rec->logic_tick_rate));
	BSV_CHECK_STATUS(bsv_auto(ctx, &rec->obj_id_bin));
	return bsv_status(ctx);
}

static inline bsv_status_t
bsv_ssync_obj_create_record(bsv_ctx_t* ctx, ssync_obj_create_record_t* rec) {
	BSV_CHECK_STATUS(bsv_ssync_net_id(ctx, &rec->id));
	BSV_CHECK_STATUS(bsv_ssync_tick(ctx, &rec->timestamp));
	BSV_CHECK_STATUS(bsv_auto(ctx, &rec->flags));  // Maybe shrink to bit size
	return bsv_status(ctx);
}

static inline bsv_status_t
bsv_ssync_obj_destroy_record(bsv_ctx_t* ctx, ssync_obj_destroy_record_t* rec) {
	BSV_CHECK_STATUS(bsv_ssync_net_id(ctx, &rec->id));
	BSV_CHECK_STATUS(bsv_ssync_tick(ctx, &rec->timestamp));
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

static inline void
ssync_write_obj_update(
	bitstream_out_t* bitstream,
	const ssync_obj_schema_t* schema,
	const ssync_obj_t* current_obj,
	const ssync_obj_t* previous_obj
) {
	ssync_bsv_out_t bsv_out;
	bsv_ctx_t bsv = { .out = ssync_init_bsv_out(&bsv_out, bitstream) };

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
							// Pick between delta and full value encoding
							int64_t out_value;

							// Diff for delta
							int64_t diff;
							if (!ckd_sub(&diff, current_value, previous_value)) {
								diff = INT64_MAX;
							}

							if (diff == 0) {
								bitstream_write(bitstream, &(uint8_t){ 0 }, 1);
								continue;  // No change
							} else {
								bitstream_write(bitstream, &(uint8_t){ 1 }, 1);
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
							bsv_auto(&bsv, &out_value);
						}
					}
				} else {  // Fully write all props
					bitstream_write(bitstream, &(uint8_t){ SSYNC_PROP_GROUP_OP_ADD }, 2);

					for (int prop_index = 0; prop_index < num_props; ++prop_index) {
						ssync_prop_t value = current_props[prop_index];
						bsv_auto(&bsv, &value);
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

// }}}

#endif
