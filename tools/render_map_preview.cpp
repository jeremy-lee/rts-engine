// RTS Engine — tools/render_map_preview.cpp
// 离线地形预览：读我们的地图二进制 + tileset 元数据/图集，把指定区域直接光栅化成 BMP。
// 用途：脱离 GL/编辑器/相机，客观判断"地形拼接是不是渲染逻辑的问题"。
//
// 用法：
//   rts_map_preview <map.bin> <tileset.txt> <atlas.bmp> <out.bmp> [x y w h]
//   不带区域参数时渲染整张图。
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

namespace {

struct Template {
	int id = 0;
	std::string image;
	int sizeW = 1, sizeH = 1;
	int frameBase = 0;
	int frameCount = 0;
};

struct TileSetData {
	int tileCols = 128;
	int cellW = 24, cellH = 24;
	int rows = 0;
	std::map<int, Template> templates;
};

bool LoadTileset(const char* path, TileSetData& ts) {
	std::ifstream in(path);
	if (!in) return false;
	std::string line;
	while (std::getline(in, line)) {
		if (line.empty() || line[0] == '#') continue;
		std::istringstream iss(line);
		std::string key;
		iss >> key;
		if (key == "tilecols") iss >> ts.tileCols;
		else if (key == "cell") iss >> ts.cellW >> ts.cellH;
		else if (key == "rows") iss >> ts.rows;
		else if (key == "template") {
			Template t;
			iss >> t.id >> t.image >> t.sizeW >> t.sizeH >> t.frameBase >> t.frameCount;
			if (!iss.fail()) ts.templates[t.id] = t;
		}
	}
	return !ts.templates.empty();
}

struct Image {
	int w = 0, h = 0;
	std::vector<uint8_t> px;
};

bool LoadImage(const char* path, Image& out) {
	int w = 0, h = 0, ch = 0;
	stbi_uc* data = stbi_load(path, &w, &h, &ch, 4);
	if (data == nullptr) return false;
	out.w = w;
	out.h = h;
	out.px.assign(data, data + static_cast<size_t>(w) * h * 4);
	stbi_image_free(data);
	return true;
}

bool WriteBmp32(const char* path, int w, int h, const std::vector<uint8_t>& rgba) {
	FILE* f = std::fopen(path, "wb");
	if (!f) return false;
	const int rowBytes = w * 4;
	const int dataSize = rowBytes * h;
	uint8_t header[54] = {};
	header[0] = 'B'; header[1] = 'M';
	const int32_t fileSize = 54 + dataSize;
	std::memcpy(&header[2], &fileSize, 4);
	const int32_t dataOffset = 54;
	std::memcpy(&header[10], &dataOffset, 4);
	const int32_t headerSize = 40;
	std::memcpy(&header[14], &headerSize, 4);
	std::memcpy(&header[18], &w, 4);
	std::memcpy(&header[22], &h, 4);
	const int16_t planes = 1;
	std::memcpy(&header[26], &planes, 2);
	const int16_t bpp = 32;
	std::memcpy(&header[28], &bpp, 2);
	const int32_t comp = 0;
	std::memcpy(&header[30], &comp, 4);
	std::memcpy(&header[34], &dataSize, 4);
	std::fwrite(header, 1, 54, f);
	std::vector<uint8_t> row(static_cast<size_t>(rowBytes));
	for (int y = h - 1; y >= 0; --y) {
		for (int x = 0; x < w; ++x) {
			const size_t o = (static_cast<size_t>(y) * w + x) * 4;
			row[static_cast<size_t>(x) * 4 + 0] = rgba[o + 2];
			row[static_cast<size_t>(x) * 4 + 1] = rgba[o + 1];
			row[static_cast<size_t>(x) * 4 + 2] = rgba[o + 0];
			row[static_cast<size_t>(x) * 4 + 3] = rgba[o + 3];
		}
		std::fwrite(row.data(), 1, row.size(), f);
	}
	std::fclose(f);
	return true;
}

} // namespace

int main(int argc, char** argv) {
	if (argc < 5) {
		std::fprintf(stderr,
			"usage: rts_map_preview <map.bin> <tileset.txt> <atlas.bmp> <out.bmp> [x y w h]\n");
		return 1;
	}
	const char* mapPath = argv[1];
	const char* tsPath = argv[2];
	const char* atlasPath = argv[3];
	const char* outPath = argv[4];

	// 地图
	std::FILE* f = std::fopen(mapPath, "rb");
	if (f == nullptr) { std::fprintf(stderr, "[preview] cannot open map\n"); return 1; }
	std::fseek(f, 0, SEEK_END);
	const long fsize = std::ftell(f);
	std::fseek(f, 0, SEEK_SET);
	std::vector<uint8_t> mb(static_cast<size_t>(fsize));
	if (std::fread(mb.data(), 1, mb.size(), f) != mb.size()) { std::fclose(f); return 1; }
	std::fclose(f);
	if (mb.size() < 17 || mb[0] != 2) { std::fprintf(stderr, "[preview] bad map\n"); return 1; }
	uint16_t mw = 0, mh = 0;
	std::memcpy(&mw, &mb[1], 2);
	std::memcpy(&mh, &mb[3], 2);
	uint32_t tilesOffset = 0;
	std::memcpy(&tilesOffset, &mb[5], 4);

	// tileset + 图集
	TileSetData ts;
	if (!LoadTileset(tsPath, ts)) { std::fprintf(stderr, "[preview] bad tileset\n"); return 1; }
	Image atlas;
	if (!LoadImage(atlasPath, atlas)) { std::fprintf(stderr, "[preview] bad atlas\n"); return 1; }

	int x0 = 0, y0 = 0, w = mw, h = mh;
	if (argc >= 9) {
		x0 = std::atoi(argv[5]);
		y0 = std::atoi(argv[6]);
		w = std::atoi(argv[7]);
		h = std::atoi(argv[8]);
	}
	x0 = std::max(0, std::min(x0, static_cast<int>(mw) - 1));
	y0 = std::max(0, std::min(y0, static_cast<int>(mh) - 1));
	w = std::max(1, std::min(w, static_cast<int>(mw) - x0));
	h = std::max(1, std::min(h, static_cast<int>(mh) - y0));

	// 等距布局：格中心 = ((x-y)*stepX, (x+y)*stepY)，tile 满幅 cellW x cellH 居中，
	// 按 x+y 递增顺序绘制（后画的覆盖前一个的下半部）——与引擎的画家算法一致。
	const int stepX = ts.cellW / 2;   // 12
	const int stepY = ts.cellH / 2;   // 12 (OpenRA RectangularIsometric)
	const int offX = (h > 0 ? h : 1) * stepX + ts.cellW;
	const int outW = (w + h) * stepX + ts.cellW * 2;
	const int outH = (w + h) * stepY + ts.cellH * 2;
	std::vector<uint8_t> out(static_cast<size_t>(outW) * outH * 4, 0);
	int missing = 0;

	for (int sum = 0; sum < w + h; ++sum) {
		for (int y = 0; y < h; ++y) {
			const int x = sum - y;
			if (x < 0 || x >= w) continue;
			const size_t o = tilesOffset + (static_cast<size_t>(y0 + y) * mw + (x0 + x)) * 3;
			if (o + 3 > mb.size()) continue;
			uint16_t tid = 0;
			std::memcpy(&tid, &mb[o], 2);
			const uint8_t idx = mb[o + 2];

			auto it = ts.templates.find(tid);
			if (it == ts.templates.end()) { ++missing; continue; }
			const Template& t = it->second;
			int frame = idx;
			if (frame >= t.frameCount) frame = t.frameCount - 1;
			if (t.frameCount <= 0) continue;
			const int global = t.frameBase + frame;
			const int scx = (global % ts.tileCols) * ts.cellW;
			const int scy = (global / ts.tileCols) * ts.cellH;

			// 1:1 拷贝（不缩放），tile 满幅贴在自己的等距位置上
			const int cx = (x - y) * stepX + offX;
			const int cy = (x + y) * stepY + ts.cellH;
			for (int py = 0; py < ts.cellH; ++py) {
				for (int px = 0; px < ts.cellW; ++px) {
					const int ax = scx + px;
					const int ay = scy + py;
					if (ax < 0 || ay < 0 || ax >= atlas.w || ay >= atlas.h) continue;
					const size_t so = (static_cast<size_t>(ay) * atlas.w + ax) * 4;
					if (atlas.px[so + 3] == 0) continue; // 透明保留底色
					const int ox = cx - ts.cellW / 2 + px;
					const int oy = cy - ts.cellH / 2 + py;
					if (ox < 0 || oy < 0 || ox >= outW || oy >= outH) continue;
					const size_t do_ = (static_cast<size_t>(oy) * outW + ox) * 4;
					out[do_ + 0] = atlas.px[so + 0];
					out[do_ + 1] = atlas.px[so + 1];
					out[do_ + 2] = atlas.px[so + 2];
					out[do_ + 3] = 255;
				}
			}
		}
	}

	if (!WriteBmp32(outPath, outW, outH, out)) { std::fprintf(stderr, "[preview] write failed\n"); return 1; }
	std::printf("[preview] %s: region (%d,%d) %dx%d -> %dx%d, missing templates: %d\n",
		outPath, x0, y0, w, h, outW, outH, missing);
	return 0;
}
