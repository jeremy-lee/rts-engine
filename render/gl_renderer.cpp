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
	glOrtho(0.0, width, height, 0.0, -1.0, 1.0);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();

	glEnableClientState(GL_VERTEX_ARRAY);
	glEnableClientState(GL_COLOR_ARRAY);
}

void GLRenderer::FillScreenVerts(const std::vector<SpriteVertex>& verts, bool useCamera, const Camera* camera) {
	m_screenVerts.resize(verts.size());
	float sx = 0.0f, sy = 0.0f;
	for (size_t i = 0; i < verts.size(); ++i) {
		if (useCamera && camera != nullptr) {
			camera->WorldToScreen(verts[i].x, verts[i].y, sx, sy);
		} else {
			sx = verts[i].x;
			sy = verts[i].y;
		}
		GLVertex& v = m_screenVerts[i];
		v.x = sx;
		v.y = sy;
		v.u = verts[i].u;
		v.v = verts[i].v;
		const uint32_t c = verts[i].color;
		v.r = static_cast<unsigned char>((c >> 16) & 0xFF);
		v.g = static_cast<unsigned char>((c >> 8) & 0xFF);
		v.b = static_cast<unsigned char>(c & 0xFF);
		v.a = static_cast<unsigned char>((c >> 24) & 0xFF);
	}
}

void GLRenderer::DrawBatch(const std::vector<SpriteVertex>& verts, const Camera& camera) {
	if (verts.empty()) return;
	FillScreenVerts(verts, true, &camera);
	glDisable(GL_TEXTURE_2D);
	glDisableClientState(GL_TEXTURE_COORD_ARRAY);
	glVertexPointer(2, GL_FLOAT, sizeof(GLVertex), &m_screenVerts[0].x);
	glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(GLVertex), &m_screenVerts[0].r);
	glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(m_screenVerts.size()));
}

void GLRenderer::DrawScreenBatch(const std::vector<SpriteVertex>& verts) {
	if (verts.empty()) return;
	FillScreenVerts(verts, false, nullptr);
	glDisable(GL_TEXTURE_2D);
	glDisableClientState(GL_TEXTURE_COORD_ARRAY);
	glVertexPointer(2, GL_FLOAT, sizeof(GLVertex), &m_screenVerts[0].x);
	glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(GLVertex), &m_screenVerts[0].r);
	glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(m_screenVerts.size()));
}

void GLRenderer::DrawScreenTextured(const std::vector<SpriteVertex>& verts, uint32_t textureId) {
	if (verts.empty() || textureId == 0) return;
	FillScreenVerts(verts, false, nullptr);
	glEnable(GL_TEXTURE_2D);
	glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(textureId));
	glTexCoordPointer(2, GL_FLOAT, sizeof(GLVertex), &m_screenVerts[0].u);
	glEnableClientState(GL_TEXTURE_COORD_ARRAY);
	glVertexPointer(2, GL_FLOAT, sizeof(GLVertex), &m_screenVerts[0].x);
	glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(GLVertex), &m_screenVerts[0].r);
	glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(m_screenVerts.size()));
	glDisableClientState(GL_TEXTURE_COORD_ARRAY);
	glDisable(GL_TEXTURE_2D);
}

void GLRenderer::UpdateTexture(uint32_t textureId, const std::vector<uint8_t>& rgba, int width, int height) {
	if (textureId == 0 || width <= 0 || height <= 0) return;
	glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(textureId));
	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
}

void GLRenderer::EndFrame() {
	glDisableClientState(GL_COLOR_ARRAY);
	glDisableClientState(GL_VERTEX_ARRAY);
	if (m_hdc) ::SwapBuffers(m_hdc);
}

uint32_t GLRenderer::UploadTexture(const std::vector<uint8_t>& rgba, int width, int height) {
	if (width <= 0 || height <= 0) return 0;
	GLuint tex = 0;
	glGenTextures(1, &tex);
	if (tex == 0) return 0;
	glBindTexture(GL_TEXTURE_2D, tex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA,
		GL_UNSIGNED_BYTE, rgba.data());
	return static_cast<uint32_t>(tex);
}

} // namespace rts

#endif // _WIN32
