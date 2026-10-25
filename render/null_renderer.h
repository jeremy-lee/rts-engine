// RTS Engine — render/null_renderer.h
// 空渲染器：headless 运行时的默认后端，只统计调用次数。
// 它的存在证明「逻辑层不依赖任何渲染库也能完整运行」。
#pragma once

#include "renderer.h"

namespace rts {

class NullRenderer final : public IRenderer {
public:
	bool Initialize(int width, int height, const char* title) override {
		(void)title;
		m_width = width;
		m_height = height;
		return true;
	}
	void Shutdown() override {}

	void BeginFrame() override { ++m_stats.frameCount; }
	void EndFrame() override {}

	void DrawSprite(const SpriteDraw& s) override {
		(void)s;
		++m_stats.spriteCount;
	}
	void DrawRect(const Rect& r, uint32_t argb) override {
		(void)r; (void)argb;
		++m_stats.drawCalls;
	}

	void SetViewport(int width, int height) override {
		m_width = width;
		m_height = height;
	}

	const RenderStats& Stats() const { return m_stats; }
	int Width() const { return m_width; }
	int Height() const { return m_height; }

private:
	int m_width = 0;
	int m_height = 0;
	RenderStats m_stats;
};

} // namespace rts
