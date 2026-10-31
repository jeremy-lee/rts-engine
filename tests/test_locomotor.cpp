// RTS Engine — tests/test_locomotor.cpp
// 验证 Locomotor/TerrainSpeeds 与 WAngle.TickFacing 是否对齐 OpenRA。
#include "test_framework.h"

#include "core/wangle.h"
#include "sim/locomotor.h"
#include "sim/miniyaml.h"

#include <cstdio>
#include <filesystem>
#include <string>

using namespace rts;

TEST(wangle_tick_facing_reaches_target) {
	// 目标在 90° 内、turnSpeed 足够时应该一步到位（OpenRA Util.TickFacing）
	const WAngle current(0);
	const WAngle desired(100);
	CHECK(WAngle::TickFacing(current, desired, 512) == desired);
	CHECK(WAngle::TickFacing(current, desired, 100) == desired); // 刚好相等也算到达
}

TEST(wangle_tick_facing_limits_step) {
	// turnSpeed=32 时每 tick 只转 32
	const WAngle current(0);
	const WAngle desired(256); // 90°
	CHECK_EQ(WAngle::TickFacing(current, desired, 32).angle, 32);
	CHECK_EQ(WAngle::TickFacing(WAngle(32), desired, 32).angle, 64);
	// 反方向
	CHECK_EQ(WAngle::TickFacing(WAngle(0), WAngle(896), 32).angle, 992); // -32 → 992
}

TEST(wangle_shortest_delta) {
	CHECK_EQ(WAngle(0).ShortestDelta(WAngle(100)), 100);
	CHECK_EQ(WAngle(0).ShortestDelta(WAngle(900)), -124); // 900 等价 -124 → 走负方向
	CHECK_EQ(WAngle(900).ShortestDelta(WAngle(0)), 124);
}

TEST(locomotor_parse_from_yaml) {
	const char* text =
		"World:\n"
		"\tLocomotor@FOOT:\n"
		"\t\tName: foot\n"
		"\t\tSharesCell: true\n"
		"\t\tTerrainSpeeds:\n"
		"\t\t\tClear: 100\n"
		"\t\t\tRough: 89\n"
		"\t\t\tRoad: 111\n"
		"\t\t\tWater: 0\n";
	YamlNode root;
	CHECK(MiniYaml::Parse(text, root));
	const YamlNode* world = root.Child("World");
	CHECK(world != nullptr);

	LocomotorSet locos;
	locos.LoadFrom(*world);
	CHECK_EQ(locos.Size(), static_cast<size_t>(1));

	const Locomotor* foot = locos.Find("foot");
	CHECK(foot != nullptr);
	CHECK(foot->sharesCell);
	CHECK_EQ(foot->SpeedFor("Clear"), 100);
	CHECK_EQ(foot->SpeedFor("Rough"), 89);
	CHECK_EQ(foot->SpeedFor("Road"), 111);
	CHECK_EQ(foot->SpeedFor("Water"), 0);
	CHECK_EQ(foot->SpeedFor("Nonexistent"), 0); // 未定义地形 = 不可通行
	CHECK(foot->CanEnter("Clear"));
	CHECK(!foot->CanEnter("Water"));
	// 也支持用 yaml key 查找
	CHECK(locos.Find("FOOT") == foot);
}

TEST(apply_percentage_modifiers) {
	// OpenRA Util.ApplyPercentageModifiers: base * (100 + sum) / 100
	CHECK_EQ(ApplyPercentageModifiers(100, {}), 100);
	CHECK_EQ(ApplyPercentageModifiers(100, {50}), 150);
	CHECK_EQ(ApplyPercentageModifiers(100, {-50}), 50);
	CHECK_EQ(ApplyPercentageModifiers(56, {89, -20}), 94); // 56 * 169/100 = 94.64 → 94
}

TEST(locomotor_load_real_ra_world) {
	namespace fs = std::filesystem;
	const std::string path = "D:/dosbox/game/OpenRA/mods/ra/rules/world.yaml";
	if (!fs::exists(path)) { CHECK(true); return; }
	YamlNode root;
	CHECK(MiniYaml::ParseFile(path.c_str(), root));
	// Locomotor 定义在抽象的 ^BaseWorld 上（World 通过 Inherits: ^BaseWorld 继承）
	const YamlNode* baseWorld = root.Child("^BaseWorld");
	CHECK(baseWorld != nullptr);
	if (baseWorld == nullptr) return;
	LocomotorSet locos;
	locos.LoadFrom(*baseWorld);
	std::printf("  [locomotor] RA locomotors=%zu\n", locos.Size());
	CHECK(locos.Size() >= 5);
	const Locomotor* foot = locos.Find("foot");
	CHECK(foot != nullptr);
	if (foot == nullptr) return;
	std::printf("  [locomotor] foot: Clear=%d Rough=%d Road=%d Water=%d sharesCell=%d\n",
		foot->SpeedFor("Clear"), foot->SpeedFor("Rough"), foot->SpeedFor("Road"),
		foot->SpeedFor("Water"), foot->sharesCell ? 1 : 0);
	CHECK_EQ(foot->SpeedFor("Clear"), 100);
	CHECK_EQ(foot->SpeedFor("Rough"), 89);
	CHECK(foot->SpeedFor("Water") == 0);
	const Locomotor* ship = locos.Find("ship");
	if (ship != nullptr) {
		CHECK(ship->SpeedFor("Water") > 0);
		CHECK_EQ(ship->SpeedFor("Clear"), 0);
	}
}