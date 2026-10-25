// RTS Engine — sim/pathfinder.cpp
#include "pathfinder.h"

namespace rts {

void Pathfinder::Resize(int32_t width, int32_t height) {
	m_width = width;
	m_height = height;
	m_nodes.assign(static_cast<size_t>(width) * height, Node{});
}

namespace {

constexpr int32_t kStraight = 10;
constexpr int32_t kDiagonal = 14;

// octile 启发式
inline int32_t Heuristic(CellCoord a, CellCoord b) {
	const int32_t dx = (a.x > b.x) ? (a.x - b.x) : (b.x - a.x);
	const int32_t dy = (a.y > b.y) ? (a.y - b.y) : (b.y - a.y);
	if (dx > dy) return kDiagonal * dy + kStraight * (dx - dy);
	return kDiagonal * dx + kStraight * (dy - dx);
}

} // namespace

bool Pathfinder::FindPath(const Map& map, CellCoord from, CellCoord to, std::vector<CellCoord>& outPath) {
	outPath.clear();
	m_lastExpanded = 0;

	if (!InBounds(from) || !InBounds(to)) return false;
	if (from == to) return true; // 空路径
	if (!map.Walkable(to)) return false;

	// 重置节点（只重置访问过的，避免每帧全量清空；这里全量重置保证正确）
	for (auto& n : m_nodes) {
		n.g = INT32_MAX;
		n.parent = -1;
		n.closed = false;
	}
	while (!m_open.empty()) m_open.pop();

	const int32_t startIdx = Index(from);
	const int32_t goalIdx = Index(to);
	m_nodes[startIdx].g = 0;

	OpenEntry start;
	start.f = Heuristic(from, to);
	start.g = 0;
	start.index = startIdx;
	m_open.push(start);

	static constexpr int32_t kDirs[8][2] = {
		{1, 0}, {-1, 0}, {0, 1}, {0, -1},
		{1, 1}, {1, -1}, {-1, 1}, {-1, -1}
	};

	while (!m_open.empty()) {
		const OpenEntry cur = m_open.top();
		m_open.pop();

		Node& curNode = m_nodes[cur.index];
		if (curNode.closed) continue;
		curNode.closed = true;
		++m_lastExpanded;

		if (cur.index == goalIdx) break;

		const int32_t cx = cur.index % m_width;
		const int32_t cy = cur.index / m_width;

		for (int d = 0; d < 8; ++d) {
			const int32_t nx = cx + kDirs[d][0];
			const int32_t ny = cy + kDirs[d][1];
			const CellCoord nc(nx, ny);
			if (!InBounds(nc)) continue;
			if (!map.Walkable(nc)) continue;

			const bool diagonal = (d >= 4);
			if (diagonal) {
				// 禁止对角穿墙：两侧直行格都必须可通行
				if (!map.Walkable(CellCoord(cx + kDirs[d][0], cy)) ||
					!map.Walkable(CellCoord(cx, cy + kDirs[d][1]))) {
					continue;
				}
			}

			const int32_t ni = Index(nc);
			Node& neighbor = m_nodes[ni];
			if (neighbor.closed) continue;

			const int32_t cost = diagonal ? kDiagonal : kStraight;
			const int32_t tentative = curNode.g + cost;
			if (tentative < neighbor.g) {
				neighbor.g = tentative;
				neighbor.parent = cur.index;
				OpenEntry e;
				e.f = tentative + Heuristic(nc, to);
				e.g = tentative;
				e.index = ni;
				m_open.push(e);
			}
		}
	}

	if (m_nodes[goalIdx].g == INT32_MAX) return false;

	// 回溯路径（不含起点）
	int32_t idx = goalIdx;
	while (idx != startIdx && idx >= 0) {
		outPath.push_back(CellCoord(idx % m_width, idx / m_width));
		idx = m_nodes[idx].parent;
	}
	// 当前是从终点→起点，反转为起点→终点
	std::reverse(outPath.begin(), outPath.end());
	return true;
}

} // namespace rts
