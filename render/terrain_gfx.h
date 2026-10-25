// RTS Engine — render/terrain_gfx.h
// 程序化地形纹理（64×32，与矩形网格 1:1，无拉伸变形）。
// 说明：C&C 的 TMP 地形是菱形 tile，与矩形网格不匹配（会拉伸成横条），
//       因此地形改用程序化生成的 2:1 纹理；单位/建筑仍用 RA 素材。
#pragma once

#include <cstdint>
#include <vector>

namespace rts {

enum class TerrainKind : uint8_t {
	Grass = 0,
	Water = 1,
	Rock = 2,
	Sand = 3,
	Highland = 4,
	Count = 5,
};

class TerrainGfx {
public:
	static constexpr int kTileW = 64;
	static constexpr int kTileH = 32;
	static constexpr int kCols = 5;

	static std::vector<uint8_t> Build(int& outW, int& outH);
	static void GetUV(TerrainKind kind, float& u0, float& v0, float& u1, float& v1);
};

} // namespace rts
