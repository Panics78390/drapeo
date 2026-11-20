// rendering/Shader — programme GLSL (compilation, liaison, uniforms)
#pragma once

#include <string>

#include <glm/glm.hpp>

#include "rendering/GlFunctions.h"

namespace drapeo::rendering {

class Shader {
public:
    Shader() = default;
    ~Shader();
    Shader(const Shader&) = delete;
    Shader& operator=(const Shader&) = delete;

    /// Compile et lie ; en cas d'échec, `error` contient le journal du pilote.
    bool create(const char* vertexSource, const char* fragmentSource, std::string& error);
    void use() const;

    void set(const char* name, int v) const;
    void set(const char* name, float v) const;
    void set(const char* name, const glm::vec2& v) const;
    void set(const char* name, const glm::vec3& v) const;
    void set(const char* name, const glm::mat4& m) const;

private:
    gl::GLuint program_ = 0;
};

} // namespace drapeo::rendering
