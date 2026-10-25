// RTS Engine — render/assets.cpp
// 程序化像素艺术图集：全部由代码绘制，无外部资产。
#include "assets.h"

#include <cmath>
#include <cstdlib>

namespace rts {

namespace {

// 确定性伪随机（纹理噪点）
struct Noise {
	uint32_t s = 0x12345678u;
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
	int ox = 0, oy = 0;

	void Put(int x, int y, uint32_t argb) {
		if (x < 0 || y < 0 || x >= AssetAtlas::kCell || y >= AssetAtlas::kCell) return;
		const int px = ox + x;
		const int py = oy + y;
		const size_t o = (static_cast<size_t>(py) * w + px) * 4;
		const int a = static_cast<int>((argb >> 24) & 0xFF);
		const int r = static_cast<int>((argb >> 16) & 0xFF);
		const int g = static_cast<int>((argb >> 8) & 0xFF);
		const int b = static_cast<int>(argb & 0xFF);
		if (a >= 255) {
			data[o + 0] = static_cast<uint8_t>(r);
			data[o + 1] = static_cast<uint8_t>(g);
			data[o + 2] = static_cast<uint8_t>(b);
			data[o + 3] = 255;
		} else if (a > 0) {
			const int inv = 255 - a;
			data[o + 0] = static_cast<uint8_t>((r * a + data[o + 0] * inv) / 255);
			data[o + 1] = static_cast<uint8_t>((g * a + data[o + 1] * inv) / 255);
			data[o + 2] = static_cast<uint8_t>((b * a + data[o + 2] * inv) / 255);
			data[o + 3] = static_cast<uint8_t>(data[o + 3] + (255 - data[o + 3]) * a / 255);
		}
	}

	void Fill(uint32_t argb) {
		for (int y = 0; y < AssetAtlas::kCell; ++y)
			for (int x = 0; x < AssetAtlas::kCell; ++x) Put(x, y, argb);
	}

	void Rect(int x, int y, int rw, int rh, uint32_t argb) {
		for (int j = 0; j < rh; ++j)
			for (int i = 0; i < rw; ++i) Put(x + i, y + j, argb);
	}

	void Circle(int cx, int cy, int r, uint32_t argb) {
		for (int y = -r; y <= r; ++y)
			for (int x = -r; x <= r; ++x)
				if (x * x + y * y <= r * r) Put(cx + x, cy + y, argb);
	}

	void Line(int x0, int y0, int x1, int y1, uint32_t argb) {
		int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
		int dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
		int err = dx + dy;
		for (;;) {
			Put(x0, y0, argb);
			if (x0 == x1 && y0 == y1) break;
			const int e2 = 2 * err;
			if (e2 >= dy) { err += dy; x0 += sx; }
			if (e2 <= dx) { err += dx; y0 += sy; }
		}
	}
};

// ---- 各地形/单位绘制 ----
void DrawGrass(Canvas& c) {
	c.Fill(Rgb(58, 90, 42));
	Noise n;
	for (int i = 0; i < 90; ++i) {
		const int x = n.Range(32), y = n.Range(32);
		const int shade = n.Range(3);
		const uint32_t col = (shade == 0) ? Rgb(70, 108, 50) : (shade == 1) ? Rgb(48, 76, 34) : Rgb(62, 96, 44);
		c.Put(x, y, col);
	}
	for (int i = 0; i < 12; ++i) {
		const int x = n.Range(32), y = n.Range(30);
		c.Put(x, y, Rgb(92, 130, 62));
		c.Put(x, y + 1, Rgb(80, 116, 54));
	}
}

void DrawWater(Canvas& c) {
	c.Fill(Rgb(26, 58, 107));
	Noise n;
	for (int y = 0; y < 32; y += 4) {
		for (int x = 0; x < 32; ++x) {
			if ((x + y) % 7 == 0) c.Put(x, y, Rgb(40, 80, 140));
		}
	}
	for (int i = 0; i < 20; ++i) {
		const int x = n.Range(32), y = n.Range(32);
		c.Put(x, y, Rgb(56, 100, 160));
	}
}

void DrawSand(Canvas& c) {
	c.Fill(Rgb(166, 140, 90));
	Noise n;
	for (int i = 0; i < 80; ++i) {
		const int x = n.Range(32), y = n.Range(32);
		const int s = n.Range(3);
		c.Put(x, y, s == 0 ? Rgb(150, 126, 80) : s == 1 ? Rgb(184, 158, 104) : Rgb(172, 146, 94));
	}
}

void DrawRock(Canvas& c) {
	c.Fill(Rgb(80, 80, 82));
	Noise n;
	for (int i = 0; i < 60; ++i) {
		const int x = n.Range(32), y = n.Range(32);
		const int s = n.Range(3);
		c.Put(x, y, s == 0 ? Rgb(64, 64, 66) : s == 1 ? Rgb(96, 96, 98) : Rgb(72, 72, 74));
	}
	// 裂纹
	c.Line(6, 8, 18, 22, Rgb(52, 52, 54));
	c.Line(18, 22, 28, 18, Rgb(52, 52, 54));
}

void DrawHighland(Canvas& c) {
	c.Fill(Rgb(74, 122, 58));
	Noise n;
	for (int i = 0; i < 70; ++i) {
		const int x = n.Range(32), y = n.Range(32);
		const int s = n.Range(3);
		c.Put(x, y, s == 0 ? Rgb(60, 104, 48) : s == 1 ? Rgb(88, 140, 68) : Rgb(68, 114, 54));
	}
}

// 单位用中性浅灰绘制，渲染时按阵营 tint
void DrawInfantry(Canvas& c) {
	c.Circle(16, 10, 6, Rgb(220, 220, 225));   // 头盔
	c.Rect(11, 16, 10, 10, Rgb(200, 200, 205)); // 身体
	c.Rect(12, 26, 3, 5, Rgb(160, 160, 165));   // 腿
	c.Rect(17, 26, 3, 5, Rgb(160, 160, 165));
	c.Line(20, 18, 28, 12, Rgb(90, 90, 95));    // 枪
	c.Line(20, 18, 28, 12, Rgb(120, 120, 125));
	c.Put(15, 9, Rgb(240, 240, 245));
}

void DrawTank(Canvas& c) {
	c.Rect(5, 20, 22, 7, Rgb(70, 70, 72));      // 履带
	c.Rect(7, 21, 18, 2, Rgb(110, 110, 112));   // 履带纹
	c.Rect(4, 12, 24, 9, Rgb(120, 130, 110));   // 车身
	c.Rect(10, 8, 12, 7, Rgb(140, 150, 128));   // 炮塔
	c.Line(22, 11, 30, 9, Rgb(90, 90, 92));     // 炮管
	c.Line(22, 12, 30, 10, Rgb(110, 110, 112));
	c.Rect(4, 12, 24, 1, Rgb(150, 160, 138));   // 高光
}

void DrawHarvester(Canvas& c) {
	c.Rect(5, 14, 20, 12, Rgb(200, 160, 60));   // 车斗
	c.Rect(7, 16, 16, 8, Rgb(170, 130, 40));    // 车斗内
	c.Rect(4, 24, 24, 4, Rgb(80, 80, 82));      // 底盘
	c.Circle(9, 28, 3, Rgb(40, 40, 42));        // 轮
	c.Circle(20, 28, 3, Rgb(40, 40, 42));
	c.Rect(24, 20, 5, 6, Rgb(140, 110, 40));    // 车头
	c.Put(26, 22, Rgb(255, 255, 200));          // 灯
}

void DrawBarracks(Canvas& c) {
	c.Rect(3, 24, 26, 6, Rgb(90, 80, 70));      // 地基
	c.Rect(5, 10, 22, 14, Rgb(180, 170, 150));  // 墙
	c.Rect(5, 10, 22, 2, Rgb(200, 190, 170));   // 墙顶高光
	c.Line(8, 12, 8, 22, Rgb(150, 140, 120));   // 砖缝
	c.Line(16, 12, 16, 22, Rgb(150, 140, 120));
	c.Line(24, 12, 24, 22, Rgb(150, 140, 120));
	c.Rect(13, 16, 6, 8, Rgb(110, 80, 50));     // 门
	c.Rect(9, 4, 14, 6, Rgb(160, 60, 50));      // 屋顶
	c.Rect(9, 4, 14, 1, Rgb(190, 80, 65));
	c.Rect(20, 0, 1, 5, Rgb(120, 120, 120));    // 旗杆
	c.Rect(21, 1, 4, 3, Rgb(220, 200, 60));     // 旗
}

void DrawTree(Canvas& c) {
	c.Rect(14, 22, 4, 8, Rgb(110, 80, 50));     // 树干
	c.Circle(16, 13, 8, Rgb(50, 110, 45));      // 树冠
	c.Circle(12, 11, 5, Rgb(60, 130, 55));
	c.Circle(20, 12, 5, Rgb(45, 100, 40));
	c.Circle(16, 8, 4, Rgb(70, 145, 62));
}

void DrawRocks(Canvas& c) {
	c.Circle(10, 22, 5, Rgb(100, 100, 102));
	c.Circle(20, 24, 4, Rgb(88, 88, 90));
	c.Circle(15, 26, 3, Rgb(110, 110, 112));
	c.Put(9, 20, Rgb(130, 130, 132));
	c.Put(19, 22, Rgb(120, 120, 122));
}

void DrawShadow(Canvas& c) {
	for (int y = 20; y < 30; ++y) {
		for (int x = 4; x < 28; ++x) {
			const int dx = (x - 16);
			const int dy = (y - 25);
			if (dx * dx / 144 + dy * dy / 9 <= 1) {
				c.Put(x, y, Rgb(0, 0, 0, 90));
			}
		}
	}
}

} // namespace

std::vector<uint8_t> AssetAtlas::Build(int& outW, int& outH) {
	outW = kCols * kCell;
	outH = kRows * kCell;
	std::vector<uint8_t> pixels(static_cast<size_t>(outW) * outH * 4, 0);

	auto draw = [&](SpriteId id, void (*fn)(Canvas&)) {
		Canvas c;
		c.data = pixels.data();
		c.w = outW;
		c.ox = (Index(id) % kCols) * kCell;
		c.oy = (Index(id) / kCols) * kCell;
		fn(c);
	};

	draw(SpriteId::Grass, DrawGrass);
	draw(SpriteId::Water, DrawWater);
	draw(SpriteId::Sand, DrawSand);
	draw(SpriteId::Rock, DrawRock);
	draw(SpriteId::Highland, DrawHighland);
	draw(SpriteId::Infantry, DrawInfantry);
	draw(SpriteId::Tank, DrawTank);
	draw(SpriteId::Harvester, DrawHarvester);
	draw(SpriteId::Barracks, DrawBarracks);
	draw(SpriteId::Tree, DrawTree);
	draw(SpriteId::Rocks, DrawRocks);
	draw(SpriteId::Shadow, DrawShadow);

	return pixels;
}

void AssetAtlas::GetUV(SpriteId id, float& u0, float& v0, float& u1, float& v1) {
	const int idx = Index(id);
	const int col = idx % kCols;
	const int row = idx / kCols;
	const float texW = static_cast<float>(kCols * kCell);
	const float texH = static_cast<float>(kRows * kCell);
	u0 = static_cast<float>(col * kCell) / texW;
	v0 = static_cast<float>(row * kCell) / texH;
	u1 = static_cast<float>((col + 1) * kCell) / texW;
	v1 = static_cast<float>((row + 1) * kCell) / texH;
}

} // namespace rts
