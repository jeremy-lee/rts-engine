// RTS Engine — sim/world.cpp
#include "world.h"
#include "activity.h"

namespace rts {

World::World(int32_t width, int32_t height, uint64_t seed)
	: m_map(width, height, 0, kWalkable | kBuildable), m_rng(seed) {}

World::World(Map map, uint64_t seed)
	: m_map(std::move(map)), m_rng(seed) {}

void World::ApplyCommand(const Command& cmd) {
	switch (cmd.type) {
		case CommandType::Spawn: {
			const uint8_t components =
				kCompMovable | kCompHealth | kCompArmament | kCompVision;
			m_entities.Create(cmd.playerId, SubCoord(cmd.x, cmd.y), components);
			break;
		}
		case CommandType::Move: {
			if (Actor* a = m_entities.Get(cmd.entityId)) {
				activity::SetMoveTo(*a, SubCoord(cmd.x, cmd.y), m_tick);
			}
			break;
		}
		case CommandType::Attack: {
			if (Actor* a = m_entities.Get(cmd.entityId)) {
				activity::SetAttack(*a, cmd.targetId, m_tick);
			}
			break;
		}
		case CommandType::Stop: {
			if (Actor* a = m_entities.Get(cmd.entityId)) {
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

void World::Advance(CommandQueue& queue) {
	std::vector<Command> cmds;
	queue.Consume(cmds);
	for (const auto& c : cmds) {
		ApplyCommand(c);
	}
	TickActors();
	++m_tick;
}

uint64_t World::StateHash() const {
	uint64_t h = 1469598103934665603ull;
	h = HashMix(h, static_cast<uint64_t>(m_tick));
	h = HashMix(h, m_map.Hash());
	h = HashMix(h, m_entities.Hash());
	h = HashMix(h, m_rng.StateHash());
	return h;
}

} // namespace rts
