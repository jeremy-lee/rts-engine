// RTS Engine — sim/world.cpp
#include "world.h"
#include "activity.h"

namespace rts {

World::World(int32_t width, int32_t height, uint64_t seed)
	: m_map(width, height, 0, kWalkable | kBuildable), m_rng(seed) {
	m_pathfinder.Resize(width, height);
}

World::World(Map map, uint64_t seed)
	: m_map(std::move(map)), m_rng(seed) {
	m_pathfinder.Resize(m_map.Width(), m_map.Height());
}

bool World::ComputePath(EntityId id, CellCoord target) {
	const Actor* a = m_entities.Get(id);
	if (a == nullptr) return false;

	std::vector<CellCoord> cells;
	const bool ok = m_pathfinder.FindPath(m_map, a->pos.ToCell(), target, cells);
	if (!ok) {
		m_paths.erase(id);
		return false;
	}
	m_paths[id] = UnitPath{cells, 0};
	return true;
}

UnitPath* World::GetPath(EntityId id) {
	const auto it = m_paths.find(id);
	return (it == m_paths.end()) ? nullptr : &it->second;
}

const UnitPath* World::GetPath(EntityId id) const {
	const auto it = m_paths.find(id);
	return (it == m_paths.end()) ? nullptr : &it->second;
}

void World::ClearPath(EntityId id) {
	m_paths.erase(id);
}

void World::ApplyCommand(const Command& cmd) {
	switch (cmd.type) {
		case CommandType::Spawn: {
			const uint8_t components =
				kCompMovable | kCompHealth | kCompArmament | kCompVision;
			// 生成点若不可通行，吸附到最近可通行格（M4 修正）
			const CellCoord spawnCell = Map::FindNearestWalkable(m_map, SubCoord(cmd.x, cmd.y).ToCell());
			const SubCoord spawnPos(
				spawnCell.x * kSubPerCell + kSubPerCell / 2,
				spawnCell.y * kSubPerCell + kSubPerCell / 2);
			m_entities.Create(cmd.playerId, spawnPos, components);
			break;
		}
		case CommandType::Move: {
			Actor* a = m_entities.Get(cmd.entityId);
			if (a == nullptr) break;
			const CellCoord from = a->pos.ToCell();
			// 目标格不可通行时吸附到最近可通行格
			const CellCoord to = Map::FindNearestWalkable(m_map, SubCoord(cmd.x, cmd.y).ToCell());
			std::vector<CellCoord> cells;
			if (m_pathfinder.FindPath(m_map, from, to, cells)) {
				m_paths[cmd.entityId] = UnitPath{cells, 0};
				const CellCoord finalCell = cells.empty() ? from : to;
				const SubCoord center = SubCoord::FromCell(finalCell) + SubCoord(kSubPerCell / 2, kSubPerCell / 2);
				activity::SetMoveTo(*a, center, m_tick);
			} else {
				// 寻路失败：退化为直线移动（会被地形阻挡）
				m_paths.erase(cmd.entityId);
				activity::SetMoveTo(*a, SubCoord(cmd.x, cmd.y), m_tick);
			}
			break;
		}
		case CommandType::Attack: {
			if (Actor* a = m_entities.Get(cmd.entityId)) {
				m_paths.erase(cmd.entityId);
				activity::SetAttack(*a, cmd.targetId, m_tick);
			}
			break;
		}
		case CommandType::Stop: {
			if (Actor* a = m_entities.Get(cmd.entityId)) {
				m_paths.erase(cmd.entityId);
				activity::SetIdle(*a, m_tick);
			}
			break;
		}
		default:
			break;
	}
}

void World::TickActors() {
	for (auto& a : m_entities.All()) {
		if (!a.alive) continue;
		activity::Update(*this, a);
	}
}

void World::ApplySeparation() {
	// 简单 O(n²) 分离：半径 256 子格（0.25 格），互相推开，避免单位重叠。
	// 确定性：i<j 固定顺序、整数运算、推不开时按固定方向。
	auto& actors = m_entities.All();
	constexpr int32_t kRadius = 256; // 子格
	const int64_t minDist = 2 * kRadius;
	const int64_t minDistSq = minDist * minDist;

	for (size_t i = 0; i < actors.size(); ++i) {
		if (!actors[i].alive) continue;
		for (size_t j = i + 1; j < actors.size(); ++j) {
			if (!actors[j].alive) continue;
			int64_t dx = static_cast<int64_t>(actors[j].pos.x) - actors[i].pos.x;
			int64_t dy = static_cast<int64_t>(actors[j].pos.y) - actors[i].pos.y;
			int64_t d2 = dx * dx + dy * dy;
			if (d2 >= minDistSq) continue;
			int64_t d = static_cast<int64_t>(Isqrt(static_cast<uint64_t>(d2)));
			if (d == 0) {
				// 完全重叠：确定性微推（沿 +x）
				actors[i].pos.x -= 1;
				actors[j].pos.x += 1;
				continue;
			}
			const int64_t push = (minDist - d) / 2;
			if (push <= 0) continue;
			const int32_t ox = static_cast<int32_t>((dx * push) / d);
			const int32_t oy = static_cast<int32_t>((dy * push) / d);
			// 推开后必须位于可通行格
			const CellCoord ci = SubCoord(actors[i].pos.x - ox, actors[i].pos.y - oy).ToCell();
			const CellCoord cj = SubCoord(actors[j].pos.x + ox, actors[j].pos.y + oy).ToCell();
			if (m_map.InBounds(ci) && m_map.Walkable(ci)) {
				actors[i].pos.x -= ox;
				actors[i].pos.y -= oy;
			}
			if (m_map.InBounds(cj) && m_map.Walkable(cj)) {
				actors[j].pos.x += ox;
				actors[j].pos.y += oy;
			}
		}
	}
}

void World::Advance(CommandQueue& queue) {
	std::vector<Command> cmds;
	queue.Consume(cmds);
	for (const auto& c : cmds) {
		ApplyCommand(c);
	}
	TickActors();
	ApplySeparation();
	++m_tick;
}

uint64_t World::StateHash() const {
	uint64_t h = 1469598103934665603ull;
	h = HashMix(h, static_cast<uint64_t>(m_tick));
	h = HashMix(h, m_map.Hash());
	h = HashMix(h, m_entities.Hash());
	h = HashMix(h, m_rng.StateHash());
	// 路径数据也纳入哈希（顺序由 std::map 保证）
	for (const auto& kv : m_paths) {
		h = HashMix(h, static_cast<uint64_t>(kv.first));
		h = HashMix(h, static_cast<uint64_t>(kv.second.cells.size()));
		h = HashMix(h, static_cast<uint64_t>(kv.second.index));
		for (const auto& c : kv.second.cells) {
			h = HashMix(h, static_cast<uint64_t>(static_cast<uint32_t>(c.x)));
			h = HashMix(h, static_cast<uint64_t>(static_cast<uint32_t>(c.y)));
		}
	}
	return h;
}

} // namespace rts
