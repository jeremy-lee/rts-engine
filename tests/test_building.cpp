// RTS Engine — tests/test_building.cpp
// M11 验收：建筑占格、重叠拒绝、寻路避让、摧毁释放
#include "test_framework.h"
#include "sim/simulation.h"
#include "sim/gamedefs.h"
#include "sim/pathfinder.h"

using namespace rts;

namespace {

Rules BuildRules() {
	return Rules::FromYAML(R"(
units:
  infantry:
    name: "步兵"
    cost: 50
    buildTime: 10
    hp: 100
    speed: 32
    weapon:
      damage: 10
      range: 4096
      cooldown: 15
buildings:
  barracks:
    name: "兵营"
    cost: 300
    buildTime: 20
    hp: 500
    width: 4
    height: 4
    produces:
      - infantry
)");
}

CellCoord BuildBarracks(World& world, int32_t cx, int32_t cy) {
	Command b;
	b.type = CommandType::Build;
	b.playerId = 0;
	b.x = cx * kSubPerCell;
	b.y = cy * kSubPerCell;
	b.targetId = 0;
	world.ApplyCommand(b);
	return CellCoord(cx, cy);
}

} // namespace

TEST(building_footprint_occupies_cells) {
	Map map(32, 32, 0, kWalkable | kBuildable);
	World world(map, 1, BuildRules());
	BuildBarracks(world, 10, 10);

	const Map& m = world.GetMap();
	CHECK(m.Occupied(CellCoord(10, 10)));
	CHECK(m.Occupied(CellCoord(13, 13)));
	CHECK(!m.Occupied(CellCoord(14, 14)));
	CHECK(!m.Occupied(CellCoord(9, 10)));

	// 建筑实体位置在占格中心
	const Actor& a = world.Entities().All()[0];
	CHECK_EQ(a.footprintW, 4);
	CHECK_EQ(a.footprintH, 4);
}

TEST(building_overlap_rejected) {
	Map map(32, 32, 0, kWalkable | kBuildable);
	World world(map, 1, BuildRules());
	BuildBarracks(world, 10, 10); // 占 10-13

	const int32_t creditsAfterFirst = world.Credits(0);
	BuildBarracks(world, 12, 12); // 重叠 → 拒绝
	CHECK_EQ(world.Entities().Count(), static_cast<size_t>(1));
	CHECK_EQ(world.Credits(0), creditsAfterFirst); // 资源未扣
}

TEST(building_on_water_rejected) {
	Map map(32, 32, 0, kWalkable | kBuildable);
	map.SetFlags(CellCoord(10, 10), kWater); // 区域内一格是水
	World world(map, 1, BuildRules());
	BuildBarracks(world, 10, 10);
	CHECK_EQ(world.Entities().Count(), static_cast<size_t>(0));
}

TEST(pathfinding_avoids_building) {
	Map map(32, 32, 0, kWalkable | kBuildable);
	World world(map, 1, BuildRules());
	BuildBarracks(world, 10, 10); // 占 10-13

	Pathfinder pf(32, 32);
	std::vector<CellCoord> path;
	// 从 (8,12) 到 (15,12)：直接穿越会碰建筑，必须绕行
	CHECK(pf.FindPath(world.GetMap(), CellCoord(8, 12), CellCoord(15, 12), path));
	CHECK(!path.empty());
	for (const auto& c : path) {
		CHECK(!world.GetMap().Occupied(c));
	}
}

TEST(unit_movement_blocked_by_building) {
	Map map(32, 32, 0, kWalkable | kBuildable);
	World world(map, 1, BuildRules());
	BuildBarracks(world, 10, 10);

	// 单位移动到建筑中心 → 应停在建筑外
	const EntityId id = world.Entities().Create(0, SubCoord(8 * kSubPerCell, 12 * kSubPerCell),
		kCompMovable | kCompHealth);
	Command m;
	m.type = CommandType::Move;
	m.entityId = id;
	m.x = 12 * kSubPerCell;
	m.y = 12 * kSubPerCell;
	world.ApplyCommand(m);

	for (int i = 0; i < 300; ++i) {
		CommandQueue q;
		world.Advance(q);
	}
	const Actor* a = world.Entities().Get(id);
	CHECK(a != nullptr);
	// 单位最终格不可为建筑占格
	CHECK(!world.GetMap().Occupied(a->pos.ToCell()));
}

TEST(building_destroyed_frees_footprint) {
	Map map(32, 32, 0, kWalkable | kBuildable);
	World world(map, 1, BuildRules());
	BuildBarracks(world, 10, 10);

	const EntityId building = world.Entities().All()[0].id;
	// 摧毁建筑
	world.Entities().Get(building)->alive = false;

	CommandQueue q;
	world.Advance(q); // TickActors 中释放

	CHECK(!world.GetMap().Occupied(CellCoord(10, 10)));
	CHECK(!world.GetMap().Occupied(CellCoord(13, 13)));

	// 释放后可在同位置重新建造
	const int32_t credits = world.Credits(0);
	BuildBarracks(world, 10, 10);
	CHECK_EQ(world.Credits(0), credits - 300);
}
