// RTS Engine — tools/import_assets.cpp
// 资源导入工具（配置驱动）：把一堆帧 PNG（任意来源）打包成引擎图集 + 元数据。
//
// 用法：
//   rts_import <framesDir> <config.txt> <outAtlas.bmp> <outMeta.txt>
//
// 配置格式（每行；# 注释）：
//   cell <w> <h>           图集格子尺寸
//   cols <n>               图集列数
//   team <count>           阵营数（默认 2）
//   team0 <r> <g> <b>      阵营 0 替换色
//   team1 <r> <g> <b>      阵营 1 替换色
//   sprite <name> <prefix> <facings> <frames> <mode> <standStart> <runStart> <turretStart> <row>
//     name        = CncSpriteId 名（Infantry/Tank/...）
//     prefix      = 帧文件前缀（帧文件 = <prefix>-<%04d>.png）
//     facings     = 8 或 32（0 = 建筑单朝向）
//     frames      = stand/run 动画帧数（1 = 无动画）
//     mode        = unit8 | unit32 | building | tank32
//     standStart  = stand 帧起始索引
//     runStart    = run 帧起始索引（mode=unit8 时使用；每朝向 6 帧，取前 frames-1 帧）
//     turretStart = 炮塔帧起始（mode=tank32；-1 = 无炮塔）
//     row         = 图集起始行
//
// 输出：
//   图集：BMP（32 位 RGBA，引擎用 stb_image 读取）
//   元数据：与 assets/cnc_atlas.txt 相同格式（引擎 CncAtlas::LoadMeta 读取）
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

namespace {

struct SpriteDef {
	std::string name;
	std::string prefix;
	int facings = 8;
	int frames = 1;
	std::string mode = "unit8";
	int standStart = 0;
	int runStart = 16;
	int turretStart = -1;
	int row = 0;
};

struct Config {
	int cellW = 80;
	int cellH = 64;
	int cols = 64;
	int teamCount = 2;
	uint8_t team[2][3] = {{70, 90, 255}, {235, 60, 60}};
	std::vector<SpriteDef> sprites;
};

bool ParseConfig(const char* path, Config& cfg) {
	FILE* f = std::fopen(path, "r");
	if (!f) { std::fprintf(stderr, "[import] cannot open config %s\n", path); return false; }
	char line[512];
	while (std::fgets(line, sizeof(line), f)) {
		char* p = line;
		while (*p == ' ' || *p == '\t') ++p;
		if (*p == '#' || *p == '\n' || *p == '\r' || *p == 0) continue;
		char key[64] = {};
		if (std::sscanf(p, "%63s", key) != 1) continue;
		if (std::strcmp(key, "cell") == 0) {
			std::sscanf(p, "%*s %d %d", &cfg.cellW, &cfg.cellH);
		} else if (std::strcmp(key, "cols") == 0) {
			std::sscanf(p, "%*s %d", &cfg.cols);
		} else if (std::strcmp(key, "team") == 0) {
			std::sscanf(p, "%*s %d", &cfg.teamCount);
		} else if (std::strcmp(key, "team0") == 0) {
			int r = 0, g = 0, b = 0;
			std::sscanf(p, "%*s %d %d %d", &r, &g, &b);
			cfg.team[0][0] = static_cast<uint8_t>(r);
			cfg.team[0][1] = static_cast<uint8_t>(g);
			cfg.team[0][2] = static_cast<uint8_t>(b);
		} else if (std::strcmp(key, "team1") == 0) {
			int r = 0, g = 0, b = 0;
			std::sscanf(p, "%*s %d %d %d", &r, &g, &b);
			cfg.team[1][0] = static_cast<uint8_t>(r);
			cfg.team[1][1] = static_cast<uint8_t>(g);
			cfg.team[1][2] = static_cast<uint8_t>(b);
		} else if (std::strcmp(key, "sprite") == 0) {
			char name[64] = {}, prefix[64] = {}, mode[32] = {};
			SpriteDef d;
			if (std::sscanf(p, "%*s %63s %63s %d %d %31s %d %d %d %d",
				name, prefix, &d.facings, &d.frames, mode, &d.standStart, &d.runStart,
				&d.turretStart, &d.row) >= 8) {
				d.name = name;
				d.prefix = prefix;
				d.mode = mode;
				cfg.sprites.push_back(d);
			} else {
				std::fprintf(stderr, "[import] bad sprite line: %s\n", p);
			}
		}
	}
	std::fclose(f);
	return !cfg.sprites.empty();
}

// 图像（RGBA）
struct Image {
	int w = 0, h = 0;
	std::vector<uint8_t> px;
};

bool LoadPng(const std::string& path, Image& out) {
	int w = 0, h = 0, ch = 0;
	stbi_uc* data = stbi_load(path.c_str(), &w, &h, &ch, 4);
	if (data == nullptr) return false;
	out.w = w;
	out.h = h;
	out.px.assign(data, data + static_cast<size_t>(w) * h * 4);
	stbi_image_free(data);
	return true;
}

// 裁剪到内容 bbox（alpha > 50），返回 false 表示全透明
bool CropBBox(const Image& src, int& ox, int& oy, int& ow, int& oh) {
	int minX = src.w, maxX = -1, minY = src.h, maxY = -1;
	for (int y = 0; y < src.h; ++y) {
		for (int x = 0; x < src.w; ++x) {
			if (src.px[(static_cast<size_t>(y) * src.w + x) * 4 + 3] > 50) {
				minX = std::min(minX, x);
				maxX = std::max(maxX, x);
				minY = std::min(minY, y);
				maxY = std::max(maxY, y);
			}
		}
	}
	if (maxX < 0) return false;
	ox = minX;
	oy = minY;
	ow = maxX - minX + 1;
	oh = maxY - minY + 1;
	return true;
}

// 合成：turret 覆盖 body（alpha > 50）
Image Composite(const Image& body, const Image& turret) {
	Image out = body;
	const int w = std::min(body.w, turret.w);
	const int h = std::min(body.h, turret.h);
	for (int y = 0; y < h; ++y) {
		for (int x = 0; x < w; ++x) {
			const size_t to = (static_cast<size_t>(y) * turret.w + x) * 4;
			if (turret.px[to + 3] > 50) {
				const size_t bo = (static_cast<size_t>(y) * body.w + x) * 4;
				out.px[bo + 0] = turret.px[to + 0];
				out.px[bo + 1] = turret.px[to + 1];
				out.px[bo + 2] = turret.px[to + 2];
				out.px[bo + 3] = turret.px[to + 3];
			}
		}
	}
	return out;
}

// 阵营色替换（RA 的 house color ≈ 绿色标记 R60-120, G>200, B60-120）
void ApplyTeam(Image& img, const uint8_t rgb[3]) {
	for (size_t i = 0; i + 3 < img.px.size(); i += 4) {
		const uint8_t r = img.px[i + 0], g = img.px[i + 1], b = img.px[i + 2], a = img.px[i + 3];
		if (a > 0 && r > 60 && r < 120 && g > 200 && b > 60 && b < 120) {
			img.px[i + 0] = rgb[0];
			img.px[i + 1] = rgb[1];
			img.px[i + 2] = rgb[2];
		}
	}
}

bool WriteBmp32(const char* path, int w, int h, const std::vector<uint8_t>& rgba) {
	FILE* f = std::fopen(path, "wb");
	if (!f) return false;
	const int rowBytes = w * 4;
	const int dataSize = rowBytes * h;
	uint8_t header[54] = {};
	header[0] = 'B';
	header[1] = 'M';
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
	const int32_t compression = 0;
	std::memcpy(&header[30], &compression, 4);
	std::memcpy(&header[34], &dataSize, 4);
	std::fwrite(header, 1, 54, f);
	// BMP 行序自底向上；像素顺序 BGRA
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
			"usage: rts_import <framesDir> <config.txt> <outAtlas.bmp> <outMeta.txt>\n");
		return 1;
	}
	const std::string framesDir = argv[1];
	const char* configPath = argv[2];
	const char* atlasPath = argv[3];
	const char* metaPath = argv[4];

	Config cfg;
	if (!ParseConfig(configPath, cfg)) return 1;

	// 图集行数 = 最大 row + 每个 sprite 占用的行数（8/32 朝向无动画 = 1 行/阵营；动画 = frames 行/阵营）
	int totalRows = 0;
	for (const auto& d : cfg.sprites) {
		int rows = 1;
		if (d.facings == 0) rows = 1;
		else if (d.facings == 8) rows = d.frames;
		else rows = 1;
		totalRows = std::max(totalRows, d.row + rows * cfg.teamCount);
	}

	const int atlasW = cfg.cols * cfg.cellW;
	const int atlasH = totalRows * cfg.cellH;
	std::vector<uint8_t> atlas(static_cast<size_t>(atlasW) * atlasH * 4, 0);

	auto place = [&](const Image& img, int col, int row) {
		const int dx = col * cfg.cellW + (cfg.cellW - img.w) / 2;
		const int dy = row * cfg.cellH + (cfg.cellH - img.h) / 2;
		for (int y = 0; y < img.h; ++y) {
			for (int x = 0; x < img.w; ++x) {
				const int ax = dx + x;
				const int ay = dy + y;
				if (ax < 0 || ay < 0 || ax >= atlasW || ay >= atlasH) continue;
				const size_t so = (static_cast<size_t>(y) * img.w + x) * 4;
				if (img.px[so + 3] == 0) continue;
				const size_t dst = (static_cast<size_t>(ay) * atlasW + ax) * 4;
				atlas[dst + 0] = img.px[so + 0];
				atlas[dst + 1] = img.px[so + 1];
				atlas[dst + 2] = img.px[so + 2];
				atlas[dst + 3] = img.px[so + 3];
			}
		}
	};

	// 元数据输出
	std::vector<std::string> metaLines;
	metaLines.push_back("# Generated by rts_import (config-driven asset pipeline)");
	metaLines.push_back("cell " + std::to_string(cfg.cellW) + " " + std::to_string(cfg.cellH));
	metaLines.push_back("cols " + std::to_string(cfg.cols));
	metaLines.push_back("rows " + std::to_string(totalRows));
	metaLines.push_back("");

	for (const auto& d : cfg.sprites) {
		int maxW = 0, maxH = 0;
		const bool isTank = (d.mode == "tank32");
		const bool isBuilding = (d.mode == "building");
		const int facings = (d.facings == 0) ? 1 : d.facings;

		for (int team = 0; team < cfg.teamCount; ++team) {
			const int frameCount = isBuilding ? 1 : d.frames;
			for (int k = 0; k < frameCount; ++k) {
				const int row = d.row + (isBuilding ? team : ((facings == 8 ? k : 0) * cfg.teamCount + team));
				for (int f = 0; f < facings; ++f) {
					// 帧索引：stand = standStart + f；run = runStart + f*6 + (k-1)
					int frameIdx = 0;
					if (isBuilding) {
						frameIdx = 0;
					} else if (isTank) {
						frameIdx = d.standStart + f;
					} else if (k == 0) {
						frameIdx = d.standStart + f;
					} else {
						frameIdx = d.runStart + f * 6 + (k - 1);
					}
					char nameBuf[256];
					std::snprintf(nameBuf, sizeof(nameBuf), "%s/%s-%04d.png", framesDir.c_str(), d.prefix.c_str(), frameIdx);
					Image img;
					if (!LoadPng(nameBuf, img)) {
						std::fprintf(stderr, "[import] missing %s\n", nameBuf);
						continue;
					}
					if (isTank && d.turretStart >= 0) {
						char turretBuf[256];
						std::snprintf(turretBuf, sizeof(turretBuf), "%s/%s-%04d.png", framesDir.c_str(), d.prefix.c_str(), d.turretStart + f);
						Image turret;
						if (LoadPng(turretBuf, turret)) {
							img = Composite(img, turret);
						}
					}
					int ox = 0, oy = 0, ow = 0, oh = 0;
					if (!CropBBox(img, ox, oy, ow, oh)) continue;
					Image cropped;
					cropped.w = ow;
					cropped.h = oh;
					cropped.px.resize(static_cast<size_t>(ow) * oh * 4);
					for (int y = 0; y < oh; ++y) {
						for (int x = 0; x < ow; ++x) {
							const size_t so = (static_cast<size_t>(oy + y) * img.w + (ox + x)) * 4;
							const size_t dstC = (static_cast<size_t>(y) * ow + x) * 4;
							std::memcpy(&cropped.px[dstC], &img.px[so], 4);
						}
					}
					ApplyTeam(cropped, cfg.team[team]);
					maxW = std::max(maxW, ow);
					maxH = std::max(maxH, oh);
					const int col = (facings == 32) ? (f + team * 32) : (f + team * 8);
					place(cropped, col, row);
				}
			}
		}
		char metaBuf[256];
		std::snprintf(metaBuf, sizeof(metaBuf), "sprite %s %d %d %d %d %d",
			d.name.c_str(), d.row, maxW, maxH, facings, d.frames);
		metaLines.push_back(metaBuf);
		std::printf("[import] %-12s row=%d maxBBox=%dx%d facings=%d frames=%d\n",
			d.name.c_str(), d.row, maxW, maxH, facings, d.frames);
	}

	if (!WriteBmp32(atlasPath, atlasW, atlasH, atlas)) {
		std::fprintf(stderr, "[import] failed to write %s\n", atlasPath);
		return 1;
	}
	FILE* mf = std::fopen(metaPath, "w");
	if (!mf) {
		std::fprintf(stderr, "[import] failed to write %s\n", metaPath);
		return 1;
	}
	for (const auto& l : metaLines) std::fprintf(mf, "%s\n", l.c_str());
	std::fclose(mf);
	std::printf("[import] atlas %dx%d -> %s\n", atlasW, atlasH, atlasPath);
	std::printf("[import] metadata -> %s\n", metaPath);
	return 0;
}
