// RTS Engine — tools/import_terrain.cpp
// 地形导入：从 RA 的 temperat.mix 提取 TMP → 解析 → 调色板转 RGBA（索引 0 = 透明）→ 拼图集。
//
// 为什么不用 OpenRA.Utility --png：
//   Utility 导出的 PNG 把 TMP 菱形外的调色板索引 0 渲染成了不透明颜色，导致每个 tile 是满幅方块，
//   相邻 tile 互相覆盖 → 水/岩石边界被切成矩形条。OpenRA 运行时用 TmpTDLoader 读原始 index8 数据，
//   索引 0 走调色板透明通道（见 OpenRA.Mods.Cnc/SpriteLoaders/TmpTDLoader.cs）。
//   这里复刻同样的语义：index 0 → alpha 0。
//
// 用法：
//   rts_import_terrain <temperat.mix> <temperat.pal> <out.bmp>
//
// 输出：BMP（32 位 RGBA），tile 布局与 assets/cnc_atlas.txt 的地形映射一致：
//   0-15  clear1（草地 16 变体）
//   16-35 sh01（峭壁 20 个，部分为空）
//   36    w1（水）
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

struct Image {
	int w = 0;
	int h = 0;
	std::vector<uint8_t> px; // RGBA
};

// ---- MIX（RA 格式：u16 0, u16 flags, u16 count, u32 dataSize, entries[count]) ----
// 文件名哈希用 Classic（OpenRA.Mods.Cnc/FileSystem/PackageEntry.cs: HashFilename Classic）：
//   大写 → 4 字节对齐（\0 填充）→ 每 4 字节小端 u32 → hash = rotl(hash,1) + word
uint32_t ClassicHash(const std::string& name) {
	std::string n = name;
	for (char& c : n) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
	while (n.size() % 4 != 0) n.push_back('\0');
	uint32_t hash = 0;
	for (size_t i = 0; i < n.size(); i += 4) {
		uint32_t word = 0;
		std::memcpy(&word, n.data() + i, 4);
		hash = ((hash << 1) | (hash >> 31)) + word;
	}
	return hash;
}

struct MixEntry {
	uint32_t offset = 0;
	uint32_t length = 0;
};

bool ReadMix(const char* path, std::vector<uint8_t>& out, std::vector<MixEntry>& entries, uint32_t& dataStart) {
	FILE* f = std::fopen(path, "rb");
	if (!f) { std::fprintf(stderr, "[terrain] cannot open %s\n", path); return false; }
	std::fseek(f, 0, SEEK_END);
	const long size = std::ftell(f);
	std::fseek(f, 0, SEEK_SET);
	out.resize(static_cast<size_t>(size));
	if (std::fread(out.data(), 1, out.size(), f) != out.size()) { std::fclose(f); return false; }
	std::fclose(f);
	if (out.size() < 10) return false;

	uint16_t magic = 0, flags = 0, count = 0;
	std::memcpy(&magic, &out[0], 2);
	std::memcpy(&flags, &out[2], 2);
	if (magic != 0) { std::fprintf(stderr, "[terrain] not a RA-style mix\n"); return false; }
	if ((flags & 2) != 0) { std::fprintf(stderr, "[terrain] encrypted mix unsupported\n"); return false; }
	std::memcpy(&count, &out[4], 2);
	uint32_t dataSize = 0;
	std::memcpy(&dataSize, &out[6], 4);
	(void)dataSize;
	dataStart = 10 + static_cast<uint32_t>(count) * 12u;
	entries.resize(count);
	for (uint16_t i = 0; i < count; ++i) {
		const size_t base = 10 + static_cast<size_t>(i) * 12;
		uint32_t id = 0;
		std::memcpy(&id, &out[base], 4);
		std::memcpy(&entries[i].offset, &out[base + 4], 4);
		std::memcpy(&entries[i].length, &out[base + 8], 4);
	}
	std::printf("[terrain] mix: %u files, dataStart=%u\n", count, dataStart);
	return true;
}

// ---- TMP：RA 的 .tem 用 TmpRA 格式，TD 用 TmpTD 格式（两个 loader 头部布局不同）----
//   TmpRA (OpenRA.Mods.Cnc/SpriteLoaders/TmpRALoader.cs):
//     u16 w, u16 h, +12, u32 imgStart@16, +8, i32 indexEnd@28, +4, i32 indexStart@36
//     签名: u32@20 == 0 && u16@26 == 0x2C73
//   TmpTD (TmpTDLoader.cs):
//     u16 w, u16 h, +8, u32 imgStart@12, +8, i32 indexEnd@24, i32 indexStart@28
//     签名: u32@16 == 0 && u32@20 == 0x0D1AFFFF
//   index 区：每 tile 一个 u8；255 = 空 tile，否则 frameOffset = imgStart + b*w*h
struct TmpTile {
	bool valid = false;
	int w = 0;
	int h = 0;
	std::vector<uint8_t> indexed; // 调色板索引
};

bool ParseTmp(const std::vector<uint8_t>& buf, std::vector<TmpTile>& tiles) {
	if (buf.size() < 32) return false;
	uint16_t width = 0, height = 0;
	std::memcpy(&width, &buf[0], 2);
	std::memcpy(&height, &buf[2], 2);
	// 格式检测（与 OpenRA 的 IsTmpRA / IsTmpTD 一致）
	uint32_t sigA = 0, sigB = 0;
	std::memcpy(&sigA, &buf[20], 4);
	uint16_t sigC = 0;
	std::memcpy(&sigC, &buf[26], 2);
	const bool isRA = (sigA == 0 && sigC == 0x2C73);

	uint32_t imgStart = 0;
	int32_t indexEnd = 0, indexStart = 0;
	if (isRA) {
		std::memcpy(&imgStart, &buf[16], 4);
		std::memcpy(&indexEnd, &buf[28], 4);
		std::memcpy(&indexStart, &buf[36], 4);
	} else {
		std::memcpy(&imgStart, &buf[12], 4);
		std::memcpy(&indexEnd, &buf[24], 4);
		std::memcpy(&indexStart, &buf[28], 4);
	}
	if (width == 0 || height == 0 || indexStart < 0 || indexEnd < indexStart) return false;
	if (static_cast<size_t>(indexEnd) > buf.size()) return false;

	const int count = indexEnd - indexStart;
	tiles.resize(static_cast<size_t>(count));
	const size_t frameBytes = static_cast<size_t>(width) * height;
	for (int i = 0; i < count; ++i) {
		const uint8_t b = buf[static_cast<size_t>(indexStart) + i];
		TmpTile& t = tiles[static_cast<size_t>(i)];
		t.w = width;
		t.h = height;
		if (b == 255) { t.valid = false; continue; }
		const size_t off = imgStart + static_cast<size_t>(b) * frameBytes;
		if (off + frameBytes > buf.size()) { t.valid = false; continue; }
		t.valid = true;
		t.indexed.assign(buf.begin() + static_cast<long>(off), buf.begin() + static_cast<long>(off + frameBytes));
	}
	return true;
}

bool ReadPalette(const char* path, uint8_t pal[256][3]) {
	FILE* f = std::fopen(path, "rb");
	if (!f) { std::fprintf(stderr, "[terrain] cannot open palette %s\n", path); return false; }
	const size_t n = std::fread(pal, 3, 256, f);
	std::fclose(f);
	if (n < 256) { std::fprintf(stderr, "[terrain] palette too short (%zu)\n", n); return false; }
	return true;
}

// index 0 = 透明（OpenRA 调色板约定），其余查表。
// RA 的 VGA 调色板是 6 位色（0..63），显示需要 8 位 → ×4。
// maskDiamond: 把菱形外的像素也置透明。RA 的 TMP 美术是按「正交网格」画的满幅方块
// （OpenRA 的 MapGrid.Type = Rectangular, TileSize 24×24，见 mods/ra/mod.yaml），
// 直接放到我们的等距投影里会 64×32 重叠 50% 互相覆盖（水被切成横条）；
// 裁出菱形后才是等距可铺贴的 tile。
Image TileToRgba(const TmpTile& t, const uint8_t pal[256][3], bool maskDiamond) {
	Image img;
	img.w = t.w;
	img.h = t.h;
	img.px.assign(static_cast<size_t>(t.w) * t.h * 4, 0);
	if (!t.valid) return img; // 全透明
	const float cx = t.w * 0.5f;
	const float cy = t.h * 0.5f;
	for (size_t i = 0; i < t.indexed.size(); ++i) {
		const uint8_t idx = t.indexed[i];
		if (idx == 0) continue; // 透明
		if (maskDiamond) {
			const int x = static_cast<int>(i % static_cast<size_t>(t.w));
			const int y = static_cast<int>(i / static_cast<size_t>(t.w));
			const float nx = (x + 0.5f - cx) / cx;
			const float ny = (y + 0.5f - cy) / cy;
			if (std::abs(nx) + std::abs(ny) > 1.0f) continue; // 菱形外
		}
		img.px[i * 4 + 0] = static_cast<uint8_t>(std::min(255, pal[idx][0] * 4));
		img.px[i * 4 + 1] = static_cast<uint8_t>(std::min(255, pal[idx][1] * 4));
		img.px[i * 4 + 2] = static_cast<uint8_t>(std::min(255, pal[idx][2] * 4));
		img.px[i * 4 + 3] = 255;
	}
	return img;
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
	if (argc < 4) {
		std::fprintf(stderr, "usage: rts_import_terrain <temperat.mix> <temperat.pal> <out.bmp>\n");
		return 1;
	}
	const char* mixPath = argv[1];
	const char* palPath = argv[2];
	const char* outPath = argv[3];

	std::vector<uint8_t> mix;
	std::vector<MixEntry> entries;
	uint32_t dataStart = 0;
	if (!ReadMix(mixPath, mix, entries, dataStart)) return 1;

	uint8_t pal[256][3] = {};
	if (!ReadPalette(palPath, pal)) return 1;

	// 顺序：clear1(16) + sh01(20) + w1(1) = 37，与引擎 TileFor 的映射一致
	struct Source { const char* name; int expect; };
	const Source sources[] = {{"clear1.tem", 16}, {"sh01.tem", 20}, {"w1.tem", 1}};

	std::vector<Image> atlasTiles;
	for (const Source& src : sources) {
		const uint32_t id = ClassicHash(src.name);
		const MixEntry* found = nullptr;
		// 索引是按 hash 查表：重新扫描一遍（entries 里没存 hash，这里重建）
		{
			uint16_t count = 0;
			std::memcpy(&count, &mix[4], 2);
			for (uint16_t i = 0; i < count; ++i) {
				const size_t base = 10 + static_cast<size_t>(i) * 12;
				uint32_t hid = 0;
				std::memcpy(&hid, &mix[base], 4);
				if (hid == id) { found = &entries[i]; break; }
			}
		}
		if (found == nullptr) { std::fprintf(stderr, "[terrain] %s not found in mix\n", src.name); return 1; }
		if (dataStart + found->offset + found->length > mix.size()) {
			std::fprintf(stderr, "[terrain] %s out of bounds\n", src.name);
			return 1;
		}
		std::vector<uint8_t> buf(mix.begin() + static_cast<long>(dataStart + found->offset),
			mix.begin() + static_cast<long>(dataStart + found->offset + found->length));
		std::vector<TmpTile> tiles;
		if (!ParseTmp(buf, tiles)) { std::fprintf(stderr, "[terrain] %s parse failed\n", src.name); return 1; }
		std::printf("[terrain] %s: %zu tiles (expected %d)\n", src.name, tiles.size(), src.expect);
		for (const TmpTile& t : tiles) atlasTiles.push_back(TileToRgba(t, pal, false));
	}

	// 拼图集（横向排列，每格取最大尺寸）
	int cellW = 0, cellH = 0;
	for (const Image& t : atlasTiles) {
		cellW = std::max(cellW, t.w);
		cellH = std::max(cellH, t.h);
	}
	const int atlasW = cellW * static_cast<int>(atlasTiles.size());
	std::vector<uint8_t> atlas(static_cast<size_t>(atlasW) * cellH * 4, 0);

	// 统计 alpha 覆盖率，便于确认透明角部生效
	int transparent = 0;
	for (int i = 0; i < static_cast<int>(atlasTiles.size()); ++i) {
		const Image& t = atlasTiles[static_cast<size_t>(i)];
		const int dx = i * cellW;
		for (int y = 0; y < t.h; ++y) {
			for (int x = 0; x < t.w; ++x) {
				const size_t so = (static_cast<size_t>(y) * t.w + x) * 4;
				if (t.px[so + 3] == 0) ++transparent;
				const size_t do_ = (static_cast<size_t>(y) * atlasW + dx + x) * 4;
				atlas[do_ + 0] = t.px[so + 0];
				atlas[do_ + 1] = t.px[so + 1];
				atlas[do_ + 2] = t.px[so + 2];
				atlas[do_ + 3] = t.px[so + 3];
			}
		}
	}

	if (!WriteBmp32(outPath, atlasW, cellH, atlas)) {
		std::fprintf(stderr, "[terrain] failed to write %s\n", outPath);
		return 1;
	}
	const int total = atlasW * cellH;
	std::printf("[terrain] atlas %dx%d (%zu tiles), transparent %.1f%% -> %s\n",
		atlasW, cellH, atlasTiles.size(), 100.0 * transparent / total, outPath);
	return 0;
}
