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
	Count = 7,
};

class CncAtlas {
public:
	static constexpr int kCellW = 80;
	static constexpr int kCellH = 64;
	static constexpr int kCols = 8;
	static constexpr int kRows = 2; // 0 = 蓝方, 1 = 红方

	// 加载 PNG（stb_image），输出 RGBA 像素
	static bool Load(const std::string& path, std::vector<uint8_t>& outPixels, int& outW, int& outH);

	// 精灵 UV（[0,1]）：team 0=蓝方, 1=红方
	static void GetUV(CncSpriteId id, int team, float& u0, float& v0, float& u1, float& v1) {
		const int col = static_cast<int>(id);
		const int row = (team == 1) ? 1 : 0;
		const float texW = static_cast<float>(kCols * kCellW);
		const float texH = static_cast<float>(kRows * kCellH);
		u0 = static_cast<float>(col * kCellW) / texW;
		v0 = static_cast<float>(row * kCellH) / texH;
		u1 = static_cast<float>((col + 1) * kCellW) / texW;
		v1 = static_cast<float>((row + 1) * kCellH) / texH;
	}

	// 绘制尺寸：RA 原版单位（16x20），按内容尺寸放大以在 1280x720 下清晰
	static void GetSize(CncSpriteId id, int& w, int& h) {
		switch (id) {
			case CncSpriteId::Infantry:  w = 16 * 4; h = 20 * 4; break; // 64x80
			case CncSpriteId::Rocket:    w = 16 * 4; h = 20 * 4; break; // 64x80
			case CncSpriteId::Tank:      w = 16 * 4; h = 20 * 4; break; // 64x80
			case CncSpriteId::LightTank: w = 14 * 4; h = 18 * 4; break; // 56x72
			case CncSpriteId::Harvester: w = 28 * 3; h = 28 * 3; break; // 84x84
			case CncSpriteId::Barracks:  w = 48 * 2; h = 42 * 2; break; // 96x84
			case CncSpriteId::Factory:   w = 72 * 2; h = 26 * 2; break; // 144x52
			default: w = 48; h = 48; break;
		}
	}
};

} // namespace rts
