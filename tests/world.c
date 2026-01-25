#include "shared.h"
#include <slopsync/client.h>
#include <slopsync/server.h>
#include <bhash.h>
#include <stdlib.h>

static const double NET_INTERVAL_S = 1.0 / 20.0;
static const double LOGIC_INTERVAL_S = 1.0 / 30.0;

typedef enum {
	PROP_GROUP_TRANSFORM = 0,
	PROP_GROUP_HP,
} prop_group_t;

typedef struct {
	bool has_transform;
	float x;
	float y;
	float rotation;

	bool has_hp;
	int hp;
} obj_t;

typedef struct {
	ssync_t* ssync;
	BHASH_TABLE(ssync_net_id_t, obj_t) world;
} client_t;

static struct {
	ssyncd_t* server;
	client_t client1;
	client_t client2;
} world_fixture;

static void*
test_realloc(void* userdata, void* ptr, size_t size) {
	if (size == 0) {
		free(ptr);
		return NULL;
	} else {
		return realloc(ptr, size);
	}
}

static
void create_remote_obj(void* userdata, ssync_net_id_t obj_id) {
	client_t* client = userdata;
	obj_t new_obj = { 0 };
	bhash_put(&client->world, obj_id, new_obj);
}

static
void destroy_remote_obj(void* userdata, ssync_net_id_t obj_id) {
	client_t* client = userdata;
	bhash_remove(&client->world, obj_id);
}

static void
add_prop_group(void* userdata, ssync_net_id_t obj_id, ssync_local_id_t prop_group_id) {
	client_t* client = userdata;
	obj_t* obj = bhash_get_value(&client->world, obj_id);

	if (prop_group_id == PROP_GROUP_TRANSFORM) {
		obj->has_transform = true;
	} else if (prop_group_id == PROP_GROUP_HP) {
		obj->has_hp = true;
	}
}

static void
rem_prop_group(void* userdata, ssync_net_id_t obj_id, ssync_local_id_t prop_group_id) {
	client_t* client = userdata;
	obj_t* obj = bhash_get_value(&client->world, obj_id);

	if (prop_group_id == PROP_GROUP_TRANSFORM) {
		obj->has_transform = false;
	} else if (prop_group_id == PROP_GROUP_HP) {
		obj->has_hp = false;
	}
}

static bool has_prop_group(
	void* userdata,
	ssync_net_id_t obj_id,
	ssync_local_id_t prop_group_id
) {
	client_t* client = userdata;
	obj_t* obj = bhash_get_value(&client->world, obj_id);

	if (prop_group_id == PROP_GROUP_TRANSFORM) {
		return obj->has_transform;
	} else if (prop_group_id == PROP_GROUP_HP) {
		return obj->has_hp;
	} else {
		return false;
	}
}

static void
sync(
	void* userdata,
	ssync_ctx_t* ctx,
	ssync_net_id_t obj_id
) {
	client_t* client = userdata;
	obj_t* obj = bhash_get_value(&client->world, obj_id);

	if (ssync_prop_group(ctx, PROP_GROUP_TRANSFORM)) {
		ssync_prop_float(ctx, &obj->x, 3, SSYNC_PROP_POSITION_X);
		ssync_prop_float(ctx, &obj->y, 3, SSYNC_PROP_POSITION_Y);
		ssync_prop_float(ctx, &obj->rotation, 3, SSYNC_PROP_ROTATION);
	}

	if (ssync_prop_group(ctx, PROP_GROUP_HP)) {
		ssync_prop_any_int(ctx, &obj->hp, SSYNC_PROP_POSITION_X);
	}
}

static void
client_send(void* userdata, ssync_blob_t message, bool reliable) {
	int sender = userdata == &world_fixture.client1 ? 0 : 1;
	ssyncd_process_message(world_fixture.server, message, sender);
}

static void
server_send(void* userdata, int receiver, ssync_blob_t message, bool reliable) {
	if (receiver == 0) {
		ssync_process_message(world_fixture.client1.ssync, message);
	} else if (receiver == 1) {
		ssync_process_message(world_fixture.client2.ssync, message);
	}
}

static obj_t*
create_local_obj(client_t* client, ssync_obj_flags_t flags, ssync_net_id_t* id_ptr) {
	ssync_net_id_t id = ssync_create(client->ssync, flags);
	bhash_alloc_result_t result = bhash_alloc(&client->world, id);
	client->world.keys[result.index] = id;
	client->world.values[result.index] = (obj_t){ 0 };

	if (id_ptr != NULL) { *id_ptr = id; }

	return &client->world.values[result.index];
}

static void
destroy_local_obj(client_t* client, ssync_net_id_t id) {
	ssync_destroy(client->ssync, id);
	bhash_remove(&client->world, id);
}

static void
init_per_world_test(void) {
	init_per_test();

	bhash_config_t hconfig = bhash_config_default();
	bhash_init(&world_fixture.client1.world, hconfig);
	bhash_init(&world_fixture.client2.world, hconfig);

	ssync_config_t client_config = {
		.max_message_size = 1024,
		.add_prop_group = add_prop_group,
		.rem_prop_group = rem_prop_group,
		.create_obj = create_remote_obj,
		.destroy_obj = destroy_remote_obj,
		.has_prop_group = has_prop_group,
		.send_msg = client_send,
		.sync = sync,
		.interpolation_ratio = 1.f,
		.realloc = test_realloc,
	};

	client_config.userdata = &world_fixture.client1;
	world_fixture.client1.ssync = ssync_init(&client_config);

	client_config.userdata = &world_fixture.client2;
	world_fixture.client2.ssync = ssync_init(&client_config);

	size_t schema_size = ssync_info(world_fixture.client1.ssync).schema_size;
	void* schema = barena_malloc(&fixture.arena, schema_size);
	ssync_write_schema(world_fixture.client1.ssync, schema);

	ssyncd_config_t server_config = {
		.logic_tick_rate = 30,
		.net_tick_rate = 20,
		.max_message_size = 1024,
		.max_num_players = 2,
		.realloc = test_realloc,
		.obj_schema = {
			.data = schema,
			.size = schema_size,
		},
		.send_msg = server_send,
	};
	world_fixture.server = ssyncd_init(&server_config);
	ssyncd_update(world_fixture.server, 0.1);

	ssyncd_add_player(world_fixture.server, 0, "player1");
	ssyncd_add_player(world_fixture.server, 1, "player2");
}

static void
cleanup_per_world_test(void) {
	ssyncd_cleanup(world_fixture.server);

	ssync_cleanup(world_fixture.client1.ssync);
	ssync_cleanup(world_fixture.client2.ssync);
	bhash_cleanup(&world_fixture.client1.world);
	bhash_cleanup(&world_fixture.client2.world);

	cleanup_per_test();
}

static btest_suite_t world = {
	.name = "world",
	.init_per_suite = init_per_suite,
	.cleanup_per_suite = cleanup_per_suite,

	.init_per_test = init_per_world_test,
	.cleanup_per_test = cleanup_per_world_test,
};

BTEST(world, create_destroy) {
	client_t* client1 = &world_fixture.client1;
	client_t* client2 = &world_fixture.client2;
	ssyncd_t* server = world_fixture.server;

	ssync_net_id_t id;
	create_local_obj(client1, SSYNC_OBJ_DEFAULT, &id);
	ssync_update(client1->ssync, NET_INTERVAL_S);

	ssyncd_update(world_fixture.server, NET_INTERVAL_S);
	ssyncd_broadcast(server);

	// Advance time past the interpolation delay
	ssync_update(client2->ssync, NET_INTERVAL_S * 2);

	BTEST_EXPECT_EQUAL("%d", bhash_len(&client2->world), 1);

	ssync_update(client1->ssync, NET_INTERVAL_S);
	ssync_update(client2->ssync, NET_INTERVAL_S);

	BTEST_EXPECT_EQUAL("%d", bhash_len(&client2->world), 1);

	destroy_local_obj(client1, id);
	ssync_update(client1->ssync, NET_INTERVAL_S);

	ssyncd_update(world_fixture.server, NET_INTERVAL_S);
	ssyncd_broadcast(server);

	ssync_update(client2->ssync, LOGIC_INTERVAL_S);

	BTEST_EXPECT_EQUAL("%d", bhash_len(&client2->world), 0);
}
