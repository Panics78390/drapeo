#include "rendering/Shader.h"

#include <vector>

#include <glm/gtc/type_ptr.hpp>

namespace drapeo::rendering {

namespace {
gl::GLuint compileStage(gl::GLenum type, const char* source, std::string& error)
{
    const gl::GLuint s = gl::CreateShader(type);
    gl::ShaderSource(s, 1, &source, nullptr);
    gl::CompileShader(s);
    gl::GLint ok = 0;
    gl::GetShaderiv(s, gl::COMPILE_STATUS, &ok);
    if (!ok) {
        std::vector<char> log(4096);
        gl::GetShaderInfoLog(s, static_cast<gl::GLsizei>(log.size()), nullptr, log.data());
        error = std::string(type == gl::VERTEX_SHADER ? "vertex : " : "fragment : ") + log.data();
        gl::DeleteShader(s);
        return 0;
    }
    return s;
}
} // namespace

Shader::~Shader()
{
    if (program_ != 0 && gl::DeleteProgram != nullptr) gl::DeleteProgram(program_);
}

bool Shader::create(const char* vertexSource, const char* fragmentSource, std::string& error)
{
    const gl::GLuint vs = compileStage(gl::VERTEX_SHADER, vertexSource, error);
    if (vs == 0) return false;
    const gl::GLuint fs = compileStage(gl::FRAGMENT_SHADER, fragmentSource, error);
    if (fs == 0) {
        gl::DeleteShader(vs);
        return false;
    }
    program_ = gl::CreateProgram();
    gl::AttachShader(program_, vs);
    gl::AttachShader(program_, fs);
    gl::LinkProgram(program_);
    gl::DeleteShader(vs);
    gl::DeleteShader(fs);
    gl::GLint ok = 0;
    gl::GetProgramiv(program_, gl::LINK_STATUS, &ok);
    if (!ok) {
        std::vector<char> log(4096);
        gl::GetProgramInfoLog(program_, static_cast<gl::GLsizei>(log.size()), nullptr, log.data());
        error = std::string("liaison : ") + log.data();
        return false;
    }
    return true;
}

void Shader::use() const { gl::UseProgram(program_); }
void Shader::set(const char* name, int v) const { gl::Uniform1i(gl::GetUniformLocation(program_, name), v); }
void Shader::set(const char* name, float v) const { gl::Uniform1f(gl::GetUniformLocation(program_, name), v); }
void Shader::set(const char* name, const glm::vec3& v) const
{
    gl::Uniform3fv(gl::GetUniformLocation(program_, name), 1, glm::value_ptr(v));
}
void Shader::set(const char* name, const glm::mat4& m) const
{
    gl::UniformMatrix4fv(gl::GetUniformLocation(program_, name), 1, gl::FALSE_, glm::value_ptr(m));
}

} // namespace drapeo::rendering
