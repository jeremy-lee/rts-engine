// 移动速度各方向一致性（回归测试）。
//
// 用户报"往不同方向移动的时候不等距"。旧实现：
//     sx = (dx*speed)/d;  sy = (dy*speed)/d;
//     next = (pos.x + (sx != 0 ? sx : (dx > 0 ? 1 : -1)),
//             pos.y + (sy != 0 ? sy : (dy > 0 ? 1 : -1)));
// 零分量被强行补成 ±1，于是：
//   * 纯水平移动每 tick 额外往 y 偏 -1（正右走会持续上漂）
//   * 正右步长 ≈ speed+1，45° 斜向 = floor(speed/√2) ≈ 0.707*speed
//   * 斜/直 速度比 ≈ 0.69 —— 就是"不等距"
// 修法：把不足一子格的余数存进 movables.errX/errY 下 tick 补上，去掉 ±1 兜底。
//
// OpenRA 对应实现：Mobile.cs 用 `(toPos - fromPos).Length / speed` 算耗时，即按世界坐标
// 距离而非格数计时，因此斜向走 √2 倍距离就花 √2 倍时间，速度在世界空间内均匀。
#include "test_framework.h"

#include "sim/simulation.h"
#include "sim/activity.h"
#include "sim/rules.h"

using namespace rts;

namespace {

Rules UniformTestRules() {
	return Rules::FromYAML(R"(
units:
  infantry:
    name: "infantry"
    cost: 50
    buildTime: 10
    hp: 100
    speed: 32
  tank:
    name: "tank"
    cost: 150
    buildTime: 20
    hp: 300
    speed: 32
buildings:
  barracks:
    name: "barracks"
    cost: 300
    buildTime: 20
    hp: 500
    width: 2
    height: 2
    produces:
      - tank
)");
}

// 在一张空地上把坦克放在 (20,20)，下令移动到 (tx,ty)，推进 ticks 后返回位移长度（子格）。
// 每次都用全新的 sim，避免上一段移动的余数影响到下一段。
double WalkDistance(int tx, int ty, int ticks) {
	Simulation sim(Map::GenerateTestMap(64, 64, 7), 0x2024, UniformTestRules());

	// 注意：必须生成在【格子中心】，和 world.cpp 的 Spawn 一致
	// （spawnCell * kSubPerCell + kSubPerCell/2）。生成在格子角落会让第一段路径变成斜线，
	// 看起来像"y 漂移"。
	Command spawn;
	spawn.type = CommandType::Spawn;
	spawn.playerId = 0;
	spawn.x = 20 * kSubPerCell + kSubPerCell / 2;
	spawn.y = 20 * kSubPerCell + kSubPerCell / 2;
	spawn.targetId = 0;            // produces[0] = tank
	sim.Commands().Push(spawn);
	sim.Step();

	const auto& all = sim.GetWorld().Entities().All();
	if (all.empty()) return -1.0;
	const EntityId id = all.back().id;

	Command mv;
	mv.type = CommandType::Move;
	mv.entityId = id;
	mv.playerId = 0;
	mv.x = tx * kSubPerCell;
	mv.y = ty * kSubPerCell;
	sim.Commands().Push(mv);

	for (int i = 0; i < ticks; ++i) sim.Step();

	const Actor* a = sim.GetWorld().Entities().Get(id);
	if (a == nullptr) return -1.0;
	const double dx = static_cast<double>(a->pos.x - (20 * kSubPerCell + kSubPerCell / 2));
	const double dy = static_cast<double>(a->pos.y - (20 * kSubPerCell + kSubPerCell / 2));
	return std::sqrt(dx * dx + dy * dy);
}

} // namespace

TEST(move_speed_uniform_across_directions) {
	const int kTicks = 40;
	const double right = WalkDistance(45, 20, kTicks);
	const double down = WalkDistance(20, 45, kTicks);
	const double diag = WalkDistance(38, 38, kTicks);
	const double shallow = WalkDistance(45, 26, kTicks);

	std::printf("    正右 %.0f  正下 %.0f  45° %.0f  浅角 %.0f\n", right, down, diag, shallow);

	CHECK(right > 0.0);
	CHECK(down > 0.0);
	CHECK(diag > 0.0);

	// 四个方向的位移必须落在同一区间（旧实现斜向只有 69%）
	const double tol = 0.10;
	if (right > 0.0) {
		CHECK(std::fabs(down / right - 1.0) < tol);
		CHECK(std::fabs(diag / right - 1.0) < tol);
		CHECK(std::fabs(shallow / right - 1.0) < tol);
	}
}

TEST(move_horizontal_has_no_vertical_drift) {
	// 旧实现在纯水平移动时每 tick 往 y 补 -1，40 tick 会漂 -40 子格
	Simulation sim(Map::GenerateTestMap(64, 64, 7), 0x2024, UniformTestRules());

	Command spawn;
	spawn.type = CommandType::Spawn;
	spawn.playerId = 0;
	spawn.x = 20 * kSubPerCell + kSubPerCell / 2;
	spawn.y = 20 * kSubPerCell + kSubPerCell / 2;
	spawn.targetId = 0;
	sim.Commands().Push(spawn);
	sim.Step();

	const EntityId id = sim.GetWorld().Entities().All().back().id;

	Command mv;
	mv.type = CommandType::Move;
	mv.entityId = id;
	mv.playerId = 0;
	mv.x = 45 * kSubPerCell;
	mv.y = 20 * kSubPerCell;      // 同一行
	sim.Commands().Push(mv);

	for (int i = 0; i < 40; ++i) sim.Step();

	const Actor* a = sim.GetWorld().Entities().Get(id);
	CHECK(a != nullptr);
	if (a == nullptr) return;

	const int32_t drift = a->pos.y - (20 * kSubPerCell + kSubPerCell / 2);
	std::printf("    y 漂移 = %d 子格\n", drift);
	CHECK(drift == 0);
}
