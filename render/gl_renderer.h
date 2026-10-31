// RTS Engine — render/gl_renderer.h
// OpenGL 1.1 渲染器：精灵批处理 + 纹理（字体/UI）+ 相机变换。
// 零第三方依赖（系统 opengl32），M3 的"首个后端"。接口与 render/renderer.h 契约一致。
#pragma once

#ifdef _WIN32

#include <windows.h>
#include <GL/gl.h>
#include <vector>
#include <cstdint>
#include "sprite_batch.h"
#include "camera.h"

namespace rts {

class GLRenderer {
public:
	bool Initialize(HDC hdc);
	void Shutdown();

	void BeginFrame(int width, int height);
	// 顶点为世界坐标，内部做相机变换（纯色）
	void DrawBatch(const std::vector<SpriteVertex>& verts, const Camera& camera);
	// 顶点为屏幕坐标（UI），不做相机变换（纯色）
	void DrawScreenBatch(const std::vector<SpriteVertex>& verts);
	// 顶点为屏幕坐标 + 纹理（文字/图标）
	void DrawScreenTextured(const std::vector<SpriteVertex>& verts, uint32_t textureId);
	// 顶点为世界坐标 + 纹理（精灵图集），内部做相机变换
	// opaque=true：不透明绘制（关混合）。地形 tile 在等距网格上互相重叠，必须不透明覆盖，
	// 否则重叠区域会按 alpha 混合出可见方块边界。
	void DrawTexturedBatch(const std::vector<SpriteVertex>& verts, const Camera& camera,
		uint32_t textureId, bool opaque = false);
	void EndFrame();

	// 上传 RGBA 纹理，返回纹理 id（0=失败）
	uint32_t UploadTexture(const std::vector<uint8_t>& rgba, int width, int height);
	// 更新已有纹理内容（同尺寸）
	void UpdateTexture(uint32_t textureId, const std::vector<uint8_t>& rgba, int width, int height);

	void SetClearColor(float r, float g, float b) {
		m_clearR = r; m_clearG = g; m_clearB = b;
	}

private:
	struct GLVertex {
		float x, y;
		float u, v;
		unsigned char r, g, b, a;
	};

	void FillScreenVerts(const std::vector<SpriteVertex>& verts, bool useCamera, const Camera* camera);

	HDC m_hdc = nullptr;
	std::vector<GLVertex> m_screenVerts;
	float m_clearR = 0.05f, m_clearG = 0.06f, m_clearB = 0.08f;
	int m_viewW = 0, m_viewH = 0; // BeginFrame 记录的视口尺寸（诊断/读回用）
};

} // namespace rts

#endif // _WIN32
