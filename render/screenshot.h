// RTS Engine — render/screenshot.h
// 帧缓冲截图（glReadPixels → BMP），用于调试/验证渲染结果。
#pragma once

#ifdef _WIN32

namespace rts {

// 从当前后台缓冲读取像素并保存为 24-bit BMP。
// 必须在 SwapBuffers（EndFrame）之前调用。
bool CaptureScreenshot(int width, int height, const char* path);

// 调试用：读回帧缓冲并统计「清屏色」像素的分布（走 RHI，不直接调 GL）。
// 用于定位"某些格子没画出来、露出背景色"这类问题。
void AnalyzeFrameClearColour(int width, int height);

} // namespace rts

#endif // _WIN32
