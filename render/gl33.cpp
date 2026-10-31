// RTS Engine — render/gl33.cpp
#include "gl33.h"

#include <cstdio>
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#include <GL/gl.h>
#endif

namespace rts {
namespace gl33 {

Fn g;

namespace {

#ifdef _WIN32
template <typename T>
bool LoadOne(T& slot, const char* name) {
	slot = reinterpret_cast<T>(wglGetProcAddress(name));
	if (slot == nullptr) {
		// 少数驱动把 1.2+ 的入口放在 opengl32.dll 里（不应发生，但兜底）
		HMODULE mod = GetModuleHandleA("opengl32.dll");
		if (mod != nullptr) slot = reinterpret_cast<T>(GetProcAddress(mod, name));
	}
	if (slot == nullptr) {
		std::fprintf(stderr, "[gl33] missing entry point: %s\n", name);
		return false;
	}
	return true;
}
#endif

} // namespace

bool Load() {
#ifdef _WIN32
	bool ok = true;
	ok &= LoadOne(g.GenBuffers, "glGenBuffers");
	ok &= LoadOne(g.DeleteBuffers, "glDeleteBuffers");
	ok &= LoadOne(g.BindBuffer, "glBindBuffer");
	ok &= LoadOne(g.BufferData, "glBufferData");
	ok &= LoadOne(g.BufferSubData, "glBufferSubData");

	ok &= LoadOne(g.GenVertexArrays, "glGenVertexArrays");
	ok &= LoadOne(g.DeleteVertexArrays, "glDeleteVertexArrays");
	ok &= LoadOne(g.BindVertexArray, "glBindVertexArray");
	ok &= LoadOne(g.EnableVertexAttribArray, "glEnableVertexAttribArray");
	ok &= LoadOne(g.VertexAttribPointer, "glVertexAttribPointer");

	ok &= LoadOne(g.CreateShader, "glCreateShader");
	ok &= LoadOne(g.ShaderSource, "glShaderSource");
	ok &= LoadOne(g.CompileShader, "glCompileShader");
	ok &= LoadOne(g.GetShaderiv, "glGetShaderiv");
	ok &= LoadOne(g.GetShaderInfoLog, "glGetShaderInfoLog");
	ok &= LoadOne(g.DeleteShader, "glDeleteShader");
	ok &= LoadOne(g.CreateProgram, "glCreateProgram");
	ok &= LoadOne(g.AttachShader, "glAttachShader");
	ok &= LoadOne(g.LinkProgram, "glLinkProgram");
	ok &= LoadOne(g.GetProgramiv, "glGetProgramiv");
	ok &= LoadOne(g.GetProgramInfoLog, "glGetProgramInfoLog");
	ok &= LoadOne(g.DeleteProgram, "glDeleteProgram");
	ok &= LoadOne(g.UseProgram, "glUseProgram");
	ok &= LoadOne(g.GetUniformLocation, "glGetUniformLocation");
	ok &= LoadOne(g.Uniform1i, "glUniform1i");
	ok &= LoadOne(g.Uniform1f, "glUniform1f");
	ok &= LoadOne(g.Uniform2f, "glUniform2f");
	ok &= LoadOne(g.ActiveTexture, "glActiveTexture");
	return ok;
#else
	return false;
#endif
}

GLuint_ BuildProgram(const char* vsSource, const char* fsSource) {
	if (g.CreateShader == nullptr) return 0;

	const auto compile = [](GLenum_ type, const char* src) -> GLuint_ {
		const GLuint_ sh = g.CreateShader(type);
		if (sh == 0) return 0;
		g.ShaderSource(sh, 1, &src, nullptr);
		g.CompileShader(sh);
		GLint_ status = 0;
		g.GetShaderiv(sh, GL33_COMPILE_STATUS, &status);
		if (status == 0) {
			GLint_ len = 0;
			g.GetShaderiv(sh, GL33_INFO_LOG_LENGTH, &len);
			char log[2048] = {};
			if (len > 0 && len < static_cast<GLint_>(sizeof(log))) {
				g.GetShaderInfoLog(sh, len, nullptr, log);
			}
			std::fprintf(stderr, "[gl33] shader compile failed: %s\n", log);
			g.DeleteShader(sh);
			return 0;
		}
		return sh;
	};

	const GLuint_ vs = compile(GL33_VERTEX_SHADER, vsSource);
	if (vs == 0) return 0;
	const GLuint_ fs = compile(GL33_FRAGMENT_SHADER, fsSource);
	if (fs == 0) {
		g.DeleteShader(vs);
		return 0;
	}

	const GLuint_ prog = g.CreateProgram();
	g.AttachShader(prog, vs);
	g.AttachShader(prog, fs);
	g.LinkProgram(prog);
	g.DeleteShader(vs);
	g.DeleteShader(fs);

	GLint_ ok = 0;
	g.GetProgramiv(prog, GL33_LINK_STATUS, &ok);
	if (ok == 0) {
		GLint_ len = 0;
		g.GetProgramiv(prog, GL33_INFO_LOG_LENGTH, &len);
		char log[2048] = {};
		if (len > 0 && len < static_cast<GLint_>(sizeof(log))) {
			g.GetProgramInfoLog(prog, len, nullptr, log);
		}
		std::fprintf(stderr, "[gl33] program link failed: %s\n", log);
		g.DeleteProgram(prog);
		return 0;
	}
	return prog;
}

} // namespace gl33
} // namespace rts
