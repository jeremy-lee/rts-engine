// RTS Engine — core/wdist.h
// 移植自 OpenRA 的 WDist（OpenRA.Game/WDist.cs）：
//   1D 世界距离，1024 单位 = 1 格。
// 与我们的 SubCoord（子格，1024/格）单位一致，提供类型安全的距离运算。
#pragma once

#include <cstdint>

namespace rts {

// 1D world distance: 1024 units = 1 cell（与 OpenRA WDist 一致）
struct WDist {
	int32_t length = 0;

	WDist() = default;
	explicit constexpr WDist(int32_t r) : length(r) {}

	static constexpr WDist Zero() { return WDist(0); }
	static constexpr WDist FromCells(int cells) { return WDist(cells * 1024); }

	constexpr int ToCells() const { return length / 1024; }
	constexpr int64_t LengthSquared() const { return static_cast<int64_t>(length) * length; }

	WDist operator+(WDist o) const { return WDist(length + o.length); }
	WDist operator-(WDist o) const { return WDist(length - o.length); }
	WDist operator-() const { return WDist(-length); }
	WDist operator/(int32_t d) const { return WDist(length / d); }
	WDist operator*(int32_t m) const { return WDist(length * m); }
	bool operator==(const WDist& o) const { return length == o.length; }
	bool operator!=(const WDist& o) const { return length != o.length; }
	bool operator<(const WDist& o) const { return length < o.length; }
	bool operator>(const WDist& o) const { return length > o.length; }
	bool operator<=(const WDist& o) const { return length <= o.length; }
	bool operator>=(const WDist& o) const { return length >= o.length; }
};

} // namespace rts
