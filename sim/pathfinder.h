// RTS Engine — sim/pathfinder.h
// A* 网格寻路（8 方向，整数代价，确定性）。
// 设计：
//   - 全部整数运算（逻辑层禁浮点）
//   - 代价：直行 10，对角 14（≈10·√2），启发式 octile
//   - 优先队列 tie-break 严格（f → g → 节点索引），保证同输入同路径
//   - 对角移动检查两侧直行格可通行，禁止穿墙
#pragma once

#include <cstdint>
#include <vector>
#include <queue>
#include "core/core.h"
#include "map.h"

namespace rts {

class Pathfinder {
public:
	Pathfinder() = default;
	explicit Pathfinder(int32_t width, int32_t height) { Resize(width, height); }

	void Resize(int32_t width, int32_t height);

	// 寻路：from → to（格子坐标）。
	// 返回 true 时 outPath 为路径（不含起点，含终点）；失败返回 false。
	bool FindPath(const Map& map, CellCoord from, CellCoord to, std::vector<CellCoord>& outPath);

	// 统计（调试）
	uint64_t LastExpanded() const { return m_lastExpanded; }

private:
	struct Node {
		int32_t g = INT32_MAX;
		int32_t parent = -1;
		bool closed = false;
	};

	struct OpenEntry {
		int32_t f = 0;
		int32_t g = 0;
		int32_t index = 0;
	};

	// 优先队列比较：f 小者优先；f 相同 g 大者优先；再按 index 稳定
	struct Compare {
		bool operator()(const OpenEntry& a, const OpenEntry& b) const {
			if (a.f != b.f) return a.f > b.f;
			if (a.g != b.g) return a.g < b.g;
			return a.index > b.index;
		}
	};

	int32_t Index(CellCoord c) const { return c.y * m_width + c.x; }
	bool InBounds(CellCoord c) const {
		return c.x >= 0 && c.y >= 0 && c.x < m_width && c.y < m_height;
	}

	int32_t m_width = 0;
	int32_t m_height = 0;
	std::vector<Node> m_nodes;
	std::priority_queue<OpenEntry, std::vector<OpenEntry>, Compare> m_open;
	uint64_t m_lastExpanded = 0;
};

} // namespace rts
