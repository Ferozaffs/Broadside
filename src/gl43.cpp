#include "gl43.h"
#include "raylib.h"
#include <cstdio>
#include <string>
#include <vector>

extern "C" void (*glfwGetProcAddress(const char* procname))(void);

namespace gl {

#define GL43_DEFINE(ret, name, args) PFN_##name name = nullptr;
GL43_FUNCS(GL43_DEFINE)
#undef GL43_DEFINE

bool Load() {
    bool ok = true;
#define GL43_LOAD(ret, name, args)                                              \
    name = (PFN_##name)glfwGetProcAddress("gl" #name);                          \
    if (!name) { TraceLog(LOG_ERROR, "GL43: missing gl%s", #name); ok = false; }
    GL43_FUNCS(GL43_LOAD)
#undef GL43_LOAD
    return ok;
}

static std::string ResolveShaderDir() {
    std::string local = std::string(GetApplicationDirectory()) + "shaders/";
    if (DirectoryExists(local.c_str())) return local;
    return BROADSIDE_SHADER_DIR;
}

static std::string ReadText(const std::string& path) {
    char* txt = LoadFileText(path.c_str());
    if (!txt) {
        TraceLog(LOG_ERROR, "Shader file not found: %s", path.c_str());
        return {};
    }
    std::string s(txt);
    UnloadFileText(txt);
    return s;
}

std::string LoadShaderSource(const char* file, const char* defines) {
    static std::string dir = ResolveShaderDir();
    std::string body = ReadText(dir + file);
    // expand: #include "x.glsl"
    std::string out;
    size_t pos = 0;
    while (true) {
        size_t inc = body.find("#include \"", pos);
        if (inc == std::string::npos) { out += body.substr(pos); break; }
        out += body.substr(pos, inc - pos);
        size_t q0 = inc + 10;
        size_t q1 = body.find('"', q0);
        out += ReadText(dir + body.substr(q0, q1 - q0));
        out += "\n";
        pos = body.find('\n', q1);
        if (pos == std::string::npos) break;
    }
    return std::string("#version 430 core\n") + defines + "\n" + out;
}

static GLuint_ CompileStage(GLenum_ type, const std::string& src, const char* name) {
    GLuint_ s = CreateShader(type);
    const char* p = src.c_str();
    ShaderSource(s, 1, &p, nullptr);
    CompileShader(s);
    GLint_ okv = 0;
    GetShaderiv(s, COMPILE_STATUS, &okv);
    if (!okv) {
        char log[4096];
        GetShaderInfoLog(s, sizeof(log), nullptr, log);
        TraceLog(LOG_ERROR, "Shader compile failed (%s):\n%s", name, log);
    }
    return s;
}

static GLuint_ Link(std::initializer_list<GLuint_> stages, const char* name) {
    GLuint_ p = CreateProgram();
    for (GLuint_ s : stages) AttachShader(p, s);
    LinkProgram(p);
    GLint_ okv = 0;
    GetProgramiv(p, LINK_STATUS, &okv);
    if (!okv) {
        char log[4096];
        GetProgramInfoLog(p, sizeof(log), nullptr, log);
        TraceLog(LOG_ERROR, "Program link failed (%s):\n%s", name, log);
    }
    for (GLuint_ s : stages) DeleteShader(s);
    return p;
}

GLuint_ CompileCompute(const char* file, const char* defines) {
    GLuint_ cs = CompileStage(COMPUTE_SHADER, LoadShaderSource(file, defines), file);
    return Link({cs}, file);
}

GLuint_ CompileProgram(const char* vsFile, const char* fsFile, const char* defines) {
    GLuint_ vs = CompileStage(VERTEX_SHADER, LoadShaderSource(vsFile, defines), vsFile);
    GLuint_ fs = CompileStage(FRAGMENT_SHADER, LoadShaderSource(fsFile, defines), fsFile);
    return Link({vs, fs}, fsFile);
}

}  // namespace gl
