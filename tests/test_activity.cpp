// RTS Engine — tests/test_activity.cpp
#include "test_framework.h"
#include "sim/simulation.h"
#include "sim/activity.h"

using namespace rts;

namespace {

struct Pair {
	EntityId attacker = 0;
	EntityId target = 0;
};

Pair SpawnPair(Simulation& sim, int32_t ax, int32_t ay, int32_t tx, int32_t ty) {
	{
		Command c;
		c.type = CommandType::Spawn;
		c.playerId = 0;
		c.x = ax;
		c.y = ay;
		sim.Commands().Push(c);
	}
	{
		Command c;
		c.type = CommandType::Spawn;
		c.playerId = 1;
		c.x = tx;
		c.y = ty;
		sim.Commands().Push(c);
	}
	sim.Step();
	Pair p{};
	const auto& all = sim.GetWorld().Entities().All();
	if (all.size() >= 2) {
		p.attacker = all[0].id;
		p.target = all[1].id;
	}
	return p;
}

} // namespace

TEST(activity_move_to_reaches_target) {
	World world(64, 64, 1);
	const EntityId id = world.Entities().Create(0, SubCoord(10 * kSubPerCell, 10 * kSubPerCell),
		kCompMovable | kCompHealth);
	Actor* a = world.Entities().Get(id);
	activity::SetMoveTo(*a, SubCoord(12 * kSubPerCell, 10 * kSubPerCell), 0);
	CHECK_EQ(a->activity.type, ActivityType::MoveTo);
	CHECK(a->movable.moving);

	for (int i = 0; i < 100; ++i) {
		activity::Update(world, *world.Entities().Get(id));
	}
	a = world.Entities().Get(id);
	CHECK_EQ(a->pos.x, 12 * kSubPerCell);
	CHECK_EQ(a->pos.y, 10 * kSubPerCell);
	CHECK_EQ(a->activity.type, ActivityType::Idle);
}

TEST(activity_move_blocked_by_water) {
	Map map(16, 16, 0, kWalkable | kBuildable);
	map.SetFlags(CellCoord(8, 8), kWater);
	World world(map, 2);

	const EntityId id = world.Entities().Create(0, SubCoord(4 * kSubPerCell, 8 * kSubPerCell), kCompMovable);
	Actor* a = world.Entities().Get(id);
	activity::SetMoveTo(*a, SubCoord(8 * kSubPerCell, 8 * kSubPerCell), 0);

	for (int i = 0; i < 200; ++i) {
		activity::Update(world, *world.Entities().Get(id));
	}
	a = world.Entities().Get(id);
	const CellCoord c = a->pos.ToCell();
	CHECK(!world.GetMap().Water(c));
	CHECK(a->pos.x < 8 * kSubPerCell);
}

TEST(activity_attack_damages_target) {
	Simulation sim(32, 32, 3);
	const Pair p = SpawnPair(sim, 10 * kSubPerCell, 10 * kSubPerCell, 12 * kSubPerCell, 10 * kSubPerCell);
	CHECK(p.attacker != 0);
	CHECK(p.target != 0);

	Command atk;
	atk.type = CommandType::Attack;
	atk.entityId = p.attacker;
	atk.targetId = p.target;
	sim.Commands().Push(atk);

	const int32_t hpBefore = sim.GetWorld().Entities().Get(p.target)->health.hp;
	sim.Run(60); // 2 秒：足够冷却与开火
	const int32_t hpAfter = sim.GetWorld().Entities().Get(p.target)->health.hp;
	CHECK(hpAfter < hpBefore);
}

TEST(activity_attack_kills_target) {
	Simulation sim(32, 32, 4);
	const Pair p = SpawnPair(sim, 10 * kSubPerCell, 10 * kSubPerCell, 11 * kSubPerCell, 10 * kSubPerCell);

	sim.GetWorld().Entities().Get(p.target)->health.hp = 5;

	Command atk;
	atk.type = CommandType::Attack;
	atk.entityId = p.attacker;
	atk.targetId = p.target;
	sim.Commands().Push(atk);

	sim.Run(80);
	const Actor* target = sim.GetWorld().Entities().Get(p.target);
	CHECK(!target->alive);
	CHECK_EQ(sim.GetWorld().Entities().Get(p.attacker)->activity.type, ActivityType::Idle);
}

TEST(activity_stop_resets) {
	World world(16, 16, 5);
	const EntityId id = world.Entities().Create(0, SubCoord(2 * kSubPerCell, 2 * kSubPerCell), kCompMovable);
	Actor* a = world.Entities().Get(id);
	activity::SetMoveTo(*a, SubCoord(10 * kSubPerCell, 10 * kSubPerCell), 0);
	activity::SetIdle(*a, 0);
	CHECK_EQ(a->activity.type, ActivityType::Idle);
	CHECK(!a->movable.moving);
}

TEST(activity_dead_actor_skipped) {
	World world(16, 16, 6);
	const EntityId id = world.Entities().Create(0, SubCoord(2 * kSubPerCell, 2 * kSubPerCell), kCompMovable);
	Actor* a = world.Entities().Get(id);
	a->alive = false;
	activity::SetMoveTo(*a, SubCoord(8 * kSubPerCell, 8 * kSubPerCell), 0);
	for (int i = 0; i < 10; ++i) {
		activity::Update(world, *world.Entities().Get(id));
	}
	CHECK_EQ(world.Entities().Get(id)->pos.x, 2 * kSubPerCell);
}
