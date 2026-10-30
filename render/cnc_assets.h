// RTS Engine — render/cnc_assets.h
// C&C（泰伯利亚黎明）素材图集：从 OpenRA 提取的 SHP 转换而来（占位美术）。
// 图集：8 列 × 1 行，每格 80×64（精灵居中）。
#pragma once

#include "core/wangle.h"
#include "render/sheet.h"

#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace rts {

enum class CncSpriteId : uint8_t {
	Infantry = 0,   // e1 步枪兵
	Rocket = 1,     // e3 火箭兵
	Tank = 2,       // mtnk 中型坦克
	LightTank = 3,  // ltnk 轻型坦克
	Harvester = 4,  // harv 采矿车
	Barracks = 5,   // pyle 兵营
	Factory = 6,    // fact 建造厂
	// 地形 tile（图集行 5）
	Grass = 7,     // clear1 草地
	Water = 8,     // w1 水面
	Sand = 9,      // sh01 沙地
	Highland = 10, // clear1 变体（高地）
	Count = 11,
};

class CncAtlas {
public:
	static constexpr int kCellW = 80;
	static constexpr int kCellH = 64;
	static constexpr int kCols = 64; // 32 朝向 × 2 阵营（坦克）
	static constexpr int kRows = 34; // 2 坦克行 + 28 步兵/矿车行 + 4 建筑行

	// 加载 PNG（stb_image），输出 RGBA 像素
	static bool Load(const std::string& path, std::vector<uint8_t>& outPixels, int& outW, int& outH);

	// OpenRA Sheet 语义：图集尺寸（Load 后设置，GetUV 用 MakeSpriteRect 计算 UV）
	static Sheet& ImgSheet() {
		static Sheet s;
		return s;
	}

	static constexpr int kUnitFrames = 7; // 0 = stand，1-6 = run 动画（完整 6 帧循环）

	// ---- 资源驱动的精灵元数据（OpenRA sequences yaml 语义）----
	// 尺寸/布局不再硬编码：从 assets/cnc_atlas.txt 加载（图集变化时无需改代码）。
	struct SpriteMeta {
		int row = 0;
		int w = 0; // 0 = 未加载（面板据此过滤）
		int h = 0;
		int facings = 8; // 8 / 32 / 0（建筑）
		int frames = 1;  // 动画帧数
	};

	static const char* SpriteName(CncSpriteId id) {
		switch (id) {
			case CncSpriteId::Infantry:  return "Infantry";
			case CncSpriteId::Rocket:    return "Rocket";
			case CncSpriteId::Tank:      return "Tank";
			case CncSpriteId::LightTank: return "LightTank";
			case CncSpriteId::Harvester: return "Harvester";
			case CncSpriteId::Barracks:  return "Barracks";
			case CncSpriteId::Factory:   return "Factory";
			case CncSpriteId::Grass:     return "Grass";
			case CncSpriteId::Water:     return "Water";
			case CncSpriteId::Sand:      return "Sand";
			case CncSpriteId::Highland:  return "Highland";
			default: return "Unknown";
		}
	}

	static CncSpriteId SpriteIdFromName(const std::string& n) {
		for (int i = 0; i < static_cast<int>(CncSpriteId::Count); ++i) {
			const CncSpriteId id = static_cast<CncSpriteId>(i);
			if (n == SpriteName(id)) return id;
		}
		return CncSpriteId::Count;
	}

	// ---- 动态精灵表：前 CncSpriteId::Count 项是引擎内置精灵，运行时导入的追加在后面 ----
	static std::vector<SpriteMeta>& MetaTable() {
		static std::vector<SpriteMeta> t(static_cast<size_t>(CncSpriteId::Count));
		return t;
	}
	static std::vector<std::string>& NameTable() {
		static std::vector<std::string> t = [] {
			std::vector<std::string> v;
			for (int i = 0; i < static_cast<int>(CncSpriteId::Count); ++i)
				v.emplace_back(SpriteName(static_cast<CncSpriteId>(i)));
			return v;
		}();
		return t;
	}
	static int Count() { return static_cast<int>(MetaTable().size()); }
	static const char* NameAt(int i) {
		const auto& t = NameTable();
		return (i >= 0 && i < static_cast<int>(t.size())) ? t[static_cast<size_t>(i)].c_str() : "";
	}
	static int FindByName(const std::string& n) {
		const auto& t = NameTable();
		for (int i = 0; i < static_cast<int>(t.size()); ++i) {
			if (t[static_cast<size_t>(i)] == n) return i;
		}
		return -1;
	}

	// 加载图集元数据（OpenRA sequences 的等价物）。未知精灵名会被注册（动态追加）。
	static bool LoadMeta(const std::string& path) {
		std::ifstream in(path);
		if (!in) return false;
		std::string line;
		while (std::getline(in, line)) {
			if (line.empty() || line[0] == '#') continue;
			std::istringstream iss(line);
			std::string key;
			iss >> key;
			if (key == "rows") {
				int rows = 0;
				if (iss >> rows && rows > 0) {
					ImgSheet().width = kCols * kCellW;
					ImgSheet().height = rows * kCellH;
				}
				continue;
			}
			if (key != "sprite") continue;
			std::string name;
			SpriteMeta m;
			if (!(iss >> name >> m.row >> m.w >> m.h >> m.facings >> m.frames)) continue;
			int idx = FindByName(name);
			if (idx < 0) {
				idx = Count();
				MetaTable().push_back(m);
				NameTable().push_back(name);
			} else {
				MetaTable()[static_cast<size_t>(idx)] = m;
			}
		}
		return true;
	}

	// 精灵 UV（按动态索引）
	static void GetUVAt(int index, int team, WAngle facing, int frame, float& u0, float& v0, float& u1, float& v1) {
		if (index < 0 || index >= Count()) { u0 = 0; v0 = 0; u1 = 1; v1 = 1; return; }
		const int t = (team == 1) ? 1 : 0;
		const SpriteMeta& m = MetaTable()[static_cast<size_t>(index)];
		const int k = (frame < 0) ? 0 : (frame >= m.frames ? m.frames - 1 : frame);
		int row = m.row;
		int col = 0;
		if (m.facings == 32) {
			const int f32 = facing.angle / 32; // 0-31
			col = ((32 - f32) % 32) + t * 32;
		} else if (m.facings == 8) {
			const int f8 = facing.angle / 128; // 0-7（与 SHP 逆时针一致）
			row += k * 2 + t;
			col = ((8 - f8) % 8) + t * 8;
		} else {
			row += t;
			col = 0;
		}
		// OpenRA Sheet/Sprite 语义：预计算 bounds 的 UV（含 1/128 像素 inset，避免采样越界）
		const int ox = col * kCellW + (kCellW - m.w) / 2;
		const int oy = row * kCellH + (kCellH - m.h) / 2;
		Sheet& sheet = ImgSheet();
		if (!sheet.Valid()) {
			sheet.width = kCols * kCellW;
			sheet.height = kRows * kCellH;
		}
		const SpriteRect r = MakeSpriteRect(sheet, ox, oy, m.w, m.h);
		u0 = r.u0; v0 = r.v0; u1 = r.u1; v1 = r.v1;
	}

	// 精灵尺寸（按动态索引）
	static void GetSizeAt(int index, int& w, int& h) {
		if (index < 0 || index >= Count()) { w = 16; h = 16; return; }
		const SpriteMeta& m = MetaTable()[static_cast<size_t>(index)];
		w = m.w;
		h = m.h;
	}

	// 兼容 CncSpriteId 的便捷包装
	static void GetUV(CncSpriteId id, int team, WAngle facing, int frame, float& u0, float& v0, float& u1, float& v1) {
		GetUVAt(static_cast<int>(id), team, facing, frame, u0, v0, u1, v1);
	}
	static void GetContentSize(CncSpriteId id, int& w, int& h) { GetSizeAt(static_cast<int>(id), w, h); }
	static void GetSize(CncSpriteId id, int& w, int& h) { GetSizeAt(static_cast<int>(id), w, h); }
};

} // namespace rts
