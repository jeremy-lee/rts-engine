// RTS Engine — render/gl_renderer.h
// OpenGL 1.1 渲染器：精灵批处理 + 相机变换。
// 零第三方依赖（系统 opengl32），M3 的"首个后端"。接口与 render/renderer.h 契约一致。
#pragma once

#ifdef _WIN32

#include <windows.h>
#include <GL/gl.h>
#include <vector>
#include "sprite_batch.h"
#include "camera.h"

namespace rts {

class GLRenderer {
public:
	bool Initialize(HDC hdc);
	void Shutdown();

	void BeginFrame(int width, int height);
	// 顶点为世界坐标，内部做相机变换
	void DrawBatch(const std::vector<SpriteVertex>& verts, const Camera& camera);
	void EndFrame();

	void SetClearColor(float r, float g, float b) {
		m_clearR = r; m_clearG = g; m_clearB = b;
	}

private:
	struct GLVertex {
		float x, y;
		unsigned char r, g, b, a;
	};

	HDC m_hdc = nullptr;
	std::vector<GLVertex> m_screenVerts;
	float m_clearR = 0.05f, m_clearG = 0.06f, m_clearB = 0.08f;
};

} // namespace rts

#endif // _WIN32
