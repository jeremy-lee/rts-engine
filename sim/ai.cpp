// RTS Engine — sim/ai.cpp
#include "ai.h"
#include "gamedefs.h"

namespace rts {

AIPlayer::AIPlayer(uint8_t playerId, uint64_t seed)
	: m_playerId(playerId), m_rng(seed) {}

bool AIPlayer::FindNearestResource(const World& world, SubCoord from, CellCoord& out) const {
	const Map& map = world.GetMap();
	int64_t bestDist = INT64_MAX;
	bool found = false;
	for (int32_t y = 0; y < map.Height(); ++y) {
		for (int32_t x = 0; x < map.Width(); ++x) {
			const CellCoord c(x, y);
			if (map.ResourceAmount(c) <= 0) continue;
			const int64_t d = DistanceSq(from, SubCoord::FromCell(c));
			if (d < bestDist) {
				bestDist = d;
				out = c;
				found = true;
			}
		}
	}
	return found;
}

EntityId AIPlayer::FindNearestEnemy(const World& world, SubCoord from) const {
	EntityId best = kInvalidEntity;
	int64_t bestDist = INT64_MAX;
	for (const auto& a : world.Entities().All()) {
		if (!a.alive || a.playerId == m_playerId) continue;
		const int64_t d = DistanceSq(from, a.pos);
		if (d < bestDist) {
			bestDist = d;
			best = a.id;
		}
	}
	return best;
}

EntityId AIPlayer::FindIdleBarracks(const World& world) const {
	for (const auto& a : world.Entities().All()) {
		if (!a.alive || a.playerId != m_playerId) continue;
		if (a.Has(kCompBuilding) && a.producer.queued == 0) return a.id;
	}
	return kInvalidEntity;
}

EntityId AIPlayer::FindMainBuilding(const World& world) const {
	for (const auto& a : world.Entities().All()) {
		if (!a.alive || a.playerId != m_playerId) continue;
		if (a.Has(kCompBuilding)) return a.id;
	}
	return kInvalidEntity;
}

void AIPlayer::Update(World& world, CommandQueue& queue) {
	const Tick now = world.CurrentTick();
	if (m_lastDecision >= 0 && now - m_lastDecision < kAIDecisionInterval) return;
	m_lastDecision = now;

	const Rules& rules = world.GetRules();
	if (rules.Empty()) return;

	// ---- 统计自己的状态 ----
	int harvesterCount = 0;
	int combatCount = 0;
	EntityId mainBuilding = kInvalidEntity;
	SubCoord basePos;
	bool haveBase = false;
	for (const auto& a : world.Entities().All()) {
		if (!a.alive || a.playerId != m_playerId) continue;
		if (a.Has(kCompHarvester)) ++harvesterCount;
		else if (a.Has(kCompBuilding)) {
			if (mainBuilding == kInvalidEntity) mainBuilding = a.id;
			if (!haveBase) { basePos = a.pos; haveBase = true; }
		} else if (a.Has(kCompArmament) && a.armament.damage > 0) {
			++combatCount;
		}
		if (!haveBase) { basePos = a.pos; haveBase = true; }
	}

	// 兵营（本引擎的建筑都可生产；mainBuilding 即生产建筑）
	EntityId producer = FindIdleBarracks(world);

	// ---- 1. 建造：没有建筑且资源足够 ----
	if (mainBuilding == kInvalidEntity) {
		if (world.Credits(m_playerId) >= 300) {
			// M11：建筑占格后需要找可建造区域
			const BuildingDef* bdef = world.GetRules().FindBuilding(world.GetRules().buildingIds.empty()
				? "" : world.GetRules().buildingIds[0]);
			const int32_t fw = (bdef != nullptr && bdef->width > 0) ? bdef->width : 4;
			const int32_t fh = (bdef != nullptr && bdef->height > 0) ? bdef->height : 4;

			CellCoord baseCell = haveBase ? basePos.ToCell() : CellCoord(10, 10);
			CellCoord origin;
			bool found = false;
			// 从基地附近螺旋扫描可建造区域
			for (int32_t r = 0; r <= 24 && !found; ++r) {
				for (int32_t dy = -r; dy <= r && !found; ++dy) {
					for (int32_t dx = -r; dx <= r && !found; ++dx) {
						if (r > 0 && (dx > -r && dx < r) && (dy > -r && dy < r)) continue;
						const CellCoord cand(baseCell.x + dx + 2, baseCell.y + dy + 2);
						if (world.GetMap().CanBuildAt(cand, fw, fh)) {
							origin = cand;
							found = true;
						}
					}
				}
			}
			if (found) {
				Command b;
				b.type = CommandType::Build;
				b.playerId = m_playerId;
				b.x = origin.x * kSubPerCell;
				b.y = origin.y * kSubPerCell;
				b.targetId = 0;
				queue.Push(b);
			}
		}
		return;
	}

	// ---- 2. 经济：至少 1 个采矿车 ----
	if (harvesterCount == 0) {
		if (producer != kInvalidEntity) {
			const int32_t harvesterNum = 0; // 约定：0 = harvester
			const UnitDef* def = rules.FindUnitByNumeric(harvesterNum);
			if (def != nullptr && def->isHarvester && world.Credits(m_playerId) >= def->cost) {
				Command p;
				p.type = CommandType::Produce;
				p.entityId = producer;
				p.targetId = static_cast<uint32_t>(harvesterNum);
				queue.Push(p);
			}
		}
		return;
	}

	// ---- 3. 采矿：给每台采矿车分配最近矿点 ----
	{
		std::vector<EntityId> harvesters;
		for (const auto& a : world.Entities().All()) {
			if (a.alive && a.playerId == m_playerId && a.Has(kCompHarvester)) {
				harvesters.push_back(a.id);
			}
		}
		for (EntityId hid : harvesters) {
			const Actor* h = world.Entities().Get(hid);
			if (h == nullptr) continue;
			if (h->activity.type == ActivityType::Harvest) continue; // 已在采矿
			CellCoord mine;
			if (FindNearestResource(world, h->pos, mine)) {
				Command hv;
				hv.type = CommandType::Harvest;
				hv.entityId = hid;
				hv.x = mine.x * kSubPerCell;
				hv.y = mine.y * kSubPerCell;
				queue.Push(hv);
			}
		}
	}

	// ---- 4. 生产：兵营空闲时补充兵力 ----
	if (producer != kInvalidEntity && combatCount < 8) {
		const int32_t unitNum = (m_rng.Range(3) == 0) ? 2 : 1; // 1=infantry, 2=tank
		const UnitDef* def = rules.FindUnitByNumeric(unitNum);
		if (def != nullptr && world.Credits(m_playerId) >= def->cost) {
			Command p;
			p.type = CommandType::Produce;
			p.entityId = producer;
			p.targetId = static_cast<uint32_t>(unitNum);
			queue.Push(p);
		}
	}

	// ---- 5. 集结进攻（OpenRA SquadManagerBotModule 风格）----
	//    兵力 >= 4：先所有战斗单位向集结点靠拢，集结完成后再整队推进
	if (combatCount >= 4) {
		const EntityId enemy = FindNearestEnemy(world, basePos);
		if (m_phase == AIPhase::Gather) {
			// 集结点固定（只在进入 Gather 时算一次，避免目标漂移）
			if (!m_rallySet) {
				CellCoord rally = basePos.ToCell();
				if (enemy != kInvalidEntity) {
					const Actor* e = world.Entities().Get(enemy);
					if (e != nullptr) {
						const CellCoord ec = e->pos.ToCell();
						const CellCoord bc = basePos.ToCell();
						const int32_t ddx = ec.x - bc.x;
						const int32_t ddy = ec.y - bc.y;
						const int32_t adx = (ddx < 0) ? -ddx : ddx;
						const int32_t ady = (ddy < 0) ? -ddy : ddy;
						const int32_t len = (adx > ady) ? (adx > 1 ? adx : 1) : (ady > 1 ? ady : 1);
						rally.x = bc.x + ddx * 8 / len;
						rally.y = bc.y + ddy * 8 / len;
					}
				}
				m_rally = Map::FindNearestWalkable(world.GetMap(), rally);
				m_rallySet = true;
				m_gatherTick = now;
			}
			// 集结：所有战斗单位一次性向集结点移动（不重发，避免抖动）
			if (now - m_gatherTick < 120) {
				for (const auto& a : world.Entities().All()) {
					if (!a.alive || a.playerId != m_playerId) continue;
					if (a.Has(kCompBuilding) || a.Has(kCompHarvester)) continue;
					if (!a.Has(kCompArmament) || a.armament.damage <= 0) continue;
					const CellCoord ac = a.pos.ToCell();
					const int32_t ddx = ac.x - m_rally.x;
					const int32_t ddy = ac.y - m_rally.y;
					if (ddx * ddx + ddy * ddy > 9) {
						Command mv;
						mv.type = CommandType::Move;
						mv.entityId = a.id;
						mv.x = m_rally.x * kSubPerCell;
						mv.y = m_rally.y * kSubPerCell;
						queue.Push(mv);
					}
				}
			}
			// 集结 4 秒（120 tick）后整队推进
			if (now - m_gatherTick >= 120) {
				m_phase = AIPhase::Attack;
				m_rallySet = false;
			}
		} else {
			// 推进：整队攻击最近敌人
			if (enemy != kInvalidEntity && now - m_lastAttack >= kAIAttackInterval) {
				m_lastAttack = now;
				for (const auto& a : world.Entities().All()) {
					if (!a.alive || a.playerId != m_playerId) continue;
					if (a.Has(kCompBuilding) || a.Has(kCompHarvester)) continue;
					if (!a.Has(kCompArmament) || a.armament.damage <= 0) continue;
					Command atk;
					atk.type = CommandType::Attack;
					atk.entityId = a.id;
					atk.targetId = enemy;
					queue.Push(atk);
				}
			}
			// 兵力不足 → 回到集结
			if (combatCount < 2) m_phase = AIPhase::Gather;
		}
	} else {
		m_phase = AIPhase::Gather; // 兵力不足时回到集结状态
	}
}

} // namespace rts
