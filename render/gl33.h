// RTS Engine — render/gl33.h
// GL 3.3 核心模式所需的函数指针与常量。
//
// Windows 的 opengl32.dll 只静态导出 GL 1.1；3.x 的入口必须通过 wglGetProcAddress 拿。
// 所有类型/常量在此自定义，避免依赖 glext.h。
#pragma once

#include <cstddef>
#include <cstdint>

#ifndef APIENTRY
#define APIENTRY __stdcall
#endif

// ---- GL 基础类型（与 gl.h 一致的平台宽度）----
typedef unsigned int GLenum_;
typedef unsigned int GLuint_;
typedef int GLint_;
typedef int GLsizei_;
typedef float GLfloat_;
typedef char GLchar_;
typedef ptrdiff_t GLsizeiptr_;
typedef ptrdiff_t GLintptr_;
typedef unsigned char GLboolean_;
typedef unsigned char GLubyte_;

// ---- 我们需要的 GL 3.3 常量（自定义，避开 gl.h 里可能缺失的定义）----
#define GL33_ARRAY_BUFFER 0x8892
#define GL33_ELEMENT_ARRAY_BUFFER 0x8893
#define GL33_STATIC_DRAW 0x88E4
#define GL33_DYNAMIC_DRAW 0x88E8
#define GL33_FRAGMENT_SHADER 0x8B30
#define GL33_VERTEX_SHADER 0x8B31
#define GL33_COMPILE_STATUS 0x8B81
#define GL33_LINK_STATUS 0x8B82
#define GL33_INFO_LOG_LENGTH 0x8B84
#define GL33_TEXTURE0 0x84C0
#define GL33_CLAMP_TO_EDGE 0x812F
#define GL33_RGBA8 0x8058
#define GL33_FALSE_ 0
#define GL33_TRUE_ 1

// ---- 函数指针类型 ----
typedef void (APIENTRY* PFN_glGenBuffers)(GLsizei_ n, GLuint_* buffers);
typedef void (APIENTRY* PFN_glDeleteBuffers)(GLsizei_ n, const GLuint_* buffers);
typedef void (APIENTRY* PFN_glBindBuffer)(GLenum_ target, GLuint_ buffer);
typedef void (APIENTRY* PFN_glBufferData)(GLenum_ target, GLsizeiptr_ size, const void* data, GLenum_ usage);
typedef void (APIENTRY* PFN_glBufferSubData)(GLenum_ target, GLintptr_ offset, GLsizeiptr_ size, const void* data);

typedef void (APIENTRY* PFN_glGenVertexArrays)(GLsizei_ n, GLuint_* arrays);
typedef void (APIENTRY* PFN_glDeleteVertexArrays)(GLsizei_ n, const GLuint_* arrays);
typedef void (APIENTRY* PFN_glBindVertexArray)(GLuint_ array);
typedef void (APIENTRY* PFN_glEnableVertexAttribArray)(GLuint_ index);
typedef void (APIENTRY* PFN_glVertexAttribPointer)(GLuint_ index, GLint_ size, GLenum_ type,
	GLboolean_ normalized, GLsizei_ stride, const void* pointer);

typedef GLuint_ (APIENTRY* PFN_glCreateShader)(GLenum_ type);
typedef void (APIENTRY* PFN_glShaderSource)(GLuint_ shader, GLsizei_ count, const GLchar_* const* string,
	const GLint_* length);
typedef void (APIENTRY* PFN_glCompileShader)(GLuint_ shader);
typedef void (APIENTRY* PFN_glGetShaderiv)(GLuint_ shader, GLenum_ pname, GLint_* params);
typedef void (APIENTRY* PFN_glGetShaderInfoLog)(GLuint_ shader, GLsizei_ bufSize, GLsizei_* length,
	GLchar_* infoLog);
typedef void (APIENTRY* PFN_glDeleteShader)(GLuint_ shader);
typedef GLuint_ (APIENTRY* PFN_glCreateProgram)(void);
typedef void (APIENTRY* PFN_glAttachShader)(GLuint_ program, GLuint_ shader);
typedef void (APIENTRY* PFN_glLinkProgram)(GLuint_ program);
typedef void (APIENTRY* PFN_glGetProgramiv)(GLuint_ program, GLenum_ pname, GLint_* params);
typedef void (APIENTRY* PFN_glGetProgramInfoLog)(GLuint_ program, GLsizei_ bufSize, GLsizei_* length,
	GLchar_* infoLog);
typedef void (APIENTRY* PFN_glDeleteProgram)(GLuint_ program);
typedef void (APIENTRY* PFN_glUseProgram)(GLuint_ program);
typedef GLint_ (APIENTRY* PFN_glGetUniformLocation)(GLuint_ program, const GLchar_* name);
typedef void (APIENTRY* PFN_glUniform1i)(GLint_ location, GLint_ v0);
typedef void (APIENTRY* PFN_glUniform1f)(GLint_ location, GLfloat_ v0);
typedef void (APIENTRY* PFN_glUniform2f)(GLint_ location, GLfloat_ v0, GLfloat_ v1);
typedef void (APIENTRY* PFN_glActiveTexture)(GLenum_ texture);

namespace rts {
namespace gl33 {

// 函数指针表（加载后非空）
struct Fn {
	PFN_glGenBuffers GenBuffers = nullptr;
	PFN_glDeleteBuffers DeleteBuffers = nullptr;
	PFN_glBindBuffer BindBuffer = nullptr;
	PFN_glBufferData BufferData = nullptr;
	PFN_glBufferSubData BufferSubData = nullptr;

	PFN_glGenVertexArrays GenVertexArrays = nullptr;
	PFN_glDeleteVertexArrays DeleteVertexArrays = nullptr;
	PFN_glBindVertexArray BindVertexArray = nullptr;
	PFN_glEnableVertexAttribArray EnableVertexAttribArray = nullptr;
	PFN_glVertexAttribPointer VertexAttribPointer = nullptr;

	PFN_glCreateShader CreateShader = nullptr;
	PFN_glShaderSource ShaderSource = nullptr;
	PFN_glCompileShader CompileShader = nullptr;
	PFN_glGetShaderiv GetShaderiv = nullptr;
	PFN_glGetShaderInfoLog GetShaderInfoLog = nullptr;
	PFN_glDeleteShader DeleteShader = nullptr;
	PFN_glCreateProgram CreateProgram = nullptr;
	PFN_glAttachShader AttachShader = nullptr;
	PFN_glLinkProgram LinkProgram = nullptr;
	PFN_glGetProgramiv GetProgramiv = nullptr;
	PFN_glGetProgramInfoLog GetProgramInfoLog = nullptr;
	PFN_glDeleteProgram DeleteProgram = nullptr;
	PFN_glUseProgram UseProgram = nullptr;
	PFN_glGetUniformLocation GetUniformLocation = nullptr;
	PFN_glUniform1i Uniform1i = nullptr;
	PFN_glUniform1f Uniform1f = nullptr;
	PFN_glUniform2f Uniform2f = nullptr;
	PFN_glActiveTexture ActiveTexture = nullptr;
};

extern Fn g;

// 加载所有入口；返回 false 表示当前上下文不支持 GL 3.3 所需的一切
bool Load();

// 编译一个 program；失败返回 0 并把日志打到 stderr
GLuint_ BuildProgram(const char* vsSource, const char* fsSource);

} // namespace gl33
} // namespace rts
