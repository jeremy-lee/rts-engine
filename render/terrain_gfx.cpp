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

	// 菱形 tile（OpenRA 等距地形：|dx|/(W/2) + |dy|/(H/2) <= 1）
	static bool InDiamond(int x, int y) {
		const float dx = (x < TerrainGfx::kTileW / 2)
			? static_cast<float>(TerrainGfx::kTileW / 2 - x)
			: static_cast<float>(x - TerrainGfx::kTileW / 2);
		const float dy = (y < TerrainGfx::kTileH / 2)
			? static_cast<float>(TerrainGfx::kTileH / 2 - y)
			: static_cast<float>(y - TerrainGfx::kTileH / 2);
		return (dx / (TerrainGfx::kTileW / 2.0f)) + (dy / (TerrainGfx::kTileH / 2.0f)) <= 1.0f;
	}

	void Put(int x, int y, uint32_t c) {
		if (x < 0 || y < 0 || x >= TerrainGfx::kTileW || y >= TerrainGfx::kTileH) return;
		if (!InDiamond(x, y)) return;
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
	c.Fill(Rgb(58, 88, 42));
	Noise n;
	// 低对比噪点（±4），避免菱形/矩形边界可见
	for (int i = 0; i < 320; ++i) {
		const int x = n.Range(TerrainGfx::kTileW);
		const int y = n.Range(TerrainGfx::kTileH);
		const int s = n.Range(3);
		c.Put(x, y, s == 0 ? Rgb(61, 91, 44) : s == 1 ? Rgb(55, 85, 40) : Rgb(59, 89, 43));
	}
}

void DrawWater(Canvas& c) {
	c.Fill(Rgb(26, 56, 104));
	Noise n;
	for (int y = 2; y < TerrainGfx::kTileH; y += 5) {
		for (int x = 0; x < TerrainGfx::kTileW; ++x) {
			if ((x + y) % 9 < 3) c.Put(x, y, Rgb(40, 78, 136));
		}
	}
	for (int i = 0; i < 40; ++i) {
		c.Put(n.Range(TerrainGfx::kTileW), n.Range(TerrainGfx::kTileH), Rgb(56, 98, 158));
	}
}

void DrawRock(Canvas& c) {
	c.Fill(Rgb(78, 78, 80));
	Noise n;
	for (int i = 0; i < 160; ++i) {
		const int x = n.Range(TerrainGfx::kTileW);
		const int y = n.Range(TerrainGfx::kTileH);
		const int s = n.Range(3);
		c.Put(x, y, s == 0 ? Rgb(62, 62, 64) : s == 1 ? Rgb(94, 94, 96) : Rgb(70, 70, 72));
	}
	// 裂纹
	for (int x = 10; x < 40; x += 6) c.Put(x, 8 + x / 8, Rgb(50, 50, 52));
	for (int x = 20; x < 56; x += 8) c.Put(x, 22 - x / 10, Rgb(52, 52, 54));
}

void DrawSand(Canvas& c) {
	c.Fill(Rgb(166, 140, 92));
	Noise n;
	for (int i = 0; i < 200; ++i) {
		const int x = n.Range(TerrainGfx::kTileW);
		const int y = n.Range(TerrainGfx::kTileH);
		const int s = n.Range(3);
		c.Put(x, y, s == 0 ? Rgb(150, 126, 82) : s == 1 ? Rgb(182, 156, 104) : Rgb(172, 146, 98));
	}
}

void DrawHighland(Canvas& c) {
	c.Fill(Rgb(74, 120, 58));
	Noise n;
	for (int i = 0; i < 180; ++i) {
		const int x = n.Range(TerrainGfx::kTileW);
		const int y = n.Range(TerrainGfx::kTileH);
		const int s = n.Range(3);
		c.Put(x, y, s == 0 ? Rgb(60, 102, 48) : s == 1 ? Rgb(88, 140, 68) : Rgb(68, 112, 54));
	}
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
	u0 = static_cast<float>(idx * kTileW) / texW;
	v0 = 0.0f;
	u1 = static_cast<float>((idx + 1) * kTileW) / texW;
	v1 = 1.0f;
}

} // namespace rts
