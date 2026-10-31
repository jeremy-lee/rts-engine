// RTS Engine — render/rhi.cpp
#include "rhi.h"

#ifdef _WIN32
#include <windows.h>
#endif
#include <GL/gl.h>

#include <cstdio>
#include <cstring>

namespace rts {
namespace rhi {

// ---- GL_EXT_framebuffer_object 函数指针（GL 1.1 头文件里没有）----
#ifndef GL_FRAMEBUFFER_EXT
#define GL_FRAMEBUFFER_EXT 0x8D40
#endif
#ifndef GL_COLOR_ATTACHMENT0_EXT
#define GL_COLOR_ATTACHMENT0_EXT 0x8CE0
#endif
#ifndef GL_FRAMEBUFFER_COMPLETE_EXT
#define GL_FRAMEBUFFER_COMPLETE_EXT 0x8CD5
#endif

typedef void (APIENTRY* PFN_GenFramebuffersEXT)(GLsizei, GLuint*);
typedef void (APIENTRY* PFN_BindFramebufferEXT)(GLenum, GLuint);
typedef void (APIENTRY* PFN_FramebufferTexture2DEXT)(GLenum, GLenum, GLenum, GLuint, GLint);
typedef GLenum (APIENTRY* PFN_CheckFramebufferStatusEXT)(GLenum);
typedef void (APIENTRY* PFN_DeleteFramebuffersEXT)(GLsizei, const GLuint*);

namespace {
PFN_GenFramebuffersEXT g_genFBO = nullptr;
PFN_BindFramebufferEXT g_bindFBO = nullptr;
PFN_FramebufferTexture2DEXT g_fboTex = nullptr;
PFN_CheckFramebufferStatusEXT g_checkFBO = nullptr;
PFN_DeleteFramebuffersEXT g_delFBO = nullptr;

bool LoadFboEntryPoints() {
	if (g_genFBO != nullptr) return true;
	g_genFBO = reinterpret_cast<PFN_GenFramebuffersEXT>(wglGetProcAddress("glGenFramebuffersEXT"));
	g_bindFBO = reinterpret_cast<PFN_BindFramebufferEXT>(wglGetProcAddress("glBindFramebufferEXT"));
	g_fboTex = reinterpret_cast<PFN_FramebufferTexture2DEXT>(wglGetProcAddress("glFramebufferTexture2DEXT"));
	g_checkFBO = reinterpret_cast<PFN_CheckFramebufferStatusEXT>(wglGetProcAddress("glCheckFramebufferStatusEXT"));
	g_delFBO = reinterpret_cast<PFN_DeleteFramebuffersEXT>(wglGetProcAddress("glDeleteFramebuffersEXT"));
	return g_genFBO && g_bindFBO && g_fboTex && g_checkFBO && g_delFBO;
}

void ApplyParam(GLenum target, GLint nearest) {
	glTexParameteri(target, GL_TEXTURE_MIN_FILTER, nearest ? GL_NEAREST : GL_LINEAR);
	glTexParameteri(target, GL_TEXTURE_MAG_FILTER, nearest ? GL_NEAREST : GL_LINEAR);
	glTexParameteri(target, GL_TEXTURE_WRAP_S, GL_CLAMP);
	glTexParameteri(target, GL_TEXTURE_WRAP_T, GL_CLAMP);
}
} // namespace

bool Device::Init() {
	if (m_initialized) return true;
	m_textures.clear();
	m_textures.emplace_back(); // 槽 0 = 无效
	m_free.clear();
	m_renderTargets.clear();
	m_renderTargets.emplace_back(); // 槽 0 = backbuffer
	m_rendererString = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
	if (m_rendererString == nullptr) m_rendererString = "unknown";
	m_supportsFBO = false;
	const char* exts = reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
	if (exts != nullptr && std::strstr(exts, "GL_EXT_framebuffer_object") != nullptr) {
		m_supportsFBO = LoadFboEntryPoints();
	}
	std::printf("[rhi] renderer=%s  FBO=%s\n", m_rendererString, m_supportsFBO ? "yes" : "no");
	std::fflush(stdout);
	m_initialized = true;
	return true;
}

void Device::Shutdown() {
	for (Entry& e : m_textures) {
		if (e.glName != 0) {
			const GLuint n = e.glName;
			glDeleteTextures(1, &n);
		}
	}
	m_textures.clear();
	m_free.clear();
	m_initialized = false;
}

TextureRef Device::CreateTexture(const TextureDesc& desc, const void* rgba) {
	if (!m_initialized) Init();
	if (desc.width <= 0 || desc.height <= 0) return kInvalidTexture;
	GLuint name = 0;
	glGenTextures(1, &name);
	if (name == 0) return kInvalidTexture;
	glBindTexture(GL_TEXTURE_2D, name);
	ApplyParam(GL_TEXTURE_2D, desc.nearest ? 1 : 0);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, desc.width, desc.height, 0,
		GL_RGBA, GL_UNSIGNED_BYTE, rgba);

	Entry e;
	e.glName = name;
	e.width = desc.width;
	e.height = desc.height;
	TextureRef ref = 0;
	if (!m_free.empty()) {
		ref = m_free.back();
		m_free.pop_back();
		m_textures[ref] = e;
	} else {
		ref = static_cast<TextureRef>(m_textures.size());
		m_textures.push_back(e);
	}
	return ref;
}

void Device::UpdateTexture(TextureRef tex, const void* rgba, int width, int height) {
	if (tex == kInvalidTexture || tex >= m_textures.size()) return;
	Entry& e = m_textures[tex];
	if (e.glName == 0) return;
	glBindTexture(GL_TEXTURE_2D, e.glName);
	if (width != e.width || height != e.height) {
		e.width = width;
		e.height = height;
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0,
			GL_RGBA, GL_UNSIGNED_BYTE, rgba);
	} else {
		glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height,
			GL_RGBA, GL_UNSIGNED_BYTE, rgba);
	}
}

void Device::DestroyTexture(TextureRef tex) {
	if (tex == kInvalidTexture || tex >= m_textures.size()) return;
	Entry& e = m_textures[tex];
	if (e.glName != 0) {
		const GLuint n = e.glName;
		glDeleteTextures(1, &n);
		e.glName = 0;
	}
	m_free.push_back(tex);
}

void Device::BindTexture(TextureRef tex) {
	if (tex == kInvalidTexture || tex >= m_textures.size()) {
		glBindTexture(GL_TEXTURE_2D, 0);
		return;
	}
	glBindTexture(GL_TEXTURE_2D, m_textures[tex].glName);
}

uint32_t Device::NativeHandle(TextureRef tex) const {
	if (tex == kInvalidTexture || tex >= m_textures.size()) return 0;
	return m_textures[tex].glName;
}

void Device::SetBlendAlpha(bool enabled) {
	if (m_blend == enabled) return;
	m_blend = enabled;
	if (enabled) {
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	} else {
		glDisable(GL_BLEND);
	}
}

void Device::SetTexture2D(bool enabled) {
	if (m_texture2D == enabled) return;
	m_texture2D = enabled;
	if (enabled) glEnable(GL_TEXTURE_2D); else glDisable(GL_TEXTURE_2D);
}

void Device::SetDepthTest(bool enabled) {
	if (m_depthTest == enabled) return;
	m_depthTest = enabled;
	if (enabled) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
}

void Device::SetViewport(int width, int height) {
	glViewport(0, 0, width, height);
}

void Device::Clear(uint32_t rgba) {
	const float r = static_cast<float>((rgba >> 16) & 0xFF) / 255.0f;
	const float g = static_cast<float>((rgba >> 8) & 0xFF) / 255.0f;
	const float b = static_cast<float>(rgba & 0xFF) / 255.0f;
	const float a = static_cast<float>((rgba >> 24) & 0xFF) / 255.0f;
	glClearColor(r, g, b, a);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

Device::RenderTargetRef Device::CreateRenderTarget(int width, int height) {
	if (!m_supportsFBO || width <= 0 || height <= 0) return RenderTargetRef{};
	GLuint fbo = 0;
	GLuint tex = 0;
	g_genFBO(1, &fbo);
	if (fbo == 0) return RenderTargetRef{};
	glGenTextures(1, &tex);
	if (tex == 0) { g_delFBO(1, &fbo); return RenderTargetRef{}; }
	glBindTexture(GL_TEXTURE_2D, tex);
	ApplyParam(GL_TEXTURE_2D, 1);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
	g_bindFBO(GL_FRAMEBUFFER_EXT, fbo);
	g_fboTex(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, GL_TEXTURE_2D, tex, 0);
	const GLenum status = g_checkFBO(GL_FRAMEBUFFER_EXT);
	g_bindFBO(GL_FRAMEBUFFER_EXT, 0);
	if (status != GL_FRAMEBUFFER_COMPLETE_EXT) {
		std::fprintf(stderr, "[rhi] FBO incomplete (0x%X), disabling render targets\n", status);
		g_delFBO(1, &fbo);
		const GLuint n = tex;
		glDeleteTextures(1, &n);
		m_supportsFBO = false;
		return RenderTargetRef{};
	}
	RtEntry e;
	e.fbo = fbo;
	e.colorTex = tex;
	e.width = width;
	e.height = height;
	const uint32_t id = static_cast<uint32_t>(m_renderTargets.size());
	m_renderTargets.push_back(e);
	return RenderTargetRef{id};
}

void Device::BindRenderTarget(RenderTargetRef rt) {
	if (!m_supportsFBO) return;
	if (rt.id == 0 || rt.id >= m_renderTargets.size()) {
		g_bindFBO(GL_FRAMEBUFFER_EXT, 0);
		return;
	}
	g_bindFBO(GL_FRAMEBUFFER_EXT, m_renderTargets[rt.id].fbo);
}

void Device::DestroyRenderTarget(RenderTargetRef rt) {
	if (!m_supportsFBO || rt.id == 0 || rt.id >= m_renderTargets.size()) return;
	RtEntry& e = m_renderTargets[rt.id];
	if (e.fbo != 0) { const GLuint f = e.fbo; g_delFBO(1, &f); e.fbo = 0; }
	if (e.colorTex != 0) { const GLuint t = e.colorTex; glDeleteTextures(1, &t); e.colorTex = 0; }
}

uint32_t Device::RenderTargetTexture(RenderTargetRef rt) const {
	if (rt.id == 0 || rt.id >= m_renderTargets.size()) return 0;
	return m_renderTargets[rt.id].colorTex;
}

Stats Device::GetStats() const {
	Stats s;
	for (const Entry& e : m_textures) {
		if (e.glName == 0) continue;
		s.textureCount += 1;
		s.textureBytes += static_cast<uint64_t>(e.width) * e.height * 4u;
	}
	return s;
}

Device& GetDevice() {
	static Device s_device;
	return s_device;
}

} // namespace rhi
} // namespace rts
