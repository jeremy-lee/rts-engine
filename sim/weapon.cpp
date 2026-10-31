// RTS Engine — sim/weapon.cpp
#include "weapon.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>

#include "locomotor.h" // ApplyPercentageModifiers

namespace rts {

int32_t ParseWDist(const std::string& raw, int32_t fallback) {
	const std::string s = MiniYaml::Trim(raw);
	if (s.empty()) return fallback;

	// 找到 'c'（cell 分隔符）
	const size_t c = s.find('c');
	if (c == std::string::npos) {
		// 纯数字：按子格解释（OpenRA 里裸数字也是 subcell）
		char* end = nullptr;
		const long v = std::strtol(s.c_str(), &end, 10);
		return (end == s.c_str()) ? fallback : static_cast<int32_t>(v);
	}
	const std::string cellPart = MiniYaml::Trim(s.substr(0, c));
	const std::string subPart = MiniYaml::Trim(s.substr(c + 1));
	const long cells = cellPart.empty() ? 0 : std::strtol(cellPart.c_str(), nullptr, 10);
	const long subs = subPart.empty() ? 0 : std::strtol(subPart.c_str(), nullptr, 10);
	return static_cast<int32_t>(cells * 1024 + subs);
}

bool WarheadInfo::LoadFrom(const YamlNode& node) {
	// 节点形如 `Warhead@1Dam: SpreadDamage` → key=Warhead, variant=1Dam, value=SpreadDamage
	type = node.value;
	if (type.empty()) type = "SpreadDamage";

	if (const YamlNode* n = node.Child("Damage")) damage = MiniYaml::ToInt(n->value, 0);
	if (const YamlNode* n = node.Child("Spread")) spread = ParseWDist(n->value, 0);

	if (const YamlNode* v = node.Child("Versus")) {
		for (const auto& t : v->children) {
			if (t.key.empty()) continue;
			versus[t.key] = MiniYaml::ToInt(t.value, 0);
		}
	}
	return true;
}

int32_t WarheadInfo::DamageVersus(const std::vector<std::string>& armorTypes) const {
	// DamageWarhead.DamageVersus：无 Versus 定义时直接 100
	if (versus.empty()) return 100;
	std::vector<int32_t> vals;
	for (const auto& a : armorTypes) {
		const auto it = versus.find(a);
		if (it != versus.end()) vals.push_back(it->second);
	}
	// 一个护甲类型都没命中 → ApplyPercentageModifiers(100, {}) = 100
	return ApplyPercentageModifiers(100, vals);
}

int32_t WarheadInfo::ComputeDamage(const std::vector<std::string>& armorTypes,
	const std::vector<int32_t>& damageModifiers) const {
	std::vector<int32_t> all = damageModifiers;
	all.push_back(DamageVersus(armorTypes));
	return ApplyPercentageModifiers(damage, all);
}

bool WeaponInfo::LoadFrom(const YamlNode& node, const std::string& weaponName) {
	name = weaponName;
	if (const YamlNode* n = node.Child("Range")) range = ParseWDist(n->value, 0);
	if (const YamlNode* n = node.Child("MinRange")) minRange = ParseWDist(n->value, 0);
	if (const YamlNode* n = node.Child("ReloadDelay")) reloadDelay = MiniYaml::ToInt(n->value, 1);
	if (const YamlNode* n = node.Child("Burst")) burst = MiniYaml::ToInt(n->value, 1);
	if (const YamlNode* n = node.Child("AfterFireSoundDelay")) afterFireSoundDelay = MiniYaml::ToInt(n->value, 0);
	if (const YamlNode* n = node.Child("CanTargetSelf")) canTargetSelf = MiniYaml::ToBool(n->value);
	if (const YamlNode* n = node.Child("Projectile")) projectile = n->value;

	if (const YamlNode* n = node.Child("BurstDelays")) {
		burstDelays.clear();
		for (const auto& d : n->children) {
			if (d.key.empty()) continue;
			burstDelays.push_back(MiniYaml::ToInt(d.value, 5));
		}
		if (burstDelays.empty()) burstDelays.push_back(5);
	}

	for (const auto& child : node.children) {
		if (child.key != "Warhead") continue;
		WarheadInfo wh;
		wh.LoadFrom(child);
		warheads.push_back(std::move(wh));
	}
	return true;
}

void WeaponSet::LoadFromNodes(const YamlNode& root) {
	for (const auto& node : root.children) {
		if (node.key.empty()) continue;
		WeaponInfo w;
		if (!w.LoadFrom(node, node.key)) continue;
		// 同名武器后定义覆盖先定义（与 OpenRA 的 Ruleset 一致）
		weapons[node.key] = std::move(w);
	}
}

bool WeaponSet::LoadFile(const char* path, std::string* error) {
	YamlNode root;
	if (!MiniYaml::ParseFile(path, root)) {
		if (error) *error = std::string("cannot parse ") + path;
		return false;
	}
	LoadFromNodes(root);
	return true;
}

bool WeaponSet::LoadDirectory(const char* dirPath, std::string* error) {
	namespace fs = std::filesystem;
	std::error_code ec;
	if (!fs::is_directory(dirPath, ec)) {
		if (error) *error = std::string("not a directory: ") + dirPath;
		return false;
	}
	std::vector<fs::path> files;
	for (const auto& e : fs::directory_iterator(dirPath, ec)) {
		if (!e.is_regular_file()) continue;
		std::string ext = e.path().extension().string();
		for (char& ch : ext) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
		if (ext == ".yaml") files.push_back(e.path());
	}
	std::sort(files.begin(), files.end());
	for (const auto& f : files) {
		YamlNode root;
		if (!MiniYaml::ParseFile(f.string().c_str(), root)) {
			if (error) *error = std::string("cannot parse ") + f.string();
			return false;
		}
		LoadFromNodes(root);
	}
	return true;
}

} // namespace rts
