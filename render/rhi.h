// RTS Engine — render/rhi.h
// 薄 RHI（Render Hardware Interface）——UE5 RHI 分层的最小等价物。
//
// 分层（对齐 UE5）：
//   Game/Editor  →  RenderGraph（帧图，render/render_graph.h）
//                →  GLRenderer（顶点变换 + draw 提交）
//                →  rhi::Device（资源 + 全局 GL 状态）   ← 本文件
//                →  OpenGL
//
// UE5 对应物：Engine/Source/Runtime/RHI/Public/DynamicRHI.h（FDynamicRHI 创建资源）
//            + RHICommandList.h（SetRenderTarget / 状态切换）
// 我们只保留 2D 精灵批处理需要的那一小块：纹理生命周期 + 三个全局开关 + 清屏/视口。
#pragma once

#include <cstdint>
#include <vector>

namespace rts {
namespace rhi {

// 纹理句柄（UE5 的 FRHITextureRef；这里用索引，0 表示无效）
using TextureRef = uint32_t;
inline constexpr TextureRef kInvalidTexture = 0;

struct TextureDesc {
	int width = 0;
	int height = 0;
	bool nearest = true; // 像素风：默认 NEAREST
};

struct Stats {
	uint32_t textureCount = 0;
	uint64_t textureBytes = 0;
};

class Device {
public:
	bool Init();
	void Shutdown();

	// ---- 资源 ----
	TextureRef CreateTexture(const TextureDesc& desc, const void* rgba);
	void UpdateTexture(TextureRef tex, const void* rgba, int width, int height);
	void DestroyTexture(TextureRef tex);
	void BindTexture(TextureRef tex);
	uint32_t NativeHandle(TextureRef tex) const;

	// ---- 全局状态（只在变化时真正调 GL）----
	void SetBlendAlpha(bool enabled);
	void SetTexture2D(bool enabled);
	void SetDepthTest(bool enabled);
	void SetViewport(int width, int height);
	void Clear(uint32_t rgba);

	Stats GetStats() const;

private:
	struct Entry {
		uint32_t glName = 0;
		int width = 0;
		int height = 0;
	};
	std::vector<Entry> m_textures; // 0 号槽保留（kInvalidTexture）
	std::vector<uint32_t> m_free;
	bool m_blend = false;
	bool m_texture2D = false;
	bool m_depthTest = false;
	bool m_initialized = false;
};

// 全局设备（对应 UE5 的 GDynamicRHI）；引擎只有一个后端，单例足够了
Device& GetDevice();

} // namespace rhi
} // namespace rts
