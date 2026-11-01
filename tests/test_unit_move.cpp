// RTS Engine — tests/test_unit_move.cpp
// 用户报"坦克右键后不动"。前面的排查证明 UI 侧没问题（点选命中、命令下发、
// 目标格都对），所以这里用真实地图 + 真实规则复现整条 sim 链路：
//   Spawn -> Move 命令 -> 推进 N tick -> 位置必须变化
// 同时也覆盖"生产的单位"这条路径，因为 Produce 和 Spawn 建的实体组件不同。
#include "test_framework.h"

#include "sim/simulation.h"
#include "sim/activity.h"
#include "sim/rules.h"

using namespace rts;

namespace {

// 和 test_building.cpp 用的同一份规则（步兵 + 兵营 + 坦克）
Rules BuildTestRules() {
	return Rules::FromYAML(R"(
units:
  infantry:
    name: "infantry"
    cost: 50
    buildTime: 10
    hp: 100
    speed: 32
    weapon:
      damage: 10
      range: 4096
      cooldown: 15
  tank:
    name: "tank"
    cost: 150
    buildTime: 20
    hp: 300
    speed: 32
    weapon:
      damage: 30
      range: 4096
      cooldown: 20
buildings:
  barracks:
    name: "barracks"
    cost: 300
    buildTime: 20
    hp: 500
    width: 2
    height: 2
    produces:
      - infantry
      - tank
)");
}
// 在 (cx,cy) 生成一个单位并返回 id；用 CommandQueue + Step 走正常流程，
// 保证和 rts_game 的 startLevel 走同一条代码路径。
EntityId SpawnAt(Simulation& sim, uint8_t player, int32_t cx, int32_t cy, uint32_t unitType) {
	const size_t before = sim.GetWorld().Entities().All().size();
	Command c;
	c.type = CommandType::Spawn;
	c.playerId = player;
	c.x = cx * kSubPerCell;
	c.y = cy * kSubPerCell;
	c.targetId = unitType;
	sim.Commands().Push(c);
	sim.Step();
	const auto& all = sim.GetWorld().Entities().All();
	CHECK(all.size() > before);
	return all.back().id;
}

void IssueMove(Simulation& sim, EntityId id, int32_t cx, int32_t cy) {
	Command c;
	c.type = CommandType::Move;
	c.entityId = id;
	c.playerId = 0;
	c.x = cx * kSubPerCell;
	c.y = cy * kSubPerCell;
	sim.Commands().Push(c);
}

} // namespace

TEST(unit_move_changes_position_on_test_map) {
	// 基线：测试地图上必须能动（如果这个都不过，问题在移动系统本身）
	Simulation sim(Map::GenerateTestMap(64, 64, 7), 0x2024, BuildTestRules());
	const EntityId id = SpawnAt(sim, 0, 20, 20, 0);

	const Actor* a = sim.GetWorld().Entities().Get(id);
	CHECK(a != nullptr);
	if (a == nullptr) return;
	const SubCoord start = a->pos;
	CHECK(a->movable.speed > 0);
	std::printf("  [move] spawned id=%u speed=%d comps=0x%02X at (%d,%d)\n",
		id, a->movable.speed, a->components, start.x / kSubPerCell, start.y / kSubPerCell);

	IssueMove(sim, id, 28, 20);
	sim.Run(60);

	a = sim.GetWorld().Entities().Get(id);
	CHECK(a != nullptr);
	if (a == nullptr) return;
	const SubCoord end = a->pos;
	std::printf("  [move] after 60 ticks: (%d,%d) -> (%d,%d)\n",
		start.x / kSubPerCell, start.y / kSubPerCell, end.x / kSubPerCell, end.y / kSubPerCell);
	CHECK(end.x != start.x || end.y != start.y);
}

TEST(unit_move_works_on_real_ra_map) {
	// 真实地图（130x74）上的同一链路。注意要用地图中心附近的可走格，
	// 否则目标会被 FindNearestWalkable 吸附回原地，变成"没动"。
	Map realMap;
	if (!realMap.LoadBinary("grid_map.map")) {
		std::printf("  [move] grid_map.map not present, skipping\n");
		return;
	}
	std::printf("  [move] real map %dx%d\n", realMap.Width(), realMap.Height());
	Simulation sim(realMap, 0x2024, BuildTestRules());

	const int32_t cx = realMap.Width() / 2;
	const int32_t cy = realMap.Height() / 2;
	const EntityId id = SpawnAt(sim, 0, cx, cy, 0);

	const Actor* a = sim.GetWorld().Entities().Get(id);
	CHECK(a != nullptr);
	if (a == nullptr) return;
	const SubCoord start = a->pos;
	std::printf("  [move] real: id=%u speed=%d comps=0x%02X at (%d,%d)\n",
		id, a->movable.speed, a->components, start.x / kSubPerCell, start.y / kSubPerCell);

	// 沿 +x 找 4 格外的最近可走格作为目标，模拟玩家右键
	int32_t tx = cx, ty = cy;
	const Map& m = sim.GetWorld().GetMap();
	for (int32_t d = 4; d <= 10; ++d) {
		if (m.Walkable(CellCoord(cx + d, cy))) { tx = cx + d; ty = cy; break; }
		if (m.Walkable(CellCoord(cx, cy + d))) { tx = cx; ty = cy + d; break; }
	}
	std::printf("  [move] real: target (%d,%d) walkable=%d\n", tx, ty,
		m.Walkable(CellCoord(tx, ty)) ? 1 : 0);

	IssueMove(sim, id, tx, ty);
	sim.Run(90);

	a = sim.GetWorld().Entities().Get(id);
	CHECK(a != nullptr);
	if (a == nullptr) return;
	const SubCoord end = a->pos;
	std::printf("  [move] real after 90 ticks: (%d,%d) -> (%d,%d)\n",
		start.x / kSubPerCell, start.y / kSubPerCell, end.x / kSubPerCell, end.y / kSubPerCell);
	CHECK(end.x != start.x || end.y != start.y);
}

TEST(produced_unit_can_move) {
	// 走 Produce 路径造出来的单位（组件位和 Spawn 不同）也必须能动，
	// 否则玩家造出来的坦克一样会"不动"。
	Simulation sim(Map::GenerateTestMap(64, 64, 11), 0x2024, BuildTestRules());
	sim.GetWorld().AddCredits(0, 5000);

	// 先造一个兵营
	{
		Command c;
		c.type = CommandType::Build;
		c.playerId = 0;
		c.x = 20 * kSubPerCell;
		c.y = 20 * kSubPerCell;
		c.targetId = 1;
		sim.Commands().Push(c);
		sim.Step();
	}
	EntityId barracks = 0;
	for (const auto& e : sim.GetWorld().Entities().All()) {
		if (e.alive && e.playerId == 0 && e.Has(kCompBuilding)) { barracks = e.id; break; }
	}
	CHECK(barracks != 0);
	if (barracks == 0) return;

	// 下一个坦克生产命令，跑到完成。targetId 是 unit 的 numericId，
	// 这里按名字查出来，别手写数字（tank 在本规则里是 1，不是 2）。
	const UnitDef* tankDef = sim.GetWorld().GetRules().FindUnit("tank");
	CHECK(tankDef != nullptr);
	if (tankDef == nullptr) return;
	const size_t before = sim.GetWorld().Entities().All().size();
	{
		Command c;
		c.type = CommandType::Produce;
		c.entityId = barracks;
		c.playerId = 0;
		c.targetId = static_cast<uint32_t>(tankDef->numericId);
		sim.Commands().Push(c);
	}
	sim.Run(400);

	const auto& all = sim.GetWorld().Entities().All();
	CHECK(all.size() > before);
	EntityId tank = 0;
	for (const auto& e : all) {
		if (e.alive && e.playerId == 0 && e.Has(kCompMovable) && !e.Has(kCompBuilding)) {
			tank = e.id;
			break;
		}
	}
	CHECK(tank != 0);
	if (tank == 0) return;

	const Actor* a = sim.GetWorld().Entities().Get(tank);
	CHECK(a != nullptr);
	if (a == nullptr) return;
	std::printf("  [move] produced tank id=%u speed=%d comps=0x%02X vision=%d\n",
		tank, a->movable.speed, a->components, a->Has(kCompVision) ? 1 : 0);
	CHECK(a->Has(kCompMovable));
	CHECK(a->Has(kCompVision));      // 缺 Vision 会让它周围一直是黑的
	CHECK(a->movable.speed > 0);

	const SubCoord start = a->pos;
	IssueMove(sim, tank, start.x / kSubPerCell + 5, start.y / kSubPerCell);
	sim.Run(90);
	a = sim.GetWorld().Entities().Get(tank);
	CHECK(a != nullptr);
	if (a == nullptr) return;
	CHECK(a->pos.x != start.x || a->pos.y != start.y);
}
