// RTS Engine — sim/world.cpp
// World 实现（M0 最小模拟：生成/移动/停止 + 状态哈希）
#include "world.h"

namespace rts {

void World::ApplyCommand(const Command& cmd) {
	switch (cmd.type) {
		case CommandType::Spawn: {
			Unit u;
			u.id = m_nextUnitId++;
			u.playerId = cmd.playerId;
			u.pos = SubCoord(cmd.x, cmd.y);
			u.target = u.pos;
			u.speed = 32; // 32 子格/tick = 32/1024 格/tick ≈ 0.94 格/秒（30Hz 下）
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
		// 整数步进：每个 tick 最多走 speed 子格（沿直线，整数运算）
		const int64_t step = static_cast<int64_t>(u.speed) * u.speed;
		if (dist <= step) {
			u.pos = u.target;
			u.moving = false;
			continue;
		}
		// 归一化方向（整数近似）：dir = (dx, dy) / |d| * speed
		const int64_t d = static_cast<int64_t>(Isqrt(static_cast<uint64_t>(dist)));
		if (d == 0) { u.moving = false; continue; }
		const int64_t sx = (static_cast<int64_t>(dx) * u.speed) / d;
		const int64_t sy = (static_cast<int64_t>(dy) * u.speed) / d;
		// 防止因取整为零导致卡住
		u.pos.x += static_cast<int32_t>(sx != 0 ? sx : (dx > 0 ? 1 : -1));
		u.pos.y += static_cast<int32_t>(sy != 0 ? sy : (dy > 0 ? 1 : -1));
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
	mix(static_cast<uint64_t>(m_width));
	mix(static_cast<uint64_t>(m_height));
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
