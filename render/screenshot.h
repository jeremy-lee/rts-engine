// RTS Engine — render/screenshot.h
// 帧缓冲截图（glReadPixels → BMP），用于调试/验证渲染结果。
#pragma once

#ifdef _WIN32

namespace rts {

// 从当前后台缓冲读取像素并保存为 24-bit BMP。
// 必须在 SwapBuffers（EndFrame）之前调用。
bool CaptureScreenshot(int width, int height, const char* path);

} // namespace rts

#endif // _WIN32
