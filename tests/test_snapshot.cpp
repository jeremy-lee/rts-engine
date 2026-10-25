// RTS Engine — tests/test_snapshot.cpp
#include "test_framework.h"
#include "render/snapshot.h"

using namespace rts;

TEST(snapshot_from_world_counts) {
	Map map(8, 8, 0, kWalkable | kBuildable);
	World world(map, 1);
	world.Entities().Create(0, SubCoord(2 * kSubPerCell, 2 * kSubPerCell), kCompMovable);
	world.Entities().Create(1, SubCoord(3 * kSubPerCell, 3 * kSubPerCell), kCompMovable);

	const RenderSnapshot snap = RenderSnapshot::FromWorld(world);
	// 8x8 地形 + 2 单位
	CHECK_EQ(snap.sprites.size(), static_cast<size_t>(8 * 8 + 2));
	CHECK_EQ(snap.mapW, 8);
	CHECK_EQ(snap.mapH, 8);
}

TEST(snapshot_unit_colors_by_player) {
	Map map(4, 4, 0, kWalkable | kBuildable);
	World world(map, 1);
	const EntityId a = world.Entities().Create(0, SubCoord(1 * kSubPerCell, 1 * kSubPerCell), kCompMovable);
	const EntityId b = world.Entities().Create(1, SubCoord(2 * kSubPerCell, 2 * kSubPerCell), kCompMovable);
	(void)a; (void)b;

	const RenderSnapshot snap = RenderSnapshot::FromWorld(world);
	// 找到最后两个单位精灵
	bool foundP0 = false, foundP1 = false;
	for (const auto& s : snap.sprites) {
		if (s.entityId == 0) continue;
		// 玩家0 蓝 (0xFF00C8FF)，玩家1 红 (0xFFFF4040)
		if (s.color == 0xFF00C8FF) foundP0 = true;
		if (s.color == 0xFFFF4040) foundP1 = true;
	}
	CHECK(foundP0);
	CHECK(foundP1);
}

TEST(snapshot_dead_units_excluded) {
	Map map(4, 4, 0, kWalkable | kBuildable);
	World world(map, 1);
	const EntityId id = world.Entities().Create(0, SubCoord(1 * kSubPerCell, 1 * kSubPerCell), kCompMovable);
	world.Entities().Get(id)->alive = false;

	const RenderSnapshot snap = RenderSnapshot::FromWorld(world);
	for (const auto& s : snap.sprites) {
		CHECK_EQ(s.entityId, 0u); // 没有单位精灵
	}
}

TEST(snapshot_deterministic) {
	Map map = Map::GenerateTestMap(16, 16, 42);
	World world(map, 7);
	world.Entities().Create(0, SubCoord(2 * kSubPerCell, 2 * kSubPerCell), kCompMovable);

	const RenderSnapshot a = RenderSnapshot::FromWorld(world);
	const RenderSnapshot b = RenderSnapshot::FromWorld(world);
	CHECK_EQ(a.sprites.size(), b.sprites.size());
	for (size_t i = 0; i < a.sprites.size(); ++i) {
		CHECK_EQ(a.sprites[i].worldX, b.sprites[i].worldX);
		CHECK_EQ(a.sprites[i].worldY, b.sprites[i].worldY);
		CHECK_EQ(a.sprites[i].color, b.sprites[i].color);
	}
}
