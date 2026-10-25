// RTS Engine — tools/rules_lint.cpp
// 数据校验工具：检查 rules.yaml 的引用完整性/数值合理性。
// 用法：rts_lint <rules.yaml>
#include <cstdio>
#include <string>
#include <vector>

#include "core/core.h"
#include "sim/gamedefs.h"

using namespace rts;

namespace {

int g_errors = 0;
int g_warnings = 0;

void Error(const std::string& msg) {
	std::printf("  [ERROR] %s\n", msg.c_str());
	++g_errors;
}

void Warn(const std::string& msg) {
	std::printf("  [WARN ] %s\n", msg.c_str());
	++g_warnings;
}

void Ok(const std::string& msg) {
	std::printf("  [OK   ] %s\n", msg.c_str());
}

} // namespace

int main(int argc, char** argv) {
	if (argc < 2) {
		std::printf("用法: rts_lint <rules.yaml>\n");
		return 1;
	}
	const std::string path = argv[1];
	std::printf("=== RTS rules lint: %s ===\n\n", path.c_str());

	Rules rules;
	if (!Rules::LoadFile(path, rules)) {
		std::printf("  [ERROR] 无法加载规则文件\n");
		return 1;
	}

	std::printf("loaded: %zu units, %zu buildings\n\n", rules.units.size(), rules.buildings.size());

	// ---- 1. 单位检查 ----
	std::printf("[units]\n");
	if (rules.units.empty()) Error("没有任何单位定义");

	bool hasHarvester = false;
	bool hasCombat = false;
	for (const auto& kv : rules.units) {
		const UnitDef& u = kv.second;
		const std::string tag = "unit '" + u.id + "'";
		if (u.hp <= 0) Error(tag + ": hp <= 0");
		if (u.speed < 0) Error(tag + ": speed < 0");
		if (u.cost < 0) Error(tag + ": cost < 0");
		if (u.buildTime < 0) Error(tag + ": buildTime < 0");
		if (u.isHarvester) hasHarvester = true;
		if (!u.isHarvester && u.weapon.damage > 0) hasCombat = true;
		if (u.weapon.damage < 0) Error(tag + ": weapon.damage < 0");
		if (u.weapon.range < 0) Error(tag + ": weapon.range < 0");
		if (u.weapon.cooldown <= 0) Warn(tag + ": weapon.cooldown <= 0");
		Ok(tag);
	}
	if (!hasHarvester) Warn("没有采矿单位（harvester: true），经济闭环不完整");
	if (!hasCombat) Warn("没有战斗单位（weapon.damage > 0），无法作战");

	// ---- 2. 建筑检查 ----
	std::printf("\n[buildings]\n");
	if (rules.buildings.empty()) Error("没有任何建筑定义");
	for (const auto& kv : rules.buildings) {
		const BuildingDef& b = kv.second;
		const std::string tag = "building '" + b.id + "'";
		if (b.hp <= 0) Error(tag + ": hp <= 0");
		if (b.cost < 0) Error(tag + ": cost < 0");
		if (b.buildTime < 0) Error(tag + ": buildTime < 0");
		if (b.width <= 0 || b.height <= 0) Error(tag + ": 尺寸无效");
		if (b.produces.empty()) Warn(tag + ": produces 为空（不能生产任何东西）");
		for (const auto& unitId : b.produces) {
			if (rules.FindUnit(unitId) == nullptr) {
				Error(tag + ": produces 引用不存在的单位 '" + unitId + "'");
			}
		}
		Ok(tag);
	}

	// ---- 3. 引用完整性（数字 id 与字符串一致）----
	std::printf("\n[ids]\n");
	for (size_t i = 0; i < rules.unitIds.size(); ++i) {
		const UnitDef* u = rules.FindUnitByNumeric(static_cast<int32_t>(i));
		if (u == nullptr || u->id != rules.unitIds[i]) {
			Error("unit numericId " + std::to_string(i) + " 映射不一致");
		}
	}
	Ok("numeric id mapping");

	// ---- 汇总 ----
	std::printf("\n=== summary ===\n");
	std::printf("errors: %d, warnings: %d\n", g_errors, g_warnings);
	if (g_errors > 0) {
		std::printf("[FAIL] rules invalid\n");
		return 1;
	}
	std::printf("[PASS] rules valid\n");
	return 0;
}
