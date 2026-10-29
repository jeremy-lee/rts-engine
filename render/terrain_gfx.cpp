// RTS Engine — render/terrain_gfx.cpp
#include "terrain_gfx.h"

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

struct Canvas {
	uint8_t* data = nullptr;
	int w = 0;
	int ox = 0;

	// 矩形 tile（无缝铺贴）：等距投影下相邻格中心相距 (32,16)，矩形 64×32 重叠但同色不可见。
	// 注：OpenRA 用 TMP 菱形模板铺满；我们无 TMP，菱形会留黑三角缝隙，故用矩形。
	void Put(int x, int y, uint32_t c) {
		if (x < 0 || y < 0 || x >= TerrainGfx::kTileW || y >= TerrainGfx::kTileH) return;
		const size_t o = (static_cast<size_t>(y) * w + ox + x) * 4;
		data[o + 0] = static_cast<uint8_t>((c >> 16) & 0xFF);
		data[o + 1] = static_cast<uint8_t>((c >> 8) & 0xFF);
		data[o + 2] = static_cast<uint8_t>(c & 0xFF);
		data[o + 3] = static_cast<uint8_t>((c >> 24) & 0xFF);
	}

	void Fill(uint32_t c) {
		for (int y = 0; y < TerrainGfx::kTileH; ++y)
			for (int x = 0; x < TerrainGfx::kTileW; ++x) Put(x, y, c);
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
	outW = kCols * kTileW;
	outH = kTileH;
	std::vector<uint8_t> pixels(static_cast<size_t>(outW) * outH * 4, 0);

	auto draw = [&](TerrainKind kind, void (*fn)(Canvas&)) {
		Canvas c;
		c.data = pixels.data();
		c.w = outW;
		c.ox = static_cast<int>(kind) * kTileW;
		fn(c);
	};

	draw(TerrainKind::Grass, DrawGrass);
	draw(TerrainKind::Water, DrawWater);
	draw(TerrainKind::Rock, DrawRock);
	draw(TerrainKind::Sand, DrawSand);
	draw(TerrainKind::Highland, DrawHighland);
	return pixels;
}

void TerrainGfx::GetUV(TerrainKind kind, float& u0, float& v0, float& u1, float& v1) {
	const int idx = static_cast<int>(kind);
	const float texW = static_cast<float>(kCols * kTileW);
	const float texH = static_cast<float>(kTileH);
	// inset 1/128 像素（OpenRA Sprite 语义）：避免 GL 在 tile 边界采样到相邻 tile 的像素（黑边）
	const float inset = 1.0f / 128.0f;
	u0 = (static_cast<float>(idx * kTileW) + inset) / texW;
	v0 = inset / texH;
	u1 = (static_cast<float>((idx + 1) * kTileW) - inset) / texW;
	v1 = (texH - inset) / texH;
}

} // namespace rts
