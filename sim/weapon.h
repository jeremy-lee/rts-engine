// RTS Engine — sim/weapon.h
// 对齐 OpenRA 的 WeaponInfo / DamageWarhead / Armament。
//
// 源码依据：
//   OpenRA.Game/GameRules/WeaponInfo.cs
//     Range (WDist), MinRange, ReloadDelay (默认 1), Burst (默认 1),
//     BurstDelays (默认 {5}), Projectile, Warheads
//   OpenRA.Mods.Common/Warheads/DamageWarhead.cs
//     Damage (int), Versus (Dictionary<armorType,int>)
//     DamageVersus: Versus 为空返回 100，否则 ApplyPercentageModifiers(100, 该目标的护甲条目)
//     InflictDamage: ApplyPercentageModifiers(Damage, args.DamageModifiers + [DamageVersus])
//   OpenRA.Mods.Common/Util.cs
//     ApplyPercentageModifiers 是连乘 base * Π(p/100)，不是 (100+Σ)/100
//
// WDist 字符串格式（OpenRA 的 WDist 构造器）："5c0" = 5 格 0 子格，1 格 = 1024 子格。
#pragma once

#include "miniyaml.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace rts {

// ---- WDist 解析 ----
// 支持 "5c0" / "0c512" / "128"（纯数字按子格）/ "1c"（c 后缺省为 0）
int32_t ParseWDist(const std::string& s, int32_t fallback = 0);

// ---- 弹头 ----
struct WarheadInfo {
	std::string type;                        // SpreadDamage / HealthPercentageDamage / ...
	int32_t damage = 0;                      // Damage
	std::map<std::string, int32_t> versus;    // 护甲类型 → 伤害百分比
	int32_t spread = 0;                      // Spread（WDist），仅 SpreadDamage 有

	// 对齐 DamageWarhead.DamageVersus：
	//   Versus 为空 → 100；否则 ApplyPercentageModifiers(100, 目标护甲对应的 Versus 值)
	int32_t DamageVersus(const std::vector<std::string>& armorTypes) const;

	// 对齐 DamageWarhead.InflictDamage：
	//   ApplyPercentageModifiers(Damage, damageModifiers + [DamageVersus])
	int32_t ComputeDamage(const std::vector<std::string>& armorTypes,
		const std::vector<int32_t>& damageModifiers = {}) const;

	bool LoadFrom(const YamlNode& node);
};

// ---- 武器 ----
struct WeaponInfo {
	std::string name;
	int32_t range = 0;                        // WDist（子格）
	int32_t minRange = 0;
	int32_t reloadDelay = 1;                   // tick
	int32_t burst = 1;
	std::vector<int32_t> burstDelays{5};
	int32_t afterFireSoundDelay = 0;
	bool canTargetSelf = false;
	std::vector<WarheadInfo> warheads;
	std::string projectile;

	bool LoadFrom(const YamlNode& node, const std::string& weaponName);
};

// ---- 武器表 ----
class WeaponSet {
public:
	std::map<std::string, WeaponInfo> weapons;

	bool Empty() const { return weapons.empty(); }
	size_t Size() const { return weapons.size(); }
	const WeaponInfo* Find(const std::string& name) const {
		const auto it = weapons.find(name);
		return it == weapons.end() ? nullptr : &it->second;
	}

	// 加载一个目录下所有 *.yaml（mods/ra/weapons/）
	bool LoadDirectory(const char* dirPath, std::string* error = nullptr);
	bool LoadFile(const char* path, std::string* error = nullptr);
	void LoadFromNodes(const YamlNode& root);
};

// ---- 武器挂载（对应 OpenRA 的 Armament trait）----
struct Armament {
	std::string weapon;         // Weapon: m60mg
	int32_t fireDelay = 0;      // FireDelay
	int32_t localOffsetX = 0;   // LocalOffset（暂用两个 int，避免引入 WVec）
	int32_t localOffsetY = 0;
	int32_t localOffsetZ = 0;
	bool turreted = false;
};

} // namespace rts
