// RTS Engine — render/cnc_assets.h
// C&C（泰伯利亚黎明）素材图集：从 OpenRA 提取的 SHP 转换而来（占位美术）。
// 图集：8 列 × 1 行，每格 80×64（精灵居中）。
#pragma once

#include <cstdint>
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
	static constexpr int kCols = 16;
	static constexpr int kRows = 60; // 单位 56 行（4 单位 × 7 帧 × 2 阵营）+ 建筑 4 行

	// 加载 PNG（stb_image），输出 RGBA 像素
	static bool Load(const std::string& path, std::vector<uint8_t>& outPixels, int& outW, int& outH);

	static constexpr int kUnitFrames = 7; // 0 = stand，1-6 = run 动画（完整 6 帧循环）

	// 精灵 UV（[0,1]）：team 0=蓝方, 1=红方；facing 0-7（0=北，顺时针）；frame 0=stand,1-4=run
	// 图集布局：
	//   行 0-39：单位（ui*10 + frame*2 + team，列 = facing + team*8）
	//   行 40-43：建筑（Barracks 40/41、Factory 42/43，列 0）
	static void GetUV(CncSpriteId id, int team, int facing, int frame, float& u0, float& v0, float& u1, float& v1) {
		const int t = (team == 1) ? 1 : 0;
		const int k = (frame < 0) ? 0 : (frame >= kUnitFrames ? kUnitFrames - 1 : frame);
		int row = 0;
		int col = 0;
		bool isUnit = true;
		switch (id) {
			case CncSpriteId::Infantry:  row = 0 * 14; break;
			case CncSpriteId::Rocket:    row = 0 * 14; break;
			case CncSpriteId::Tank:      row = 1 * 14; break;
			case CncSpriteId::LightTank: row = 2 * 14; break;
			case CncSpriteId::Harvester: row = 3 * 14; break;
			case CncSpriteId::Barracks:  row = 56; isUnit = false; break;
			case CncSpriteId::Factory:   row = 58; isUnit = false; break;
			default: row = 0; break;
		}
		if (isUnit) {
			// 朝向映射到 SHP 帧（C&C SHP 逆时针：0=N, 1=NW, ...）
			const int f = ((facing % 8) + 8) % 8;
			const int shpFrame = (8 - f) % 8;
			row += k * 2 + t;
			col = shpFrame + t * 8;
		} else {
			row += t;
			col = 0;
		}
		const float texW = static_cast<float>(kCols * kCellW);
		const float texH = static_cast<float>(kRows * kCellH);
		int tw = 16, th = 16;
		GetContentSize(id, tw, th);
		const int ox = col * kCellW + (kCellW - tw) / 2;
		const int oy = row * kCellH + (kCellH - th) / 2;
		u0 = static_cast<float>(ox) / texW;
		v0 = static_cast<float>(oy) / texH;
		u1 = static_cast<float>(ox + tw) / texW;
		v1 = static_cast<float>(oy + th) / texH;
	}

	// 图集里精灵的尺寸 = 裁剪到内容的紧凑 bbox（OpenRA TrimmedFrame 语义）
	// 8 朝向的 stand 帧最大 bbox（去掉了 SHP 画布的 padding）
	static void GetContentSize(CncSpriteId id, int& w, int& h) {
		switch (id) {
			case CncSpriteId::Infantry:  w = 14; h = 17; break;
			case CncSpriteId::Rocket:    w = 14; h = 17; break;
			case CncSpriteId::Tank:      w = 13; h = 21; break; // 车体+炮塔合成
			case CncSpriteId::LightTank: w = 12; h = 19; break; // 车体+炮塔合成
			case CncSpriteId::Harvester: w = 40; h = 27; break;
			case CncSpriteId::Barracks:  w = 48; h = 39; break;
			case CncSpriteId::Factory:   w = 72; h = 24; break;
			default: w = 14; h = 17; break;
		}
	}

	// 绘制尺寸 = 紧凑尺寸 × 放大倍数（小单位 5x 大建筑 2-3x，保证屏上视觉统一）
	static void GetSize(CncSpriteId id, int& w, int& h) {
		int cw = 16, ch = 17;
		GetContentSize(id, cw, ch);
		const int scale = (cw >= 60) ? 2 : ((cw >= 30) ? 3 : 5);
		w = cw * scale;
		h = ch * scale;
	}
};

} // namespace rts
