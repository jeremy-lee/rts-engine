// RTS Engine — render/ui.h
// 轻量 UI：面板/按钮（屏幕坐标），点击检测。
#pragma once

#include <string>
#include <vector>
#include "sprite_batch.h"

namespace rts {
namespace ui {

struct Rect {
	float x = 0, y = 0, w = 0, h = 0;
	bool Contains(float px, float py) const {
		return px >= x && px <= x + w && py >= y && py <= y + h;
	}
};

struct Button {
	Rect rect;
	std::string label;
	bool enabled = true;
	bool hovered = false;

	bool Hit(float px, float py) const { return enabled && rect.Contains(px, py); }
};

// 纯色面板
inline void BuildPanel(std::vector<SpriteVertex>& out, const Rect& r, uint32_t argb) {
	SpriteVertex v[6];
	BuildQuad(r.x, r.y, r.w, r.h, 0.0f, 0.0f, 1.0f, 1.0f, argb, v);
	out.insert(out.end(), v, v + 6);
}

// 带边框的按钮（描边用 4 个细矩形）
inline void BuildButton(std::vector<SpriteVertex>& out, const Button& b, uint32_t fill, uint32_t border) {
	const float t = 2.0f; // 边框厚度
	BuildPanel(out, Rect{b.rect.x, b.rect.y, b.rect.w, t}, border);
	BuildPanel(out, Rect{b.rect.x, b.rect.y + b.rect.h - t, b.rect.w, t}, border);
	BuildPanel(out, Rect{b.rect.x, b.rect.y, t, b.rect.h}, border);
	BuildPanel(out, Rect{b.rect.x + b.rect.w - t, b.rect.y, t, b.rect.h}, border);
	BuildPanel(out, Rect{b.rect.x + t, b.rect.y + t, b.rect.w - 2 * t, b.rect.h - 2 * t}, fill);
}

} // namespace ui
} // namespace rts
