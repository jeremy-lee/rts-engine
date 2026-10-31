// RTS Engine — render/screenshot.cpp
#include "screenshot.h"
#include "rhi.h"

#ifdef _WIN32

#include <windows.h>
#include <GL/gl.h>

#include <cstdio>
#include <vector>

namespace rts {

namespace {

// 小端写入
void WriteU16(FILE* f, uint16_t v) {
	fputc(v & 0xFF, f);
	fputc((v >> 8) & 0xFF, f);
}

void WriteU32(FILE* f, uint32_t v) {
	fputc(v & 0xFF, f);
	fputc((v >> 8) & 0xFF, f);
	fputc((v >> 16) & 0xFF, f);
	fputc((v >> 24) & 0xFF, f);
}

} // namespace

bool CaptureScreenshot(int width, int height, const char* path, bool flipY) {
	if (width <= 0 || height <= 0) return false;

	// 读取帧缓冲：走 RHI 层，不直接调用 glReadPixels
	std::vector<uint8_t> rgba;
	if (!rhi::GetDevice().ReadPixels(0, 0, width, height, rgba)) return false;
	// 转成 RGB（丢弃 alpha），保持后续 BMP 写出逻辑不变
	std::vector<uint8_t> pixels(static_cast<size_t>(width) * height * 3);
	for (size_t i = 0, n = static_cast<size_t>(width) * height; i < n; ++i) {
		pixels[i * 3 + 0] = rgba[i * 4 + 0];
		pixels[i * 3 + 1] = rgba[i * 4 + 1];
		pixels[i * 3 + 2] = rgba[i * 4 + 2];
	}

	// 每行字节数（4 字节对齐）
	const int rowBytes = (width * 3 + 3) & ~3;
	const uint32_t imageSize = static_cast<uint32_t>(rowBytes * height);
	const uint32_t fileSize = 14 + 40 + imageSize;

	FILE* f = std::fopen(path, "wb");
	if (f == nullptr) return false;

	// BITMAPFILEHEADER
	fputc('B', f);
	fputc('M', f);
	WriteU32(f, fileSize);
	WriteU16(f, 0);
	WriteU16(f, 0);
	WriteU32(f, 14 + 40);

	// BITMAPINFOHEADER
	WriteU32(f, 40);
	WriteU32(f, static_cast<uint32_t>(width));
	WriteU32(f, static_cast<uint32_t>(height));
	WriteU16(f, 1);
	WriteU16(f, 24);
	WriteU32(f, 0); // BI_RGB
	WriteU32(f, imageSize);
	WriteU32(f, 2835);
	WriteU32(f, 2835);
	WriteU32(f, 0);
	WriteU32(f, 0);

	// 像素（GL 原点在左下，BMP 需要自下而上；BGR 顺序）
	std::vector<uint8_t> row(static_cast<size_t>(rowBytes), 0);
	for (int i = 0; i < height; ++i) {
		// BMP 像素数据自下而上；pixels 里是 glReadPixels 的原序（GL 原点在左下）。
		// flipY=true 按 BMP 惯例自下而上写出（正常截图），false 原样写出（用于对比坐标约定）。
		const int y = flipY ? (height - 1 - i) : i;
		const uint8_t* src = &pixels[static_cast<size_t>(y) * width * 3];
		for (int x = 0; x < width; ++x) {
			row[x * 3 + 0] = src[x * 3 + 2]; // B
			row[x * 3 + 1] = src[x * 3 + 1]; // G
			row[x * 3 + 2] = src[x * 3 + 0]; // R
		}
		fwrite(row.data(), 1, static_cast<size_t>(rowBytes), f);
	}

	std::fclose(f);
	return true;
}

void AnalyzeFrameClearColour(int width, int height) {
	if (width <= 0 || height <= 0) return;
	std::vector<uint8_t> px;
	if (!rhi::GetDevice().ReadPixels(0, 0, width, height, px)) return;

	// GL 原点在左下；统计时换算成左上原点的坐标，和 UI/网格坐标一致
	int count = 0;
	int minX = width, maxX = -1, minY = height, maxY = -1;
	int rowHits[2000] = {};
	// 只统计地形区：y 在 [50, height-60)，x 在 [0, width-230)。避开 UI 会减少误报。
	const int regionTop = 50;
	const int regionBottom = height - 60;
	const int regionRight = width - 230;
	for (int gy = 0; gy < height; ++gy) {
		const int y = height - 1 - gy; // 翻成左上原点
		if (y < regionTop || y >= regionBottom) continue;
		for (int x = 0; x < regionRight; ++x) {
			const size_t o = (static_cast<size_t>(gy) * width + x) * 4;
			// 清屏色 = (0.05,0.06,0.08)*255 = (12,15,20)，留 ±2 容差
			const int dr = static_cast<int>(px[o + 0]) - 12;
			const int dg = static_cast<int>(px[o + 1]) - 15;
			const int db = static_cast<int>(px[o + 2]) - 20;
			if (dr * dr + dg * dg + db * db > 12) continue;
			++count;
			if (x < minX) minX = x;
			if (x > maxX) maxX = x;
			if (y < minY) minY = y;
			if (y > maxY) maxY = y;
			if (y < 2000) ++rowHits[y];
		}
	}
	std::fprintf(stderr, "[analyze] clearColourPixels=%d bbox=(%d,%d)-(%d,%d)\n",
		count, minX, minY, maxX, maxY);
	// 打印命中数最多的 5 行（能看出清屏色是否集中在某些水平带）
	for (int k = 0; k < 5; ++k) {
		int bestRow = -1, bestVal = 0;
		for (int y = 0; y < height && y < 2000; ++y) {
			if (rowHits[y] > bestVal) { bestVal = rowHits[y]; bestRow = y; }
		}
		if (bestRow < 0 || bestVal == 0) break;
		std::fprintf(stderr, "[analyze]   row y=%d hits=%d\n", bestRow, bestVal);
		rowHits[bestRow] = 0;
	}
	// 打印前几行的清屏色 x 区间（能看出是否和 24px 的格子对齐）
	int printed = 0;
	for (int gy = 0; gy < height && printed < 3; ++gy) {
		std::vector<std::pair<int,int>> runs;
		int start = -1;
		for (int x = 0; x < width; ++x) {
			const size_t o = (static_cast<size_t>(gy) * width + x) * 4;
			const int dr = static_cast<int>(px[o + 0]) - 12;
			const int dg = static_cast<int>(px[o + 1]) - 15;
			const int db = static_cast<int>(px[o + 2]) - 20;
			const bool isClear = (dr * dr + dg * dg + db * db <= 12);
			if (isClear && start < 0) start = x;
			else if (!isClear && start >= 0) { runs.push_back({start, x - 1}); start = -1; }
		}
		if (start >= 0) runs.push_back({start, width - 1});
		if (runs.empty()) continue;
		std::fprintf(stderr, "[analyze] screenY=%d runs=%zu:", height - 1 - gy, runs.size());
		for (size_t i = 0; i < runs.size() && i < 8; ++i) {
			std::fprintf(stderr, " [%d..%d]", runs[i].first, runs[i].second);
		}
		std::fprintf(stderr, "\n");
		++printed;
	}
	std::fflush(stderr);
}

} // namespace rts

#endif // _WIN32
