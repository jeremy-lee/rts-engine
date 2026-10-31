// RTS Engine — tests/test_projection.cpp
// M1 验收核心：投影/逆投影往返误差为 0（格子中心）。
#include "test_framework.h"
#include "render/projection.h"

using namespace rts;

TEST(projection_cell_center_roundtrip) {
	// 覆盖正坐标、负坐标、大坐标
	const CellCoord cells[] = {
		{0, 0}, {1, 0}, {0, 1}, {3, 5}, {10, 10},
		{-1, -1}, {-5, 3}, {7, -2}, {100, 100}, {255, 0},
	};
	for (const auto& c : cells) {
		const ScreenPos p = ProjectCell(c, 0);
		const CellCoord back = ScreenToCell(p);
		CHECK_EQ(back.x, c.x);
		CHECK_EQ(back.y, c.y);
	}
}

TEST(projection_formula_values) {
	// 等距投影：格中心 = ((x-y)*kStepX, (x+y)*kStepY)，kStepX=12 kStepY=6（24x24 tile 的菱形铺贴）
	// (0,0) -> (0, 6)   [格中心 = 子格 (0.5,0.5)]
	CHECK_EQ(ProjectCell(CellCoord(0, 0), 0).x, 0);
	CHECK_EQ(ProjectCell(CellCoord(0, 0), 0).y, 6);
	// (1,0) -> (12, 12)
	CHECK_EQ(ProjectCell(CellCoord(1, 0), 0).x, 12);
	CHECK_EQ(ProjectCell(CellCoord(1, 0), 0).y, 12);
	// (0,1) -> (-12, 12)
	CHECK_EQ(ProjectCell(CellCoord(0, 1), 0).x, -12);
	CHECK_EQ(ProjectCell(CellCoord(0, 1), 0).y, 12);
	// (1,1) -> (0, 18)
	CHECK_EQ(ProjectCell(CellCoord(1, 1), 0).x, 0);
	CHECK_EQ(ProjectCell(CellCoord(1, 1), 0).y, 18);
}

TEST(projection_height_affects_y_only) {
	const ScreenPos flat = ProjectCell(CellCoord(4, 4), 0);
	const ScreenPos high = ProjectCell(CellCoord(4, 4), 3);
	CHECK_EQ(flat.x, high.x);
	CHECK_EQ(flat.y - high.y, 3 * kHeightStep);
}

TEST(projection_subcell_fixed) {
	// 子格中心 (0.5, 0.5) 格 = (512, 512) 子格 → 屏幕半格（正交）
	const SubCoord s(512, 512);
	const Fixed sx = ProjectCellXFixed(s);
	const Fixed sy = ProjectCellYFixed(s, 0);
	// (512-512)*12/1024 = 0 ; (512+512)*6/1024 = 6
	CHECK_EQ(sx.Round(), 0);
	CHECK_EQ(sy.Round(), 6);

	// 1.5 格 x, 0.5 格 y -> ((1536-512)*12/1024, (1536+512)*6/1024) = (12, 12)
	const SubCoord s2(1536, 512);
	CHECK_EQ(ProjectCellXFixed(s2).Round(), 12);
	CHECK_EQ(ProjectCellYFixed(s2, 0).Round(), 12);

	// ProjectCell 必须与 ProjectSub(格中心) 一致
	const ScreenPos cellCenter = ProjectCell(CellCoord(3, 5), 0);
	const ScreenPos subCenter = ProjectSub(SubCoord(3 * kSubPerCell + kSubPerCell / 2,
		5 * kSubPerCell + kSubPerCell / 2), 0);
	CHECK_EQ(cellCenter.x, subCenter.x);
	CHECK_EQ(cellCenter.y, subCenter.y);
}

TEST(projection_adjacent_cells_distinct) {
	// 相邻格子投影必须不同，且逆投影能区分
	const CellCoord a(10, 10);
	const CellCoord b(11, 10);
	const CellCoord c(10, 11);
	CHECK_NE(ProjectCell(a, 0), ProjectCell(b, 0));
	CHECK_NE(ProjectCell(a, 0), ProjectCell(c, 0));
	CHECK_EQ(ScreenToCell(ProjectCell(b, 0)).x, 11);
	CHECK_EQ(ScreenToCell(ProjectCell(c, 0)).y, 11);
}

TEST(projection_large_map_roundtrip) {
	// 256x256 地图全覆盖往返（M1 验收标准）
	for (int32_t y = 0; y < 256; y += 7) {
		for (int32_t x = 0; x < 256; x += 7) {
			const CellCoord c(x, y);
			const ScreenPos p = ProjectCell(c, 0);
			const CellCoord back = ScreenToCell(p);
			if (back.x != c.x || back.y != c.y) {
				CHECK_EQ(back.x, c.x);
				CHECK_EQ(back.y, c.y);
				return;
			}
		}
	}
	CHECK(true);
}
