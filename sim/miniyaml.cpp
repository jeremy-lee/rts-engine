// RTS Engine — sim/miniyaml.cpp
#include "miniyaml.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace rts {

std::string MiniYaml::Trim(const std::string& s) {
	size_t a = 0, b = s.size();
	while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r')) ++a;
	while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r')) --b;
	return s.substr(a, b - a);
}

bool MiniYaml::ToBool(const std::string& s, bool fallback) {
	const std::string t = Trim(s);
	if (t == "True" || t == "true" || t == "yes" || t == "1") return true;
	if (t == "False" || t == "false" || t == "no" || t == "0") return false;
	return fallback;
}

int32_t MiniYaml::ToInt(const std::string& s, int32_t fallback) {
	const std::string t = Trim(s);
	if (t.empty()) return fallback;
	char* end = nullptr;
	const long v = std::strtol(t.c_str(), &end, 10);
	if (end == t.c_str()) return fallback;
	return static_cast<int32_t>(v);
}

std::vector<std::string> MiniYaml::SplitList(const std::string& s) {
	std::vector<std::string> out;
	std::string cur;
	for (const char c : s) {
		if (c == ',') {
			const std::string t = Trim(cur);
			if (!t.empty()) out.push_back(t);
			cur.clear();
		} else {
			cur.push_back(c);
		}
	}
	const std::string t = Trim(cur);
	if (!t.empty()) out.push_back(t);
	return out;
}

bool MiniYaml::ToPair(const std::string& s, int32_t& a, int32_t& b) {
	const auto parts = SplitList(s);
	if (parts.size() < 2) return false;
	a = ToInt(parts[0]);
	b = ToInt(parts[1]);
	return true;
}

namespace {

// 统计行首缩进（tab 记 1 列，与 OpenRA 的 yaml 一致：OpenRA 的 mod 数据就是用 tab）
int IndentOf(const std::string& line) {
	int n = 0;
	for (const char c : line) {
		if (c == '\t') ++n;
		else if (c == ' ') ++n;
		else break;
	}
	return n;
}

// 去掉行内注释（` #` 之后），注意值里可能有 '#'（极少见，OpenRA 数据里没有）
std::string StripInlineComment(const std::string& line) {
	for (size_t i = 0; i < line.size(); ++i) {
		if (line[i] == '#' && (i == 0 || line[i - 1] == ' ' || line[i - 1] == '\t')) {
			return line.substr(0, i);
		}
	}
	return line;
}

// 解析 "Key@Variant: Value" / "Key: Value" / "- Value"
bool ParseLine(const std::string& line, YamlNode& node) {
	const std::string body = MiniYaml::Trim(line);
	if (body.empty()) return false;

	// 序列项
	if (body.size() >= 2 && body[0] == '-' && (body[1] == ' ' || body[1] == '\t')) {
		node.key.clear();
		node.variant.clear();
		node.value = MiniYaml::Trim(body.substr(1));
		return true;
	}

	const size_t colon = body.find(':');
	if (colon == std::string::npos) return false;

	std::string keyPart = MiniYaml::Trim(body.substr(0, colon));
	node.value = MiniYaml::Trim(body.substr(colon + 1));

	// Key@Variant
	const size_t at = keyPart.find('@');
	if (at != std::string::npos) {
		node.variant = keyPart.substr(at + 1);
		keyPart = keyPart.substr(0, at);
	}
	node.key = keyPart;
	return !node.key.empty() || !node.variant.empty();
}

} // namespace

bool MiniYaml::Parse(const std::string& text, YamlNode& root) {
	root = YamlNode{};
	// 栈：每层存 (indent, node*)
	std::vector<std::pair<int, YamlNode*>> stack;
	stack.emplace_back(-1, &root);

	size_t pos = 0;
	while (pos <= text.size()) {
		size_t eol = text.find('\n', pos);
		if (eol == std::string::npos) eol = text.size();
		std::string line = text.substr(pos, eol - pos);
		pos = eol + 1;
		if (line.empty() && pos > text.size()) break;

		// 去行尾 \r
		if (!line.empty() && line.back() == '\r') line.pop_back();
		// 整行注释 / 空行
		const std::string stripped = Trim(line);
		if (stripped.empty() || stripped[0] == '#') continue;

		const int indent = IndentOf(line);
		YamlNode node;
		if (!ParseLine(StripInlineComment(line), node)) continue;

		// 回退到父层
		while (stack.size() > 1 && stack.back().first >= indent) stack.pop_back();
		if (stack.empty()) stack.emplace_back(-1, &root);

		YamlNode* parent = stack.back().second;
		parent->children.push_back(std::move(node));
		YamlNode* inserted = &parent->children.back();
		stack.emplace_back(indent, inserted);
	}
	return true;
}

bool MiniYaml::ParseFile(const char* path, YamlNode& root) {
	std::ifstream in(path, std::ios::binary);
	if (!in) return false;
	std::ostringstream ss;
	ss << in.rdbuf();
	return Parse(ss.str(), root);
}

} // namespace rts
