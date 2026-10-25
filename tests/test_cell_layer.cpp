// RTS Engine — tests/test_cell_layer.cpp
#include "test_framework.h"
#include "core/cell_layer.h"

using namespace rts;

TEST(cell_layer_basic) {
	CellLayer<uint8_t> layer(16, 16, 0);
	CHECK_EQ(layer.Width(), 16);
	CHECK_EQ(layer.Height(), 16);
	CHECK_EQ(layer.CellCount(), static_cast<size_t>(256));

	layer.Set(CellCoord(3, 5), 42);
	CHECK_EQ(layer.Get(CellCoord(3, 5)), 42);
	CHECK_EQ(layer.Get(CellCoord(0, 0)), 0);
}

TEST(cell_layer_out_of_bounds_safe) {
	CellLayer<int32_t> layer(8, 8, -1);
	// 越界读取返回 fallback
	CHECK_EQ(layer.Get(CellCoord(-1, 0), 999), 999);
	CHECK_EQ(layer.Get(CellCoord(100, 100), 999), 999);
	// 越界写入被忽略
	layer.Set(CellCoord(-5, -5), 123);
	layer.Set(CellCoord(50, 50), 123);
	CHECK_EQ(layer.Get(CellCoord(0, 0)), -1);
}

TEST(cell_layer_hash_deterministic) {
	CellLayer<uint8_t> a(32, 32, 0);
	CellLayer<uint8_t> b(32, 32, 0);
	for (int y = 0; y < 32; ++y)
		for (int x = 0; x < 32; ++x) {
			const uint8_t v = static_cast<uint8_t>((x * 7 + y * 13) & 0xFF);
			a.Set(CellCoord(x, y), v);
			b.Set(CellCoord(x, y), v);
		}
	CHECK_EQ(a.Hash(), b.Hash());

	// 修改一格后哈希必须变化
	b.Set(CellCoord(1, 1), 255);
	CHECK_NE(a.Hash(), b.Hash());
}

TEST(subcoord_roundtrip) {
	const CellCoord c(12, 34);
	const SubCoord s = SubCoord::FromCell(c);
	CHECK_EQ(s.x, 12 * kSubPerCell);
	CHECK_EQ(s.y, 34 * kSubPerCell);
	const CellCoord back = s.ToCell();
	CHECK_EQ(back.x, 12);
	CHECK_EQ(back.y, 34);
	CHECK_EQ(s.OffsetX(), 0);
}

TEST(subcoord_negative_floor) {
	const SubCoord s(-1, -1025);
	// -1 子格 -> 格 -1；-1025 子格 -> 格 -2
	const CellCoord c = s.ToCell();
	CHECK_EQ(c.x, -1);
	CHECK_EQ(c.y, -2);
	// 偏移始终在 [0, 1024)
	CHECK(s.OffsetX() >= 0 && s.OffsetX() < kSubPerCell);
	CHECK(s.OffsetY() >= 0 && s.OffsetY() < kSubPerCell);
}

TEST(distance_and_isqrt) {
	CHECK_EQ(Isqrt(0), 0ull);
	CHECK_EQ(Isqrt(1), 1ull);
	CHECK_EQ(Isqrt(4), 2ull);
	CHECK_EQ(Isqrt(9), 3ull);
	CHECK_EQ(Isqrt(100), 10ull);
	CHECK_EQ(Isqrt(99), 9ull);

	// 3-4-5 直角三角形：距离 = 5 格
	const SubCoord a(0, 0), b(3 * kSubPerCell, 4 * kSubPerCell);
	// 3-4-5: 距离 = 5 格 = 5120 子格
	CHECK_EQ(DistanceSub(a, b), 5 * kSubPerCell);
	const Fixed df = DistanceFixed(a, b);
	CHECK_EQ(df.Round(), 5 * kSubPerCell);
}
