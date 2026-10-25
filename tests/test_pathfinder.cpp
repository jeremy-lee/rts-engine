// RTS Engine — tests/test_pathfinder.cpp
// M4 验收：寻路正确性 / 无穿墙 / 确定性 / 性能
#include "test_framework.h"
#include "sim/pathfinder.h"
#include "sim/world.h"
#include "sim/simulation.h"
#include <chrono>
#include <cstdio>

using namespace rts;

TEST(pathfinder_straight_line) {
	Map map(32, 32, 0, kWalkable | kBuildable);
	Pathfinder pf(32, 32);
	std::vector<CellCoord> path;
	CHECK(pf.FindPath(map, CellCoord(2, 2), CellCoord(10, 2), path));
	CHECK(!path.empty());
	// 路径应沿 x 方向，终点为 (10,2)
	CHECK_EQ(path.back().x, 10);
	CHECK_EQ(path.back().y, 2);
	// 步数 = 8（不含起点，每步 1 格）
	CHECK_EQ(path.size(), static_cast<size_t>(8));
}

TEST(pathfinder_same_cell_empty_path) {
	Map map(8, 8, 0, kWalkable | kBuildable);
	Pathfinder pf(8, 8);
	std::vector<CellCoord> path;
	CHECK(pf.FindPath(map, CellCoord(3, 3), CellCoord(3, 3), path));
	CHECK(path.empty());
}

TEST(pathfinder_unreachable) {
	Map map(8, 8, 0, kWalkable | kBuildable);
	// 目标点被水包围
	for (int x = 0; x < 8; ++x) map.SetFlags(CellCoord(x, 4), kWater);
	Pathfinder pf(8, 8);
	std::vector<CellCoord> path;
	CHECK(!pf.FindPath(map, CellCoord(2, 2), CellCoord(2, 6), path));
}

TEST(pathfinder_avoids_water) {
	Map map(16, 16, 0, kWalkable | kBuildable);
	// 一堵水墙，中间留一个口
	for (int y = 0; y < 16; ++y) {
		if (y == 8) continue; // 留口
		map.SetFlags(CellCoord(8, y), kWater);
	}
	Pathfinder pf(16, 16);
	std::vector<CellCoord> path;
	CHECK(pf.FindPath(map, CellCoord(2, 2), CellCoord(14, 2), path));
	CHECK(!path.empty());
	// 路径上不能出现水域
	for (const auto& c : path) {
		CHECK(!map.Water(c));
	}
	// 必须经过开口 (8,8)
	bool throughGap = false;
	for (const auto& c : path) {
		if (c.x == 8 && c.y == 8) throughGap = true;
	}
	CHECK(throughGap);
}

TEST(pathfinder_no_corner_cutting) {
	Map map(16, 16, 0, kWalkable | kBuildable);
	// 对角障碍：(1,0) 和 (0,1) 是水，(1,1) 可通行 → (1,1) 被围死，不可达
	map.SetFlags(CellCoord(1, 0), kWater);
	map.SetFlags(CellCoord(0, 1), kWater);
	Pathfinder pf(16, 16);
	std::vector<CellCoord> path;
	// 从 (0,0) 无法到达被水包围的 (1,1)（禁止对角穿墙）
	CHECK(!pf.FindPath(map, CellCoord(0, 0), CellCoord(1, 1), path));

	// 另一种：只有一侧是水，(0,0)→(1,1) 允许对角但两侧检查仍成立
	Map map2(16, 16, 0, kWalkable | kBuildable);
	map2.SetFlags(CellCoord(1, 0), kWater); // 只有一侧堵
	Pathfinder pf2(16, 16);
	std::vector<CellCoord> path2;
	CHECK(pf2.FindPath(map2, CellCoord(0, 0), CellCoord(1, 1), path2));
	// 因为 (1,0) 是水，对角 (0,0)->(1,1) 被禁止，必须绕行（(0,1)->(1,1) 或先向下）
	for (const auto& c : path2) {
		CHECK(!map2.Water(c));
	}
}

TEST(pathfinder_deterministic) {
	Map map = Map::GenerateTestMap(64, 64, 123);
	Pathfinder pf(64, 64);
	std::vector<CellCoord> a, b;
	pf.FindPath(map, CellCoord(2, 2), CellCoord(60, 60), a);
	pf.FindPath(map, CellCoord(2, 2), CellCoord(60, 60), b);
	CHECK_EQ(a.size(), b.size());
	for (size_t i = 0; i < a.size() && i < b.size(); ++i) {
		CHECK_EQ(a[i].x, b[i].x);
		CHECK_EQ(a[i].y, b[i].y);
	}
}

TEST(world_path_movement) {
	// 单位沿路径移动到目标，最终位置在目标格中心
	Map map(32, 32, 0, kWalkable | kBuildable);
	World world(map, 1);
	const EntityId id = world.Entities().Create(0, SubCoord(2 * kSubPerCell, 2 * kSubPerCell),
		kCompMovable | kCompHealth);

	// 用 Move 命令（走寻路）
	Command cmd;
	cmd.type = CommandType::Move;
	cmd.entityId = id;
	cmd.x = 10 * kSubPerCell;
	cmd.y = 2 * kSubPerCell;
	world.ApplyCommand(cmd);
	CHECK(world.GetPath(id) != nullptr);

	// 推进模拟
	for (int i = 0; i < 300; ++i) {
		CommandQueue q;
		world.Advance(q);
	}
	const Actor* a = world.Entities().Get(id);
	CHECK_EQ(a->pos.ToCell().x, 10);
	CHECK_EQ(a->pos.ToCell().y, 2);
	CHECK_EQ(a->activity.type, ActivityType::Idle);
	CHECK(world.GetPath(id) == nullptr); // 路径已清理
}

TEST(pathfinder_performance_256) {
	// 256x256 空地 + 柱状障碍：确保起终点可达
	Map map(256, 256, 0, kWalkable | kBuildable);
	for (int y = 20; y < 236; y += 40) {
		for (int x = 20; x < 236; x += 40) {
			map.SetFlags(CellCoord(x, y), kWater); // 稀疏柱子
		}
	}
	Pathfinder pf(256, 256);
	std::vector<CellCoord> path;
	const auto t0 = std::chrono::steady_clock::now();
	const bool ok = pf.FindPath(map, CellCoord(2, 2), CellCoord(250, 250), path);
	const auto t1 = std::chrono::steady_clock::now();
	const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
	std::printf("    [perf] 256x256 A*: %.2f ms, path=%zu, expanded=%llu\n",
		ms, path.size(), static_cast<unsigned long long>(pf.LastExpanded()));
	CHECK(ok);
	CHECK(ms < 500.0);
}
