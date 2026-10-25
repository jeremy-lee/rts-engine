// RTS Engine — sim/world.cpp
#include "world.h"

namespace rts {

World::World(int32_t width, int32_t height, uint64_t seed)
	: m_map(width, height, 0, kWalkable | kBuildable), m_rng(seed) {}

World::World(Map map, uint64_t seed)
	: m_map(std::move(map)), m_rng(seed) {}

void World::ApplyCommand(const Command& cmd) {
	switch (cmd.type) {
		case CommandType::Spawn: {
			Unit u;
			u.id = m_nextUnitId++;
			u.playerId = cmd.playerId;
			u.pos = SubCoord(cmd.x, cmd.y);
			u.target = u.pos;
			u.speed = 32; // 32 子格/tick
			m_units.push_back(u);
			break;
		}
		case CommandType::Move: {
			if (Unit* u = FindUnit(cmd.entityId)) {
				u->target = SubCoord(cmd.x, cmd.y);
				u->moving = true;
			}
			break;
		}
		case CommandType::Stop: {
			if (Unit* u = FindUnit(cmd.entityId)) {
				u->moving = false;
			}
			break;
		}
		default:
			break;
	}
}

void World::MoveUnits() {
	for (auto& u : m_units) {
		if (!u.alive || !u.moving) continue;
		const int32_t dx = u.target.x - u.pos.x;
		const int32_t dy = u.target.y - u.pos.y;
		const int64_t dist = DistanceSq(u.pos, u.target);
		if (dist == 0) {
			u.moving = false;
			continue;
		}
		const int64_t step = static_cast<int64_t>(u.speed) * u.speed;
		if (dist <= step) {
			// 到达前检查目标格可通行（M1）
			const CellCoord tc = u.target.ToCell();
			if (!m_map.InBounds(tc) || !m_map.Walkable(tc)) {
				u.moving = false;
				continue;
			}
			u.pos = u.target;
			u.moving = false;
			continue;
		}
		const int64_t d = static_cast<int64_t>(Isqrt(static_cast<uint64_t>(dist)));
		if (d == 0) { u.moving = false; continue; }
		const int64_t sx = (static_cast<int64_t>(dx) * u.speed) / d;
		const int64_t sy = (static_cast<int64_t>(dy) * u.speed) / d;

		// 落点格不可通行则停止（M1 简化碰撞：仅检查落点）
		SubCoord next(
			u.pos.x + static_cast<int32_t>(sx != 0 ? sx : (dx > 0 ? 1 : -1)),
			u.pos.y + static_cast<int32_t>(sy != 0 ? sy : (dy > 0 ? 1 : -1)));
		const CellCoord nc = next.ToCell();
		if (m_map.InBounds(nc) && m_map.Walkable(nc)) {
			u.pos = next;
		} else {
			u.moving = false;
		}
	}
}

void World::Advance(CommandQueue& queue) {
	std::vector<Command> cmds;
	queue.Consume(cmds);
	for (const auto& c : cmds) {
		ApplyCommand(c);
	}
	MoveUnits();
	++m_tick;
}

uint64_t World::StateHash() const {
	uint64_t h = 1469598103934665603ull;
	auto mix = [&h](uint64_t v) {
		h ^= v;
		h *= 1099511628211ull;
	};
	mix(static_cast<uint64_t>(m_tick));
	mix(m_map.Hash());
	mix(static_cast<uint64_t>(m_units.size()));
	for (const auto& u : m_units) {
		mix(u.id);
		mix(u.playerId);
		mix(u.alive ? 1u : 0u);
		mix(u.moving ? 1u : 0u);
		mix(static_cast<uint64_t>(static_cast<uint32_t>(u.pos.x)));
		mix(static_cast<uint64_t>(static_cast<uint32_t>(u.pos.y)));
		mix(static_cast<uint64_t>(static_cast<uint32_t>(u.target.x)));
		mix(static_cast<uint64_t>(static_cast<uint32_t>(u.target.y)));
		mix(static_cast<uint64_t>(static_cast<uint32_t>(u.speed)));
	}
	return h;
}

} // namespace rts
