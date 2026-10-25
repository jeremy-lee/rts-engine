// RTS Engine — sim/world.cpp
#include "world.h"
#include <sstream>
#include "activity.h"
#include "fog.h"

namespace rts {

World::World(int32_t width, int32_t height, uint64_t seed)
	: m_map(width, height, 0, kWalkable | kBuildable), m_rng(seed) {
	m_pathfinder.Resize(width, height);
}

World::World(Map map, uint64_t seed)
	: m_map(std::move(map)), m_rng(seed) {
	m_pathfinder.Resize(m_map.Width(), m_map.Height());
}

World::World(Map map, uint64_t seed, Rules rules)
	: m_map(std::move(map)), m_rules(std::move(rules)), m_rng(seed) {
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

bool World::SpendCredits(int player, int32_t amount) {
	if (player < 0 || player >= 2) return false;
	if (amount < 0) return false;
	if (m_credits[player] < amount) return false;
	m_credits[player] -= amount;
	return true;
}

void World::AddCredits(int player, int32_t amount) {
	if (player < 0 || player >= 2) return;
	m_credits[player] += amount;
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
			const EntityId id = m_entities.Create(cmd.playerId, spawnPos, components);
			// M5：应用数据定义（targetId 存 unit numericId）
			if (const UnitDef* def = m_rules.FindUnitByNumeric(static_cast<int32_t>(cmd.targetId))) {
				Actor* a = m_entities.Get(id);
				if (a != nullptr) {
					a->health.hp = def->hp;
					a->health.maxHp = def->hp;
					a->movable.speed = def->speed;
					a->armament.damage = def->weapon.damage;
					a->armament.range = def->weapon.range;
					a->armament.cooldown = def->weapon.cooldown;
					if (def->isHarvester) {
						a->components |= kCompHarvester;
						a->harvester.basePos = spawnPos;
						a->harvester.mine = spawnCell;
						a->harvester.cargo = 0;
						a->harvester.capacity = 100;
						a->harvester.perTick = 10;
						a->harvester.phase = HarvestPhase::ToMine;
					}
				}
			}
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
		case CommandType::Harvest: {
			Actor* a = m_entities.Get(cmd.entityId);
			if (a == nullptr || !a->Has(kCompHarvester)) break;
			a->harvester.mine = SubCoord(cmd.x, cmd.y).ToCell();
			a->harvester.phase = HarvestPhase::ToMine;
			m_paths.erase(cmd.entityId);
			activity::SetMoveTo(*a, SubCoord::FromCell(a->harvester.mine) + SubCoord(kSubPerCell / 2, kSubPerCell / 2), m_tick);
			a->activity.type = ActivityType::Harvest;
			break;
		}
		case CommandType::Build: {
			const BuildingDef* def = m_rules.FindBuilding("barracks");
			// targetId 为 building numericId
			if (cmd.targetId < m_rules.buildingIds.size()) {
				def = m_rules.FindBuilding(m_rules.buildingIds[cmd.targetId]);
			}
			if (def == nullptr) break;
			// M11：检查占地（宽高来自数据定义）
			const int32_t fw = (def->width > 0) ? def->width : 1;
			const int32_t fh = (def->height > 0) ? def->height : 1;
			CellCoord origin = SubCoord(cmd.x, cmd.y).ToCell();
			// 吸附到最近可建区域（简化：尝试原位置，失败则放弃）
			if (!m_map.CanBuildAt(origin, fw, fh)) break;
			if (!SpendCredits(cmd.playerId, def->cost)) break;

			// 建筑中心 = 占格区域中心
			const SubCoord pos(
				(origin.x + fw / 2) * kSubPerCell + kSubPerCell / 2,
				(origin.y + fh / 2) * kSubPerCell + kSubPerCell / 2);
			const EntityId id = m_entities.Create(cmd.playerId, pos, kCompHealth | kCompBuilding);
			if (Actor* a = m_entities.Get(id)) {
				a->health.hp = def->hp;
				a->health.maxHp = def->hp;
				a->footprintW = static_cast<uint8_t>(fw);
				a->footprintH = static_cast<uint8_t>(fh);
				// M19：防御塔（建筑 + 武器 + 视野）
				if (def->turret && def->weapon.damage > 0) {
					a->components |= kCompArmament | kCompVision;
					a->armament.range = def->weapon.range;
					a->armament.damage = def->weapon.damage;
					a->armament.cooldown = def->weapon.cooldown;
					a->armament.lastFire = 0;
					a->vision.range = def->weapon.range;
				}
			}
			m_map.SetFootprint(origin, fw, fh, id);
			break;
		}
		case CommandType::Upgrade: {
			// M19：科技升级（攻/防/速，扣资源）
			const UpgradeDef& def = m_rules.upgrade;
			if (!SpendCredits(cmd.playerId, def.cost)) break;
			UpgradeState& up = m_upgrades[cmd.playerId];
			up.attackBonus += def.attack;
			up.hpBonus += def.hp;
			up.speedBonus += def.speed;
			// 应用到现有单位
			for (auto& a : m_entities.All()) {
				if (!a.alive || a.playerId != cmd.playerId) continue;
				if (a.Has(kCompArmament)) a.armament.damage += def.attack;
				if (a.Has(kCompHealth)) {
					a.health.maxHp += def.hp;
					a.health.hp += def.hp;
				}
				if (a.Has(kCompMovable)) a.movable.speed += def.speed;
			}
			break;
		}

		case CommandType::Produce: {
			Actor* a = m_entities.Get(cmd.entityId);
			if (a == nullptr || !a->Has(kCompBuilding)) break;
			const UnitDef* def = m_rules.FindUnitByNumeric(static_cast<int32_t>(cmd.targetId));
			if (def == nullptr) break;
			if (!SpendCredits(a->playerId, def->cost)) break;
			a->producer.unitNumericId = static_cast<int32_t>(cmd.targetId);
			a->producer.total = def->buildTime;
			a->producer.remaining = def->buildTime;
			a->producer.queued = 1;
			break;
		}
		default:
			break;
	}
}

void World::TickEconomy() {
	std::vector<EntityId> pendingSpawns;

	for (auto& a : m_entities.All()) {
		if (!a.alive) continue;

		// ---- 采矿者 ----
		if (a.Has(kCompHarvester)) {
			switch (a.harvester.phase) {
				case HarvestPhase::ToMine:
					if (a.pos.ToCell() == a.harvester.mine) {
						a.harvester.phase = HarvestPhase::Mining;
					}
					break;
				case HarvestPhase::Mining: {
					const int32_t taken = m_map.TakeResource(a.harvester.mine, a.harvester.perTick);
					if (taken > 0) a.harvester.cargo += taken;
					if (a.harvester.cargo >= a.harvester.capacity || taken == 0) {
						a.harvester.phase = HarvestPhase::ToBase;
						activity::SetMoveTo(a, a.harvester.basePos, m_tick);
						a.activity.type = ActivityType::Harvest;
					}
					break;
				}
				case HarvestPhase::ToBase:
					if (a.pos.ToCell() == a.harvester.basePos.ToCell()) {
						AddCredits(a.playerId, a.harvester.cargo);
						a.harvester.cargo = 0;
						a.harvester.phase = HarvestPhase::ToMine;
						activity::SetMoveTo(a, SubCoord::FromCell(a.harvester.mine) + SubCoord(kSubPerCell / 2, kSubPerCell / 2), m_tick);
						a.activity.type = ActivityType::Harvest;
					}
					break;
			}
		}

		// ---- 建筑生产 ----
		if (a.Has(kCompBuilding) && a.producer.queued > 0) {
			if (a.producer.remaining > 0) --a.producer.remaining;
			if (a.producer.remaining <= 0) {
				pendingSpawns.push_back(a.id);
			}
		}
	}

	// 循环外生成单位（避免 vector 扩容使指针失效：先取数据，Create 后重新 Get）
	for (EntityId buildingId : pendingSpawns) {
		Actor* b = m_entities.Get(buildingId);
		if (b == nullptr) continue;
		const int32_t unitNumericId = b->producer.unitNumericId;
		const uint8_t playerId = b->playerId;
		const CellCoord base = b->pos.ToCell();
		const SubCoord buildingPos = b->pos;

		const UnitDef* def = m_rules.FindUnitByNumeric(unitNumericId);
		if (def != nullptr) {
			const CellCoord spawnCell = Map::FindNearestWalkable(m_map, CellCoord(base.x + 1, base.y));
			const SubCoord pos(spawnCell.x * kSubPerCell + kSubPerCell / 2,
				spawnCell.y * kSubPerCell + kSubPerCell / 2);
			const EntityId id = m_entities.Create(playerId, pos,
				kCompMovable | kCompHealth | kCompArmament);
			if (Actor* u = m_entities.Get(id)) {
				u->health.hp = def->hp;
				u->health.maxHp = def->hp;
				u->movable.speed = def->speed;
				u->armament.damage = def->weapon.damage;
				u->armament.range = def->weapon.range;
				u->armament.cooldown = def->weapon.cooldown;
				if (def->isHarvester) {
					u->components |= kCompHarvester;
					u->harvester.basePos = buildingPos;
					u->harvester.mine = spawnCell;
					u->harvester.capacity = 100;
					u->harvester.perTick = 10;
					u->harvester.phase = HarvestPhase::ToMine;
				}
			}
		}
		// 重新获取（Create 可能使旧指针失效）
		if (Actor* b2 = m_entities.Get(buildingId)) {
			b2->producer.queued = 0;
			b2->producer.remaining = 0;
			b2->producer.unitNumericId = -1;
		}
	}
}

void World::TickActors() {
	for (auto& a : m_entities.All()) {
		if (!a.alive) {
			// M11：死亡建筑释放占格（footprintW=0 表示已释放）
			if (a.Has(kCompBuilding) && a.footprintW > 0) {
				m_map.ClearFootprint(a.id);
				a.footprintW = 0;
				a.footprintH = 0;
			}
			continue;
		}
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
	ClearAttackFx();
	TickActors();
	TickEconomy();
	ApplySeparation();
	fog::UpdateVisibility(*this, 0); // M12：玩家 0 视野
	++m_tick;
}

uint64_t World::StateHash() const {
	uint64_t h = 1469598103934665603ull;
	h = HashMix(h, static_cast<uint64_t>(m_tick));
	h = HashMix(h, m_map.Hash());
	h = HashMix(h, m_entities.Hash());
	h = HashMix(h, m_rng.StateHash());
	h = HashMix(h, static_cast<uint64_t>(static_cast<uint32_t>(m_credits[0])));
	h = HashMix(h, static_cast<uint64_t>(static_cast<uint32_t>(m_credits[1])));
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

// ============================================================
// 存档 / 读档（M21）
// ============================================================

std::string World::SaveToText() const {
	std::stringstream ss;
	ss << "RTSAVE 1\n";
	ss << m_tick << "\n";
	ss << m_credits[0] << " " << m_credits[1] << "\n";
	ss << m_map.Width() << " " << m_map.Height() << "\n";
	for (int32_t y = 0; y < m_map.Height(); ++y) {
		for (int32_t x = 0; x < m_map.Width(); ++x) {
			const CellCoord c(x, y);
			ss << static_cast<int>(m_map.TerrainHeight(c)) << " "
				<< static_cast<int>(m_map.Flags(c)) << " "
				<< static_cast<int>(m_map.Fog(c)) << " "
				<< m_map.ResourceAmount(c) << " "
				<< m_map.Occupancy(c) << "\n";
		}
	}
	ss << m_entities.Count() << "\n";
	for (const auto& a : m_entities.All()) {
		ss << a.id << " " << static_cast<int>(a.playerId) << " "
			<< a.pos.x << " " << a.pos.y << " "
			<< static_cast<int>(a.components) << " "
			<< static_cast<int>(a.footprintW) << " " << static_cast<int>(a.footprintH) << " "
			<< a.health.hp << " " << a.health.maxHp << " "
			<< a.movable.speed << " " << static_cast<int>(a.movable.facing) << " "
			<< (a.movable.moving ? 1 : 0) << " "
			<< a.armament.range << " " << a.armament.damage << " "
			<< a.armament.cooldown << " " << a.armament.lastFire << " "
			<< a.vision.range << " "
			<< static_cast<int>(a.activity.type) << " "
			<< a.activity.targetPos.x << " " << a.activity.targetPos.y << " "
			<< a.activity.targetEntity << " " << a.activity.started << " "
			<< (a.alive ? 1 : 0) << " "
			<< (a.producer.queued > 0 ? 1 : 0) << " "
			<< a.producer.remaining << " " << a.producer.total << "\n";
	}
	return ss.str();
}

bool World::LoadFromText(const std::string& text) {
	std::stringstream ss(text);
	std::string tag;
	int version = 0;
	ss >> tag >> version;
	if (tag != "RTSAVE" || version != 1) return false;

	int64_t tick = 0;
	ss >> tick;
	m_tick = static_cast<Tick>(tick);
	int32_t c0 = 0, c1 = 0;
	ss >> c0 >> c1;
	m_credits[0] = c0;
	m_credits[1] = c1;

	int32_t w = 0, h = 0;
	ss >> w >> h;
	if (w <= 0 || h <= 0) return false;
	m_map = Map(w, h, 0, kWalkable | kBuildable);
	for (int32_t y = 0; y < h; ++y) {
		for (int32_t x = 0; x < w; ++x) {
			const CellCoord c(x, y);
			int terrain = 0, flags = 0, fog = 0, res = 0;
			uint32_t occ = 0;
			ss >> terrain >> flags >> fog >> res >> occ;
			m_map.SetTerrainHeight(c, static_cast<uint8_t>(terrain));
			m_map.SetFlags(c, static_cast<uint8_t>(flags));
			m_map.SetFog(c, static_cast<uint8_t>(fog));
			m_map.SetResourceAmount(c, res);
			m_map.SetOccupancy(c, occ);
		}
	}

	m_entities.Clear();
	m_paths.clear();
	size_t count = 0;
	ss >> count;
	for (size_t i = 0; i < count; ++i) {
		EntityId id = 0;
		int player = 0;
		int32_t px = 0, py = 0;
		int comps = 0, fw = 1, fh = 1;
		int32_t hp = 0, maxHp = 0;
		int32_t speed = 0;
		int facing = 0, moving = 0;
		int32_t arange = 0, adamage = 0, acooldown = 0, alastFire = 0;
		int32_t vrange = 0;
		int activityType = 0;
		int32_t tx = 0, ty = 0;
		EntityId targetEntity = 0;
		Tick started = 0;
		int alive = 0;
		int producing = 0;
		int32_t remaining = 0, total = 0;
		ss >> id >> player >> px >> py >> comps >> fw >> fh
			>> hp >> maxHp >> speed >> facing >> moving
			>> arange >> adamage >> acooldown >> alastFire
			>> vrange >> activityType >> tx >> ty >> targetEntity >> started >> alive
			>> producing >> remaining >> total;
		if (!ss) return false;

		const EntityId newId = m_entities.Create(static_cast<uint8_t>(player),
			SubCoord(px, py), static_cast<uint8_t>(comps));
		Actor* a = m_entities.Get(newId);
		if (a == nullptr) continue;
		a->id = id;
		a->footprintW = static_cast<uint8_t>(fw);
		a->footprintH = static_cast<uint8_t>(fh);
		a->health.hp = hp;
		a->health.maxHp = maxHp;
		a->movable.speed = speed;
		a->movable.facing = static_cast<uint8_t>(facing);
		a->movable.moving = (moving != 0);
		a->armament.range = arange;
		a->armament.damage = adamage;
		a->armament.cooldown = static_cast<Tick>(acooldown);
		a->armament.lastFire = static_cast<Tick>(alastFire);
		a->vision.range = vrange;
		a->activity.type = static_cast<ActivityType>(activityType);
		a->activity.targetPos = SubCoord(tx, ty);
		a->activity.targetEntity = targetEntity;
		a->activity.started = started;
		a->alive = (alive != 0);
		a->producer.queued = (producing != 0) ? 1 : 0;
		a->producer.remaining = remaining;
		a->producer.total = total;
	}
	return true;
}

} // namespace rts
