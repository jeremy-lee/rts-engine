// RTS Engine — render/software_renderer.h
// 软件渲染后端（M14）：CPU 光栅化到内存位图，零第三方依赖。
// 用途：验证渲染抽象可替换性；与 GL 后端做性能/一致性对比。
#pragma once

#include <cstdint>
#include <vector>

namespace rts {

class SoftwareRenderer {
public:
	void Resize(int width, int height);
	void Clear(uint32_t argb);
	// 填充矩形（带 alpha 混合），屏幕坐标
	void FillRect(float x, float y, float w, float h, uint32_t argb);

	int Width() const { return m_width; }
	int Height() const { return m_height; }
	// 把 ARGB 内部缓冲同步为 RGBA（上传纹理前调用）
	void SyncRgba();

	// RGBA 像素（用于上传纹理）
	const std::vector<uint8_t>& Rgba() const { return m_rgba; }

	uint64_t PixelsDrawn() const { return m_pixelsDrawn; }
	void ResetStats() { m_pixelsDrawn = 0; }

private:
	int m_width = 0;
	int m_height = 0;
	std::vector<uint32_t> m_argb;
	std::vector<uint8_t> m_rgba;
	uint64_t m_pixelsDrawn = 0;
};

} // namespace rts
