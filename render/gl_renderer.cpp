// RTS Engine — render/gl_renderer.cpp
// GL 3.3 核心模式渲染器：VBO + VAO + GLSL 330。
//
// 迁移原因：原来的 GL 1.1 立即模式（glVertexPointer/glColorPointer + glDrawArrays）
// 在本机驱动上会随机丢弃/截断图元——地形格、网格线、光标乃至单位精灵都会缺块，
// 而顶点数据、批次内容、GL 错误全部正常。换到核心模式后走的是现代驱动的主路径。
//
// 顶点格式：pos(vec2) + uv(vec2) + color(ubyte4 normalized)，共 20 字节。
#include "gl_renderer.h"
#include "gl33.h"
#include "rhi.h"

#ifdef _WIN32

#include <cmath>
#include <cstdio>
#include <cstring>

namespace rts {

namespace {

// 屏幕坐标（原点左上、y 向下）→ NDC
const char* kVS = R"(#version 330 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;
layout(location = 2) in vec4 aColor;
uniform vec2 uViewSize;
out vec2 vUV;
out vec4 vColor;
void main() {
    vec2 ndc = vec2(aPos.x / uViewSize.x * 2.0 - 1.0, 1.0 - aPos.y / uViewSize.y * 2.0);
    gl_Position = vec4(ndc, 0.0, 1.0);
    vUV = aUV;
    vColor = aColor;
}
)";

const char* kFS = R"(#version 330 core
in vec2 vUV;
in vec4 vColor;
uniform sampler2D uTex;
uniform float uUseTexture;
out vec4 FragColor;
void main() {
    vec4 t = (uUseTexture > 0.5) ? texture(uTex, vUV) : vec4(1.0, 1.0, 1.0, 1.0);
    FragColor = t * vColor;
}
)";

} // namespace

bool GLRenderer::Initialize(HDC hdc) {
	m_hdc = hdc;
	if (m_hdc == nullptr) return false;

	// 加载 3.3 入口点
	if (!gl33::Load()) {
		std::fprintf(stderr, "[gl] GL 3.3 entry points unavailable\n");
		return false;
	}

	glDisable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	// VAO + VBO
	gl33::g.GenVertexArrays(1, &m_vao);
	gl33::g.GenBuffers(1, &m_vbo);
	gl33::g.BindVertexArray(m_vao);
	gl33::g.BindBuffer(GL33_ARRAY_BUFFER, m_vbo);

	const GLsizei_ stride = static_cast<GLsizei_>(sizeof(GLVertex));
	// location 0: pos
	gl33::g.EnableVertexAttribArray(0);
	gl33::g.VertexAttribPointer(0, 2, GL_FLOAT, GL33_FALSE_, stride,
		reinterpret_cast<const void*>(offsetof(GLVertex, x)));
	// location 1: uv
	gl33::g.EnableVertexAttribArray(1);
	gl33::g.VertexAttribPointer(1, 2, GL_FLOAT, GL33_FALSE_, stride,
		reinterpret_cast<const void*>(offsetof(GLVertex, u)));
	// location 2: color（unsigned byte 归一化）
	gl33::g.EnableVertexAttribArray(2);
	gl33::g.VertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL33_TRUE_, stride,
		reinterpret_cast<const void*>(offsetof(GLVertex, r)));
	gl33::g.BindVertexArray(0);

	m_program = gl33::BuildProgram(kVS, kFS);
	if (m_program == 0) {
		std::fprintf(stderr, "[gl] shader program build failed\n");
		return false;
	}
	m_locViewSize = gl33::g.GetUniformLocation(m_program, "uViewSize");
	m_locUseTexture = gl33::g.GetUniformLocation(m_program, "uUseTexture");
	m_locTex = gl33::g.GetUniformLocation(m_program, "uTex");
	m_gl33 = true;
	std::printf("[gl] renderer: GL 3.3 core (VBO + GLSL 330)\n");
	std::fflush(stdout);
	return true;
}

void GLRenderer::Shutdown() {
	if (m_gl33) {
		if (m_program != 0) gl33::g.DeleteProgram(m_program);
		if (m_vbo != 0) gl33::g.DeleteBuffers(1, &m_vbo);
		if (m_vao != 0) gl33::g.DeleteVertexArrays(1, &m_vao);
		m_program = m_vbo = m_vao = 0;
		m_gl33 = false;
	}
	m_hdc = nullptr;
}

void GLRenderer::BeginFrame(int width, int height) {
	m_viewW = width;
	m_viewH = height;
	rhi::Device& rhiDevice = rhi::GetDevice();
	rhiDevice.Init();
	rhiDevice.SetViewport(width, height);
	rhiDevice.Clear((0xFFu << 24) |
		(static_cast<uint32_t>(m_clearR * 255.0f) << 16) |
		(static_cast<uint32_t>(m_clearG * 255.0f) << 8) |
		static_cast<uint32_t>(m_clearB * 255.0f));

	if (!m_gl33) return;
	gl33::g.UseProgram(m_program);
	gl33::g.Uniform2f(m_locViewSize, static_cast<float>(width), static_cast<float>(height));
	gl33::g.Uniform1i(m_locTex, 0);
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

// 把当前 m_screenVerts 上传到 VBO 并画出来。
// useTexture=false 时走纯色分支（着色器里乘白色）。
void GLRenderer::DrawCurrentBatch(uint32_t textureId, bool useTexture) {
	if (!m_gl33 || m_screenVerts.empty()) return;

	gl33::g.BindVertexArray(m_vao);
	gl33::g.BindBuffer(GL33_ARRAY_BUFFER, m_vbo);
	gl33::g.BufferData(GL33_ARRAY_BUFFER,
		static_cast<GLsizeiptr_>(m_screenVerts.size() * sizeof(GLVertex)),
		m_screenVerts.data(), GL33_DYNAMIC_DRAW);

	if (useTexture) {
		gl33::g.ActiveTexture(GL33_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(textureId));
		gl33::g.Uniform1f(m_locUseTexture, 1.0f);
	} else {
		gl33::g.Uniform1f(m_locUseTexture, 0.0f);
	}

	glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(m_screenVerts.size()));
	gl33::g.BindVertexArray(0);
}

void GLRenderer::DrawBatch(const std::vector<SpriteVertex>& verts, const Camera& camera) {
	if (verts.empty()) return;
	FillScreenVerts(verts, true, &camera);
	DrawCurrentBatch(0, false);
}

void GLRenderer::DrawScreenBatch(const std::vector<SpriteVertex>& verts) {
	if (verts.empty()) return;
	FillScreenVerts(verts, false, nullptr);
	DrawCurrentBatch(0, false);
}

void GLRenderer::DrawScreenTextured(const std::vector<SpriteVertex>& verts, uint32_t textureId) {
	if (verts.empty() || textureId == 0) return;
	FillScreenVerts(verts, false, nullptr);
	DrawCurrentBatch(textureId, true);
}

void GLRenderer::DrawTexturedBatch(const std::vector<SpriteVertex>& verts, const Camera& camera,
	uint32_t textureId, bool opaque) {
	if (verts.empty() || textureId == 0) return;
	FillScreenVerts(verts, true, &camera);
	if (opaque) {
		const bool prevBlend = true; // 进入时一定是开着的（Initialize 里开的）
		(void)prevBlend;
		glDisable(GL_BLEND);
	}
	DrawCurrentBatch(textureId, true);
	if (opaque) glEnable(GL_BLEND);
}

void GLRenderer::EndFrame() {
	// 呈现：没有这一步画面根本不会显示（GL 只画到后台缓冲）
	if (m_hdc) ::SwapBuffers(m_hdc);
}

uint32_t GLRenderer::UploadTexture(const std::vector<uint8_t>& rgba, int width, int height) {
	if (width <= 0 || height <= 0 || rgba.empty()) return 0;
	GLuint tex = 0;
	glGenTextures(1, &tex);
	if (tex == 0) return 0;
	glBindTexture(GL_TEXTURE_2D, tex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
	return tex;
}

void GLRenderer::UpdateTexture(uint32_t textureId, const std::vector<uint8_t>& rgba, int width, int height) {
	if (textureId == 0 || width <= 0 || height <= 0) return;
	glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(textureId));
	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
}

} // namespace rts

#endif // _WIN32
