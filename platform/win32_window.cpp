// RTS Engine — platform/win32_window.cpp
#include "win32_window.h"

#ifdef _WIN32

#include <cstdio>

namespace rts {

static Win32Window* g_instance = nullptr;

LRESULT CALLBACK Win32Window::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	Win32Window* self = g_instance;
	if (self == nullptr) return DefWindowProc(hwnd, msg, wParam, lParam);

	switch (msg) {
		case WM_CLOSE:
			self->m_quit = true;
			return 0;
		case WM_DESTROY:
			PostQuitMessage(0);
			return 0;
		case WM_KEYDOWN:
			if (wParam < 256) self->m_keys[wParam] = true;
			return 0;
		case WM_KEYUP:
			if (wParam < 256) self->m_keys[wParam] = false;
			return 0;
		case WM_MOUSEMOVE:
			self->m_mouseX = static_cast<int>(LOWORD(lParam));
			self->m_mouseY = static_cast<int>(HIWORD(lParam));
			return 0;
		case WM_LBUTTONDOWN: self->m_mouse[0] = true; return 0;
		case WM_LBUTTONUP: self->m_mouse[0] = false; return 0;
		case WM_RBUTTONDOWN: self->m_mouse[1] = true; return 0;
		case WM_RBUTTONUP: self->m_mouse[1] = false; return 0;
		case WM_MBUTTONDOWN: self->m_mouse[2] = true; return 0;
		case WM_MBUTTONUP: self->m_mouse[2] = false; return 0;
		case WM_MOUSEWHEEL:
			self->m_wheel += GET_WHEEL_DELTA_WPARAM(wParam);
			return 0;
		case WM_SIZE:
			self->m_width = LOWORD(lParam);
			self->m_height = HIWORD(lParam);
			return 0;
		case WM_DROPFILES: {
			// 文件拖放：收集所有拖入路径（ANSI），供编辑器导入资源
			HDROP drop = reinterpret_cast<HDROP>(wParam);
			const UINT count = DragQueryFileA(drop, 0xFFFFFFFF, nullptr, 0);
			for (UINT i = 0; i < count; ++i) {
				char path[MAX_PATH] = {};
				if (DragQueryFileA(drop, i, path, MAX_PATH) > 0) {
					self->m_dropped.emplace_back(path);
				}
			}
			DragFinish(drop);
			return 0;
		}
		default:
			return DefWindowProc(hwnd, msg, wParam, lParam);
	}
}

bool Win32Window::Create(const char* title, int width, int height) {
	g_instance = this;
	m_width = width;
	m_height = height;

	WNDCLASSA wc = {};
	wc.style = CS_OWNDC;
	wc.lpfnWndProc = WndProc;
	wc.hInstance = GetModuleHandleA(nullptr);
	wc.hCursor = LoadCursorA(nullptr, IDC_ARROW);
	wc.lpszClassName = "RTSEngineWindow";
	if (!RegisterClassA(&wc)) {
		std::fprintf(stderr, "[win32] RegisterClass failed\n");
		return false;
	}

	DWORD style = WS_OVERLAPPEDWINDOW;
	RECT rect = {0, 0, width, height};
	AdjustWindowRect(&rect, style, FALSE);
	m_hwnd = CreateWindowA(
		"RTSEngineWindow", title, style,
		CW_USEDEFAULT, CW_USEDEFAULT,
		rect.right - rect.left, rect.bottom - rect.top,
		nullptr, nullptr, wc.hInstance, nullptr);
	if (!m_hwnd) {
		std::fprintf(stderr, "[win32] CreateWindow failed\n");
		return false;
	}

	m_hdc = GetDC(m_hwnd);

	PIXELFORMATDESCRIPTOR pfd = {};
	pfd.nSize = sizeof(pfd);
	pfd.nVersion = 1;
	pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
	pfd.iPixelType = PFD_TYPE_RGBA;
	pfd.cColorBits = 32;
	pfd.cDepthBits = 24;
	pfd.iLayerType = PFD_MAIN_PLANE;

	const int pf = ChoosePixelFormat(m_hdc, &pfd);
	if (pf == 0) {
		std::fprintf(stderr, "[win32] ChoosePixelFormat failed\n");
		return false;
	}
	if (!SetPixelFormat(m_hdc, pf, &pfd)) {
		std::fprintf(stderr, "[win32] SetPixelFormat failed\n");
		return false;
	}

	m_glrc = wglCreateContext(m_hdc);
	if (!m_glrc) {
		std::fprintf(stderr, "[win32] wglCreateContext failed\n");
		return false;
	}
	if (!wglMakeCurrent(m_hdc, m_glrc)) {
		std::fprintf(stderr, "[win32] wglMakeCurrent failed\n");
		return false;
	}

	DragAcceptFiles(m_hwnd, TRUE); // 允许文件拖放到窗口
	ShowWindow(m_hwnd, SW_SHOW);
	UpdateWindow(m_hwnd);
	return true;
}

void Win32Window::Destroy() {
	if (m_glrc) {
		wglMakeCurrent(nullptr, nullptr);
		wglDeleteContext(m_glrc);
		m_glrc = nullptr;
	}
	if (m_hdc && m_hwnd) {
		ReleaseDC(m_hwnd, m_hdc);
		m_hdc = nullptr;
	}
	if (m_hwnd) {
		DestroyWindow(m_hwnd);
		m_hwnd = nullptr;
	}
	g_instance = nullptr;
}

bool Win32Window::PumpMessages() {
	MSG msg;
	while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
		if (msg.message == WM_QUIT) {
			m_quit = true;
			break;
		}
		TranslateMessage(&msg);
		DispatchMessageA(&msg);
	}
	return !m_quit;
}

void Win32Window::SwapBuffers() {
	if (m_hdc) ::SwapBuffers(m_hdc);
}

} // namespace rts

#endif // _WIN32
