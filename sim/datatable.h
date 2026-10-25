// RTS Engine — sim/datatable.h
// 零依赖 YAML 子集解析器（map / list / scalar）。
// 支持：`key: value`、`key:`（嵌套 map）、`- item`（list）、缩进、注释(#)、引号字符串。
// 不支持：锚点/别名、多行字符串、flow 语法——RTS 数据用不到。
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace rts {

class DataNode {
public:
	enum class Type { Null, Scalar, Map, List };

	Type type = Type::Null;
	std::string scalar;
	std::vector<std::pair<std::string, DataNode>> map;
	std::vector<DataNode> list;

	bool IsNull() const { return type == Type::Null; }
	bool IsScalar() const { return type == Type::Scalar; }
	bool IsMap() const { return type == Type::Map; }
	bool IsList() const { return type == Type::List; }

	// ---- map 访问 ----
	bool Has(const std::string& key) const;
	DataNode* Find(const std::string& key);
	const DataNode* Find(const std::string& key) const;

	// ---- 类型转换（带默认值）----
	std::string AsString(const std::string& fallback = "") const;
	int32_t AsInt(int32_t fallback = 0) const;
	int64_t AsInt64(int64_t fallback = 0) const;
	bool AsBool(bool fallback = false) const;

	// 查询辅助
	int32_t GetInt(const std::string& key, int32_t fallback = 0) const;
	std::string GetString(const std::string& key, const std::string& fallback = "") const;
	bool GetBool(const std::string& key, bool fallback = false) const;
	const DataNode* GetNode(const std::string& key) const;

	// ---- 解析 ----
	static DataNode Parse(const std::string& text);
	static bool ParseFile(const std::string& path, DataNode& out);
};

} // namespace rts
