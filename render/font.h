// RTS Engine — render/font.h
// 零依赖位图字体（5×7 像素，ASCII 95 字符，小写映射大写）。
// 用途：HUD 文字（资源、单位数、按钮标签）。
// 纹理数据由字体位图生成，渲染时按字符 UV 采样。
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace rts {

class BitmapFont {
public:
	static constexpr int kGlyphW = 6;   // 含 1px 间距
	static constexpr int kGlyphH = 8;   // 含 1px 行距
	static constexpr int kFirstChar = 32;
	static constexpr int kLastChar = 126;
	static constexpr int kCharCount = kLastChar - kFirstChar + 1;

	// 生成字体纹理（RGBA，白色着色，alpha=1 表示有笔画）
	// 返回像素数据；outW/outH 为纹理尺寸
	static std::vector<uint8_t> BuildTextureData(int& outW, int& outH);

	// 字符 → 纹理 UV（[0,1]）
	static void GlyphUV(char c, float& u0, float& v0, float& u1, float& v1);

	// 文本宽度（像素）
	static float MeasureText(const std::string& text, float scale = 1.0f) {
		return static_cast<float>(text.size()) * static_cast<float>(kGlyphW) * scale;
	}

private:
	// 5×7 点阵：每字符 7 行 × 5 列（bit4=最左）
	static const uint8_t* GlyphBits(char c);
};

} // namespace rts
