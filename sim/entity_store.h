// RTS Engine — sim/entity_store.h
// 实体存储：连续数组（确定性遍历顺序）+ id→index 索引。
// 规则：遍历顺序即创建顺序，任何依赖遍历顺序的逻辑都必须走 All()。
#pragma once

#include <cstdint>
#include <vector>
#include <unordered_map>
#include "entity.h"

namespace rts {

class EntityStore {
public:
	// 创建实体，返回 id（从 1 开始递增）
	EntityId Create(uint8_t playerId, SubCoord pos, uint8_t components);

	Actor* Get(EntityId id) {
		const auto it = m_index.find(id);
		return (it == m_index.end()) ? nullptr : &m_actors[it->second];
	}
	const Actor* Get(EntityId id) const {
		const auto it = m_index.find(id);
		return (it == m_index.end()) ? nullptr : &m_actors[it->second];
	}

	// 顺序遍历（确定性）
	std::vector<Actor>& All() { return m_actors; }
	const std::vector<Actor>& All() const { return m_actors; }

	size_t Count() const { return m_actors.size(); }
	EntityId NextId() const { return m_nextId; }

	void Clear() {
		m_actors.clear();
		m_index.clear();
		m_nextId = 1;
	}

	uint64_t Hash() const {
		uint64_t h = 1469598103934665603ull;
		for (const auto& a : m_actors) {
			h = HashMix(h, HashActor(a));
		}
		h = HashMix(h, static_cast<uint64_t>(m_actors.size()));
		return h;
	}

private:
	std::vector<Actor> m_actors;
	std::unordered_map<EntityId, EntityIndex> m_index;
	EntityId m_nextId = 1;
};

inline EntityId EntityStore::Create(uint8_t playerId, SubCoord pos, uint8_t components) {
	Actor a;
	a.id = m_nextId++;
	a.playerId = playerId;
	a.pos = pos;
	a.components = components;
	a.activity.type = ActivityType::Idle;
	a.activity.started = 0;
	a.activity.targetEntity = kInvalidEntity;
	a.activity.targetPos = pos;
	a.movable.target = pos;
	a.movable.speed = 32;
	a.movable.moving = false;
	a.health.hp = 100;
	a.health.maxHp = 100;
	m_index[a.id] = static_cast<EntityIndex>(m_actors.size());
	m_actors.push_back(a);
	return a.id;
}

} // namespace rts
