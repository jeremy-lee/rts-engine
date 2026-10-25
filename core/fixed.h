// RTS Engine — core/fixed.h
// Q16.16 定点数（int32 存储，范围 ±32768，精度 1/65536）
// 规则：逻辑层禁止浮点，所有比例/速度/插值系数使用 Fixed。
#pragma once

#include <cstdint>
#include <cassert>
#include <cmath>

namespace rts {

using int32 = std::int32_t;
using int64 = std::int64_t;
using uint32 = std::uint32_t;
using uint64 = std::uint64_t;

class Fixed {
public:
	static constexpr int kFracBits = 16;
	static constexpr int32 kOne = 1 << kFracBits;      // 65536
	static constexpr int32 kHalf = kOne / 2;
	static constexpr int32 kMax = INT32_MAX;
	static constexpr int32 kMin = INT32_MIN;

	constexpr Fixed() = default;
	explicit constexpr Fixed(int32 raw) : m_raw(raw) {}

	static constexpr Fixed FromInt(int32 v) { return Fixed(v * kOne); }
	static constexpr Fixed FromRaw(int32 raw) { return Fixed(raw); }

	// 精确截断（向零）
	static constexpr int32 ToInt(Fixed f) { return f.m_raw / kOne; }

	constexpr int32 Raw() const { return m_raw; }

	constexpr Fixed operator+(Fixed o) const { return Fixed(m_raw + o.m_raw); }
	constexpr Fixed operator-(Fixed o) const { return Fixed(m_raw - o.m_raw); }
	constexpr Fixed operator-() const { return Fixed(-m_raw); }

	constexpr Fixed operator*(Fixed o) const {
		const int64 r = static_cast<int64>(m_raw) * o.m_raw;
		return Fixed(static_cast<int32>(r / kOne));
	}

	// 除法：o 不得为 0
	constexpr Fixed operator/(Fixed o) const {
		const int64 r = (static_cast<int64>(m_raw) * kOne) / o.m_raw;
		return Fixed(static_cast<int32>(r));
	}

	constexpr Fixed& operator+=(Fixed o) { m_raw += o.m_raw; return *this; }
	constexpr Fixed& operator-=(Fixed o) { m_raw -= o.m_raw; return *this; }
	constexpr Fixed& operator*=(Fixed o) { *this = *this * o; return *this; }
	constexpr Fixed& operator/=(Fixed o) { *this = *this / o; return *this; }

	constexpr bool operator==(Fixed o) const { return m_raw == o.m_raw; }
	constexpr bool operator!=(Fixed o) const { return m_raw != o.m_raw; }
	constexpr bool operator<(Fixed o) const { return m_raw < o.m_raw; }
	constexpr bool operator<=(Fixed o) const { return m_raw <= o.m_raw; }
	constexpr bool operator>(Fixed o) const { return m_raw > o.m_raw; }
	constexpr bool operator>=(Fixed o) const { return m_raw >= o.m_raw; }

	// 乘除整数（避免反复构造）
	constexpr Fixed MulInt(int32 v) const { return Fixed(static_cast<int32>(static_cast<int64>(m_raw) * v / kOne)); }

	// 四舍五入到最近整数
	constexpr int32 Round() const { return (m_raw >= 0) ? ((m_raw + kHalf) / kOne) : -((kHalf - m_raw) / kOne); }

private:
	int32 m_raw = 0;
};

// 常用常量
constexpr Fixed kFixedZero = Fixed::FromRaw(0);
constexpr Fixed kFixedOne = Fixed::FromRaw(Fixed::kOne);
constexpr Fixed kFixedHalf = Fixed::FromRaw(Fixed::kHalf);

} // namespace rts
