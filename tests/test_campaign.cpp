// RTS Engine — tests/test_campaign.cpp
// M15 验收：关卡加载、波次生成、胜负判定
#include "test_framework.h"
#include "sim/campaign.h"
#include "sim/simulation.h"
#include "sim/gamedefs.h"
#include "sim/command_queue.h"

using namespace rts;

namespace {

Rules CampaignRules() {
	return Rules::FromYAML(R"(
units:
  infantry:
    name: "步兵"
    cost: 50
    buildTime: 10
    hp: 100
    speed: 32
    weapon:
      damage: 10
      range: 4096
      cooldown: 15
  tank:
    name: "坦克"
    cost: 150
    buildTime: 30
    hp: 300
    speed: 20
    weapon:
      damage: 25
      range: 5120
      cooldown: 30
buildings:
  barracks:
    name: "兵营"
    cost: 300
    buildTime: 20
    hp: 500
    width: 4
    height: 4
    produces:
      - infantry
      - tank
)");
}

const char* kLevelYaml = R"(
name: "测试关卡"
description: "测试用"
goal: destroy_all
playerStart: [10, 10]
waves:
  - tick: 0
    units:
      - type: infantry
        count: 2
        pos: [30, 30]
  - tick: 100
    units:
      - type: tank
        count: 1
        pos: [35, 35]
)";

} // namespace

TEST(campaign_parse_level) {
	LevelDef level;
	CHECK(LoadLevelFromText(kLevelYaml, level, "test"));
	CHECK_EQ(level.name, std::string("测试关卡"));
	CHECK(level.goal == GoalType::DestroyAll);
	CHECK_EQ(level.waves.size(), static_cast<size_t>(2));
	CHECK_EQ(level.waves[0].tick, static_cast<Tick>(0));
	CHECK_EQ(level.waves[0].units.size(), static_cast<size_t>(1));
	CHECK_EQ(level.waves[0].units[0].type, std::string("infantry"));
	CHECK_EQ(level.waves[0].units[0].count, 2);
	CHECK_EQ(level.waves[0].units[0].pos.x, 30);
}

TEST(campaign_wave_spawns) {
	Map map(64, 64, 0, kWalkable | kBuildable);
	World world(map, 1, CampaignRules());
	LevelDef level;
	CHECK(LoadLevelFromText(kLevelYaml, level, "test"));
	Campaign campaign(level, 0, 1);

	CommandQueue queue;
	// 玩家单位（避免"玩家全灭"判定）
	world.Entities().Create(0, SubCoord(10 * kSubPerCell, 10 * kSubPerCell), kCompMovable);

	// 辅助：执行命令（模拟真实 tick 循环）
	auto exec = [&](const std::vector<Command>& cmds) {
		for (const auto& c : cmds) world.ApplyCommand(c);
	};

	// tick 0：第一波生成并执行
	campaign.Update(world, queue, 0);
	std::vector<Command> cmds;
	queue.Consume(cmds);
	CHECK_EQ(cmds.size(), static_cast<size_t>(2));
	for (const auto& c : cmds) {
		CHECK(c.type == CommandType::Spawn);
		CHECK_EQ(c.playerId, 1);
	}
	exec(cmds);

	// tick 50：无新波次
	campaign.Update(world, queue, 50);
	queue.Consume(cmds);
	CHECK_EQ(cmds.size(), static_cast<size_t>(0));

	// tick 100：第二波
	campaign.Update(world, queue, 100);
	queue.Consume(cmds);
	CHECK_EQ(cmds.size(), static_cast<size_t>(1));
}

TEST(campaign_victory_destroy_all) {
	Map map(64, 64, 0, kWalkable | kBuildable);
	World world(map, 1, CampaignRules());
	// 无波次关卡：玩家有单位且无敌军 → 立即胜利
	const char* yaml = R"(
name: "无波次"
goal: destroy_all
playerStart: [10, 10]
)";
	LevelDef level;
	CHECK(LoadLevelFromText(yaml, level, "nowave"));
	Campaign campaign(level, 0, 1);

	world.Entities().Create(0, SubCoord(10 * kSubPerCell, 10 * kSubPerCell), kCompMovable);

	CommandQueue queue;
	const CampaignState st = campaign.Update(world, queue, 0);
	CHECK(st == CampaignState::Victory);
	CHECK(campaign.Finished());
}

TEST(campaign_defeat_player_dead) {
	Map map(64, 64, 0, kWalkable | kBuildable);
	World world(map, 1, CampaignRules());
	LevelDef level;
	CHECK(LoadLevelFromText(kLevelYaml, level, "test"));
	Campaign campaign(level, 0, 1);

	// 只有敌军，玩家无单位 → 失败
	world.Entities().Create(1, SubCoord(30 * kSubPerCell, 30 * kSubPerCell), kCompMovable);

	CommandQueue queue;
	const CampaignState st = campaign.Update(world, queue, 0);
	CHECK(st == CampaignState::Defeat);
}

TEST(campaign_victory_survive) {
	Map map(64, 64, 0, kWalkable | kBuildable);
	World world(map, 1, CampaignRules());
	const char* yaml = R"(
name: "生存"
goal: survive
surviveTicks: 100
playerStart: [10, 10]
waves:
  - tick: 0
    units:
      - type: infantry
        count: 1
        pos: [30, 30]
)";
	LevelDef level;
	CHECK(LoadLevelFromText(yaml, level, "survive"));
	Campaign campaign(level, 0, 1);
	world.Entities().Create(0, SubCoord(10 * kSubPerCell, 10 * kSubPerCell), kCompMovable);
	world.Entities().Create(1, SubCoord(30 * kSubPerCell, 30 * kSubPerCell), kCompMovable);

	CommandQueue queue;
	CHECK(campaign.Update(world, queue, 50) == CampaignState::Playing);
	CHECK(campaign.Update(world, queue, 100) == CampaignState::Victory);
}

TEST(campaign_victory_collect) {
	Map map(64, 64, 0, kWalkable | kBuildable);
	World world(map, 1, CampaignRules());
	const char* yaml = R"(
name: "采集"
goal: collect
collectTarget: 500
playerStart: [10, 10]
)";
	LevelDef level;
	CHECK(LoadLevelFromText(yaml, level, "collect"));
	Campaign campaign(level, 0, 1);
	world.Entities().Create(0, SubCoord(10 * kSubPerCell, 10 * kSubPerCell), kCompMovable);

	CommandQueue queue;
	world.AddCredits(0, 100); // 初始 1000 + 100 = 1100？实际初始 1000，加 100 = 1100 > 500
	CHECK(campaign.Update(world, queue, 0) == CampaignState::Victory);
}

TEST(campaign_time_limit_defeat) {
	Map map(64, 64, 0, kWalkable | kBuildable);
	World world(map, 1, CampaignRules());
	const char* yaml = R"(
name: "限时"
goal: destroy_all
timeLimit: 50
playerStart: [10, 10]
waves:
  - tick: 0
    units:
      - type: infantry
        count: 1
        pos: [30, 30]
)";
	LevelDef level;
	CHECK(LoadLevelFromText(yaml, level, "timed"));
	Campaign campaign(level, 0, 1);
	world.Entities().Create(0, SubCoord(10 * kSubPerCell, 10 * kSubPerCell), kCompMovable);
	world.Entities().Create(1, SubCoord(30 * kSubPerCell, 30 * kSubPerCell), kCompMovable);

	CommandQueue queue;
	CHECK(campaign.Update(world, queue, 49) == CampaignState::Playing);
	CHECK(campaign.Update(world, queue, 50) == CampaignState::Defeat);
}
