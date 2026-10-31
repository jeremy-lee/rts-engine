// RTS Engine — sim/miniyaml.h
// 迷你 YAML 子集解析器（对齐 OpenRA 的 MiniYaml 语义）。
//
// 支持 OpenRA mod yaml 的实际形态：
//   - 缩进（tab 或空格）表示层级
//   - `Key: Value`           普通键值
//   - `Key:`                 带子节点
//   - `Key@Variant:`         trait 变体 / PlayerReference（'@' 分隔，与 ActorInfo.TraitInstanceSeparator 一致）
//   - `- Value`              序列项（映射为 Key 为空、带一个子节点 Value 的节点）
//   - `#` 整行注释
//   - 行内注释（值后面 ` # ...`）
//
// 不支持的：锚点/别名、多行字符串、flow 集合（[]{}, ），OpenRA 的 mod 数据也用得极少。
#pragma once

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace rts {

struct YamlNode {
	std::string key;                     // 键（不含 '@' 变体后缀）
	std::string variant;                 // '@' 后面的部分（无则为空）
	std::string value;                   // 冒号后的值（可能为空）
	std::vector<YamlNode> children;

	bool IsLeaf() const { return children.empty(); }

	// 直接子节点（按 key 精确匹配，忽略 variant）
	const YamlNode* Child(const std::string& k) const {
		for (const auto& c : children) {
			if (c.key == k) return &c;
		}
		return nullptr;
	}
	// 带变体的子节点（如 PlayerReference@USSR 用 key="PlayerReference", variant="USSR" 查）
	const YamlNode* VariantChild(const std::string& k, const std::string& v) const {
		for (const auto& c : children) {
			if (c.key == k && c.variant == v) return &c;
		}
		return nullptr;
	}
	// 所有指定 key 的子节点（保持出现顺序）
	std::vector<const YamlNode*> Children(const std::string& k) const {
		std::vector<const YamlNode*> out;
		for (const auto& c : children) {
			if (c.key == k) out.push_back(&c);
		}
		return out;
	}
	// 取叶子值；不存在返回 fallback
	const std::string& ValueOr(const std::string& fallback = "") const {
		return IsLeaf() ? value : fallback;
	}
};

class MiniYaml {
public:
	// 解析整段文本；返回根节点（key 为空，children 为顶层节点）
	static bool Parse(const std::string& text, YamlNode& root);
	static bool ParseFile(const char* path, YamlNode& root);

	// 便捷读取
	static std::string Trim(const std::string& s);
	static bool ToBool(const std::string& s, bool fallback = false);
	static int32_t ToInt(const std::string& s, int32_t fallback = 0);
	// "a, b, c" → ["a","b","c"]（去空白）
	static std::vector<std::string> SplitList(const std::string& s);
	// "x,y" → 两个整数
	static bool ToPair(const std::string& s, int32_t& a, int32_t& b);
};

} // namespace rts
