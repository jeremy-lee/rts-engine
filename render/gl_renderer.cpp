// RTS Engine — render/gl_renderer.cpp
#include "gl_renderer.h"

#ifdef _WIN32

#include <cstdio>

namespace rts {

bool GLRenderer::Initialize(HDC hdc) {
	m_hdc = hdc;
	if (m_hdc == nullptr) return false;

	glDisable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glShadeModel(GL_SMOOTH);
	return true;
}

void GLRenderer::Shutdown() {
	m_hdc = nullptr;
}

void GLRenderer::BeginFrame(int width, int height) {
	glViewport(0, 0, width, height);
	glClearColor(m_clearR, m_clearG, m_clearB, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);

	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	// 屏幕坐标：左上角原点，y 向下
	glOrtho(0.0, width, height, 0.0, -1.0, 1.0);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();

	glEnableClientState(GL_VERTEX_ARRAY);
	glEnableClientState(GL_COLOR_ARRAY);
}

void GLRenderer::DrawBatch(const std::vector<SpriteVertex>& verts, const Camera& camera) {
	if (verts.empty()) return;

	m_screenVerts.resize(verts.size());
	float sx = 0.0f, sy = 0.0f;
	for (size_t i = 0; i < verts.size(); ++i) {
		camera.WorldToScreen(verts[i].x, verts[i].y, sx, sy);
		GLVertex& v = m_screenVerts[i];
		v.x = sx;
		v.y = sy;
		const uint32_t c = verts[i].color;
		v.r = static_cast<unsigned char>((c >> 16) & 0xFF);
		v.g = static_cast<unsigned char>((c >> 8) & 0xFF);
		v.b = static_cast<unsigned char>(c & 0xFF);
		v.a = static_cast<unsigned char>((c >> 24) & 0xFF);
	}

	glVertexPointer(2, GL_FLOAT, sizeof(GLVertex), &m_screenVerts[0].x);
	glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(GLVertex), &m_screenVerts[0].r);
	glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(m_screenVerts.size()));
}

void GLRenderer::EndFrame() {
	glDisableClientState(GL_COLOR_ARRAY);
	glDisableClientState(GL_VERTEX_ARRAY);
	if (m_hdc) ::SwapBuffers(m_hdc);
}

} // namespace rts

#endif // _WIN32
