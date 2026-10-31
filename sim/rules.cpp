// RTS Engine — sim/rules.cpp
#include "rules.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>

namespace rts {

// ---------------- TraitRegistry ----------------

namespace {
std::map<std::string, TraitRegistry::Factory>& Registry() {
	static std::map<std::string, TraitRegistry::Factory> s_reg;
	return s_reg;
}

// 未注册 trait 的占位实现：保留名字，便于统计我们已覆盖多少
class UnknownTrait : public TraitInfo {
public:
	explicit UnknownTrait(std::string n) : name(std::move(n)) {}
	const char* TypeName() const override { return name.c_str(); }
	std::unique_ptr<TraitInfo> Clone() const override { return std::make_unique<UnknownTrait>(name); }
	std::string name;
};
}

void TraitRegistry::Register(const std::string& typeName, Factory factory) {
	Registry()[typeName] = std::move(factory);
}

std::unique_ptr<TraitInfo> TraitRegistry::Create(const std::string& typeName) {
	const auto it = Registry().find(typeName);
	if (it == Registry().end()) return nullptr;
	return it->second();
}

bool TraitRegistry::Has(const std::string& typeName) {
	return Registry().find(typeName) != Registry().end();
}

std::vector<std::string> TraitRegistry::AllTypes() {
	std::vector<std::string> out;
	out.reserve(Registry().size());
	for (const auto& kv : Registry()) out.push_back(kv.first);
	return out;
}

// ---------------- Ruleset ----------------

size_t Ruleset::ConcreteActorCount() const {
	size_t n = 0;
	for (const auto& kv : actors) {
		if (!kv.second.IsAbstract()) ++n;
	}
	return n;
}

void Ruleset::LoadFromNodes(const YamlNode& root) {
	// 第一遍：收集 Inherits 关系（actor 名 → 父名列表），后面用于校验
	std::map<std::string, std::vector<std::string>> inherits;
	for (const auto& node : root.children) {
		if (node.key.empty()) continue;
		if (const YamlNode* inh = node.Child("Inherits")) {
			inherits[node.key] = MiniYaml::SplitList(inh->value);
		}
	}

	// 第二遍：为每个 actor 实例化 trait（Inherits 本身不是 trait）
	for (const auto& node : root.children) {
		if (node.key.empty()) continue;
		ActorInfo info;
		info.name = node.key;
		for (const auto& child : node.children) {
			if (child.key.empty() || child.key == "Inherits") continue;
			auto trait = TraitRegistry::Create(child.key);
			if (!trait) trait = std::make_unique<UnknownTrait>(child.key);
			trait->LoadFrom(child);
			info.traits.push_back(std::move(trait));
		}
		// 同一 actor 可能出现在多个文件里（例如 campaign-tooltips 给 defaults 里的
		// 抽象 actor 补 Tooltip）：合并 trait，同类型以新定义替换，其余追加。
		const auto existing = actors.find(node.key);
		if (existing == actors.end()) {
			actors[node.key] = std::move(info);
		} else {
			ActorInfo& dst = existing->second;
			for (auto& t : info.traits) {
				const std::string typeName = t->TypeName();
				bool replaced = false;
				for (auto& old : dst.traits) {
					if (typeName == old->TypeName()) { old = std::move(t); replaced = true; break; }
				}
				if (!replaced) dst.traits.push_back(std::move(t));
			}
		}
	}

	// 暂存继承关系；校验留给 ValidateInherits（所有文件加载完后才能判定）
	for (const auto& kv : inherits) {
		auto& dst = m_pendingInherits[kv.first];
		for (const std::string& p : kv.second) {
			if (std::find(dst.begin(), dst.end(), p) == dst.end()) dst.push_back(p);
		}
	}

	// 合并继承：把父 actor 的 trait 克隆给子 actor（子已定义的同类型 trait 优先，不覆盖）。
	// 迭代到稳定，以处理 ^Infantry → ^Soldier → E1 这样的多级继承链。
	for (int round = 0; round < 64; ++round) {
		bool changed = false;
		for (const auto& kv : m_pendingInherits) {
			const auto childIt = actors.find(kv.first);
			if (childIt == actors.end()) continue;
			ActorInfo& child = childIt->second;
			for (const std::string& parent : kv.second) {
				const auto parentIt = actors.find(parent);
				if (parentIt == actors.end()) continue;
				const ActorInfo& p = parentIt->second;
				for (const auto& pt : p.traits) {
					if (child.Find(pt->TypeName()) != nullptr) continue; // 子覆盖父
					auto cloned = pt->Clone();
					if (cloned) {
						child.traits.push_back(std::move(cloned));
						changed = true;
					}
				}
			}
		}
		if (!changed) break;
	}
}

void Ruleset::ValidateInherits() const {
	for (const auto& kv : m_pendingInherits) {
		for (const std::string& parent : kv.second) {
			if (actors.find(parent) == actors.end()) {
				std::fprintf(stderr, "[rules] %s inherits missing actor '%s'\n",
					kv.first.c_str(), parent.c_str());
			}
		}
	}
}

bool Ruleset::LoadFile(const char* path, std::string* error) {
	YamlNode root;
	if (!MiniYaml::ParseFile(path, root)) {
		if (error) *error = std::string("cannot parse ") + path;
		return false;
	}
	LoadFromNodes(root);
	return true;
}

bool Ruleset::LoadDirectory(const char* dirPath, std::string* error) {
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
	ValidateInherits();
	return true;
}

} // namespace rts
