// RTS Engine — render/renderdoc_capture.cpp
#include "renderdoc_capture.h"

#ifdef _WIN32
#include <windows.h>
#endif

#include <cstdio>
#include <cstring>

// renderdoc_app.h 需要 RENDERDOC_PROGRAMMATIC_API 或者 RENDERDOC_EXPORTS 之类的宏？
// 默认走 RENDERDOC_GetAPI 的运行时路径，这里直接包含即可。
#include "renderdoc/renderdoc_app.h"

namespace rts {
namespace rdoc {

namespace {
#ifdef _WIN32
HMODULE g_module = nullptr;
#endif
RENDERDOC_API_1_1_2* g_api = nullptr;
bool g_tried = false;
} // namespace

bool Init() {
	if (g_tried) return g_api != nullptr;
	g_tried = true;
#ifdef _WIN32
	// 先试 exe 同目录，再试 RenderDoc 安装目录
	const char* candidates[] = {
		"renderdoc.dll",
		"D:/rts-engine/tools/renderdoc/RenderDoc_1.46_64/renderdoc.dll",
		"C:/Program Files/RenderDoc/renderdoc.dll",
	};
	for (const char* path : candidates) {
		g_module = LoadLibraryA(path);
		if (g_module != nullptr) {
			break;
		}
	}
	if (g_module == nullptr) {
		std::fprintf(stderr, "[rdoc] renderdoc.dll not found; capture disabled\n");
		return false;
	}

	// 从模块里取 RENDERDOC_GetAPI
	using pRENDERDOC_GetAPI = int (*)(RENDERDOC_Version, void**);
	const auto getApi = reinterpret_cast<pRENDERDOC_GetAPI>(
		reinterpret_cast<void*>(GetProcAddress(g_module, "RENDERDOC_GetAPI")));
	if (getApi == nullptr) {
		std::fprintf(stderr, "[rdoc] RENDERDOC_GetAPI missing\n");
		return false;
	}
	if (getApi(eRENDERDOC_API_Version_1_1_2, reinterpret_cast<void**>(&g_api)) != 1 ||
		g_api == nullptr) {
		std::fprintf(stderr, "[rdoc] RENDERDOC_GetAPI failed\n");
		g_api = nullptr;
		return false;
	}
	std::printf("[rdoc] RenderDoc attached (capture ready)\n");
	std::fflush(stdout);
	return true;
#else
	return false;
#endif
}

bool Active() { return g_api != nullptr; }

void SetCapturePath(const char* pathTemplate) {
	if (g_api == nullptr || pathTemplate == nullptr) return;
	g_api->SetCaptureFilePathTemplate(pathTemplate);
}

void BeginCapture() {
	if (g_api == nullptr) return;
	g_api->StartFrameCapture(nullptr, nullptr);
}

void EndCapture() {
	if (g_api == nullptr) return;
	g_api->EndFrameCapture(nullptr, nullptr);
	uint32_t count = 0;
	const uint32_t n = g_api->GetNumCaptures();
	if (n > 0) {
		// 打印最新一个捕获的路径，方便用户直接打开
		uint32_t len = 0;
		g_api->GetCapture(n - 1, nullptr, &len, nullptr);
		if (len > 0 && len < 4096) {
			std::string path(len, '\0');
			g_api->GetCapture(n - 1, path.data(), &len, nullptr);
			if (!path.empty() && path.back() == '\0') path.pop_back();
			std::printf("[rdoc] capture written: %s\n", path.c_str());
			std::fflush(stdout);
			++count;
		}
	}
	if (count == 0) {
		std::printf("[rdoc] capture finished but no path reported\n");
		std::fflush(stdout);
	}
}

} // namespace rdoc
} // namespace rts
