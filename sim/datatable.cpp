// RTS Engine — sim/datatable.cpp
#include "datatable.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace rts {

// ============================================================
// DataNode 访问
// ============================================================

bool DataNode::Has(const std::string& key) const { return Find(key) != nullptr; }

DataNode* DataNode::Find(const std::string& key) {
	for (auto& kv : map) {
		if (kv.first == key) return &kv.second;
	}
	return nullptr;
}

const DataNode* DataNode::Find(const std::string& key) const {
	for (const auto& kv : map) {
		if (kv.first == key) return &kv.second;
	}
	return nullptr;
}

std::string DataNode::AsString(const std::string& fallback) const {
	return (type == Type::Scalar) ? scalar : fallback;
}

int32_t DataNode::AsInt(int32_t fallback) const {
	if (type != Type::Scalar) return fallback;
	char* end = nullptr;
	const long v = std::strtol(scalar.c_str(), &end, 10);
	if (end == scalar.c_str()) return fallback;
	return static_cast<int32_t>(v);
}

int64_t DataNode::AsInt64(int64_t fallback) const {
	if (type != Type::Scalar) return fallback;
	char* end = nullptr;
	const long long v = std::strtoll(scalar.c_str(), &end, 10);
	if (end == scalar.c_str()) return fallback;
	return static_cast<int64_t>(v);
}

bool DataNode::AsBool(bool fallback) const {
	if (type != Type::Scalar) return fallback;
	const std::string v = scalar;
	if (v == "true" || v == "True" || v == "yes" || v == "1") return true;
	if (v == "false" || v == "False" || v == "no" || v == "0") return false;
	return fallback;
}

int32_t DataNode::GetInt(const std::string& key, int32_t fallback) const {
	const DataNode* n = Find(key);
	return n ? n->AsInt(fallback) : fallback;
}

std::string DataNode::GetString(const std::string& key, const std::string& fallback) const {
	const DataNode* n = Find(key);
	return n ? n->AsString(fallback) : fallback;
}

bool DataNode::GetBool(const std::string& key, bool fallback) const {
	const DataNode* n = Find(key);
	return n ? n->AsBool(fallback) : fallback;
}

const DataNode* DataNode::GetNode(const std::string& key) const { return Find(key); }

// ============================================================
// YAML 子集解析
// ============================================================

namespace {

struct Line {
	int indent = 0;
	std::string text; // 去掉缩进后的内容
};

// 去掉行尾注释（引号外的 #）
std::string StripComment(const std::string& s) {
	bool inSingle = false, inDouble = false;
	for (size_t i = 0; i < s.size(); ++i) {
		const char c = s[i];
		if (c == '\'' && !inDouble) inSingle = !inSingle;
		else if (c == '"' && !inSingle) inDouble = !inDouble;
		else if (c == '#' && !inSingle && !inDouble) return s.substr(0, i);
	}
	return s;
}

std::string Trim(const std::string& s) {
	size_t b = 0, e = s.size();
	while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
	while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
	return s.substr(b, e - b);
}

std::string Unquote(const std::string& s) {
	if (s.size() >= 2) {
		if ((s.front() == '"' && s.back() == '"') || (s.front() == '\'' && s.back() == '\'')) {
			return s.substr(1, s.size() - 2);
		}
	}
	return s;
}

bool IsListLine(const std::string& t) {
	return !t.empty() && t[0] == '-' && (t.size() == 1 || t[1] == ' ');
}

DataNode ParseBlock(const std::vector<Line>& lines, size_t& idx, int indent);

// 解析 flow 列表 `[a, b, c]`
bool TryParseFlowList(const std::string& value, DataNode& out) {
	if (value.size() < 2 || value.front() != '[' || value.back() != ']') return false;
	out.type = DataNode::Type::List;
	const std::string inner = value.substr(1, value.size() - 2);
	size_t start = 0;
	for (;;) {
		const size_t comma = inner.find(',', start);
		const std::string item = Trim(inner.substr(start, (comma == std::string::npos) ? std::string::npos : comma - start));
		if (!item.empty()) {
			DataNode n;
			n.type = DataNode::Type::Scalar;
			n.scalar = Unquote(item);
			out.list.push_back(std::move(n));
		}
		if (comma == std::string::npos) break;
		start = comma + 1;
	}
	return true;
}

// 解析一个 map 值（key: value | key: 嵌套）
DataNode ParseMapValue(const std::vector<Line>& lines, size_t& idx, int indent, const std::string& value) {
	DataNode node;
	if (!value.empty()) {
		if (TryParseFlowList(value, node)) return node;
		node.type = DataNode::Type::Scalar;
		node.scalar = Unquote(value);
		return node;
	}
	// 空值：看下一行是否更深（嵌套 map）或 list
	if (idx < lines.size() && lines[idx].indent > indent) {
		return ParseBlock(lines, idx, lines[idx].indent);
	}
	node.type = DataNode::Type::Null;
	return node;
}

// 解析缩进块：可能为 map 或 list
DataNode ParseBlock(const std::vector<Line>& lines, size_t& idx, int indent) {
	DataNode node;
	if (idx >= lines.size()) return node;

	const bool isList = IsListLine(lines[idx].text);
	node.type = isList ? DataNode::Type::List : DataNode::Type::Map;

	while (idx < lines.size()) {
		const Line& ln = lines[idx];
		if (ln.indent < indent) break;
		if (ln.indent > indent) {
			// 缩进异常（跳过多余）
			++idx;
			continue;
		}

		if (isList) {
			if (!IsListLine(ln.text)) break;
			const std::string item = Trim(ln.text.substr(1));
			DataNode itemNode;
			if (item.empty()) {
				// 空 "-"：嵌套块
				++idx;
				if (idx < lines.size() && lines[idx].indent > indent) {
					itemNode = ParseBlock(lines, idx, lines[idx].indent);
				} else {
					itemNode.type = DataNode::Type::Null;
				}
			} else {
				// "- key: value" 或 "- scalar"
				const size_t colon = item.find(':');
				if (colon != std::string::npos) {
					// list 中的 map 项
					itemNode.type = DataNode::Type::Map;
					const std::string k = Trim(item.substr(0, colon));
					const std::string v = Trim(item.substr(colon + 1));
					itemNode.map.emplace_back(k, ParseMapValue(lines, idx, indent, v));
					++idx;
					// 继续读取同缩进的后续 map 键（比 list 缩进更深）
					while (idx < lines.size() && lines[idx].indent > indent && !IsListLine(lines[idx].text)) {
						const Line& inner = lines[idx];
						const size_t c2 = inner.text.find(':');
						if (c2 == std::string::npos) break;
						const std::string k2 = Trim(inner.text.substr(0, c2));
						const std::string v2 = Trim(inner.text.substr(c2 + 1));
						const int innerIndent = inner.indent;
						++idx;
						itemNode.map.emplace_back(k2, ParseMapValue(lines, idx, innerIndent, v2));
						if (idx < lines.size() && lines[idx].indent != innerIndent) break;
					}
				} else {
					itemNode.type = DataNode::Type::Scalar;
					itemNode.scalar = Unquote(item);
					++idx;
				}
			}
			node.list.push_back(std::move(itemNode));
			continue;
		}

		// map 项
		const size_t colon = ln.text.find(':');
		if (colon == std::string::npos) { ++idx; continue; }
		const std::string key = Trim(ln.text.substr(0, colon));
		const std::string value = Trim(ln.text.substr(colon + 1));
		++idx;
		DataNode child = ParseMapValue(lines, idx, indent, value);
		node.map.emplace_back(key, std::move(child));
	}
	return node;
}

} // namespace

DataNode DataNode::Parse(const std::string& text) {
	// 切行 + 去注释 + 去空行 + 计算缩进
	std::vector<Line> lines;
	std::istringstream in(text);
	std::string raw;
	while (std::getline(in, raw)) {
		if (!raw.empty() && raw.back() == '\r') raw.pop_back();
		const std::string noComment = StripComment(raw);
		const std::string trimmed = Trim(noComment);
		if (trimmed.empty()) continue;
		int indent = 0;
		while (indent < static_cast<int>(raw.size()) && raw[indent] == ' ') ++indent;
		lines.push_back(Line{indent, trimmed});
	}
	if (lines.empty()) return DataNode{};

	size_t idx = 0;
	return ParseBlock(lines, idx, lines[0].indent);
}

bool DataNode::ParseFile(const std::string& path, DataNode& out) {
	std::ifstream in(path);
	if (!in) return false;
	std::stringstream ss;
	ss << in.rdbuf();
	out = Parse(ss.str());
	return !out.IsNull();
}

} // namespace rts
