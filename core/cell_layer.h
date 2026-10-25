// RTS Engine — core/cell_layer.h
// 2D 格子层：地图上每个格子存一个 T（地形高度、可通行性、影响范围、迷雾…）。
// 设计要点：连续内存、越界安全（越界返回默认值）、确定性遍历顺序。
#pragma once

#include <cstdint>
#include <vector>
#include <cassert>
#include "math.h"

namespace rts {

template <typename T>
class CellLayer {
public:
	CellLayer() = default;
	CellLayer(int32 width, int32 height, T fill = T{})
		: m_width(width), m_height(height), m_data(static_cast<size_t>(width) * height, fill) {}

	void Resize(int32 width, int32 height, T fill = T{}) {
		m_width = width;
		m_height = height;
		m_data.assign(static_cast<size_t>(width) * height, fill);
	}

	int32 Width() const { return m_width; }
	int32 Height() const { return m_height; }
	size_t CellCount() const { return m_data.size(); }

	bool InBounds(CellCoord c) const {
		return c.x >= 0 && c.y >= 0 && c.x < m_width && c.y < m_height;
	}

	// 越界返回 fallback（默认 T{}），保证逻辑层不崩
	T Get(CellCoord c, T fallback = T{}) const {
		if (!InBounds(c)) return fallback;
		return m_data[Index(c)];
	}

	void Set(CellCoord c, T value) {
		if (!InBounds(c)) return;
		m_data[Index(c)] = value;
	}

	T& At(CellCoord c) {
		assert(InBounds(c));
		return m_data[Index(c)];
	}

	const T& At(CellCoord c) const {
		assert(InBounds(c));
		return m_data[Index(c)];
	}

	// 确定性哈希（顺序聚合，供 World::StateHash 使用）
	uint64 Hash(uint64 h = 1469598103934665603ull) const {
		// FNV-1a over raw bytes
		const uint8_t* p = reinterpret_cast<const uint8_t*>(m_data.data());
		const size_t n = m_data.size() * sizeof(T);
		for (size_t i = 0; i < n; ++i) {
			h ^= p[i];
			h *= 1099511628211ull;
		}
		// 混入尺寸
		h ^= static_cast<uint64>(m_width);
		h *= 1099511628211ull;
		h ^= static_cast<uint64>(m_height);
		h *= 1099511628211ull;
		return h;
	}

private:
	size_t Index(CellCoord c) const {
		return static_cast<size_t>(c.y) * m_width + c.x;
	}

	int32 m_width = 0;
	int32 m_height = 0;
	std::vector<T> m_data;
};

} // namespace rts
