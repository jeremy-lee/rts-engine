// RTS Engine — render/sheet.h
// 移植自 OpenRA 的 Sheet / Sprite 语义（OpenRA.Game/Graphics/Sprite.cs, SheetBuilder.cs）：
//   Sheet  = 一张纹理（width/height）；
//   Sprite = Sheet 内的矩形（bounds）+ 预计算的 UV（含 inset，避免采样越界）。
// 与 OpenRA 一致：UV inset 1/128 像素，避免 GPU 在非 1:1 framebuffer 采样到矩形外。
#pragma once

#include <cstdint>

namespace rts {

// 图集（一张纹理）
struct Sheet {
	uint32_t texture = 0; // GL 纹理对象
	int32_t width = 0;
	int32_t height = 0;

	bool Valid() const { return texture != 0 && width > 0 && height > 0; }
};

// 图集内的一个精灵矩形 + 预计算 UV（OpenRA Sprite.Bounds + Left/Top/Right/Bottom）
struct SpriteRect {
	int32_t x = 0;
	int32_t y = 0;
	int32_t w = 0;
	int32_t h = 0;
	float u0 = 0.0f;
	float v0 = 0.0f;
	float u1 = 0.0f;
	float v1 = 0.0f;
};

// OpenRA 的 UV inset：1/128 像素（Sprite 构造函数中的 Inset 常量）
inline constexpr float kSpriteInset = 1.0f / 128.0f;

// 计算 sprite 在 sheet 内的 UV（与 OpenRA Sprite 构造函数一致）
inline SpriteRect MakeSpriteRect(const Sheet& sheet, int32_t x, int32_t y, int32_t w, int32_t h) {
	SpriteRect r;
	r.x = x;
	r.y = y;
	r.w = w;
	r.h = h;
	const float sw = static_cast<float>(sheet.width);
	const float sh = static_cast<float>(sheet.height);
	r.u0 = (static_cast<float>(x) + kSpriteInset) / sw;
	r.v0 = (static_cast<float>(y) + kSpriteInset) / sh;
	r.u1 = (static_cast<float>(x + w) - kSpriteInset) / sw;
	r.v1 = (static_cast<float>(y + h) - kSpriteInset) / sh;
	return r;
}

} // namespace rts
