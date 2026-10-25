// RTS Engine — tests/test_gamedefs.cpp
// M5 验收：YAML 解析 + 数据驱动单位/建筑
#include "test_framework.h"
#include "sim/datatable.h"
#include "sim/gamedefs.h"
#include "sim/world.h"

using namespace rts;

TEST(datatable_parse_simple_map) {
	const DataNode n = DataNode::Parse(R"(
name: "test"
hp: 100
nested:
  a: 1
  b: 2
)");
	CHECK(n.IsMap());
	CHECK_EQ(n.GetString("name"), std::string("test"));
	CHECK_EQ(n.GetInt("hp"), 100);
	const DataNode* nested = n.GetNode("nested");
	CHECK(nested != nullptr);
	CHECK_EQ(nested->GetInt("a"), 1);
	CHECK_EQ(nested->GetInt("b"), 2);
}

TEST(datatable_parse_list) {
	const DataNode n = DataNode::Parse(R"(
items:
  - alpha
  - beta
  - 42
)");
	const DataNode* items = n.GetNode("items");
	CHECK(items != nullptr);
	CHECK(items->IsList());
	CHECK_EQ(items->list.size(), static_cast<size_t>(3));
	CHECK_EQ(items->list[0].AsString(), std::string("alpha"));
	CHECK_EQ(items->list[2].AsInt(), 42);
}

TEST(datatable_comments_and_quotes) {
	const DataNode n = DataNode::Parse(R"(
# 这是注释
key: "带 # 的字符串"  # 行尾注释
num: 7
)");
	CHECK_EQ(n.GetString("key"), std::string("带 # 的字符串"));
	CHECK_EQ(n.GetInt("num"), 7);
}

TEST(gamedefs_load_rules_yaml) {
	const std::string yaml = R"(
units:
  infantry:
    name: "步兵"
    cost: 50
    hp: 100
    speed: 32
    weapon:
      damage: 10
      range: 8192
      cooldown: 15
  tank:
    name: "坦克"
    cost: 150
    hp: 300
    speed: 20
    weapon:
      damage: 25
      range: 10240
      cooldown: 30
buildings:
  barracks:
    name: "兵营"
    cost: 300
    buildTime: 120
    hp: 500
    width: 4
    height: 4
    produces:
      - infantry
      - tank
)";
	const Rules rules = Rules::FromYAML(yaml);
	CHECK_EQ(rules.units.size(), static_cast<size_t>(2));
	CHECK_EQ(rules.buildings.size(), static_cast<size_t>(1));

	const UnitDef* inf = rules.FindUnit("infantry");
	CHECK(inf != nullptr);
	CHECK_EQ(inf->name, std::string("步兵"));
	CHECK_EQ(inf->cost, 50);
	CHECK_EQ(inf->hp, 100);
	CHECK_EQ(inf->weapon.damage, 10);
	CHECK_EQ(inf->weapon.range, 8192);
	CHECK_EQ(inf->weapon.cooldown, 15);

	const BuildingDef* b = rules.FindBuilding("barracks");
	CHECK(b != nullptr);
	CHECK_EQ(b->width, 4);
	CHECK_EQ(b->produces.size(), static_cast<size_t>(2));
	CHECK_EQ(b->produces[0], std::string("infantry"));
}

TEST(gamedefs_numeric_ids) {
	const std::string yaml = R"(
units:
  a:
    hp: 10
  b:
    hp: 20
)";
	const Rules rules = Rules::FromYAML(yaml);
	CHECK_EQ(rules.FindUnitByNumeric(0)->id, std::string("a"));
	CHECK_EQ(rules.FindUnitByNumeric(1)->id, std::string("b"));
	CHECK(rules.FindUnitByNumeric(2) == nullptr);
}

TEST(gamedefs_data_driven_spawn) {
	// 用规则数据生成单位：hp/speed/武器都应来自数据
	const std::string yaml = R"(
units:
  tank:
    name: "坦克"
    hp: 300
    speed: 20
    weapon:
      damage: 25
      range: 10240
      cooldown: 30
)";
	const Rules rules = Rules::FromYAML(yaml);
	CHECK(rules.FindUnit("tank") != nullptr);

	Map map(32, 32, 0, kWalkable | kBuildable);
	World world(map, 1, rules);

	// Spawn 命令：targetId = numericId(0)
	Command c;
	c.type = CommandType::Spawn;
	c.playerId = 0;
	c.x = 10 * kSubPerCell;
	c.y = 10 * kSubPerCell;
	c.targetId = 0; // tank
	world.ApplyCommand(c);

	const auto& all = world.Entities().All();
	CHECK_EQ(all.size(), static_cast<size_t>(1));
	const Actor& a = all[0];
	CHECK_EQ(a.health.hp, 300);
	CHECK_EQ(a.health.maxHp, 300);
	CHECK_EQ(a.movable.speed, 20);
	CHECK_EQ(a.armament.damage, 25);
	CHECK_EQ(a.armament.range, 10240);
	CHECK_EQ(a.armament.cooldown, 30);
}
