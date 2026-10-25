// RTS Engine — render/screenshot.cpp
#include "screenshot.h"

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

bool CaptureScreenshot(int width, int height, const char* path) {
	if (width <= 0 || height <= 0) return false;

	// 读取帧缓冲（RGB）
	std::vector<uint8_t> pixels(static_cast<size_t>(width) * height * 3);
	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());

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
	for (int y = 0; y < height; ++y) {
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

} // namespace rts

#endif // _WIN32
