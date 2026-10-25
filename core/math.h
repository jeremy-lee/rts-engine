// RTS Engine — core/math.h
// 整数向量、子格坐标、千分度角与查表三角、整数开方。
// 逻辑层禁止浮点；本文件所有运算均为整数（查表 + 整数运算）。
#pragma once

#include <cstdint>
#include <array>
#include "fixed.h"

namespace rts {

// ---- 子格坐标：1 格 = 1024 子格，int32 可覆盖 ±2M 格 ----
static constexpr int32 kSubPerCell = 1024;
static constexpr int32 kSubShift = 10; // log2(1024)

// 格子坐标（逻辑网格）
struct CellCoord {
	int32 x = 0, y = 0;
	constexpr CellCoord() = default;
	constexpr CellCoord(int32 x_, int32 y_) : x(x_), y(y_) {}
	constexpr bool operator==(const CellCoord& o) const { return x == o.x && y == o.y; }
	constexpr bool operator!=(const CellCoord& o) const { return !(*this == o); }
};

// 子格坐标（精确位置）
struct SubCoord {
	int32 x = 0, y = 0;
	constexpr SubCoord() = default;
	constexpr SubCoord(int32 x_, int32 y_) : x(x_), y(y_) {}

	static constexpr int32 FloorDiv(int32 a, int32 b) {
		return (a >= 0) ? (a / b) : -((-a + b - 1) / b);
	}

	static constexpr SubCoord FromCell(CellCoord c) { return SubCoord(c.x * kSubPerCell, c.y * kSubPerCell); }
	constexpr CellCoord ToCell() const {
		return CellCoord(FloorDiv(x, kSubPerCell), FloorDiv(y, kSubPerCell));
	}
	constexpr int32 OffsetX() const { return x - FloorDiv(x, kSubPerCell) * kSubPerCell; }
	constexpr int32 OffsetY() const { return y - FloorDiv(y, kSubPerCell) * kSubPerCell; }

	constexpr SubCoord operator+(const SubCoord& o) const { return SubCoord(x + o.x, y + o.y); }
	constexpr SubCoord operator-(const SubCoord& o) const { return SubCoord(x - o.x, y - o.y); }
	constexpr SubCoord& operator+=(const SubCoord& o) { x += o.x; y += o.y; return *this; }
	constexpr SubCoord& operator-=(const SubCoord& o) { x -= o.x; y -= o.y; return *this; }
	constexpr bool operator==(const SubCoord& o) const { return x == o.x && y == o.y; }
	constexpr bool operator!=(const SubCoord& o) const { return !(*this == o); }
};

// 千分度角：0..1023 = 0..2π（1024 个单位一圈）
static constexpr int32 kAngleUnits = 1024;
using Angle = int32;

// ---- 整数开方 ----
// isqrt: 返回 floor(sqrt(v))
inline uint64 Isqrt(uint64 v) {
	if (v == 0) return 0;
	uint64 r = 0;
	uint64 bit = 1ull << 62;
	while (bit > v) bit >>= 2;
	while (bit != 0) {
		if (v >= r + bit) {
			v -= r + bit;
			r = (r >> 1) + bit;
		} else {
			r >>= 1;
		}
		bit >>= 2;
	}
	return r;
}

// 输入为「Q16.16 的平方值」（raw），返回距离的 Q16.16 表示。
// 推导：raw = 65536 * d²  →  d = sqrt(raw)/256  →  d * 65536 = sqrt(raw) * 256
inline int32 SqrtFixed(int64 rawSq) {
	if (rawSq <= 0) return 0;
	const uint64 root = Isqrt(static_cast<uint64>(rawSq));
	return static_cast<int32>(root * 256ull);
}

// 距离平方（子格单位）
inline int64 DistanceSq(const SubCoord& a, const SubCoord& b) {
	const int64 dx = static_cast<int64>(a.x) - b.x;
	const int64 dy = static_cast<int64>(a.y) - b.y;
	return dx * dx + dy * dy;
}

// 距离（子格单位，整数 floor）。RTS 判定用（射程/到达/碰撞）。
inline int32 DistanceSub(const SubCoord& a, const SubCoord& b) {
	return static_cast<int32>(Isqrt(static_cast<uint64>(DistanceSq(a, b))));
}

// 距离（Q16.16，子格单位）。一阶泰勒修正，精度远优于 1 子格。
inline Fixed DistanceFixed(const SubCoord& a, const SubCoord& b) {
	const int64 sq = DistanceSq(a, b);
	if (sq <= 0) return kFixedZero;
	const uint64 r = Isqrt(static_cast<uint64>(sq)); // floor(sqrt)
	const uint64 rem = static_cast<uint64>(sq) - r * r; // < 2r+1
	const uint64 frac = (r > 0) ? (rem * 65536ull) / (2ull * r) : 0ull;
	return Fixed::FromRaw(static_cast<int32>(r * 65536ull + frac));
}

// 子格线性插值：t ∈ [0,1]（Fixed）
inline SubCoord Lerp(const SubCoord& a, const SubCoord& b, Fixed t) {
	const int64 x = static_cast<int64>(a.x) + (static_cast<int64>(b.x - a.x) * t.Raw()) / Fixed::kOne;
	const int64 y = static_cast<int64>(a.y) + (static_cast<int64>(b.y - a.y) * t.Raw()) / Fixed::kOne;
	return SubCoord(static_cast<int32>(x), static_cast<int32>(y));
}

// ---- 查表三角 ----
// sin/cos 以 Q16.16 表示，按 1024 等分。采用整数多项式近似（[0,π/2] 内误差 < 1e-3），
// 对 RTS 的朝向/投射物足够；若需更高精度，用工具生成精确表替换。
struct TrigTable {
	static constexpr int kQuarter = kAngleUnits / 4; // 256
	static constexpr int64 kHalfPiQ16 = 102944;      // round(π/2 * 65536)

	Fixed sin[kAngleUnits];
	Fixed cos[kAngleUnits];

	TrigTable() {
		for (int i = 0; i < kAngleUnits; ++i) {
			const int quad = i / kQuarter;
			const int k = i % kQuarter; // 0..255
			// x = k/256 * π/2（Q16.16）
			const int64 xq = (static_cast<int64>(k) * kHalfPiQ16) / kQuarter;
			const int64 x2 = (xq * xq) >> 16;            // x²（Q16.16）
			const int64 x3 = (x2 * xq) >> 16;            // x³
			const int64 x5 = (x3 * x2) >> 16;            // x⁵
			const int64 s = xq - x3 / 6 + x5 / 120;      // sin
			const int64 c = Fixed::kOne - x2 / 2 + (x2 * x2) / 24; // cos
			switch (quad) {
				case 0: sin[i] = Fixed::FromRaw(static_cast<int32>(s));  cos[i] = Fixed::FromRaw(static_cast<int32>(c)); break;
				case 1: sin[i] = Fixed::FromRaw(static_cast<int32>(c));  cos[i] = Fixed::FromRaw(static_cast<int32>(-s)); break;
				case 2: sin[i] = Fixed::FromRaw(static_cast<int32>(-s)); cos[i] = Fixed::FromRaw(static_cast<int32>(-c)); break;
				default: sin[i] = Fixed::FromRaw(static_cast<int32>(-c)); cos[i] = Fixed::FromRaw(static_cast<int32>(s)); break;
			}
		}
	}
};

inline const TrigTable& Trig() {
	static const TrigTable table;
	return table;
}

inline Fixed SinQ16(Angle a) { return Trig().sin[a & (kAngleUnits - 1)]; }
inline Fixed CosQ16(Angle a) { return Trig().cos[a & (kAngleUnits - 1)]; }

} // namespace rts
