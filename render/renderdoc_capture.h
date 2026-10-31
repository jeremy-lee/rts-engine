// RTS Engine — render/renderdoc_capture.h
// 把 RenderDoc 的帧捕获接进来（运行时加载，不强制依赖）。
//
// 用法（命令行程序）：
//   1. main 开头调用 rts::rdoc::Init() —— 必须在 GL 上下文创建之前，
//      RenderDoc 才能 hook 到 wglCreateContext / wglSwapBuffers。
//   2. 需要抓帧时调用 rts::rdoc::BeginCapture() ... rts::rdoc::EndCapture()，
//      RenderDoc 会在 EndCapture 时把这一帧写成 .rdc，用 qrenderdoc.exe 打开。
//
// 若 renderdoc.dll 加载不到（未安装/未部署），所有函数是安全的空操作。
#pragma once

#include <string>

namespace rts {
namespace rdoc {

// 在 GL 初始化之前调用；返回是否成功挂上 RenderDoc
bool Init();

// 是否已挂上
bool Active();

// 设置捕获输出路径模板（不带扩展名时 RenderDoc 会自己加 .rdc）
void SetCapturePath(const char* pathTemplate);

// 开启/结束一帧的捕获。EndCapture 会把该帧写成 .rdc
void BeginCapture();
void EndCapture();

} // namespace rdoc
} // namespace rts
