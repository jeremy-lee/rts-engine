// RTS Engine — platform/win32_window.h
// Win32 窗口 + OpenGL 1.1 上下文（零第三方依赖，opengl32/gdi32/user32 系统库）。
#pragma once

#ifdef _WIN32

#include <windows.h>
#include <GL/gl.h>

namespace rts {

class Win32Window {
public:
	bool Create(const char* title, int width, int height);
	void Destroy();

	// 处理消息；返回 false 表示窗口关闭
	bool PumpMessages();
	void SwapBuffers();

	HDC DeviceContext() const { return m_hdc; }
	HGLRC GlContext() const { return m_glrc; }
	HWND Handle() const { return m_hwnd; }
	int Width() const { return m_width; }
	int Height() const { return m_height; }

	// 输入
	bool IsKeyDown(int vk) const { return vk >= 0 && vk < 256 && m_keys[vk]; }
	bool IsMouseDown(int button) const { return button >= 0 && button < 3 && m_mouse[button]; }
	int MouseX() const { return m_mouseX; }
	int MouseY() const { return m_mouseY; }
	int ConsumeWheel() { const int w = m_wheel; m_wheel = 0; return w; }

private:
	static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

	HWND m_hwnd = nullptr;
	HDC m_hdc = nullptr;
	HGLRC m_glrc = nullptr;
	int m_width = 0;
	int m_height = 0;
	bool m_quit = false;
	bool m_keys[256] = {};
	bool m_mouse[3] = {};
	int m_mouseX = 0;
	int m_mouseY = 0;
	int m_wheel = 0;
};

} // namespace rts

#endif // _WIN32
