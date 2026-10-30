// RTS Engine — render/terrain_gfx.h
// 地形纹理：优先使用 C&C（RA）TMP 转出的地形图集（assets/terrain_atlas.png，24×24 tile），
// 缺失时回退到程序化生成（32×16 纯色）。
//
// 几何关系（等距 2:1）：
//   TMP tile 画布 24×24，菱形 24×12 居中。
//   我们的格间距是 (32,16)，所以菱形必须放大 4/3 到 32×16 才能边对边铺满：
//   画布 24×24 → 渲染 32×32，菱形在框内 y 8..24（中心对齐）。
//   因此 kTileW/kTileH 是「图集里的 UV 采样尺寸」，kRenderW/kRenderH 是「屏幕上的绘制尺寸」。
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
	// 图集里每个 tile 的采样尺寸（C&C TMP = 24×24）
	static constexpr int kTileW = 24;
	static constexpr int kTileH = 24;
	// TMP tile 的有效菱形区域：画布 24×24 内居中 24×12（y 6..18）。
	// 只采样这块 → 渲染 32×16（放大 4/3）= 格间距，边对边无缝。
	static constexpr int kContentY0 = 6;
	static constexpr int kContentH = 12;
	// 屏幕绘制尺寸 = 64×32（菱形 2× 格间距）。
	// 等距网格里相邻格中心相距 (32,16)，只有 64×32 的菱形能边对边铺满；
	// 32×16 的矩形在相邻格之间只角点相接 → 会留出棋盘状黑缝。
	static constexpr int kRenderW = 64;
	static constexpr int kRenderH = 32;
	// 程序化回退纹理的 tile 尺寸（32×16）
	static constexpr int kProceduralW = 32;
	static constexpr int kProceduralH = 16;
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
	// 图集布局（生成的 terrain_atlas.png，37 × 24×24 tile）：
	//   0-15  clear1（草地，16 变体，全部有内容）
	//   16-35 sh01（峭壁，其中 16/17/18/20/30/31/33/34/35 是空 tile → 必须跳过）
	//   36    w1（水）
	static int TileFor(TerrainKind kind, int32_t x, int32_t y) {
		const int variant = static_cast<int>((static_cast<uint32_t>(x) * 7u + static_cast<uint32_t>(y) * 13u) % 16u);
		// sh01 里有内容的 tile（已扫描确认）
		static constexpr int kCliffTiles[] = {19, 21, 22, 23, 24, 25, 26, 27, 28, 29, 32};
		constexpr int kCliffCount = static_cast<int>(sizeof(kCliffTiles) / sizeof(kCliffTiles[0]));
		switch (kind) {
			case TerrainKind::Grass:    return variant;                              // clear1 0-15
			case TerrainKind::Water:    return 36;                                   // w1
			case TerrainKind::Sand:     return variant;                              // 复用 clear1（RA 无独立沙 tile）
			case TerrainKind::Rock:     return kCliffTiles[variant % kCliffCount];   // sh01 有内容的
			case TerrainKind::Highland: return variant;                              // clear1（高地用高度偏移表现）
			default:                    return 0;
		}
	}

	// UV（[0,1]）：有图集时按 tile；无图集时返回 0..1（程序化纹理整张）
	static void GetUV(TerrainKind kind, float& u0, float& v0, float& u1, float& v1);
	static void GetUVAt(int tile, float& u0, float& v0, float& u1, float& v1);
};

} // namespace rts
