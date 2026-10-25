// RTS Engine — render/software_renderer.cpp
#include "software_renderer.h"

#include <algorithm>
#include <cmath>

namespace rts {

void SoftwareRenderer::Resize(int width, int height) {
	if (width == m_width && height == m_height) return;
	m_width = width;
	m_height = height;
	m_argb.assign(static_cast<size_t>(width) * height, 0xFF000000u);
	m_rgba.assign(static_cast<size_t>(width) * height * 4, 0);
}

void SoftwareRenderer::Clear(uint32_t argb) {
	std::fill(m_argb.begin(), m_argb.end(), argb);
}

void SoftwareRenderer::FillRect(float x, float y, float w, float h, uint32_t argb) {
	if (w <= 0.0f || h <= 0.0f || m_width <= 0 || m_height <= 0) return;
	const int x0 = std::max(0, static_cast<int>(std::floor(x)));
	const int y0 = std::max(0, static_cast<int>(std::floor(y)));
	const int x1 = std::min(m_width, static_cast<int>(std::ceil(x + w)));
	const int y1 = std::min(m_height, static_cast<int>(std::ceil(y + h)));
	if (x0 >= x1 || y0 >= y1) return;

	const uint32_t sa = (argb >> 24) & 0xFF;
	const uint32_t sr = (argb >> 16) & 0xFF;
	const uint32_t sg = (argb >> 8) & 0xFF;
	const uint32_t sb = argb & 0xFF;

	for (int py = y0; py < y1; ++py) {
		uint32_t* row = &m_argb[static_cast<size_t>(py) * m_width];
		for (int px = x0; px < x1; ++px) {
			if (sa >= 255) {
				row[px] = argb;
			} else if (sa > 0) {
				const uint32_t d = row[px];
				const uint32_t dr = (d >> 16) & 0xFF;
				const uint32_t dg = (d >> 8) & 0xFF;
				const uint32_t db = d & 0xFF;
				const uint32_t inv = 255 - sa;
				const uint32_t r = (sr * sa + dr * inv) / 255;
				const uint32_t g = (sg * sa + dg * inv) / 255;
				const uint32_t b = (sb * sa + db * inv) / 255;
				row[px] = 0xFF000000u | (r << 16) | (g << 8) | b;
			}
		}
	}
	++m_pixelsDrawn;
}

void SoftwareRenderer::SyncRgba() {
	const size_t n = m_argb.size();
	m_rgba.resize(n * 4);
	for (size_t i = 0; i < n; ++i) {
		const uint32_t c = m_argb[i];
		m_rgba[i * 4 + 0] = static_cast<uint8_t>((c >> 16) & 0xFF);
		m_rgba[i * 4 + 1] = static_cast<uint8_t>((c >> 8) & 0xFF);
		m_rgba[i * 4 + 2] = static_cast<uint8_t>(c & 0xFF);
		m_rgba[i * 4 + 3] = static_cast<uint8_t>((c >> 24) & 0xFF);
	}
}

} // namespace rts
