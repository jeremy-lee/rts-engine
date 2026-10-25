// RTS Engine — tests/test_wdist.cpp
// WDist / WVec：OpenRA 世界距离与向量的移植测试。
#include "core/wdist.h"
#include "core/wvec.h"
#include "test_framework.h"

using namespace rts;

TEST(wdist_from_cells) {
	CHECK_EQ(WDist::FromCells(1).length, 1024);
	CHECK_EQ(WDist::FromCells(3).length, 3072);
	CHECK_EQ(WDist::FromCells(1).ToCells(), 1);
	CHECK_EQ(WDist(2500).ToCells(), 2);
	CHECK_EQ(WDist::Zero().length, 0);
}

TEST(wdist_arithmetic) {
	const WDist a = WDist::FromCells(2);
	const WDist b = WDist::FromCells(1);
	CHECK_EQ((a + b).length, 3072);
	CHECK_EQ((a - b).length, 1024);
	CHECK_EQ((-a).length, -2048);
	CHECK_EQ((a * 3).length, 6144);
	CHECK_EQ((a / 2).length, 1024);
	CHECK(a > b);
	CHECK(b < a);
	CHECK(a >= b);
	CHECK(b <= a);
	CHECK(a != b);
}

TEST(wvec_length) {
	// 3-4-5 三角形：x=3 格, y=4 格 → 长度 5 格
	const WVec v(3 * 1024, 4 * 1024);
	CHECK_EQ(v.Length().length, 5 * 1024);
	// 对角线：1 格,1 格 → 1448（1024*sqrt(2)≈1448）
	const WVec d(1024, 1024);
	const int32_t len = d.Length().length;
	CHECK(len >= 1447 && len <= 1449);
}

TEST(wvec_arithmetic) {
	const WVec a(1024, 2048);
	const WVec b(512, 512);
	CHECK((a + b) == WVec(1536, 2560));
	CHECK((a - b) == WVec(512, 1536));
	CHECK((-a) == WVec(-1024, -2048));
	CHECK((a * 2) == WVec(2048, 4096));
	CHECK_EQ(a.Dot(b), 1024 * 512 + 2048 * 512);
	CHECK(WVec::Zero() == WVec(0, 0, 0));
}

TEST(wvec_cell_conversion) {
	const WVec v(3 * 1024 + 500, 2 * 1024 + 900);
	const CellCoord c = v.ToCell();
	CHECK_EQ(c.x, 3);
	CHECK_EQ(c.y, 2);
	const WVec fromCell = WVec::FromCell(CellCoord(5, 7));
	CHECK(fromCell == WVec(5 * 1024, 7 * 1024, 0));
}

TEST(wvec_deterministic) {
	const WVec v(12345, 67890, 42);
	CHECK_EQ(v.Length().length, v.Length().length);
	CHECK_EQ(v.LengthSquared(), v.LengthSquared());
}
