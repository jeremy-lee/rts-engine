// RTS Engine — tools/build_tileset.cpp
// 全量地形集导入：解析 OpenRA 的 tileset yaml + temperat.mix 里所有 TMP，
// 生成多行图集 + 模板元数据。
//
// 数据来源（OpenRA mods/ra/tilesets/temperat.yaml）：
//   Templates:
//     Template@6:
//       Id: 6                 ← 地图里存的 templateId
//       Images: sh04.tem      ← 该模板用哪个 TMP
//       Size: 3,3             ← 占几格
//       Categories: Beach
//       Tiles:
//         0: Beach            ← index → TerrainType
//         6: Water
//   Terrain:
//     TerrainType@Clear:
//       Type: Clear
//       TargetTypes: Ground
//       Color: 284428
//
// 输出：
//   <out.bmp>   多行图集（每 tile 24×24）
//   <out.txt>   元数据：
//                 tilecols <n>
//                 tile <globalIndex> <w> <h>
//                 template <id> <image> <sizeW> <sizeH> <frameBase> <frameCount>
//                 tileterrain <templateId> <index> <terrainType>
//                 terrain <name> <walkable 0|1> <colorRRGGBB>
//
// 用法：rts_build_tileset <temperat.mix> <temperat.pal> <temperat.yaml> <out.bmp> <out.txt>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace {

// ================= 极简 YAML（只覆盖 OpenRA tileset 用的子集） =================
struct YamlNode {
	std::string key;
	std::string value;
	std::map<std::string, YamlNode> children; // 保持插入顺序不重要
	std::vector<std::pair<std::string, std::string>> ordered; // 顺序保留（Tiles 需要）
	YamlNode* parent = nullptr;
};

std::string Trim(const std::string& s) {
	size_t a = 0, b = s.size();
	while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r')) ++a;
	while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' || s[b - 1] == '\n')) --b;
	return s.substr(a, b - a);
}

bool ParseYaml(const char* path, YamlNode& root) {
	FILE* f = std::fopen(path, "r");
	if (!f) { std::fprintf(stderr, "[tileset] cannot open %s\n", path); return false; }
	std::vector<YamlNode*> stack;
	stack.push_back(&root);
	char line[1024];
	while (std::fgets(line, sizeof(line), f)) {
		std::string raw(line);
		// 去掉行尾
		while (!raw.empty() && (raw.back() == '\n' || raw.back() == '\r')) raw.pop_back();
		if (raw.empty()) continue;
		const std::string t = Trim(raw);
		if (t.empty() || t[0] == '#') continue;
		// 计算缩进（tab 或 2 空格）
		int indent = 0;
		for (char ch : raw) {
			if (ch == '\t') indent++;
			else if (ch == ' ') indent += 0; // OpenRA 用 tab
			else break;
		}
		YamlNode* parent = stack.back();
		while (static_cast<int>(stack.size()) - 1 > indent && stack.size() > 1) {
			stack.pop_back();
			parent = stack.back();
		}
		std::string key, value;
		const size_t colon = t.find(':');
		if (colon == std::string::npos) continue;
		key = Trim(t.substr(0, colon));
		value = Trim(t.substr(colon + 1));
		YamlNode node;
		node.key = key;
		node.value = value;
		node.parent = parent;
		parent->children[key] = node;
		parent->ordered.emplace_back(key, value);
		// 空值 = 有子节点，压栈
		if (value.empty()) {
			// children 里的是拷贝，需要拿到真实地址
			stack.push_back(&parent->children[key]);
		}
	}
	std::fclose(f);
	return true;
}

// 取子节点（不存在返回 nullptr）
const YamlNode* Child(const YamlNode& n, const std::string& key) {
	auto it = n.children.find(key);
	return it == n.children.end() ? nullptr : &it->second;
}

// 取 "Key@Suffix" 形式的子节点集合（OpenRA 用它存列表）
std::vector<const YamlNode*> ChildrenWithPrefix(const YamlNode& n, const std::string& prefix) {
	std::vector<const YamlNode*> out;
	for (const auto& [k, v] : n.children) {
		if (k.rfind(prefix, 0) == 0) out.push_back(&v);
	}
	return out;
}

// ================= MIX / TMP（与 import_terrain.cpp 相同逻辑） =================
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

struct MixArchive {
	std::string name;
	std::vector<uint8_t> data;
	uint32_t dataStart = 0;
	std::map<uint32_t, std::pair<uint32_t, uint32_t>> index; // hash → (offset, length)
};

bool LoadMix(const char* path, MixArchive& mix) {
	mix.name = path;
	FILE* f = std::fopen(path, "rb");
	if (!f) return false;
	std::fseek(f, 0, SEEK_END);
	const long size = std::ftell(f);
	std::fseek(f, 0, SEEK_SET);
	mix.data.resize(static_cast<size_t>(size));
	const size_t got = std::fread(mix.data.data(), 1, mix.data.size(), f);
	std::fclose(f);
	if (got != mix.data.size() || mix.data.size() < 10) return false;
	uint16_t magic = 0, flags = 0, count = 0;
	std::memcpy(&magic, &mix.data[0], 2);
	std::memcpy(&flags, &mix.data[2], 2);
	if (magic != 0 || (flags & 2) != 0) return false;
	std::memcpy(&count, &mix.data[4], 2);
	mix.dataStart = 10 + static_cast<uint32_t>(count) * 12u;
	for (uint16_t i = 0; i < count; ++i) {
		const size_t base = 10 + static_cast<size_t>(i) * 12;
		uint32_t id = 0, off = 0, len = 0;
		std::memcpy(&id, &mix.data[base], 4);
		std::memcpy(&off, &mix.data[base + 4], 4);
		std::memcpy(&len, &mix.data[base + 8], 4);
		mix.index[id] = {off, len};
	}
	return true;
}

struct TmpTile {
	bool valid = false;
	int w = 0, h = 0;
	std::vector<uint8_t> indexed;
};

bool ParseTmp(const std::vector<uint8_t>& buf, std::vector<TmpTile>& tiles) {
	if (buf.size() < 40) return false;
	uint16_t width = 0, height = 0;
	std::memcpy(&width, &buf[0], 2);
	std::memcpy(&height, &buf[2], 2);
	uint32_t sigA = 0;
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
		if (b == 255) continue;
		const size_t off = imgStart + static_cast<size_t>(b) * frameBytes;
		if (off + frameBytes > buf.size()) continue;
		t.valid = true;
		t.indexed.assign(buf.begin() + static_cast<long>(off), buf.begin() + static_cast<long>(off + frameBytes));
	}
	return true;
}

bool ReadPalette(const char* path, uint8_t pal[256][3]) {
	FILE* f = std::fopen(path, "rb");
	if (!f) return false;
	const size_t n = std::fread(pal, 3, 256, f);
	std::fclose(f);
	return n == 256;
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

// 地面类型 → 可走性（先按名字判定；OpenRA 的权威来源是 Locomotor TerrainSpeeds）
bool WalkableTerrain(const std::string& t) {
	if (t == "Water" || t == "River") return false;
	if (t == "Rock" || t == "Cliff") return false;
	if (t == "Wall") return false;
	if (t == "Tree") return false;
	return true;
}

struct Template {
	int id = 0;
	std::string image;
	int sizeW = 1, sizeH = 1;
	int frameBase = 0;
	int frameCount = 0;
	std::map<int, std::string> tiles; // index → terrainType
};

} // namespace

// 为一个 tileset yaml 生成一套图集 + 元数据
bool BuildOne(const std::vector<MixArchive>& mixes, const uint8_t pal[256][3],
	const char* yamlPath, const char* outBmp, const char* outTxt) {
	YamlNode root;
	if (!ParseYaml(yamlPath, root)) return false;
	const YamlNode* templates = Child(root, "Templates");
	const YamlNode* terrain = Child(root, "Terrain");
	if (templates == nullptr) { std::fprintf(stderr, "[tileset] no Templates\n"); return 1; }

	// ---- 收集模板 ----
	std::vector<Template> tmpls;
	for (const YamlNode* t : ChildrenWithPrefix(*templates, "Template@")) {
		Template tp;
		if (const YamlNode* id = Child(*t, "Id")) tp.id = std::atoi(id->value.c_str());
		if (const YamlNode* img = Child(*t, "Images")) tp.image = img->value;
		if (const YamlNode* sz = Child(*t, "Size")) {
			std::sscanf(sz->value.c_str(), "%d,%d", &tp.sizeW, &tp.sizeH);
		}
		if (const YamlNode* tl = Child(*t, "Tiles")) {
			for (const auto& [k, v] : tl->ordered) {
				tp.tiles[std::atoi(k.c_str())] = v;
			}
			// 有 Tiles 列表时：frame 数 = 声明的条目数
		}
		if (!tp.image.empty()) tmpls.push_back(tp);
	}
	std::printf("[tileset] %zu templates, %zu terrain types\n", tmpls.size(),
		terrain ? ChildrenWithPrefix(*terrain, "TerrainType@").size() : 0);

	// ---- 每个模板取 TMP（同一 image 只解一次）----
	struct ImageFrames { int w = 24, h = 24; std::vector<TmpTile> tiles; };
	std::map<std::string, ImageFrames> imageCache;
	int missing = 0;
	for (Template& tp : tmpls) {
		if (imageCache.count(tp.image)) continue;
		const uint32_t id = ClassicHash(tp.image);
		const MixArchive* hit = nullptr;
		uint32_t hitOff = 0, hitLen = 0;
		for (const MixArchive& m : mixes) {
			auto it = m.index.find(id);
			if (it == m.index.end()) continue;
			if (m.dataStart + it->second.first + it->second.second > m.data.size()) continue;
			hit = &m;
			hitOff = it->second.first;
			hitLen = it->second.second;
			break;
		}
		if (hit == nullptr) { ++missing; continue; }
		const size_t start = hit->dataStart + hitOff;
		const size_t len = hitLen;
		std::vector<uint8_t> buf(hit->data.begin() + static_cast<long>(start),
			hit->data.begin() + static_cast<long>(start + len));
		ImageFrames frames;
		if (!ParseTmp(buf, frames.tiles) || frames.tiles.empty()) { ++missing; continue; }
		frames.w = frames.tiles[0].w;
		frames.h = frames.tiles[0].h;
		imageCache[tp.image] = std::move(frames);
	}
	std::printf("[tileset] resolved %zu images (%d missing)\n", imageCache.size(), missing);
	if (imageCache.empty()) {
		// 这套 tileset 的素材不在当前 mix 里（例如 DESERT 用 .des 扩展名，需要 desert.mix）
		std::fprintf(stderr, "[tileset] SKIP %s: no images resolved\n", yamlPath);
		return false;
	}

	// ---- 布局：每行 128 个 tile ----
	constexpr int kTilesPerRow = 128;
	constexpr int kCellW = 24;
	constexpr int kCellH = 24;
	std::vector<std::pair<const Template*, int>> slots; // (template, frameIndex)
	for (const auto& [img, frames] : imageCache) {
		(void)img;
		(void)frames;
	}
	// 按模板顺序登记 frame 区间
	int cursor = 0;
	std::vector<std::pair<std::string, int>> imageBase; // image → 起始 tile
	for (const auto& [img, frames] : imageCache) {
		imageBase.emplace_back(img, cursor);
		cursor += static_cast<int>(frames.tiles.size());
	}
	const int totalTiles = cursor;
	const int rows = (totalTiles + kTilesPerRow - 1) / kTilesPerRow;
	const int atlasW = kTilesPerRow * kCellW;
	const int atlasH = rows * kCellH;
	std::printf("[tileset] atlas %dx%d (%d tiles, %d rows)\n", atlasW, atlasH, totalTiles, rows);

	std::vector<uint8_t> atlas(static_cast<size_t>(atlasW) * atlasH * 4, 0);
	std::map<std::string, int> imageBaseMap(imageBase.begin(), imageBase.end());
	for (const auto& [img, base] : imageBase) {
		const ImageFrames& frames = imageCache[img];
		for (size_t i = 0; i < frames.tiles.size(); ++i) {
			const TmpTile& t = frames.tiles[i];
			if (!t.valid) continue;
			const int g = base + static_cast<int>(i);
			const int col = g % kTilesPerRow;
			const int row = g / kTilesPerRow;
			// 注意：不要做菱形蒙版。OpenRA 的 RA 网格是 Rectangular（正交铺贴，间距 = TileSize），
			// TMP 的 24x24 必须满幅铺满；裁成菱形会在正交网格里留下缝隙（出现棋盘格）。
			for (int y = 0; y < t.h; ++y) {
				for (int x = 0; x < t.w; ++x) {
					const uint8_t idx = t.indexed[static_cast<size_t>(y) * t.w + x];
					if (idx == 0) continue;
					const int ax = col * kCellW + x;
					const int ay = row * kCellH + y;
					if (ax < 0 || ay < 0 || ax >= atlasW || ay >= atlasH) continue;
					const size_t o = (static_cast<size_t>(ay) * atlasW + ax) * 4;
					atlas[o + 0] = static_cast<uint8_t>(std::min(255, pal[idx][0] * 4));
					atlas[o + 1] = static_cast<uint8_t>(std::min(255, pal[idx][1] * 4));
					atlas[o + 2] = static_cast<uint8_t>(std::min(255, pal[idx][2] * 4));
					atlas[o + 3] = 255;
				}
			}
		}
	}

	if (!WriteBmp32(outBmp, atlasW, atlasH, atlas)) {
		std::fprintf(stderr, "[tileset] write bmp failed\n");
		return 1;
	}

	// ---- 元数据 ----
	FILE* mf = std::fopen(outTxt, "w");
	if (!mf) { std::fprintf(stderr, "[tileset] write meta failed\n"); return 1; }
	std::fprintf(mf, "# Generated by rts_build_tileset from %s\n", yamlPath);
	std::fprintf(mf, "tilecols %d\n", kTilesPerRow);
	std::fprintf(mf, "cell %d %d\n", kCellW, kCellH);
	std::fprintf(mf, "rows %d\n", rows);
	std::fprintf(mf, "tiles %d\n", totalTiles);
	for (const auto& [img, base] : imageBase) {
		const ImageFrames& frames = imageCache[img];
		std::fprintf(mf, "image %s %d %d %d %d\n", img.c_str(), base,
			frames.w, frames.h, static_cast<int>(frames.tiles.size()));
	}
	for (const Template& tp : tmpls) {
		auto it = imageBaseMap.find(tp.image);
		if (it == imageBaseMap.end()) continue;
		const ImageFrames& frames = imageCache[tp.image];
		std::fprintf(mf, "template %d %s %d %d %d %d\n", tp.id, tp.image.c_str(),
			tp.sizeW, tp.sizeH, it->second, static_cast<int>(frames.tiles.size()));
		for (const auto& [idx, terr] : tp.tiles) {
			std::fprintf(mf, "tileterrain %d %d %s\n", tp.id, idx, terr.c_str());
		}
	}
	// TerrainType 属性
	if (terrain != nullptr) {
		for (const YamlNode* tt : ChildrenWithPrefix(*terrain, "TerrainType@")) {
			std::string name, color = "000000";
			if (const YamlNode* n = Child(*tt, "Type")) name = n->value;
			if (const YamlNode* c = Child(*tt, "Color")) color = c->value;
			if (name.empty()) continue;
			std::fprintf(mf, "terrain %s %d %s\n", name.c_str(), WalkableTerrain(name) ? 1 : 0, color.c_str());
		}
	}
	std::fclose(mf);
	std::printf("[tileset] wrote %s + %s\n", outBmp, outTxt);
	return true;
}

// 从 yaml 里取 General.Id（用作输出文件名）
std::string TilesetIdOf(const char* yamlPath) {
	YamlNode root;
	if (!ParseYaml(yamlPath, root)) return "UNKNOWN";
	const YamlNode* general = Child(root, "General");
	if (general != nullptr) {
		if (const YamlNode* id = Child(*general, "Id")) return id->value;
	}
	const std::filesystem::path p(yamlPath);
	return p.stem().string();
}

int main(int argc, char** argv) {
	if (argc < 5) {
		std::fprintf(stderr,
			"usage: rts_build_tileset <mixDirOrFile> <pal> <yamlFileOrDir> <outDir>\n"
			"  outDir receives tileset_<ID>.bmp and tileset_<ID>.txt per yaml\n");
		return 1;
	}
	const char* mixPath = argv[1];
	const char* palPath = argv[2];
	const char* yamlPath = argv[3];
	const std::string outDir = argv[4];

	// mixPath 可以是目录（扫描全部 .mix）或单个文件
	std::vector<MixArchive> mixes;
	{
		const std::filesystem::path p(mixPath);
		if (std::filesystem::is_directory(p)) {
			std::vector<std::filesystem::path> files;
			for (const auto& e : std::filesystem::directory_iterator(p)) {
				if (e.is_regular_file()) {
					std::string ext = e.path().extension().string();
					for (char& ch : ext) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
					if (ext == ".mix") files.push_back(e.path());
				}
			}
			std::sort(files.begin(), files.end());
			for (const auto& fp : files) {
				MixArchive m;
				if (LoadMix(fp.string().c_str(), m)) {
					std::printf("[tileset] mix %s: %zu files\n", fp.filename().string().c_str(), m.index.size());
					mixes.push_back(std::move(m));
				}
			}
		} else {
			MixArchive m;
			if (!LoadMix(mixPath, m)) { std::fprintf(stderr, "[tileset] mix load failed\n"); return 1; }
			mixes.push_back(std::move(m));
		}
	}
	if (mixes.empty()) { std::fprintf(stderr, "[tileset] no mix loaded\n"); return 1; }
	uint8_t pal[256][3] = {};
	if (!ReadPalette(palPath, pal)) { std::fprintf(stderr, "[tileset] palette load failed\n"); return 1; }

	// yaml 可以是目录（批量）或单个文件
	std::vector<std::filesystem::path> yamls;
	{
		const std::filesystem::path p(yamlPath);
		if (std::filesystem::is_directory(p)) {
			for (const auto& e : std::filesystem::directory_iterator(p)) {
				if (!e.is_regular_file()) continue;
				std::string ext = e.path().extension().string();
				for (char& ch : ext) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
				if (ext == ".yaml") yamls.push_back(e.path());
			}
			std::sort(yamls.begin(), yamls.end());
		} else {
			yamls.push_back(p);
		}
	}
	if (yamls.empty()) { std::fprintf(stderr, "[tileset] no yaml\n"); return 1; }

	int ok = 0;
	for (const auto& y : yamls) {
		const std::string id = TilesetIdOf(y.string().c_str());
		const std::string bmp = outDir + "/tileset_" + id + ".bmp";
		const std::string txt = outDir + "/tileset_" + id + ".txt";
		std::printf("[tileset] === %s (id=%s) ===\n", y.filename().string().c_str(), id.c_str());
		if (BuildOne(mixes, pal, y.string().c_str(), bmp.c_str(), txt.c_str())) ++ok;
		else std::fprintf(stderr, "[tileset] FAILED: %s\n", y.string().c_str());
	}
	std::printf("[tileset] %d/%zu tilesets built\n", ok, yamls.size());
	return ok == static_cast<int>(yamls.size()) ? 0 : 1;
}
