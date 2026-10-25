// RTS Engine — core/wvec.h
// 移植自 OpenRA 的 WVec（OpenRA.Game/WVec.cs）：
//   3D 世界向量，1024 单位 = 1 格（x/y 对应我们的子格，z 暂未使用）。
// 提供类型安全的向量运算；Length 用整数 Isqrt（确定性）。
#pragma once

#include <cstdint>
#include "wdist.h"
#include "core/cell_layer.h"

namespace rts {

// 整数平方根（确定性，与 core 里的 Isqrt 一致实现）
inline int64_t WVecIsqrt(uint64_t v) {
	if (v == 0) return 0;
	uint64_t x = v;
	uint64_t y = (x + 1) / 2;
	while (y < x) {
		x = y;
		y = (x + v / x) / 2;
	}
	return static_cast<int64_t>(x);
}

// 3D world vector: 1024 units = 1 cell
struct WVec {
	int32_t x = 0, y = 0, z = 0;

	WVec() = default;
	constexpr WVec(int32_t px, int32_t py, int32_t pz = 0) : x(px), y(py), z(pz) {}

	static constexpr WVec Zero() { return WVec(0, 0, 0); }
	static constexpr WVec FromCell(CellCoord c) { return WVec(c.x * 1024, c.y * 1024, 0); }

	constexpr int64_t LengthSquared() const {
		return static_cast<int64_t>(x) * x + static_cast<int64_t>(y) * y + static_cast<int64_t>(z) * z;
	}

	WDist Length() const {
		return WDist(static_cast<int32_t>(WVecIsqrt(static_cast<uint64_t>(LengthSquared()))));
	}

	constexpr int32_t Dot(const WVec& o) const { return x * o.x + y * o.y + z * o.z; }

	WVec operator+(const WVec& o) const { return WVec(x + o.x, y + o.y, z + o.z); }
	WVec operator-(const WVec& o) const { return WVec(x - o.x, y - o.y, z - o.z); }
	WVec operator-() const { return WVec(-x, -y, -z); }
	WVec operator*(int32_t m) const { return WVec(x * m, y * m, z * m); }
	WVec operator/(int32_t d) const { return WVec(x / d, y / d, z / d); }
	bool operator==(const WVec& o) const { return x == o.x && y == o.y && z == o.z; }
	bool operator!=(const WVec& o) const { return !(*this == o); }

	constexpr CellCoord ToCell() const { return CellCoord(x / 1024, y / 1024); }
};

} // namespace rts
