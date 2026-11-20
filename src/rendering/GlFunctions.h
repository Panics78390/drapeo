// =============================================================================
//  rendering/GlFunctions — chargeur OpenGL minimal, sans dépendance
//
//  Plutôt qu'un générateur (glad) ou une bibliothèque (GLEW), on déclare
//  uniquement les fonctions utilisées, dans l'espace de noms `gl`, et on les
//  charge via glfwGetProcAddress. Ajouter une fonction = une ligne dans la
//  macro DRAPEO_GL_FUNCTIONS.
// =============================================================================
#pragma once

#include <cstddef>

#if defined(_WIN32)
#define DRAPEO_GL_API __stdcall
#else
#define DRAPEO_GL_API
#endif

namespace gl {

using GLenum = unsigned int;
using GLuint = unsigned int;
using GLint = int;
using GLsizei = int;
using GLboolean = unsigned char;
using GLfloat = float;
using GLbitfield = unsigned int;
using GLchar = char;
using GLubyte = unsigned char;
using GLsizeiptr = std::ptrdiff_t;
using GLintptr = std::ptrdiff_t;

inline constexpr GLenum COLOR_BUFFER_BIT = 0x4000, DEPTH_BUFFER_BIT = 0x0100;
inline constexpr GLenum DEPTH_TEST = 0x0B71, CULL_FACE = 0x0B44, MULTISAMPLE = 0x809D, BLEND = 0x0BE2;
inline constexpr GLenum POLYGON_OFFSET_FILL = 0x8037, SCISSOR_TEST = 0x0C11;
inline constexpr GLenum TRIANGLES = 0x0004, LINES = 0x0001;
inline constexpr GLenum UNSIGNED_BYTE = 0x1401, UNSIGNED_INT = 0x1405, FLOAT = 0x1406;
inline constexpr GLenum ARRAY_BUFFER = 0x8892, ELEMENT_ARRAY_BUFFER = 0x8893;
inline constexpr GLenum STATIC_DRAW = 0x88E4, DYNAMIC_DRAW = 0x88E8;
inline constexpr GLenum VERTEX_SHADER = 0x8B31, FRAGMENT_SHADER = 0x8B30;
inline constexpr GLenum COMPILE_STATUS = 0x8B81, LINK_STATUS = 0x8B82;
inline constexpr GLenum RGB = 0x1907, RGBA = 0x1908, PACK_ALIGNMENT = 0x0D05, UNPACK_ALIGNMENT = 0x0CF5;
inline constexpr GLenum TEXTURE_2D = 0x0DE1, TEXTURE_MIN_FILTER = 0x2801, TEXTURE_MAG_FILTER = 0x2800, LINEAR = 0x2601;
inline constexpr GLenum TEXTURE_WRAP_S = 0x2802, TEXTURE_WRAP_T = 0x2803, CLAMP_TO_EDGE = 0x812F;
inline constexpr GLenum TEXTURE0 = 0x84C0, LINEAR_MIPMAP_LINEAR = 0x2703;
inline constexpr GLenum FRONT_AND_BACK = 0x0408, LINE = 0x1B01, FILL = 0x1B02;
inline constexpr GLenum VERSION = 0x1F02, RENDERER = 0x1F01;
inline constexpr GLboolean FALSE_ = 0, TRUE_ = 1;

// X(type de retour, nom sans préfixe gl, types des paramètres...)
#define DRAPEO_GL_FUNCTIONS(X)                                                              \
    X(void, Viewport, GLint, GLint, GLsizei, GLsizei)                                       \
    X(void, Scissor, GLint, GLint, GLsizei, GLsizei)                                        \
    X(void, ClearColor, GLfloat, GLfloat, GLfloat, GLfloat)                                 \
    X(void, Clear, GLbitfield)                                                              \
    X(void, Enable, GLenum)                                                                 \
    X(void, Disable, GLenum)                                                                \
    X(void, PolygonMode, GLenum, GLenum)                                                    \
    X(void, PolygonOffset, GLfloat, GLfloat)                                                \
    X(const GLubyte*, GetString, GLenum)                                                    \
    X(void, GenVertexArrays, GLsizei, GLuint*)                                              \
    X(void, BindVertexArray, GLuint)                                                        \
    X(void, DeleteVertexArrays, GLsizei, const GLuint*)                                     \
    X(void, GenBuffers, GLsizei, GLuint*)                                                   \
    X(void, BindBuffer, GLenum, GLuint)                                                     \
    X(void, BufferData, GLenum, GLsizeiptr, const void*, GLenum)                            \
    X(void, BufferSubData, GLenum, GLintptr, GLsizeiptr, const void*)                       \
    X(void, DeleteBuffers, GLsizei, const GLuint*)                                          \
    X(void, VertexAttribPointer, GLuint, GLint, GLenum, GLboolean, GLsizei, const void*)    \
    X(void, EnableVertexAttribArray, GLuint)                                                \
    X(GLuint, CreateShader, GLenum)                                                         \
    X(void, ShaderSource, GLuint, GLsizei, const GLchar* const*, const GLint*)              \
    X(void, CompileShader, GLuint)                                                          \
    X(void, GetShaderiv, GLuint, GLenum, GLint*)                                            \
    X(void, GetShaderInfoLog, GLuint, GLsizei, GLsizei*, GLchar*)                           \
    X(void, DeleteShader, GLuint)                                                           \
    X(GLuint, CreateProgram)                                                                \
    X(void, AttachShader, GLuint, GLuint)                                                   \
    X(void, LinkProgram, GLuint)                                                            \
    X(void, GetProgramiv, GLuint, GLenum, GLint*)                                           \
    X(void, GetProgramInfoLog, GLuint, GLsizei, GLsizei*, GLchar*)                          \
    X(void, DeleteProgram, GLuint)                                                          \
    X(void, UseProgram, GLuint)                                                             \
    X(GLint, GetUniformLocation, GLuint, const GLchar*)                                     \
    X(void, Uniform1i, GLint, GLint)                                                        \
    X(void, Uniform1f, GLint, GLfloat)                                                      \
    X(void, Uniform2fv, GLint, GLsizei, const GLfloat*)                                     \
    X(void, Uniform3fv, GLint, GLsizei, const GLfloat*)                                     \
    X(void, UniformMatrix4fv, GLint, GLsizei, GLboolean, const GLfloat*)                    \
    X(void, DrawElements, GLenum, GLsizei, GLenum, const void*)                             \
    X(void, DrawArrays, GLenum, GLint, GLsizei)                                             \
    X(void, PixelStorei, GLenum, GLint)                                                     \
    X(void, GenTextures, GLsizei, GLuint*)                                                  \
    X(void, DeleteTextures, GLsizei, const GLuint*)                                         \
    X(void, BindTexture, GLenum, GLuint)                                                    \
    X(void, ActiveTexture, GLenum)                                                          \
    X(void, GenerateMipmap, GLenum)                                                         \
    X(void, TexParameteri, GLenum, GLenum, GLint)                                           \
    X(void, TexImage2D, GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*) \
    X(void, ReadPixels, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*)

#define DRAPEO_GL_DECLARE(ret, name, ...)                    \
    using PFN_##name = ret(DRAPEO_GL_API*)(__VA_ARGS__);     \
    extern PFN_##name name;
DRAPEO_GL_FUNCTIONS(DRAPEO_GL_DECLARE)
#undef DRAPEO_GL_DECLARE

using ProcAddress = void (*)();
using GetProcAddressFn = ProcAddress (*)(const char*);

/// Charge toutes les fonctions. Renvoie false et le nom de la première
/// fonction manquante si le pilote ne la fournit pas.
bool load(GetProcAddressFn getProc, const char** missing = nullptr);

} // namespace gl
