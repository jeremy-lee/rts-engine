// RTS Engine — render/assets.h
// 程序化美术资产：用代码绘制像素风精灵图集（零外部文件、零版权风险）。
// 图集：每格 32×32，8 列 × 2 行。
#pragma once

#include <cstdint>
#include <vector>

namespace rts {

enum class SpriteId : uint8_t {
	Grass = 0,
	Water = 1,
	Sand = 2,
	Rock = 3,
	Highland = 4,
	Infantry = 5,
	Tank = 6,
	Harvester = 7,
	Barracks = 8,
	Tree = 9,
	Rocks = 10,
	Shadow = 11,
	Count = 12,
};

class AssetAtlas {
public:
	static constexpr int kCell = 32;
	static constexpr int kCols = 8;
	static constexpr int kRows = 2;

	// 生成 RGBA 图集像素
	static std::vector<uint8_t> Build(int& outW, int& outH);
	// 精灵在图集中的 UV（[0,1]）
	static void GetUV(SpriteId id, float& u0, float& v0, float& u1, float& v1);

private:
	static int Index(SpriteId id) { return static_cast<int>(id); }
};

} // namespace rts
