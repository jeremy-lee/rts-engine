// RTS Engine — core/random.h
// 确定性随机数：Xoshiro256**（整数运算，跨平台一致）。
// 规则：逻辑层禁止 std::random_device / rand() / 未指定种子的引擎。
#pragma once

#include <cstdint>
#include "fixed.h"

namespace rts {

class Rng {
public:
	constexpr Rng() : m_s{1, 2, 3, 4} {}
	explicit constexpr Rng(uint64 seed) { Seed(seed); }

	constexpr void Seed(uint64 seed) {
		// splitmix64 展开种子，避免小种子导致低质量序列
		uint64 x = seed + 0x9E3779B97F4A7C15ull;
		for (int i = 0; i < 4; ++i) {
			x += 0x9E3779B97F4A7C15ull;
			uint64 z = x;
			z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
			z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
			z ^= z >> 31;
			m_s[i] = z;
		}
	}

	constexpr uint64 Next() {
		const uint64 result = RotL(m_s[1] * 5ull, 7) * 9ull;
		const uint64 t = m_s[1] << 17;
		m_s[2] ^= m_s[0];
		m_s[3] ^= m_s[1];
		m_s[1] ^= m_s[2];
		m_s[0] ^= m_s[3];
		m_s[2] ^= t;
		m_s[3] = RotL(m_s[3], 45);
		return result;
	}

	// 无偏范围 [0, n)
	constexpr uint64 Range(uint64 n) {
		if (n == 0) return 0;
		const uint64 threshold = (0ull - n) % n; // 拒绝采样阈值
		for (;;) {
			const uint64 r = Next();
			if (r >= threshold) return r % n;
		}
	}

	// Fixed 范围 [0, 1)
	constexpr Fixed NextFixed() {
		// 取高 16 位作为小数部分
		return Fixed::FromRaw(static_cast<int32>(Next() >> 48));
	}

	// [lo, hi) 的整数
	constexpr int32 RangeInt(int32 lo, int32 hi) {
		const uint64 span = static_cast<uint64>(static_cast<int64>(hi) - lo);
		return static_cast<int32>(lo + static_cast<int32>(Range(span)));
	}

private:
	static constexpr uint64 RotL(uint64 x, int k) { return (x << k) | (x >> (64 - k)); }
	uint64 m_s[4];
};

} // namespace rts
