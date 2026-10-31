// RTS Engine — sim/rules.h
// 数据驱动的规则系统，对齐 OpenRA 的 Ruleset / ActorInfo / TraitInfo 架构。
//
// OpenRA 的语义（OpenRA.Game/GameRules/ActorInfo.cs）：
//   - 一个 actor 由一组 trait 组成（TraitInfo 的集合），行为全部来自 trait 组合
//   - 抽象 actor 用 '^' 前缀（AbstractActorPrefix），只用于被继承
//   - trait 变体用 '@' 分隔（TraitInstanceSeparator），如 `Armament@PRIMARY`
//   - `Inherits:` 指定父 actor（逗号分隔可多个），子 actor 继承并覆盖 trait
//   - 构造顺序由 trait 的依赖决定（TraitsInConstructOrder）
//
// C++ 没有反射，所以用注册表（TraitRegistry）代替 ObjectCreator.CreateObject。
#pragma once

#include "miniyaml.h"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace rts {

// ---- trait 基类 ----
// 对应 OpenRA 的 TraitInfo。派生类实现 TypeName()（= OpenRA 的类名去掉 "Info"）。
class TraitInfo {
public:
	virtual ~TraitInfo() = default;
	virtual const char* TypeName() const = 0;
	// 从 yaml 节点填充字段（对应 OpenRA 的 FieldLoader）
	virtual void LoadFrom(const YamlNode& node) { (void)node; }
};

// ---- trait 工厂注册表（替代反射）----
class TraitRegistry {
public:
	using Factory = std::function<std::unique_ptr<TraitInfo>()>;

	static void Register(const std::string& typeName, Factory factory);
	static std::unique_ptr<TraitInfo> Create(const std::string& typeName);
	static bool Has(const std::string& typeName);
	static std::vector<std::string> AllTypes();
};

// 静态注册辅助：在某个 .cpp 里定义
//   static TraitRegistrar<MobileInfo> s_mobileReg("Mobile");
template <typename T>
struct TraitRegistrar {
	explicit TraitRegistrar(const char* name) {
		TraitRegistry::Register(name, []() -> std::unique_ptr<TraitInfo> {
			return std::make_unique<T>();
		});
	}
};

// ---- ActorInfo ----
// 对应 OpenRA 的 ActorInfo：一个 actor 类型的完整定义。
class ActorInfo {
public:
	std::string name;
	std::vector<std::unique_ptr<TraitInfo>> traits;

	bool IsAbstract() const { return !name.empty() && name[0] == '^'; }

	// 按类型名找 trait（对应 ActorInfo.TraitInfo<T>()）
	const TraitInfo* Find(const std::string& typeName) const {
		for (const auto& t : traits) {
			if (typeName == t->TypeName()) return t.get();
		}
		return nullptr;
	}
	template <typename T>
	const T* Find() const {
		for (const auto& t : traits) {
			if (const auto* p = dynamic_cast<const T*>(t.get())) return p;
		}
		return nullptr;
	}
	// 同名 trait 的所有实例（如 Armament@PRIMARY / Armament@SECONDARY）
	std::vector<const TraitInfo*> FindAll(const std::string& typeName) const {
		std::vector<const TraitInfo*> out;
		for (const auto& t : traits) {
			if (typeName == t->TypeName()) out.push_back(t.get());
		}
		return out;
	}
};

// ---- Ruleset ----
// 对应 OpenRA 的 Ruleset：所有 actor 定义 + 武器定义。
class Ruleset {
public:
	std::map<std::string, ActorInfo> actors;

	bool Empty() const { return actors.empty(); }
	const ActorInfo* FindActor(const std::string& name) const {
		const auto it = actors.find(name);
		return it == actors.end() ? nullptr : &it->second;
	}
	// 非抽象 actor 的数量（真正可实例化的）
	size_t ConcreteActorCount() const;

	// 加载一个目录下所有 *.yaml（OpenRA 的 mods/ra/rules/ 就是一个目录的多个文件）
	// includeAbstract=true 时保留 '^' 开头的抽象 actor（它们要被继承）
	bool LoadDirectory(const char* dirPath, std::string* error = nullptr);
	// 加载单个 yaml
	bool LoadFile(const char* path, std::string* error = nullptr);
	// 从已解析的节点加载（便于测试）
	void LoadFromNodes(const YamlNode& root);

private:
	// 继承关系暂存：所有文件加载完后统一校验，否则字母序靠前的文件会误报缺失
	std::map<std::string, std::vector<std::string>> m_pendingInherits;
	void ValidateInherits() const;
};

} // namespace rts
