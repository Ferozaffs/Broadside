// Minimal OpenGL 4.3 function loader for the bits raylib's rlgl doesn't expose
// (texture arrays, image load/store, compute dispatch, framebuffer blits).
// Function pointers are fetched from GLFW, which raylib already links in.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

#ifdef _WIN32
#define GLAPIENTRY_ __stdcall
#else
#define GLAPIENTRY_
#endif

typedef unsigned int GLenum_;
typedef unsigned int GLuint_;
typedef int GLint_;
typedef int GLsizei_;
typedef float GLfloat_;
typedef unsigned char GLboolean_;
typedef unsigned int GLbitfield_;
typedef ptrdiff_t GLsizeiptr_;
typedef char GLchar_;

namespace gl {

enum : unsigned int {
    TEXTURE_2D = 0x0DE1, TEXTURE_2D_ARRAY = 0x8C1A,
    TEXTURE0 = 0x84C0,
    TEXTURE_MIN_FILTER = 0x2801, TEXTURE_MAG_FILTER = 0x2800,
    TEXTURE_WRAP_S = 0x2802, TEXTURE_WRAP_T = 0x2803,
    LINEAR = 0x2601, NEAREST = 0x2600, LINEAR_MIPMAP_LINEAR = 0x2703,
    REPEAT = 0x2901, CLAMP_TO_EDGE = 0x812F,
    TEXTURE_MAX_ANISOTROPY = 0x84FE,
    RGBA32F = 0x8814, RGBA16F = 0x881A, RG16F = 0x822F, RG32F = 0x8230, DEPTH_COMPONENT32F = 0x8CAC,
    RGBA = 0x1908, RG = 0x8227, FLOAT = 0x1406, DEPTH_COMPONENT = 0x1902, UNSIGNED_INT = 0x1405,
    READ_ONLY = 0x88B8, WRITE_ONLY = 0x88B9, READ_WRITE = 0x88BA,
    COMPUTE_SHADER = 0x91B9, VERTEX_SHADER = 0x8B31, FRAGMENT_SHADER = 0x8B30,
    COMPILE_STATUS = 0x8B81, LINK_STATUS = 0x8B82,
    SHADER_IMAGE_ACCESS_BARRIER_BIT = 0x20, TEXTURE_FETCH_BARRIER_BIT = 0x08, ALL_BARRIER_BITS = 0xFFFFFFFF,
    ARRAY_BUFFER = 0x8892, ELEMENT_ARRAY_BUFFER = 0x8893, STATIC_DRAW = 0x88E4,
    TRIANGLES = 0x0004,
    FRAMEBUFFER = 0x8D40, READ_FRAMEBUFFER = 0x8CA8, DRAW_FRAMEBUFFER = 0x8CA9,
    COLOR_ATTACHMENT0 = 0x8CE0, DEPTH_ATTACHMENT = 0x8D00, FRAMEBUFFER_COMPLETE = 0x8CD5,
    COLOR_BUFFER_BIT = 0x4000, DEPTH_BUFFER_BIT = 0x0100,
    DEPTH_TEST = 0x0B71, CULL_FACE = 0x0B44, BLEND = 0x0BE2,
    LEQUAL = 0x0203, LESS = 0x0201, ALWAYS = 0x0207,
    FRAMEBUFFER_BINDING = 0x8CA6,
    RENDERBUFFER = 0x8D41,
    MULTISAMPLE = 0x809D,
    TRUE_ = 1, FALSE_ = 0,
};

#define GL43_FUNCS(X) \
    X(void, ActiveTexture, (GLenum_ t)) \
    X(void, BindTexture, (GLenum_ target, GLuint_ tex)) \
    X(void, GenTextures, (GLsizei_ n, GLuint_* t)) \
    X(void, DeleteTextures, (GLsizei_ n, const GLuint_* t)) \
    X(void, TexStorage2D, (GLenum_ target, GLsizei_ levels, GLenum_ fmt, GLsizei_ w, GLsizei_ h)) \
    X(void, TexStorage3D, (GLenum_ target, GLsizei_ levels, GLenum_ fmt, GLsizei_ w, GLsizei_ h, GLsizei_ d)) \
    X(void, TexSubImage3D, (GLenum_ target, GLint_ level, GLint_ x, GLint_ y, GLint_ z, GLsizei_ w, GLsizei_ h, GLsizei_ d, GLenum_ fmt, GLenum_ type, const void* data)) \
    X(void, TexParameteri, (GLenum_ target, GLenum_ pname, GLint_ v)) \
    X(void, TexParameterf, (GLenum_ target, GLenum_ pname, GLfloat_ v)) \
    X(void, GenerateMipmap, (GLenum_ target)) \
    X(void, BindImageTexture, (GLuint_ unit, GLuint_ tex, GLint_ level, GLboolean_ layered, GLint_ layer, GLenum_ access, GLenum_ fmt)) \
    X(void, DispatchCompute, (GLuint_ x, GLuint_ y, GLuint_ z)) \
    X(void, MemoryBarrier, (GLbitfield_ b)) \
    X(GLuint_, CreateShader, (GLenum_ type)) \
    X(void, ShaderSource, (GLuint_ s, GLsizei_ n, const GLchar_* const* src, const GLint_* len)) \
    X(void, CompileShader, (GLuint_ s)) \
    X(void, GetShaderiv, (GLuint_ s, GLenum_ p, GLint_* v)) \
    X(void, GetShaderInfoLog, (GLuint_ s, GLsizei_ max, GLsizei_* len, GLchar_* log)) \
    X(void, DeleteShader, (GLuint_ s)) \
    X(GLuint_, CreateProgram, (void)) \
    X(void, AttachShader, (GLuint_ p, GLuint_ s)) \
    X(void, LinkProgram, (GLuint_ p)) \
    X(void, GetProgramiv, (GLuint_ p, GLenum_ e, GLint_* v)) \
    X(void, GetProgramInfoLog, (GLuint_ p, GLsizei_ max, GLsizei_* len, GLchar_* log)) \
    X(void, UseProgram, (GLuint_ p)) \
    X(GLint_, GetUniformLocation, (GLuint_ p, const GLchar_* name)) \
    X(void, Uniform1i, (GLint_ l, GLint_ v)) \
    X(void, Uniform1f, (GLint_ l, GLfloat_ v)) \
    X(void, Uniform2f, (GLint_ l, GLfloat_ a, GLfloat_ b)) \
    X(void, Uniform3f, (GLint_ l, GLfloat_ a, GLfloat_ b, GLfloat_ c)) \
    X(void, Uniform4f, (GLint_ l, GLfloat_ a, GLfloat_ b, GLfloat_ c, GLfloat_ d)) \
    X(void, UniformMatrix4fv, (GLint_ l, GLsizei_ n, GLboolean_ t, const GLfloat_* m)) \
    X(void, GenVertexArrays, (GLsizei_ n, GLuint_* v)) \
    X(void, BindVertexArray, (GLuint_ v)) \
    X(void, GenBuffers, (GLsizei_ n, GLuint_* b)) \
    X(void, BindBuffer, (GLenum_ t, GLuint_ b)) \
    X(void, BufferData, (GLenum_ t, GLsizeiptr_ size, const void* data, GLenum_ usage)) \
    X(void, VertexAttribPointer, (GLuint_ i, GLint_ size, GLenum_ type, GLboolean_ norm, GLsizei_ stride, const void* ptr)) \
    X(void, EnableVertexAttribArray, (GLuint_ i)) \
    X(void, DrawElements, (GLenum_ mode, GLsizei_ count, GLenum_ type, const void* idx)) \
    X(void, DrawArrays, (GLenum_ mode, GLint_ first, GLsizei_ count)) \
    X(void, GenFramebuffers, (GLsizei_ n, GLuint_* f)) \
    X(void, DeleteFramebuffers, (GLsizei_ n, const GLuint_* f)) \
    X(void, BindFramebuffer, (GLenum_ t, GLuint_ f)) \
    X(void, FramebufferTexture2D, (GLenum_ t, GLenum_ att, GLenum_ textarget, GLuint_ tex, GLint_ level)) \
    X(GLenum_, CheckFramebufferStatus, (GLenum_ t)) \
    X(void, GenRenderbuffers, (GLsizei_ n, GLuint_* r)) \
    X(void, DeleteRenderbuffers, (GLsizei_ n, const GLuint_* r)) \
    X(void, BindRenderbuffer, (GLenum_ t, GLuint_ r)) \
    X(void, RenderbufferStorageMultisample, (GLenum_ t, GLsizei_ samples, GLenum_ fmt, GLsizei_ w, GLsizei_ h)) \
    X(void, FramebufferRenderbuffer, (GLenum_ t, GLenum_ att, GLenum_ rbt, GLuint_ rb)) \
    X(void, BlitFramebuffer, (GLint_ sx0, GLint_ sy0, GLint_ sx1, GLint_ sy1, GLint_ dx0, GLint_ dy0, GLint_ dx1, GLint_ dy1, GLbitfield_ mask, GLenum_ filter)) \
    X(void, Enable, (GLenum_ c)) \
    X(void, Disable, (GLenum_ c)) \
    X(void, DepthMask, (GLboolean_ f)) \
    X(void, DepthFunc, (GLenum_ f)) \
    X(void, Viewport, (GLint_ x, GLint_ y, GLsizei_ w, GLsizei_ h)) \
    X(void, ClearColor, (GLfloat_ r, GLfloat_ g, GLfloat_ b, GLfloat_ a)) \
    X(void, Clear, (GLbitfield_ m)) \
    X(void, GetIntegerv, (GLenum_ p, GLint_* v))

#define GL43_DECLARE(ret, name, args) typedef ret (GLAPIENTRY_* PFN_##name) args; extern PFN_##name name;
GL43_FUNCS(GL43_DECLARE)
#undef GL43_DECLARE

bool Load();  // call once after the window/context exists

// Shader helpers. Sources are loaded from the shader directory; "#include common.glsl" is expanded.
std::string LoadShaderSource(const char* file, const char* defines = "");
GLuint_ CompileCompute(const char* file, const char* defines = "");
GLuint_ CompileProgram(const char* vsFile, const char* fsFile, const char* defines = "");

}  // namespace gl
