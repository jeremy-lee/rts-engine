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

	// 加载 PNG（stb_image），输出 RGBA 像素
	static bool Load(const std::string& path, std::vector<uint8_t>& outPixels, int& outW, int& outH);

	// 精灵 UV（[0,1]）——内联（不依赖纹理）
	static void GetUV(CncSpriteId id, float& u0, float& v0, float& u1, float& v1) {
		const int idx = static_cast<int>(id);
		const int col = idx % kCols;
		const int row = idx / kCols;
		const float texW = static_cast<float>(kCols * kCellW);
		const float texH = static_cast<float>(kCellH);
		u0 = static_cast<float>(col * kCellW) / texW;
		v0 = static_cast<float>(row * kCellH) / texH;
		u1 = static_cast<float>((col + 1) * kCellW) / texW;
		v1 = static_cast<float>((row + 1) * kCellH) / texH;
	}

	// 绘制尺寸（素材原始像素尺寸）——内联（纯查表）
	static void GetSize(CncSpriteId id, int& w, int& h) {
		switch (id) {
			case CncSpriteId::Infantry:  w = 50; h = 39; break;
			case CncSpriteId::Rocket:    w = 50; h = 39; break;
			case CncSpriteId::Tank:      w = 36; h = 36; break;
			case CncSpriteId::LightTank: w = 24; h = 24; break;
			case CncSpriteId::Harvester: w = 48; h = 48; break;
			case CncSpriteId::Barracks:  w = 48; h = 48; break;
			case CncSpriteId::Factory:   w = 72; h = 48; break;
			default: w = 32; h = 32; break;
		}
	}
};

} // namespace rts
