// RTS Engine — render/text.h
// 位图字体文字渲染：字符串 → 屏幕坐标纹理四边形。
#pragma once

#include <string>
#include <vector>
#include "sprite_batch.h"
#include "font.h"

namespace rts {
namespace text {

// 生成文字顶点（屏幕坐标，每个字符一个四边形）
inline void Build(std::vector<SpriteVertex>& out, const std::string& s,
	float x, float y, float scale, uint32_t color) {
	float cx = x;
	for (const char c : s) {
		if (c == ' ') { cx += BitmapFont::kGlyphW * scale; continue; }
		float u0 = 0, v0 = 0, u1 = 0, v1 = 0;
		BitmapFont::GlyphUV(c, u0, v0, u1, v1);
		SpriteVertex v[6];
		BuildQuad(cx, y, BitmapFont::kGlyphW * scale, BitmapFont::kGlyphH * scale,
			u0, v0, u1, v1, color, v);
		out.insert(out.end(), v, v + 6);
		cx += BitmapFont::kGlyphW * scale;
	}
}

inline float Measure(const std::string& s, float scale = 1.0f) {
	return static_cast<float>(s.size()) * static_cast<float>(BitmapFont::kGlyphW) * scale;
}

} // namespace text
} // namespace rts
