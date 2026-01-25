#include "shared.h"
#include "../src/internal.h"
#include <inttypes.h>
#include <limits.h>

static btest_suite_t object = {
	.name = "object",
	.init_per_suite = init_per_suite,
	.cleanup_per_suite = cleanup_per_suite,

	.init_per_test = init_per_test,
	.cleanup_per_test = cleanup_per_test,
};

static ssync_obj_schema_t schema = {
	.num_prop_groups = 2,
	.prop_groups = {
		{
			.num_props = 3,
			.props = {
				{
					.type = SSYNC_PROP_TYPE_FLOAT,
					.precision = 2,
					.flags = SSYNC_PROP_POSITION_X | SSYNC_PROP_INTERPOLATE,
				},
				{
					.type = SSYNC_PROP_TYPE_FLOAT,
					.precision = 2,
					.flags = SSYNC_PROP_POSITION_Y | SSYNC_PROP_INTERPOLATE,
				},
				{
					.type = SSYNC_PROP_TYPE_FLOAT,
					.precision = 2,
					.flags = SSYNC_PROP_ROTATION | SSYNC_PROP_INTERPOLATE,
				},
			},
		},
		{
			.num_props = 1,
			.props = {
				{ .type = SSYNC_PROP_TYPE_INT },
			},
		}
	},
};

BTEST(object, full_update) {
	void* packet = barena_malloc(&fixture.arena, 1024);
	bitstream_out_t out_stream = {
		.data = packet,
		.num_bytes = 1024,
	};
	ssync_bsv_out_t out;
	bsv_ctx_t out_bsv = { .out = ssync_init_bsv_out(&out, &out_stream) };

	ssync_obj_t curr_obj = { 0 };
	ssync_obj_t prev_obj = { 0 };

	curr_obj.prop_group_mask = 0x1;
	barray_push(curr_obj.props, 6, NULL);
	barray_push(curr_obj.props, 7, NULL);
	barray_push(curr_obj.props, 8, NULL);

	ssync_write_obj_update(&out_bsv, &out_stream, &schema, &curr_obj, &prev_obj);

	bitstream_in_t in_stream = {
		.data = packet,
		.num_bytes = 1024,
	};
	ssync_bsv_in_t in;
	bsv_ctx_t in_bsv = { .in = ssync_init_bsv_in(&in, &in_stream) };

	ssync_obj_t new_curr_obj = { 0 };
	ssync_read_obj_update(&in_bsv, &in_stream, NULL, &schema, &prev_obj, &new_curr_obj);

	BTEST_EXPECT_EQUAL("%d", new_curr_obj.prop_group_mask, 0x01);
	BTEST_EXPECT_EQUAL("%" PRId64, new_curr_obj.props[0], 6);
	BTEST_EXPECT_EQUAL("%" PRId64, new_curr_obj.props[1], 7);
	BTEST_EXPECT_EQUAL("%" PRId64, new_curr_obj.props[2], 8);

	ssync_cleanup_obj(&curr_obj, NULL);
	ssync_cleanup_obj(&new_curr_obj, NULL);
}

BTEST(object, delta_update) {
	void* packet = barena_malloc(&fixture.arena, 1024);
	bitstream_out_t out_stream = {
		.data = packet,
		.num_bytes = 1024,
	};
	ssync_bsv_out_t out;
	bsv_ctx_t out_bsv = { .out = ssync_init_bsv_out(&out, &out_stream) };

	ssync_obj_t curr_obj = { 0 };
	ssync_obj_t prev_obj = { 0 };

	prev_obj.prop_group_mask = 0x1;
	barray_push(prev_obj.props, 5, NULL);
	barray_push(prev_obj.props, 7, NULL);
	barray_push(prev_obj.props, 8, NULL);

	curr_obj.prop_group_mask = 0x1;
	barray_push(curr_obj.props, 6, NULL);
	barray_push(curr_obj.props, 7, NULL);
	barray_push(curr_obj.props, 8, NULL);

	ssync_write_obj_update(&out_bsv, &out_stream, &schema, &curr_obj, &prev_obj);

	bitstream_in_t in_stream = {
		.data = packet,
		.num_bytes = 1024,
	};
	ssync_bsv_in_t in;
	bsv_ctx_t in_bsv = { .in = ssync_init_bsv_in(&in, &in_stream) };

	ssync_obj_t new_curr_obj = { 0 };
	ssync_read_obj_update(&in_bsv, &in_stream, NULL, &schema, &prev_obj, &new_curr_obj);

	BTEST_EXPECT_EQUAL("%d", new_curr_obj.prop_group_mask, 0x01);
	BTEST_EXPECT_EQUAL("%" PRId64, new_curr_obj.props[0], 6);
	BTEST_EXPECT_EQUAL("%" PRId64, new_curr_obj.props[1], 7);
	BTEST_EXPECT_EQUAL("%" PRId64, new_curr_obj.props[2], 8);

	ssync_cleanup_obj(&curr_obj, NULL);
	ssync_cleanup_obj(&prev_obj, NULL);
	ssync_cleanup_obj(&new_curr_obj, NULL);
}

BTEST(object, no_change) {
	void* packet = barena_malloc(&fixture.arena, 1024);
	bitstream_out_t out_stream = {
		.data = packet,
		.num_bytes = 1024,
	};
	ssync_bsv_out_t out;
	bsv_ctx_t out_bsv = { .out = ssync_init_bsv_out(&out, &out_stream) };

	ssync_obj_t curr_obj = { 0 };
	ssync_obj_t prev_obj = { 0 };

	prev_obj.prop_group_mask = 0x1;
	barray_push(prev_obj.props, 6, NULL);
	barray_push(prev_obj.props, 7, NULL);
	barray_push(prev_obj.props, 8, NULL);

	curr_obj.prop_group_mask = 0x1;
	barray_push(curr_obj.props, 6, NULL);
	barray_push(curr_obj.props, 7, NULL);
	barray_push(curr_obj.props, 8, NULL);

	ssync_write_obj_update(&out_bsv, &out_stream, &schema, &curr_obj, &prev_obj);

	bitstream_in_t in_stream = {
		.data = packet,
		.num_bytes = 1024,
	};
	ssync_bsv_in_t in;
	bsv_ctx_t in_bsv = { .in = ssync_init_bsv_in(&in, &in_stream) };

	ssync_obj_t new_curr_obj = { 0 };
	ssync_read_obj_update(&in_bsv, &in_stream, NULL, &schema, &prev_obj, &new_curr_obj);

	BTEST_EXPECT_EQUAL("%d", new_curr_obj.prop_group_mask, 0x01);
	BTEST_EXPECT_EQUAL("%" PRId64, new_curr_obj.props[0], 6);
	BTEST_EXPECT_EQUAL("%" PRId64, new_curr_obj.props[1], 7);
	BTEST_EXPECT_EQUAL("%" PRId64, new_curr_obj.props[2], 8);

	ssync_cleanup_obj(&curr_obj, NULL);
	ssync_cleanup_obj(&prev_obj, NULL);
	ssync_cleanup_obj(&new_curr_obj, NULL);
}

BTEST(object, add_prop_group) {
	void* packet = barena_malloc(&fixture.arena, 1024);
	bitstream_out_t out_stream = {
		.data = packet,
		.num_bytes = 1024,
	};
	ssync_bsv_out_t out;
	bsv_ctx_t out_bsv = { .out = ssync_init_bsv_out(&out, &out_stream) };

	ssync_obj_t curr_obj = { 0 };
	ssync_obj_t prev_obj = { 0 };

	prev_obj.prop_group_mask = 0x1;
	barray_push(prev_obj.props, 6, NULL);
	barray_push(prev_obj.props, 7, NULL);
	barray_push(prev_obj.props, 8, NULL);

	curr_obj.prop_group_mask = 0x3;
	barray_push(curr_obj.props, 6, NULL);
	barray_push(curr_obj.props, 7, NULL);
	barray_push(curr_obj.props, 8, NULL);
	barray_push(curr_obj.props, 9, NULL);

	ssync_write_obj_update(&out_bsv, &out_stream, &schema, &curr_obj, &prev_obj);

	bitstream_in_t in_stream = {
		.data = packet,
		.num_bytes = 1024,
	};
	ssync_bsv_in_t in;
	bsv_ctx_t in_bsv = { .in = ssync_init_bsv_in(&in, &in_stream) };

	ssync_obj_t new_curr_obj = { 0 };
	ssync_read_obj_update(&in_bsv, &in_stream, NULL, &schema, &prev_obj, &new_curr_obj);

	BTEST_EXPECT_EQUAL("%d", new_curr_obj.prop_group_mask, 0x03);
	BTEST_EXPECT_EQUAL("%" PRId64, new_curr_obj.props[0], 6);
	BTEST_EXPECT_EQUAL("%" PRId64, new_curr_obj.props[1], 7);
	BTEST_EXPECT_EQUAL("%" PRId64, new_curr_obj.props[2], 8);
	BTEST_EXPECT_EQUAL("%" PRId64, new_curr_obj.props[3], 9);

	ssync_cleanup_obj(&curr_obj, NULL);
	ssync_cleanup_obj(&prev_obj, NULL);
	ssync_cleanup_obj(&new_curr_obj, NULL);
}

BTEST(object, rem_prop_group) {
	void* packet = barena_malloc(&fixture.arena, 1024);
	bitstream_out_t out_stream = {
		.data = packet,
		.num_bytes = 1024,
	};
	ssync_bsv_out_t out;
	bsv_ctx_t out_bsv = { .out = ssync_init_bsv_out(&out, &out_stream) };

	ssync_obj_t curr_obj = { 0 };
	ssync_obj_t prev_obj = { 0 };

	prev_obj.prop_group_mask = 0x3;
	barray_push(prev_obj.props, 6, NULL);
	barray_push(prev_obj.props, 7, NULL);
	barray_push(prev_obj.props, 8, NULL);
	barray_push(prev_obj.props, 9, NULL);

	curr_obj.prop_group_mask = 0x2;
	barray_push(curr_obj.props, 8, NULL);

	ssync_write_obj_update(&out_bsv, &out_stream, &schema, &curr_obj, &prev_obj);

	bitstream_in_t in_stream = {
		.data = packet,
		.num_bytes = 1024,
	};
	ssync_bsv_in_t in;
	bsv_ctx_t in_bsv = { .in = ssync_init_bsv_in(&in, &in_stream) };

	ssync_obj_t new_curr_obj = { 0 };
	ssync_read_obj_update(&in_bsv, &in_stream, NULL, &schema, &prev_obj, &new_curr_obj);

	BTEST_EXPECT_EQUAL("%d", new_curr_obj.prop_group_mask, 0x02);
	BTEST_EXPECT_EQUAL("%" PRId64, new_curr_obj.props[0], 8);

	ssync_cleanup_obj(&curr_obj, NULL);
	ssync_cleanup_obj(&prev_obj, NULL);
	ssync_cleanup_obj(&new_curr_obj, NULL);
}

BTEST(object, big_num) {
	void* packet = barena_malloc(&fixture.arena, 1024);
	bitstream_out_t out_stream = {
		.data = packet,
		.num_bytes = 1024,
	};
	ssync_bsv_out_t out;
	bsv_ctx_t out_bsv = { .out = ssync_init_bsv_out(&out, &out_stream) };

	ssync_obj_t curr_obj = { 0 };
	ssync_obj_t prev_obj = { 0 };

	curr_obj.prop_group_mask = 0x2;
	barray_push(curr_obj.props, UINT64_MAX, NULL);

	ssync_write_obj_update(&out_bsv, &out_stream, &schema, &curr_obj, &prev_obj);

	bitstream_in_t in_stream = {
		.data = packet,
		.num_bytes = 1024,
	};
	ssync_bsv_in_t in;
	bsv_ctx_t in_bsv = { .in = ssync_init_bsv_in(&in, &in_stream) };

	ssync_obj_t new_curr_obj = { 0 };
	ssync_read_obj_update(&in_bsv, &in_stream, NULL, &schema, &prev_obj, &new_curr_obj);

	BTEST_EXPECT_EQUAL("%d", new_curr_obj.prop_group_mask, 0x02);
	BTEST_EXPECT_EQUAL("%" PRIu64, (uint64_t)new_curr_obj.props[0], UINT64_MAX);

	ssync_cleanup_obj(&curr_obj, NULL);
	ssync_cleanup_obj(&prev_obj, NULL);
	ssync_cleanup_obj(&new_curr_obj, NULL);
}
