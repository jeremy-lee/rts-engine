// RTS Engine — tests/test_save.cpp
// M21 验收：存档 / 读档
#include "test_framework.h"
#include "sim/simulation.h"
#include "sim/gamedefs.h"

using namespace rts;

namespace {

Rules SaveRules() {
	return Rules::FromYAML(R"(
units:
  infantry:
    name: "步兵"
    cost: 50
    hp: 100
    speed: 32
    weapon:
      damage: 10
      range: 4096
      cooldown: 15
buildings:
  turret:
    name: "防御塔"
    cost: 200
    hp: 400
    width: 2
    height: 2
    turret: true
    weapon:
      damage: 20
      range: 6144
      cooldown: 45
)");
}

} // namespace

TEST(save_load_roundtrip) {
	Map map(32, 32, 0, kWalkable | kBuildable);
	World world(map, 1, SaveRules());
	world.AddCredits(0, 500);
	world.AddCredits(1, 300);

	// 建造防御塔 + 单位
	Command b;
	b.type = CommandType::Build;
	b.playerId = 0;
	b.x = 10 * kSubPerCell;
	b.y = 10 * kSubPerCell;
	b.targetId = 0;
	world.ApplyCommand(b);

	const EntityId u = world.Entities().Create(1,
		SubCoord(20 * kSubPerCell, 20 * kSubPerCell),
		kCompMovable | kCompHealth | kCompArmament);
	world.Entities().Get(u)->health.hp = 80;

	// 跑几 tick
	for (int i = 0; i < 10; ++i) {
		CommandQueue q;
		world.Advance(q);
	}

	const std::string save = world.SaveToText();
	CHECK(!save.empty());
	CHECK(save.find("RTSAVE 1") == 0);

	Map map2(8, 8); // 会被存档覆盖
	World loaded(map2, 1, SaveRules());
	CHECK(loaded.LoadFromText(save));

	CHECK_EQ(loaded.CurrentTick(), world.CurrentTick());
	CHECK_EQ(loaded.Credits(0), world.Credits(0));
	CHECK_EQ(loaded.Credits(1), world.Credits(1));
	CHECK_EQ(loaded.Entities().Count(), world.Entities().Count());
	CHECK_EQ(loaded.Width(), world.Width());
	CHECK_EQ(loaded.Height(), world.Height());

	// 逐实体对比位置/生命
	for (size_t i = 0; i < world.Entities().Count(); ++i) {
		const Actor& a = world.Entities().All()[i];
		const Actor& b2 = loaded.Entities().All()[i];
		CHECK_EQ(a.id, b2.id);
		CHECK_EQ(a.playerId, b2.playerId);
		CHECK_EQ(a.pos.x, b2.pos.x);
		CHECK_EQ(a.pos.y, b2.pos.y);
		CHECK_EQ(a.health.hp, b2.health.hp);
		CHECK_EQ(a.alive, b2.alive);
	}
}

TEST(save_load_map_occupancy) {
	Map map(32, 32, 0, kWalkable | kBuildable);
	World world(map, 1, SaveRules());
	world.AddCredits(0, 500);
	Command b;
	b.type = CommandType::Build;
	b.playerId = 0;
	b.x = 10 * kSubPerCell;
	b.y = 10 * kSubPerCell;
	b.targetId = 0;
	world.ApplyCommand(b);

	const std::string save = world.SaveToText();
	Map map2(8, 8);
	World loaded(map2, 1, SaveRules());
	CHECK(loaded.LoadFromText(save));

	// 建筑占格恢复
	CHECK(loaded.GetMap().Occupied(CellCoord(10, 10)));
	CHECK(loaded.GetMap().Occupied(CellCoord(11, 11)));
	CHECK(!loaded.GetMap().Occupied(CellCoord(12, 12)));
}

TEST(save_invalid_text_rejected) {
	Map map(8, 8);
	World world(map, 1, SaveRules());
	CHECK(!world.LoadFromText("garbage"));
	CHECK(!world.LoadFromText("RTSAVE 999\n"));
}
