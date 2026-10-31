// RTS Engine — tools/oramap_import.cpp
// .oramap 导入：OpenRA 的地图包是 zip，内含 map.yaml + map.bin（+ 可选 rules/lua/png）。
//
// 输出（可直接被编辑器 Ctrl+O 打开）：
//   <out>.map       Map 二进制（与 OpenRA map.bin 同构）
//   <out>_meta.txt  Bounds / Tileset（RTSMETA 1）
//   <out>_actors.txt  Actors（RTSENT 2 格式：名字 player x y）
//
// 用法：
//   rts_oramap_import <input.oramap> <outBasePath>
//   例：rts_oramap_import "mods\ra\maps\allies-01.oramap" "imported_allies01"
//       → imported_allies01.map / imported_allies01_meta.txt / imported_allies01_actors.txt
#include "inflate.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace {

// 中立（Neutral / NonCombatant）的 actor 不导入
constexpr int kNeutralPlayer = 255;

// ---------------- ZIP ----------------
struct ZipEntry {
	std::string name;
	uint32_t compressedSize = 0;
	uint32_t uncompressedSize = 0;
	uint16_t method = 0; // 0 = stored, 8 = deflate
	uint32_t localHeaderOffset = 0;
};

uint16_t ReadU16(const std::vector<uint8_t>& b, size_t off) {
	uint16_t v = 0;
	if (off + 2 <= b.size()) std::memcpy(&v, &b[off], 2);
	return v;
}
uint32_t ReadU32(const std::vector<uint8_t>& b, size_t off) {
	uint32_t v = 0;
	if (off + 4 <= b.size()) std::memcpy(&v, &b[off], 4);
	return v;
}

bool ReadWholeFile(const char* path, std::vector<uint8_t>& out) {
	std::FILE* f = std::fopen(path, "rb");
	if (f == nullptr) return false;
	std::fseek(f, 0, SEEK_END);
	const long size = std::ftell(f);
	std::fseek(f, 0, SEEK_SET);
	if (size <= 0) { std::fclose(f); return false; }
	out.resize(static_cast<size_t>(size));
	const size_t got = std::fread(out.data(), 1, out.size(), f);
	std::fclose(f);
	return got == out.size();
}

// 解析中央目录（EOCD 在文件尾部）
bool ParseZip(const std::vector<uint8_t>& zip, std::vector<ZipEntry>& entries) {
	// EOCD 签名 0x06054b50，最小 22 字节，注释最长 65535
	if (zip.size() < 22) return false;
	size_t eocd = 0;
	bool found = false;
	const size_t scanFrom = (zip.size() > 22 + 65535) ? zip.size() - 22 - 65535 : 0;
	for (size_t i = zip.size() - 22 + 1; i-- > scanFrom;) {
		if (ReadU32(zip, i) == 0x06054b50u) { eocd = i; found = true; break; }
	}
	if (!found) return false;

	const uint16_t total = ReadU16(zip, eocd + 10);
	const uint32_t cdOffset = ReadU32(zip, eocd + 16);
	size_t p = cdOffset;
	for (uint16_t i = 0; i < total; ++i) {
		if (ReadU32(zip, p) != 0x02014b50u) return false;
		ZipEntry e;
		e.method = ReadU16(zip, p + 10);
		e.compressedSize = ReadU32(zip, p + 20);
		e.uncompressedSize = ReadU32(zip, p + 24);
		const uint16_t nameLen = ReadU16(zip, p + 28);
		const uint16_t extraLen = ReadU16(zip, p + 30);
		const uint16_t commentLen = ReadU16(zip, p + 32);
		e.localHeaderOffset = ReadU32(zip, p + 42);
		if (p + 46 + nameLen > zip.size()) return false;
		e.name.assign(reinterpret_cast<const char*>(&zip[p + 46]), nameLen);
		entries.push_back(std::move(e));
		p += 46 + nameLen + extraLen + commentLen;
	}
	return true;
}

bool ExtractEntry(const std::vector<uint8_t>& zip, const ZipEntry& e, std::vector<uint8_t>& out) {
	const size_t lh = e.localHeaderOffset;
	if (ReadU32(zip, lh) != 0x04034b50u) return false;
	const uint16_t nameLen = ReadU16(zip, lh + 26);
	const uint16_t extraLen = ReadU16(zip, lh + 28);
	const size_t dataStart = lh + 30 + nameLen + extraLen;
	if (dataStart + e.compressedSize > zip.size()) return false;

	if (e.method == 0) {
		out.assign(zip.begin() + static_cast<long>(dataStart),
			zip.begin() + static_cast<long>(dataStart + e.compressedSize));
		return true;
	}
	if (e.method == 8) {
		return inflate::Inflate(&zip[dataStart], e.compressedSize, out) &&
			out.size() == e.uncompressedSize;
	}
	std::fprintf(stderr, "[oramap] unsupported compression method %u for %s\n", e.method, e.name.c_str());
	return false;
}

// ---------------- map.yaml（只取需要的字段）----------------
struct MapYaml {
	std::string tileset = "TEMPERAT";
	int32_t sizeX = 0, sizeY = 0;
	int32_t boundsX = 0, boundsY = 0, boundsW = 0, boundsH = 0;
	bool hasBounds = false;
	struct Actor {
		std::string type;
		int32_t x = 0, y = 0;
		std::string owner;
	};
	std::vector<Actor> actors;

	// Players: 段（决定 owner → player 序号）
	struct PlayerRef {
		std::string name;
		std::string faction;
		bool nonCombatant = false;
		bool playable = false;
	};
	std::vector<PlayerRef> players;
};

std::string Trim(const std::string& s) {
	size_t a = 0, b = s.size();
	while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r')) ++a;
	while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' || s[b - 1] == '\n')) --b;
	return s.substr(a, b - a);
}

bool ParseMapYaml(const std::vector<uint8_t>& text, MapYaml& out) {
	std::string s(text.begin(), text.end());
	size_t pos = 0;
	std::string currentActorType;
	std::string section;
	while (pos < s.size()) {
		size_t eol = s.find('\n', pos);
		if (eol == std::string::npos) eol = s.size();
		const std::string raw = s.substr(pos, eol - pos);
		pos = eol + 1;
		const std::string line = Trim(raw);
		if (line.empty() || line[0] == '#') continue;

		// 顶层段：Tileset / MapSize / Bounds / Actors: / Players:
		if (raw.size() > 0 && raw[0] != '\t' && raw[0] != ' ') {
			if (line == "Actors:") { section = "Actors"; continue; }
			if (line == "Players:") { section = "Players"; continue; }
			section.clear();
			const size_t colon = line.find(':');
			if (colon == std::string::npos) continue;
			const std::string key = Trim(line.substr(0, colon));
			const std::string val = Trim(line.substr(colon + 1));
			if (key == "Tileset") {
				out.tileset = val;
			} else if (key == "MapSize") {
				std::sscanf(val.c_str(), "%d,%d", &out.sizeX, &out.sizeY);
			} else if (key == "Bounds") {
				if (std::sscanf(val.c_str(), "%d,%d,%d,%d",
						&out.boundsX, &out.boundsY, &out.boundsW, &out.boundsH) == 4) {
					out.hasBounds = true;
				}
			}
			continue;
		}

		if (section == "Players") {
			// PlayerReference@USSR: 开头新玩家；缩进两层的 Name:/NonCombatant:/Playable:/Faction:
			if (raw.size() >= 2 && raw[1] != '\t' && line.rfind("PlayerReference", 0) == 0) {
				out.players.push_back(MapYaml::PlayerRef{});
				continue;
			}
			const size_t colon = line.find(':');
			if (colon == std::string::npos || out.players.empty()) continue;
			const std::string key = Trim(line.substr(0, colon));
			const std::string val = Trim(line.substr(colon + 1));
			MapYaml::PlayerRef& p = out.players.back();
			if (key == "Name") p.name = val;
			else if (key == "Faction") p.faction = val;
			else if (key == "NonCombatant") p.nonCombatant = (val == "True" || val == "true");
			else if (key == "Playable") p.playable = (val == "True" || val == "true");
			continue;
		}

		if (section == "Actors") {
			// Actor0: t16
			if (raw.size() >= 2 && raw[1] != '\t') {
				const size_t colon = line.find(':');
				if (colon != std::string::npos && line.rfind("Actor", 0) == 0) {
					currentActorType = Trim(line.substr(colon + 1));
				}
				continue;
			}
			// 		Location: 66,46  /  Owner: Neutral
			const size_t colon = line.find(':');
			if (colon == std::string::npos) continue;
			const std::string key = Trim(line.substr(0, colon));
			const std::string val = Trim(line.substr(colon + 1));
			if (key == "Location") {
				MapYaml::Actor a;
				a.type = currentActorType;
				if (std::sscanf(val.c_str(), "%d,%d", &a.x, &a.y) == 2) out.actors.push_back(a);
			} else if (key == "Owner" && !out.actors.empty()) {
				out.actors.back().owner = val;
			}
		}
	}
	return out.sizeX > 0 && out.sizeY > 0;
}

// ---------------- map.bin → 我们的二进制 ----------------
bool ConvertMapBin(const std::vector<uint8_t>& bin, const char* outPath,
	int32_t& outW, int32_t& outH) {
	if (bin.size() < 17) return false;
	if (bin[0] != 2) {
		std::fprintf(stderr, "[oramap] only map.bin format 2 supported (got %u)\n", bin[0]);
		return false;
	}
	uint16_t w = 0, h = 0;
	std::memcpy(&w, &bin[1], 2);
	std::memcpy(&h, &bin[3], 2);
	if (w == 0 || h == 0) return false;
	uint32_t tilesOffset = 0, heightsOffset = 0, resourcesOffset = 0;
	std::memcpy(&tilesOffset, &bin[5], 4);
	std::memcpy(&heightsOffset, &bin[9], 4);
	std::memcpy(&resourcesOffset, &bin[13], 4);
	const uint32_t cells = static_cast<uint32_t>(w) * h;
	if (tilesOffset + cells * 3u > bin.size()) return false;

	std::FILE* f = std::fopen(outPath, "wb");
	if (f == nullptr) return false;
	uint8_t header[17] = {};
	header[0] = 2;
	std::memcpy(&header[1], &w, 2);
	std::memcpy(&header[3], &h, 2);
	const uint32_t outTiles = 17;
	const uint32_t outHeights = outTiles + cells * 3u;
	const uint32_t outResources = outHeights + cells;
	std::memcpy(&header[5], &outTiles, 4);
	std::memcpy(&header[9], &outHeights, 4);
	std::memcpy(&header[13], &outResources, 4);
	std::fwrite(header, 1, sizeof(header), f);

	// tiles：我们的 .map 是行优先 (y*W+x)，OpenRA 的 map.bin 是列优先 (x*H+y)
	// —— OpenRA Map.cs 写 tile 的循环是 `for (i=0..X) for (j=0..Y) Tiles[MPos(i,j)]`。
	// 之前这里直接 fwrite 原始字节，等于把整张地图转置了 90 度，所以怎么拼都不对。
	for (uint32_t y = 0; y < h; ++y) {
		for (uint32_t x = 0; x < w; ++x) {
			const size_t src = static_cast<size_t>(tilesOffset) +
				(static_cast<size_t>(x) * h + y) * 3u;
			std::fwrite(&bin[src], 1, 3, f);
		}
	}
	// heights（同样列优先 → 行优先）
	for (uint32_t y = 0; y < h; ++y) {
		for (uint32_t x = 0; x < w; ++x) {
			uint8_t hv = 0;
			const size_t si = static_cast<size_t>(x) * h + y;
			if (heightsOffset != 0 && heightsOffset + si < bin.size()) hv = bin[heightsOffset + si];
			std::fwrite(&hv, 1, 1, f);
		}
	}
	// resources（同样列优先 → 行优先）
	for (uint32_t y = 0; y < h; ++y) {
		for (uint32_t x = 0; x < w; ++x) {
			uint8_t rt = 0;
			const size_t si = static_cast<size_t>(x) * h + y;
			if (resourcesOffset != 0 && resourcesOffset + si < bin.size()) rt = bin[resourcesOffset + si];
			std::fwrite(&rt, 1, 1, f);
		}
	}
	std::fclose(f);
	outW = w;
	outH = h;
	return true;
}

} // namespace

int main(int argc, char** argv) {
	if (argc < 3) {
		std::fprintf(stderr, "usage: rts_oramap_import <input.oramap> <outBasePath>\n");
		return 1;
	}
	const char* inPath = argv[1];
	const std::string outBase = argv[2];

	std::vector<uint8_t> binData;
	std::vector<uint8_t> yamlData;
	bool haveYaml = false;

	const std::filesystem::path inPathObj(inPath);
	if (std::filesystem::is_directory(inPathObj)) {
		// 解压形式的地图目录（OpenRA 的 mods/ra/maps/<name>/ 就是这种）
		const std::string binPath = (inPathObj / "map.bin").string();
		const std::string yamlPath = (inPathObj / "map.yaml").string();
		if (!ReadWholeFile(binPath.c_str(), binData)) {
			std::fprintf(stderr, "[oramap] cannot read %s\n", binPath.c_str());
			return 1;
		}
		haveYaml = ReadWholeFile(yamlPath.c_str(), yamlData);
		std::printf("[oramap] directory mode: %s\n", inPath);
	} else {
		std::vector<uint8_t> zip;
		if (!ReadWholeFile(inPath, zip)) {
			std::fprintf(stderr, "[oramap] cannot read %s\n", inPath);
			return 1;
		}
		std::vector<ZipEntry> entries;
		if (!ParseZip(zip, entries)) {
			std::fprintf(stderr, "[oramap] not a zip (or corrupt central directory)\n");
			return 1;
		}
		std::printf("[oramap] %zu entries\n", entries.size());
		const ZipEntry* binEntry = nullptr;
		const ZipEntry* yamlEntry = nullptr;
		for (const ZipEntry& e : entries) {
			if (e.name == "map.bin") binEntry = &e;
			else if (e.name == "map.yaml") yamlEntry = &e;
		}
		if (binEntry == nullptr) {
			std::fprintf(stderr, "[oramap] no map.bin\n");
			return 1;
		}
		if (!ExtractEntry(zip, *binEntry, binData)) {
			std::fprintf(stderr, "[oramap] failed to extract map.bin\n");
			return 1;
		}
		if (yamlEntry != nullptr) haveYaml = ExtractEntry(zip, *yamlEntry, yamlData);
	}

	MapYaml meta;
	if (haveYaml) {
		if (!ParseMapYaml(yamlData, meta)) {
			std::fprintf(stderr, "[oramap] map.yaml parse failed (continuing)\n");
		} else {
			std::printf("[oramap] yaml: tileset=%s size=%dx%d bounds=%d,%d,%d,%d actors=%zu\n",
				meta.tileset.c_str(), meta.sizeX, meta.sizeY,
				meta.boundsX, meta.boundsY, meta.boundsW, meta.boundsH, meta.actors.size());
		}
	}

	const std::string mapPath = outBase + ".map";
	int32_t w = 0, h = 0;
	if (!ConvertMapBin(binData, mapPath.c_str(), w, h)) {
		std::fprintf(stderr, "[oramap] map.bin conversion failed\n");
		return 1;
	}
	std::printf("[oramap] wrote %s (%dx%d)\n", mapPath.c_str(), w, h);

	// meta
	const std::string metaPath = outBase + "_meta.txt";
	{
		std::FILE* f = std::fopen(metaPath.c_str(), "w");
		if (f != nullptr) {
			const int32_t bx = meta.hasBounds ? meta.boundsX : 0;
			const int32_t by = meta.hasBounds ? meta.boundsY : 0;
			const int32_t bw = meta.hasBounds ? meta.boundsW : w;
			const int32_t bh = meta.hasBounds ? meta.boundsH : h;
			std::fprintf(f, "RTSMETA 1\n");
			std::fprintf(f, "tileset %s\n", meta.tileset.c_str());
			std::fprintf(f, "bounds %d %d %d %d\n", bx, by, bw, bh);
			std::fclose(f);
			std::printf("[oramap] wrote %s\n", metaPath.c_str());
		}
	}

	// actors → RTSENT 2（名字 player x y）
	// owner → player 序号：按 Players 段顺序，跳过 NonCombatant（Neutral 是地图装饰）
	std::map<std::string, int> ownerToPlayer;
	{
		int next = 0;
		for (const auto& p : meta.players) {
			if (p.nonCombatant) {
				ownerToPlayer[p.name] = kNeutralPlayer;
			} else if (next < 2) { // 引擎当前只驱动 2 方
				ownerToPlayer[p.name] = next++;
			} else {
				ownerToPlayer[p.name] = kNeutralPlayer;
			}
		}
		std::printf("[oramap] players:");
		for (const auto& [name, idx] : ownerToPlayer) {
			std::printf(" %s->%d", name.c_str(), idx);
		}
		std::printf("\n");
	}

	const std::string actPath = outBase + "_actors.txt";
	{
		std::FILE* f = std::fopen(actPath.c_str(), "w");
		if (f != nullptr) {
			std::fprintf(f, "RTSENT 2\n");
			int written = 0, skipped = 0;
			for (const auto& a : meta.actors) {
				auto it = ownerToPlayer.find(a.owner);
				int player = (it == ownerToPlayer.end()) ? 0 : it->second;
				// 中立装饰（树/岩石/残骸）保留为 player 0，否则地图会丢掉大量视觉内容
				if (player == kNeutralPlayer) {
					player = 0;
					++skipped; // 统计的是"中立"数量，不是丢弃数量
				}
				std::fprintf(f, "%s %d %d %d\n", a.type.c_str(), player, a.x, a.y);
				++written;
			}
			std::fclose(f);
			std::printf("[oramap] wrote %s (%d actors, %d of them neutral->player0)\n",
				actPath.c_str(), written, skipped);
		}
	}
	return 0;
}
