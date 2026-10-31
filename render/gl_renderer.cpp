// RTS Engine — render/gl_renderer.cpp
#include "gl_renderer.h"
#include "rhi.h"

#ifdef _WIN32

#include <cmath>
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
	m_viewW = width;
	m_viewH = height;
	// 视口/清屏走 RHI（UE5 里对应 RHICommandList::SetViewport + ClearRenderTargetView）
	rhi::Device& rhiDevice = rhi::GetDevice();
	rhiDevice.Init();
	rhiDevice.SetViewport(width, height);
	rhiDevice.Clear((0xFFu << 24) |
		(static_cast<uint32_t>(m_clearR * 255.0f) << 16) |
		(static_cast<uint32_t>(m_clearG * 255.0f) << 8) |
		static_cast<uint32_t>(m_clearB * 255.0f));

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
	rhi::GetDevice().SetTexture2D(false);
	glDisableClientState(GL_TEXTURE_COORD_ARRAY);
	glVertexPointer(2, GL_FLOAT, sizeof(GLVertex), &m_screenVerts[0].x);
	glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(GLVertex), &m_screenVerts[0].r);
	glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(m_screenVerts.size()));
}

void GLRenderer::DrawScreenBatch(const std::vector<SpriteVertex>& verts) {
	if (verts.empty()) return;
	FillScreenVerts(verts, false, nullptr);
	rhi::GetDevice().SetTexture2D(false);
	glDisableClientState(GL_TEXTURE_COORD_ARRAY);
	glVertexPointer(2, GL_FLOAT, sizeof(GLVertex), &m_screenVerts[0].x);
	glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(GLVertex), &m_screenVerts[0].r);
	glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(m_screenVerts.size()));
}

void GLRenderer::DrawScreenTextured(const std::vector<SpriteVertex>& verts, uint32_t textureId) {
	if (verts.empty() || textureId == 0) return;
	FillScreenVerts(verts, false, nullptr);
	rhi::GetDevice().SetTexture2D(true);
	glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(textureId));
	glTexCoordPointer(2, GL_FLOAT, sizeof(GLVertex), &m_screenVerts[0].u);
	glEnableClientState(GL_TEXTURE_COORD_ARRAY);
	glVertexPointer(2, GL_FLOAT, sizeof(GLVertex), &m_screenVerts[0].x);
	glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(GLVertex), &m_screenVerts[0].r);
	glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(m_screenVerts.size()));
	glDisableClientState(GL_TEXTURE_COORD_ARRAY);
	rhi::GetDevice().SetTexture2D(false);
}

void GLRenderer::DrawTexturedBatch(const std::vector<SpriteVertex>& verts, const Camera& camera,
	uint32_t textureId, bool opaque) {
	if (verts.empty() || textureId == 0) return;
	FillScreenVerts(verts, true, &camera);
	rhi::Device& dev = rhi::GetDevice();
	if (opaque) dev.SetBlendAlpha(false);
	dev.SetTexture2D(true);
	glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(textureId));
	glTexCoordPointer(2, GL_FLOAT, sizeof(GLVertex), &m_screenVerts[0].u);
	glEnableClientState(GL_TEXTURE_COORD_ARRAY);
	glVertexPointer(2, GL_FLOAT, sizeof(GLVertex), &m_screenVerts[0].x);
	glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(GLVertex), &m_screenVerts[0].r);
	glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(m_screenVerts.size()));
	glDisableClientState(GL_TEXTURE_COORD_ARRAY);
	rhi::GetDevice().SetTexture2D(false);
	if (opaque) rhi::GetDevice().SetBlendAlpha(true); // 恢复默认混合

	{
		// TEMP 诊断：统计本批次里异常的屏幕坐标（NaN / 超大）
		static int dbgTex = 0;
		if (dbgTex < 3) {
			int nanCnt = 0, hugeCnt = 0, offscreen = 0;
			float minX = 1e9f, maxX = -1e9f, minY = 1e9f, maxY = -1e9f;
			for (const GLVertex& v : m_screenVerts) {
				if (std::isnan(v.x) || std::isnan(v.y)) { ++nanCnt; continue; }
				if (std::fabs(v.x) > 100000.0f || std::fabs(v.y) > 100000.0f) { ++hugeCnt; continue; }
				if (v.x < -1000.0f || v.y < -1000.0f || v.x > 4000.0f || v.y > 4000.0f) ++offscreen;
				if (v.x < minX) minX = v.x;
				if (v.x > maxX) maxX = v.x;
				if (v.y < minY) minY = v.y;
				if (v.y > maxY) maxY = v.y;
			}
			// 额外：检查每个 quad 的两个三角形是否退化（零面积 → GL 不会光栅化）
			int degenerate = 0;
			int firstDegenerate = -1;
			for (size_t i = 0; i + 5 < m_screenVerts.size(); i += 6) {
				const GLVertex& a = m_screenVerts[i + 0];
				const GLVertex& b = m_screenVerts[i + 1];
				const GLVertex& c = m_screenVerts[i + 2];
				const float cr = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
				if (std::fabs(cr) < 0.5f) {
					++degenerate;
					if (firstDegenerate < 0) firstDegenerate = static_cast<int>(i / 6);
				}
			}
			std::fprintf(stderr,
				"[gl] tex=%u verts=%zu quads=%zu nan=%d huge=%d deg=%d x[%.0f,%.0f] y[%.0f,%.0f]\n",
				textureId, m_screenVerts.size(), m_screenVerts.size() / 6, nanCnt, hugeCnt,
				degenerate, minX, maxX, minY, maxY);
			std::fflush(stderr);
			++dbgTex;

		}
	}
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
