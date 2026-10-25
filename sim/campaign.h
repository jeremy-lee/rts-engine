// RTS Engine — sim/campaign.h
// 战役/关卡系统：关卡定义（目标/波次/时限）、波次生成、胜负判定。
#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include "core/core.h"
#include "datatable.h"

namespace rts {

class World;
class CommandQueue;

enum class GoalType : uint8_t {
	DestroyAll = 0, // 消灭所有敌军
	Survive = 1,    // 存活 N tick
	Collect = 2,    // 资源达到目标
};

struct WaveUnit {
	std::string type;   // 单位 id（rules.yaml 中的键）
	int32_t count = 1;
	CellCoord pos;
};

struct WaveDef {
	Tick tick = 0;
	std::vector<WaveUnit> units;
};

struct LevelDef {
	std::string id;
	std::string name;
	std::string description;
	GoalType goal = GoalType::DestroyAll;
	Tick timeLimit = 0;      // 0 = 无限制
	Tick surviveTicks = 0;   // Survive 目标时长
	int32_t collectTarget = 0;
	CellCoord playerStart{64, 64};
	std::vector<WaveDef> waves;
};

bool LoadLevel(const std::string& path, LevelDef& out);
bool LoadLevelFromText(const std::string& text, LevelDef& out, const std::string& id = "");

enum class CampaignState : uint8_t {
	Playing = 0,
	Victory = 1,
	Defeat = 2,
};

// 战役控制器：负责波次生成与胜负判定（每 tick 调用 Update）
class Campaign {
public:
	Campaign() = default;
	Campaign(LevelDef level, uint8_t playerId, uint8_t enemyPlayer);

	// 每 tick：生成到期波次 → 判定胜负
	CampaignState Update(World& world, CommandQueue& queue, Tick currentTick);

	CampaignState State() const { return m_state; }
	const LevelDef& Level() const { return m_level; }
	bool Finished() const { return m_state != CampaignState::Playing; }

private:
	void SpawnWave(World& world, CommandQueue& queue, const WaveDef& wave);

	LevelDef m_level;
	uint8_t m_playerId = 0;
	uint8_t m_enemyPlayer = 1;
	std::vector<uint8_t> m_waveSpawned;
	CampaignState m_state = CampaignState::Playing;
};

} // namespace rts
