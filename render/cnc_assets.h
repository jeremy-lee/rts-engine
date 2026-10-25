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
	static constexpr int kRows = 6; // 行 0-3 单位、行 4 建筑、行 5 地形

	// 加载 PNG（stb_image），输出 RGBA 像素
	static bool Load(const std::string& path, std::vector<uint8_t>& outPixels, int& outW, int& outH);

	// 精灵 UV（[0,1]）：team 0=蓝方, 1=红方；facing 0-7（0=北，顺时针）
	// 图集布局：
	//   行 0-3：Infantry / Tank / LightTank / Harvester（8 朝向 × 2 阵营 = 16 列）
	//   行 4：Barracks 蓝(0)/红(1)、Factory 蓝(2)/红(3)
	static void GetUV(CncSpriteId id, int team, int facing, float& u0, float& v0, float& u1, float& v1) {
		int row = 0;
		int col = 0;
		const int t = (team == 1) ? 1 : 0;
		switch (id) {
			case CncSpriteId::Infantry:  row = 0; break;
			case CncSpriteId::Rocket:    row = 0; break;
			case CncSpriteId::Tank:      row = 1; break;
			case CncSpriteId::LightTank: row = 2; break;
			case CncSpriteId::Harvester: row = 3; break;
			case CncSpriteId::Barracks:  row = 4; col = 0 + t; break;
			case CncSpriteId::Factory:   row = 4; col = 2 + t; break;
			case CncSpriteId::Grass:     row = 5; col = 0; break;
			case CncSpriteId::Water:     row = 5; col = 1; break;
			case CncSpriteId::Sand:      row = 5; col = 2; break;
			case CncSpriteId::Highland:  row = 5; col = 3; break;
			default: row = 0; break;
		}
		if (row < 4) {
			// 单位：朝向映射到 SHP 帧（C&C SHP 逆时针：0=N, 1=NW, ...）
			const int f = ((facing % 8) + 8) % 8;
			const int shpFrame = (8 - f) % 8;
			col = shpFrame + t * 8;
		}
		const float texW = static_cast<float>(kCols * kCellW);
		const float texH = static_cast<float>(kRows * kCellH);
		// 只取格子中心的内容区域（精灵是紧凑帧，居中放置）
		int tw = 16, th = 16;
		GetContentSize(id, tw, th);
		const int ox = col * kCellW + (kCellW - tw) / 2;
		const int oy = row * kCellH + (kCellH - th) / 2;
		u0 = static_cast<float>(ox) / texW;
		v0 = static_cast<float>(oy) / texH;
		u1 = static_cast<float>(ox + tw) / texW;
		v1 = static_cast<float>(oy + th) / texH;
	}

	// 图集里精灵的尺寸 = SHP 画布尺寸（OpenRA ShpTDSprite.Size 语义：所有帧共用同一画布）
	// 各朝向帧内容在画布内的位置由 SHP 数据决定，所以整画布渲染不会裁剪/跳动
	static void GetContentSize(CncSpriteId id, int& w, int& h) {
		switch (id) {
			case CncSpriteId::Infantry:  w = 50; h = 39; break;
			case CncSpriteId::Rocket:    w = 50; h = 39; break;
			case CncSpriteId::Tank:      w = 36; h = 36; break;
			case CncSpriteId::LightTank: w = 24; h = 24; break;
			case CncSpriteId::Harvester: w = 48; h = 48; break;
			case CncSpriteId::Barracks:  w = 48; h = 48; break;
			case CncSpriteId::Factory:   w = 72; h = 48; break;
			default: w = 50; h = 39; break;
		}
	}

	// 绘制尺寸 = 画布尺寸 × 放大倍数
	static void GetSize(CncSpriteId id, int& w, int& h) {
		int cw = 50, ch = 39;
		GetContentSize(id, cw, ch);
		const int scale = (cw >= 60) ? 2 : 3; // 工厂/建筑 2x，其余 3x
		w = cw * scale;
		h = ch * scale;
	}
};

} // namespace rts
