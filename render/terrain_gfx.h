// RTS Engine — render/terrain_gfx.h
// 地形纹理：优先使用 C&C（RA）TMP 转出的地形图集（assets/terrain_atlas.bmp，24×24 tile），
// 缺失时回退到程序化生成（32×16）。
// 说明：TMP tile 是 24×24 画布（菱形 24×12），等距投影下渲染为 2×（48×48）覆盖相邻格以实现无缝。
#pragma once

#include <cstdint>
#include <string>
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
	// 图集 tile 尺寸（C&C TMP = 24×24；程序化回退时 32×16）
	static constexpr int kTileW = 24;
	static constexpr int kTileH = 24;
	// TMP 图集的 tile 数（拼接顺序：clear1[16], w1[1], sh01[20]）
	static constexpr int kTileCount = 37;

	// 程序化生成（回退用）：5 种 tile，32×16
	static std::vector<uint8_t> Build(int& outW, int& outH);

	// 加载地形图集（BMP/PNG）。成功返回 true，之后 GetUV 用图集 tile。
	static bool Load(const std::string& path, std::vector<uint8_t>& outPixels, int& outW, int& outH);
	static bool HasAtlas();

	// 地形种类 → tile 索引（TMP 图集区间）：
	//   Grass   : 0-15（clear1 的 16 个变体，按格位置散列）
	//   Water   : 16（w1）
	//   Rock    : 17-36（sh01/sh02 变体）
	//   Sand    : 0（复用 clear1）
	//   Highland: 17（复用 sh01）
	static int TileFor(TerrainKind kind, int32_t x, int32_t y) {
		const int variant = static_cast<int>((static_cast<uint32_t>(x) * 7u + static_cast<uint32_t>(y) * 13u) % 16u);
		switch (kind) {
			case TerrainKind::Grass:    return variant;
			case TerrainKind::Water:    return 16;
			case TerrainKind::Rock:     return 17 + (variant % 20);
			case TerrainKind::Sand:     return variant;
			case TerrainKind::Highland: return 17 + (variant % 20);
			default:                    return 0;
		}
	}

	// UV（[0,1]）：有图集时按 tile；无图集时返回 0..1（程序化纹理整张）
	static void GetUV(TerrainKind kind, float& u0, float& v0, float& u1, float& v1);
	static void GetUVAt(int tile, float& u0, float& v0, float& u1, float& v1);
};

} // namespace rts
