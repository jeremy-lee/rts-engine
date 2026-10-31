// RTS Engine — render/rhi.cpp
#include "rhi.h"

#ifdef _WIN32
#include <windows.h>
#endif
#include <GL/gl.h>

#include <cstdio>

namespace rts {
namespace rhi {

namespace {
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
