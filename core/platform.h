// RTS Engine — core/platform.h
// Windows 平台小工具（与渲染/模拟无关）。
#pragma once

#include <string>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace rts {

// 把工作目录设为「含 assets/ 的目录」（exe 所在目录或其父目录）。
// 所有命令行程序都必须在 main 开头调用它，否则从别的目录启动时会找不到 assets/，
// 表现为图集加载失败并回退到程序化纯色纹理（编辑器曾经因此显示成一堆竖条纹）。
inline void SetWorkingDirToExe() {
#ifdef _WIN32
	char buf[MAX_PATH] = {};
	if (GetModuleFileNameA(nullptr, buf, MAX_PATH) == 0) return;
	const std::string p(buf);
	const size_t pos = p.find_last_of("\\/");
	if (pos == std::string::npos) return;
	const std::string dir = p.substr(0, pos);
	const size_t pos2 = dir.find_last_of("\\/");
	if (pos2 != std::string::npos) {
		const std::string parent = dir.substr(0, pos2);
		if (GetFileAttributesA((parent + "/assets").c_str()) != INVALID_FILE_ATTRIBUTES) {
			SetCurrentDirectoryA(parent.c_str());
			return;
		}
	}
	if (GetFileAttributesA((dir + "/assets").c_str()) != INVALID_FILE_ATTRIBUTES) {
		SetCurrentDirectoryA(dir.c_str());
	}
#endif
}

} // namespace rts
