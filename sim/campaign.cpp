// RTS Engine — sim/campaign.cpp
#include "campaign.h"
#include "command.h"
#include "command_queue.h"
#include "gamedefs.h"
#include "world.h"

#include <fstream>
#include <sstream>

namespace rts {

namespace {

GoalType ParseGoal(const std::string& s) {
	if (s == "survive") return GoalType::Survive;
	if (s == "collect") return GoalType::Collect;
	return GoalType::DestroyAll;
}

} // namespace

bool LoadLevelFromText(const std::string& text, LevelDef& out, const std::string& id) {
	const DataNode root = DataNode::Parse(text);
	if (root.IsNull()) return false;

	LevelDef level;
	level.id = id;
	level.name = root.GetString("name", "Untitled");
	level.description = root.GetString("description", "");
	level.goal = ParseGoal(root.GetString("goal", "destroy_all"));
	level.timeLimit = root.GetInt("timeLimit", 0);
	level.surviveTicks = root.GetInt("surviveTicks", 0);
	level.collectTarget = root.GetInt("collectTarget", 0);

	if (const DataNode* start = root.GetNode("playerStart"); start && start->IsList() && start->list.size() >= 2) {
		level.playerStart = CellCoord(start->list[0].AsInt(64), start->list[1].AsInt(64));
	}

	if (const DataNode* waves = root.GetNode("waves"); waves && waves->IsList()) {
		for (const auto& w : waves->list) {
			if (!w.IsMap()) continue;
			WaveDef wave;
			wave.tick = w.GetInt("tick", 0);
			if (const DataNode* units = w.GetNode("units"); units && units->IsList()) {
				for (const auto& u : units->list) {
					if (!u.IsMap()) continue;
					WaveUnit wu;
					wu.type = u.GetString("type", "infantry");
					wu.count = u.GetInt("count", 1);
					if (const DataNode* pos = u.GetNode("pos"); pos && pos->IsList() && pos->list.size() >= 2) {
						wu.pos = CellCoord(pos->list[0].AsInt(64), pos->list[1].AsInt(64));
					} else {
						wu.pos = level.playerStart;
					}
					wave.units.push_back(wu);
				}
			}
			level.waves.push_back(std::move(wave));
		}
	}

	out = std::move(level);
	return true;
}

bool LoadLevel(const std::string& path, LevelDef& out) {
	std::ifstream in(path);
	if (!in) return false;
	std::stringstream ss;
	ss << in.rdbuf();
	return LoadLevelFromText(ss.str(), out, path);
}

// ============================================================

Campaign::Campaign(LevelDef level, uint8_t playerId, uint8_t enemyPlayer)
	: m_level(std::move(level)), m_playerId(playerId), m_enemyPlayer(enemyPlayer) {
	m_waveSpawned.assign(m_level.waves.size(), 0);
}

void Campaign::SpawnWave(World& world, CommandQueue& queue, const WaveDef& wave) {
	const Rules& rules = world.GetRules();
	for (const auto& wu : wave.units) {
		const UnitDef* def = rules.FindUnit(wu.type);
		if (def == nullptr) continue;
		for (int32_t i = 0; i < wu.count; ++i) {
			Command c;
			c.type = CommandType::Spawn;
			c.playerId = m_enemyPlayer;
			c.x = wu.pos.x * kSubPerCell + (i % 3) * 256;
			c.y = wu.pos.y * kSubPerCell + (i / 3) * 256;
			c.targetId = static_cast<uint32_t>(def->numericId);
			queue.Push(c);
		}
	}
}

CampaignState Campaign::Update(World& world, CommandQueue& queue, Tick currentTick) {
	if (m_state != CampaignState::Playing) return m_state;

	// 1) 生成到期波次
	for (size_t i = 0; i < m_level.waves.size(); ++i) {
		if (m_waveSpawned[i] != 0) continue;
		const WaveDef& wave = m_level.waves[i];
		if (currentTick >= wave.tick) {
			SpawnWave(world, queue, wave);
			m_waveSpawned[i] = 1;
		}
	}

	// 2) 胜负判定
	bool playerAlive = false;
	int32_t enemyAlive = 0;
	for (const auto& a : world.Entities().All()) {
		if (!a.alive) continue;
		if (a.playerId == m_playerId) playerAlive = true;
		else ++enemyAlive;
	}
	// 还有未生成的波次 → 敌军尚未全部到场，不能判"全灭"
	bool pendingWaves = false;
	for (size_t i = 0; i < m_level.waves.size(); ++i) {
		if (m_waveSpawned[i] == 0) { pendingWaves = true; break; }
	}

	if (!playerAlive) {
		m_state = CampaignState::Defeat;
		return m_state;
	}
	if (m_level.timeLimit > 0 && currentTick >= m_level.timeLimit) {
		m_state = CampaignState::Defeat;
		return m_state;
	}

	switch (m_level.goal) {
		case GoalType::DestroyAll:
			if (enemyAlive == 0 && !pendingWaves) m_state = CampaignState::Victory;
			break;
		case GoalType::Survive:
			if (m_level.surviveTicks > 0 && currentTick >= m_level.surviveTicks) {
				m_state = CampaignState::Victory;
			}
			break;
		case GoalType::Collect:
			if (m_level.collectTarget > 0 && world.Credits(m_playerId) >= m_level.collectTarget) {
				m_state = CampaignState::Victory;
			}
			break;
	}
	return m_state;
}

} // namespace rts
