// RTS Engine — render/cnc_assets.h
// C&C（泰伯利亚黎明）素材图集：从 OpenRA 提取的 SHP 转换而来（占位美术）。
// 图集：8 列 × 1 行，每格 80×64（精灵居中）。
#pragma once

#include "core/wangle.h"
#include "render/sheet.h"

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

	// 精灵 UV（[0,1]）：team 0=蓝方, 1=红方；facing 0-7（0=北，顺时针）；frame 0=stand,1-4=run
	// 图集布局：
	//   行 0-39：单位（ui*10 + frame*2 + team，列 = facing + team*8）
	//   行 40-43：建筑（Barracks 40/41、Factory 42/43，列 0）
	// 精灵 UV（[0,1]）：team 0=蓝方, 1=红方；facing 为 WAngle；frame 0=stand,1-6=run
	// 图集布局（64 列 × 34 行，格子 80×64）：
	//   行 0：Tank 32 朝向 × 2 阵营（车体+炮塔合成）
	//   行 1：LightTank 32 朝向 × 2 阵营
	//   行 2-15：Infantry 8 朝向 × 7 帧 × 2 阵营
	//   行 16-29：Harvester 8 朝向 × 7 帧 × 2 阵营
	//   行 30-33：Barracks / Factory × 2 阵营
	static void GetUV(CncSpriteId id, int team, WAngle facing, int frame, float& u0, float& v0, float& u1, float& v1) {
		const int t = (team == 1) ? 1 : 0;
		const int k = (frame < 0) ? 0 : (frame >= kUnitFrames ? kUnitFrames - 1 : frame);
		int row = 0;
		int col = 0;
		bool isTank = false;   // 32 朝向（无动画帧）
		bool isUnit8 = false;  // 8 朝向 + 动画帧
		switch (id) {
			case CncSpriteId::Tank:      row = 0;  isTank = true; break;
			case CncSpriteId::LightTank: row = 1;  isTank = true; break;
			case CncSpriteId::Infantry:  row = 2;  isUnit8 = true; break;
			case CncSpriteId::Rocket:    row = 2;  isUnit8 = true; break;
			case CncSpriteId::Harvester: row = 16; isUnit8 = true; break;
			case CncSpriteId::Barracks:  row = 30; break;
			case CncSpriteId::Factory:   row = 32; break;
			default: row = 2; isUnit8 = true; break;
		}
		if (isTank) {
			const int f32 = facing.angle / 32; // 0-31
			col = ((32 - f32) % 32) + t * 32;
		} else if (isUnit8) {
			const int f8 = facing.angle / 128; // 0-7（与 SHP 逆时针一致）
			row += k * 2 + t;
			col = ((8 - f8) % 8) + t * 8;
		} else {
			row += t;
			col = 0;
		}
		// OpenRA Sheet/Sprite 语义：预计算 bounds 的 UV（含 1/128 像素 inset，避免采样越界）
		int tw = 16, th = 16;
		GetContentSize(id, tw, th);
		const int ox = col * kCellW + (kCellW - tw) / 2;
		const int oy = row * kCellH + (kCellH - th) / 2;
		Sheet& sheet = ImgSheet();
		if (!sheet.Valid()) {
			sheet.width = kCols * kCellW;
			sheet.height = kRows * kCellH;
		}
		const SpriteRect r = MakeSpriteRect(sheet, ox, oy, tw, th);
		u0 = r.u0; v0 = r.v0; u1 = r.u1; v1 = r.v1;
	}

	// 图集里精灵的尺寸 = 裁剪到内容的紧凑 bbox（OpenRA TrimmedFrame 语义）
	// 8 朝向的 stand 帧最大 bbox（去掉了 SHP 画布的 padding）
	// 图集内容是裁剪后的紧凑 bbox（OpenRA TrimmedFrame 语义）
	static void GetContentSize(CncSpriteId id, int& w, int& h) {
		switch (id) {
			case CncSpriteId::Infantry:  w = 14; h = 17; break;
			case CncSpriteId::Rocket:    w = 14; h = 17; break;
			case CncSpriteId::Tank:      w = 24; h = 22; break; // 32 朝向最大 bbox（斜向 24 宽）
			case CncSpriteId::LightTank: w = 22; h = 20; break;
			case CncSpriteId::Harvester: w = 40; h = 27; break;
			case CncSpriteId::Barracks:  w = 48; h = 39; break;
			case CncSpriteId::Factory:   w = 72; h = 24; break;
			default: w = 14; h = 17; break;
		}
	}

	// OpenRA 语义：Sprite.Size = 帧 bounds 尺寸（1:1 像素），放大由相机 zoom 负责
	static void GetSize(CncSpriteId id, int& w, int& h) {
		int cw = 50, ch = 39;
		GetContentSize(id, cw, ch);
		w = cw;
		h = ch;
	}
};

} // namespace rts
