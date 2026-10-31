// RTS Engine — sim/map.cpp
#include "map.h"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>
#include <sstream>
#include <string>

namespace rts {

Map::Map(int32_t width, int32_t height, uint8_t fillHeight, uint8_t fillFlags)
	: m_width(width)
	, m_height(height)
	, m_tiles(width, height, 0xFF00u) // 默认 (255, 0) = clear1 草地
	, m_heightLayer(width, height, fillHeight)
	, m_flags(width, height, fillFlags)
	, m_fog(width, height, 0)
	, m_resTypes(width, height, 0)
	, m_resources(width, height, 0)
	, m_occupancy(width, height, 0u) {
	// 默认可玩区 = 全图
	m_bounds.x = 0;
	m_bounds.y = 0;
	m_bounds.w = width;
	m_bounds.h = height;
}

// ---- 二进制地图（与 OpenRA map.bin 同构）----
bool Map::SaveBinary(const std::string& path) const {
	std::FILE* f = std::fopen(path.c_str(), "wb");
	if (f == nullptr) return false;
	const uint32_t cells = static_cast<uint32_t>(m_width) * static_cast<uint32_t>(m_height);
	uint8_t header[17] = {};
	header[0] = 2; // format
	std::memcpy(&header[1], &m_width, 2);
	std::memcpy(&header[3], &m_height, 2);
	const uint32_t tilesOffset = 17;
	const uint32_t heightsOffset = tilesOffset + cells * 3;
	const uint32_t resourcesOffset = heightsOffset + cells;
	std::memcpy(&header[5], &tilesOffset, 4);
	std::memcpy(&header[9], &heightsOffset, 4);
	std::memcpy(&header[13], &resourcesOffset, 4);
	std::fwrite(header, 1, sizeof(header), f);
	for (int32_t y = 0; y < m_height; ++y) {
		for (int32_t x = 0; x < m_width; ++x) {
			const TileRef t = Tile(CellCoord(x, y));
			std::fwrite(&t.templateId, 1, 2, f);
			std::fwrite(&t.index, 1, 1, f);
		}
	}
	for (int32_t y = 0; y < m_height; ++y) {
		for (int32_t x = 0; x < m_width; ++x) {
			const uint8_t h = TerrainHeight(CellCoord(x, y));
			std::fwrite(&h, 1, 1, f);
		}
	}
	for (int32_t y = 0; y < m_height; ++y) {
		for (int32_t x = 0; x < m_width; ++x) {
			const uint8_t type = ResourceType(CellCoord(x, y));
			std::fwrite(&type, 1, 1, f);
		}
	}
	std::fclose(f);
	return true;
}

bool Map::SaveMeta(const std::string& path) const {
	std::ofstream out(path);
	if (!out) return false;
	out << "RTSMETA 1\n";
	out << "tileset " << m_tileset << "\n";
	out << "bounds " << m_bounds.x << " " << m_bounds.y << " "
		<< m_bounds.w << " " << m_bounds.h << "\n";
	return out.good();
}

bool Map::LoadMeta(const std::string& path) {
	std::ifstream in(path);
	if (!in) return false;
	std::string magic;
	int version = 0;
	in >> magic >> version;
	if (magic != "RTSMETA" || version != 1) return false;
	std::string key;
	while (in >> key) {
		if (key == "tileset") {
			std::string name;
			if (in >> name) m_tileset = name;
		} else if (key == "bounds") {
			in >> m_bounds.x >> m_bounds.y >> m_bounds.w >> m_bounds.h;
		}
	}
	return true;
}

bool Map::LoadBinary(const std::string& path) {
	std::FILE* f = std::fopen(path.c_str(), "rb");
	if (f == nullptr) return false;
	std::fseek(f, 0, SEEK_END);
	const long size = std::ftell(f);
	std::fseek(f, 0, SEEK_SET);
	std::vector<uint8_t> buf(static_cast<size_t>(size < 0 ? 0 : size));
	if (buf.size() < 17) { std::fclose(f); return false; }
	if (std::fread(buf.data(), 1, buf.size(), f) != buf.size()) { std::fclose(f); return false; }
	std::fclose(f);
	if (buf[0] != 2) return false;
	uint16_t w = 0, h = 0;
	std::memcpy(&w, &buf[1], 2);
	std::memcpy(&h, &buf[3], 2);
	if (w == 0 || h == 0) return false;
	const uint32_t cells = static_cast<uint32_t>(w) * h;
	if (buf.size() < 17 + cells * 5) return false;
	uint32_t tilesOffset = 0, heightsOffset = 0, resourcesOffset = 0;
	std::memcpy(&tilesOffset, &buf[5], 4);
	std::memcpy(&heightsOffset, &buf[9], 4);
	std::memcpy(&resourcesOffset, &buf[13], 4);
	if (tilesOffset + cells * 3 > buf.size()) return false;

	Map loaded(w, h, 0, kWalkable | kBuildable);
	for (int32_t y = 0; y < h; ++y) {
		for (int32_t x = 0; x < w; ++x) {
			const size_t o = tilesOffset + (static_cast<size_t>(y) * w + x) * 3;
			uint16_t tid = 0;
			std::memcpy(&tid, &buf[o], 2);
			loaded.SetTile(CellCoord(x, y), tid, buf[o + 2]);
		}
	}
	if (heightsOffset != 0 && heightsOffset + cells <= buf.size()) {
		for (int32_t y = 0; y < h; ++y) {
			for (int32_t x = 0; x < w; ++x) {
				loaded.SetTerrainHeight(CellCoord(x, y),
					buf[heightsOffset + static_cast<size_t>(y) * w + x]);
			}
		}
	}
	if (resourcesOffset != 0 && resourcesOffset + cells <= buf.size()) {
		for (int32_t y = 0; y < h; ++y) {
			for (int32_t x = 0; x < w; ++x) {
				loaded.SetResourceType(CellCoord(x, y),
					buf[resourcesOffset + static_cast<size_t>(y) * w + x]);
			}
		}
	}
	*this = loaded;
	return true;
}

bool Map::LoadFromText(const std::string& path) {
	std::ifstream in(path);
	if (!in) return false;

	std::string magic;
	int version = 0;
	in >> magic >> version;
	if (magic != "RTSMAP" || (version != 1 && version != 2)) return false;

	int32_t w = 0, h = 0;
	in >> w >> h;
	if (w <= 0 || h <= 0) return false;

	Map loaded(w, h, 0, 0);
	for (int32_t y = 0; y < h; ++y) {
		for (int32_t x = 0; x < w; ++x) {
			const CellCoord c(x, y);
			if (version >= 2) {
				int tid = 255, idx = 0, hv = 0, fv = 0, gv = 255, rt = 0;
				if (!(in >> tid >> idx >> hv >> fv >> gv)) return false;
				loaded.SetTile(c, static_cast<uint16_t>(tid), static_cast<uint8_t>(idx));
				loaded.SetTerrainHeight(c, static_cast<uint8_t>(hv));
				loaded.SetFlags(c, static_cast<uint8_t>(fv));
				loaded.SetFog(c, static_cast<uint8_t>(gv));
				// resourceType 是可选的第 6 个字段（旧 v2 文件没有）
				if (in >> rt) loaded.SetResourceType(c, static_cast<uint8_t>(rt));
			} else {
				int hv = 0, fv = 0, gv = 255;
				if (!(in >> hv >> fv >> gv)) return false;
				loaded.SetTerrainHeight(c, static_cast<uint8_t>(hv));
				loaded.SetFlags(c, static_cast<uint8_t>(fv));
				loaded.SetFog(c, static_cast<uint8_t>(gv));
			}
		}
	}
	*this = loaded;
	return true;
}

bool Map::SaveToText(const std::string& path) const {
	std::ofstream out(path);
	if (!out) return false;
	out << "RTSMAP 2\n";
	out << m_width << " " << m_height << "\n";
	for (int32_t y = 0; y < m_height; ++y) {
		for (int32_t x = 0; x < m_width; ++x) {
			const CellCoord c(x, y);
			const TileRef t = Tile(c);
			out << static_cast<int>(t.templateId) << " "
				<< static_cast<int>(t.index) << " "
				<< static_cast<int>(TerrainHeight(c)) << " "
				<< static_cast<int>(Flags(c)) << " "
				<< static_cast<int>(Fog(c)) << " "
				<< static_cast<int>(ResourceType(c)) << "\n";
		}
	}
	return out.good();
}

Map Map::GenerateTestMap(int32_t width, int32_t height, uint64_t seed) {
	Map map(width, height, 0, kWalkable | kBuildable);
	Rng rng(seed);

	// 生成：平地为主，随机高地与水域障碍
	for (int32_t y = 0; y < height; ++y) {
		for (int32_t x = 0; x < width; ++x) {
			const CellCoord c(x, y);
			const uint64_t r = rng.Range(100);
			// 模板 id 来自 assets/tileset.txt（RA temperat）：
			//   255 = clear1（Clear，16 变体）、1 = w1（Water）、103..106 = p01..p04（Rock）
			if (r < 8) {
				// 高地（仍是草地，抬升一级）
				map.SetTile(c, 255, static_cast<uint8_t>(rng.Range(16)));
				// 高度只能是 0 或 1：kHeightStep=12 是半格，h=2 会整体上移一整格
				// (2*12=24=kStepY)，导致高地 tile 完全叠住上方格子、自身位置空出来
				// 露出清屏色（就是"黑块"）。
				map.SetTerrainHeight(c, static_cast<uint8_t>(1));
			} else if (r < 12) {
				// 水域障碍（不可通行/不可建造）
				map.SetTile(c, 1, 0);
				map.SetFlags(c, kWater);
				map.SetTerrainHeight(c, 0);
			} else if (r < 15) {
				// 岩石障碍（不可通行但可建造）
				map.SetTile(c, static_cast<uint16_t>(103 + rng.Range(4)), 0);
				map.SetFlags(c, kBuildable);
			} else if (r < 18) {
				// 矿点
				map.SetResourceAmount(c, 500 + static_cast<int32_t>(rng.Range(500)));
			} else {
				// 草地：随机挑一个 clear1 变体（OpenRA 里 index 就是变体号）
				map.SetTile(c, 255, static_cast<uint8_t>(rng.Range(16)));
			}
		}
	}

	// 边缘一圈设为不可通行（地图边界）
	for (int32_t x = 0; x < width; ++x) {
		map.SetTile(CellCoord(x, 0), 1, 0);
		map.SetFlags(CellCoord(x, 0), kWater);
		map.SetTile(CellCoord(x, height - 1), 1, 0);
		map.SetFlags(CellCoord(x, height - 1), kWater);
	}
	for (int32_t y = 0; y < height; ++y) {
		map.SetTile(CellCoord(0, y), 1, 0);
		map.SetFlags(CellCoord(0, y), kWater);
		map.SetTile(CellCoord(width - 1, y), 1, 0);
		map.SetFlags(CellCoord(width - 1, y), kWater);
	}
	return map;
}

uint64_t Map::Hash() const {
	uint64_t h = 1469598103934665603ull;
	auto mix = [&h](uint64_t v) {
		h ^= v;
		h *= 1099511628211ull;
	};
	mix(m_tiles.Hash(h));
	mix(m_resTypes.Hash(h));
	mix(m_heightLayer.Hash(h));
	mix(m_flags.Hash(h));
	mix(m_fog.Hash(h));
	mix(m_occupancy.Hash(h));
	mix(static_cast<uint64_t>(m_width));
	mix(static_cast<uint64_t>(m_height));
	return h;
}

} // namespace rts
