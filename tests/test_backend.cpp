// RTS Engine — tests/test_backend.cpp
// M14 验收：软件渲染后端（纯 CPU 光栅化）正确性
#include "test_framework.h"
#include "render/software_renderer.h"

using namespace rts;

namespace {
uint8_t Channel(const SoftwareRenderer& r, int x, int y, int ch) {
	const int idx = (y * r.Width() + x) * 4 + ch;
	return r.Rgba()[static_cast<size_t>(idx)];
}
} // namespace

TEST(software_renderer_clear) {
	SoftwareRenderer r;
	r.Resize(8, 8);
	r.Clear(0xFFFF0000u);
	r.SyncRgba();
	CHECK_EQ(r.Width(), 8);
	CHECK_EQ(Channel(r, 0, 0, 0), 255); // R
	CHECK_EQ(Channel(r, 0, 0, 1), 0);   // G
	CHECK_EQ(Channel(r, 0, 0, 2), 0);   // B
	CHECK_EQ(Channel(r, 0, 0, 3), 255); // A
}

TEST(software_renderer_fill_rect) {
	SoftwareRenderer r;
	r.Resize(16, 16);
	r.Clear(0xFF000000u);
	r.FillRect(2.0f, 2.0f, 4.0f, 4.0f, 0xFF00FF00u);
	r.SyncRgba();

	CHECK_EQ(Channel(r, 3, 3, 1), 255);  // 绿色内部
	CHECK_EQ(Channel(r, 10, 10, 1), 0);  // 外部黑色
	CHECK_EQ(Channel(r, 1, 1, 1), 0);    // 边界外
}

TEST(software_renderer_alpha_blend) {
	SoftwareRenderer r;
	r.Resize(4, 4);
	r.Clear(0xFF000000u); // 黑
	// 50% 透明红叠加
	r.FillRect(0, 0, 4, 4, 0x80FF0000u);
	r.SyncRgba();
	const uint8_t red = Channel(r, 1, 1, 0);
	CHECK(red > 100);   // 应显著变红
	CHECK(red < 200);   // 但未到纯红
}

TEST(software_renderer_clip_out_of_bounds) {
	SoftwareRenderer r;
	r.Resize(8, 8);
	r.Clear(0xFF000000u);
	// 超出边界的矩形不应崩溃
	r.FillRect(-5.0f, -5.0f, 4.0f, 4.0f, 0xFFFF0000u);
	r.FillRect(6.0f, 6.0f, 10.0f, 10.0f, 0xFFFF0000u);
	r.SyncRgba();
	CHECK_EQ(Channel(r, 0, 0, 0), 0);   // 左上未覆盖
	CHECK_EQ(Channel(r, 7, 7, 0), 255); // 右下被覆盖
}

TEST(software_renderer_stats) {
	SoftwareRenderer r;
	r.Resize(8, 8);
	r.ResetStats();
	r.FillRect(0, 0, 2, 2, 0xFFFFFFFFu);
	r.FillRect(4, 4, 2, 2, 0xFFFFFFFFu);
	CHECK_EQ(r.PixelsDrawn(), static_cast<uint64_t>(2));
}

TEST(software_renderer_zero_size_safe) {
	SoftwareRenderer r;
	r.Resize(0, 0); // 空尺寸安全
	r.Clear(0xFF000000u);
	r.FillRect(0, 0, 10, 10, 0xFFFF0000u);
	CHECK_EQ(r.Width(), 0);
}
