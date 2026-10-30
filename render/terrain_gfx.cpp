// RTS Engine — render/terrain_gfx.cpp
#include "terrain_gfx.h"

#include "stb_image.h"  // 实现（STB_IMAGE_IMPLEMENTATION）在 cnc_assets.cpp

namespace rts {

namespace {

struct Noise {
	uint32_t s = 0x9E3779B9u;
	uint32_t Next() {
		s ^= s << 13;
		s ^= s >> 17;
		s ^= s << 5;
		return s;
	}
	int Range(int n) { return static_cast<int>(Next() % static_cast<uint32_t>(n)); }
};

uint32_t Rgb(int r, int g, int b, int a = 255) {
	return (static_cast<uint32_t>(a) << 24) | (static_cast<uint32_t>(r) << 16) |
		(static_cast<uint32_t>(g) << 8) | static_cast<uint32_t>(b);
}

// 程序化回退用的 tile 尺寸（注意：与 TerrainGfx::kTileW/kTileH 是两回事——
// 后者是 TMP 图集的 24×24；曾经混用导致 Put 按 24 行写、缓冲区只有 16 行 → 堆越界）。
constexpr int kProceduralW = 32;
constexpr int kProceduralH = 16;

struct Canvas {
	uint8_t* data = nullptr;
	int w = 0;
	int ox = 0;
	int tileW = kProceduralW;
	int tileH = kProceduralH;

	void Put(int x, int y, uint32_t c) {
		if (x < 0 || y < 0 || x >= tileW || y >= tileH) return;
		const size_t o = (static_cast<size_t>(y) * w + ox + x) * 4;
		data[o + 0] = static_cast<uint8_t>((c >> 16) & 0xFF);
		data[o + 1] = static_cast<uint8_t>((c >> 8) & 0xFF);
		data[o + 2] = static_cast<uint8_t>(c & 0xFF);
		data[o + 3] = static_cast<uint8_t>((c >> 24) & 0xFF);
	}

	void Fill(uint32_t c) {
		for (int y = 0; y < tileH; ++y)
			for (int x = 0; x < tileW; ++x) Put(x, y, c);
	}
};

void DrawGrass(Canvas& c) {
	// 纯色（无噪点）：每格独立噪点会在 tile 边界产生可见接缝，纯色保证无缝
	c.Fill(Rgb(58, 88, 42));
}

void DrawWater(Canvas& c) {
	// 纯色（无缝）：tile 独立噪点会在边界留接缝
	c.Fill(Rgb(26, 56, 104));
}

void DrawRock(Canvas& c) {
	c.Fill(Rgb(78, 78, 80));
}

void DrawSand(Canvas& c) {
	c.Fill(Rgb(166, 140, 92));
}

void DrawHighland(Canvas& c) {
	c.Fill(Rgb(74, 120, 58));
}

} // namespace

std::vector<uint8_t> TerrainGfx::Build(int& outW, int& outH) {
	outW = 5 * kProceduralW;
	outH = kProceduralH;
	std::vector<uint8_t> pixels(static_cast<size_t>(outW) * outH * 4, 0);

	auto draw = [&](TerrainKind kind, void (*fn)(Canvas&)) {
		Canvas c;
		c.data = pixels.data();
		c.w = outW;
		c.ox = static_cast<int>(kind) * kProceduralW;
		c.tileW = kProceduralW;
		c.tileH = kProceduralH;
		fn(c);
	};

	draw(TerrainKind::Grass, DrawGrass);
	draw(TerrainKind::Water, DrawWater);
	draw(TerrainKind::Rock, DrawRock);
	draw(TerrainKind::Sand, DrawSand);
	draw(TerrainKind::Highland, DrawHighland);
	return pixels;
}

namespace {

// 地形图集状态（加载后生效）
struct AtlasState {
	bool loaded = false;
	int width = 0;
	int height = 0;
};

AtlasState g_atlas;

} // namespace

bool TerrainGfx::Load(const std::string& path, std::vector<uint8_t>& outPixels, int& outW, int& outH) {

	int w = 0, h = 0, ch = 0;
	stbi_uc* data = stbi_load(path.c_str(), &w, &h, &ch, 4);

	if (data == nullptr) return false;
	outPixels.assign(data, data + static_cast<size_t>(w) * h * 4);
	stbi_image_free(data);
	outW = w;
	outH = h;
	g_atlas.loaded = true;
	g_atlas.width = w;
	g_atlas.height = h;
	return true;
}

bool TerrainGfx::HasAtlas() { return g_atlas.loaded; }



void TerrainGfx::GetUVAt(int tile, float& u0, float& v0, float& u1, float& v1) {
	const float texW = static_cast<float>(g_atlas.width);
	const float texH = static_cast<float>(g_atlas.height);
	if (!g_atlas.loaded || texW <= 0.0f || texH <= 0.0f) {
		// 无图集：退化为程序化纹理（tile 索引按 kind 区间取模）
		GetUV(static_cast<TerrainKind>(tile % 5), u0, v0, u1, v1);
		return;
	}
	// OpenRA 语义：TMP tile 的 Offset = 0、Size = 整个画布，TerrainSpriteLayer 直接画满幅
	// （xyz = Screen3DPosition(cell) + Offset - 0.5*Size）。所以这里也取满幅 UV，不要裁菱形。
	const float inset = 1.0f / 128.0f;
	const float x0 = static_cast<float>(tile * kTileW);
	u0 = (x0 + inset) / texW;
	v0 = inset / texH;
	u1 = (x0 + static_cast<float>(kTileW) - inset) / texW;
	v1 = (texH - inset) / texH;
}

void TerrainGfx::GetUV(TerrainKind kind, float& u0, float& v0, float& u1, float& v1) {
	if (g_atlas.loaded) {
		GetUVAt(TileFor(kind, 0, 0), u0, v0, u1, v1);
		return;
	}
	// 回退：程序化纹理（5 种 tile，32×16）
	const int idx = static_cast<int>(kind);
	const float texW = static_cast<float>(5 * kProceduralW);
	const float texH = static_cast<float>(kProceduralH);
	const float inset = 1.0f / 128.0f;
	u0 = (static_cast<float>(idx * kProceduralW) + inset) / texW;
	v0 = inset / texH;
	u1 = (static_cast<float>((idx + 1) * kProceduralW) - inset) / texW;
	v1 = (texH - inset) / texH;
}

} // namespace rts
