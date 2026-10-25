// RTS Engine — render/renderer.h
// 渲染抽象接口：逻辑层只依赖数据，不依赖本文件。
// M3 将实现具体后端（Sokol / The Forge / SDL3），此处先定义契约。
#pragma once

#include <cstdint>
#include <cstddef>

namespace rts {

// 2D 精灵 ID（后端内部映射到纹理/图集）
using SpriteId = uint32_t;

// 屏幕空间矩形（像素）
struct Rect {
	float x = 0, y = 0, w = 0, h = 0;
};

// 单个精灵绘制命令（渲染层从模拟快照转换而来）
struct SpriteDraw {
	SpriteId sprite = 0;
	float x = 0, y = 0;   // 屏幕坐标（常为精灵中心）
	float scale = 1.0f;
	float rotation = 0.0f; // 弧度（渲染层可自由使用浮点）
	uint32_t tint = 0xFFFFFFFF; // ARGB
};

// 渲染后端契约。所有方法都在渲染线程调用。
class IRenderer {
public:
	virtual ~IRenderer() = default;

	virtual bool Initialize(int width, int height, const char* title) = 0;
	virtual void Shutdown() = 0;

	// 帧生命周期
	virtual void BeginFrame() = 0;
	virtual void EndFrame() = 0;

	// 绘制
	virtual void DrawSprite(const SpriteDraw& s) = 0;
	virtual void DrawRect(const Rect& r, uint32_t argb) = 0;

	// 视口/相机
	virtual void SetViewport(int width, int height) = 0;
};

// 渲染统计（调试/性能剖析）
struct RenderStats {
	uint64_t frameCount = 0;
	uint64_t spriteCount = 0;
	uint32_t drawCalls = 0;
};

} // namespace rts
