// RTS Engine — sim/map.cpp
#include "map.h"
#include <fstream>
#include <sstream>
#include <string>

namespace rts {

Map::Map(int32_t width, int32_t height, uint8_t fillHeight, uint8_t fillFlags)
	: m_width(width)
	, m_height(height)
	, m_heightLayer(width, height, fillHeight)
	, m_flags(width, height, fillFlags)
	, m_fog(width, height, 255)
	, m_resources(width, height, 0) {}

bool Map::LoadFromText(const std::string& path) {
	std::ifstream in(path);
	if (!in) return false;

	std::string magic;
	int version = 0;
	in >> magic >> version;
	if (magic != "RTSMAP" || version != 1) return false;

	int32_t w = 0, h = 0;
	in >> w >> h;
	if (w <= 0 || h <= 0) return false;

	Map loaded(w, h, 0, 0);
	for (int32_t y = 0; y < h; ++y) {
		for (int32_t x = 0; x < w; ++x) {
			int hv = 0, fv = 0, gv = 255;
			if (!(in >> hv >> fv >> gv)) return false;
			loaded.SetTerrainHeight(CellCoord(x, y), static_cast<uint8_t>(hv));
			loaded.SetFlags(CellCoord(x, y), static_cast<uint8_t>(fv));
			loaded.SetFog(CellCoord(x, y), static_cast<uint8_t>(gv));
		}
	}
	*this = loaded;
	return true;
}

bool Map::SaveToText(const std::string& path) const {
	std::ofstream out(path);
	if (!out) return false;
	out << "RTSMAP 1\n";
	out << m_width << " " << m_height << "\n";
	for (int32_t y = 0; y < m_height; ++y) {
		for (int32_t x = 0; x < m_width; ++x) {
			const CellCoord c(x, y);
			out << static_cast<int>(TerrainHeight(c)) << " "
				<< static_cast<int>(Flags(c)) << " "
				<< static_cast<int>(Fog(c)) << "\n";
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
			if (r < 8) {
				// 高地
				map.SetTerrainHeight(c, static_cast<uint8_t>(1 + rng.Range(2)));
			} else if (r < 12) {
				// 水域障碍（不可通行/不可建造）
				map.SetFlags(c, kWater);
				map.SetTerrainHeight(c, 0);
			} else if (r < 15) {
				// 岩石障碍（不可通行但可建造）
				map.SetFlags(c, kBuildable);
			} else if (r < 18) {
				// 矿点
				map.SetResourceAmount(c, 500 + static_cast<int32_t>(rng.Range(500)));
			}
		}
	}

	// 边缘一圈设为不可通行（地图边界）
	for (int32_t x = 0; x < width; ++x) {
		map.SetFlags(CellCoord(x, 0), kWater);
		map.SetFlags(CellCoord(x, height - 1), kWater);
	}
	for (int32_t y = 0; y < height; ++y) {
		map.SetFlags(CellCoord(0, y), kWater);
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
	mix(m_heightLayer.Hash(h));
	mix(m_flags.Hash(h));
	mix(m_fog.Hash(h));
	mix(static_cast<uint64_t>(m_width));
	mix(static_cast<uint64_t>(m_height));
	return h;
}

} // namespace rts
