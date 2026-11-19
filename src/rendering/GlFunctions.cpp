#include "rendering/GlFunctions.h"

namespace gl {

#define DRAPEO_GL_DEFINE(ret, name, ...) PFN_##name name = nullptr;
DRAPEO_GL_FUNCTIONS(DRAPEO_GL_DEFINE)
#undef DRAPEO_GL_DEFINE

bool load(GetProcAddressFn getProc, const char** missing)
{
#define DRAPEO_GL_LOAD(ret, name, ...)                                    \
    name = reinterpret_cast<PFN_##name>(getProc("gl" #name));             \
    if (name == nullptr) {                                                \
        if (missing) *missing = "gl" #name;                               \
        return false;                                                     \
    }
    DRAPEO_GL_FUNCTIONS(DRAPEO_GL_LOAD)
#undef DRAPEO_GL_LOAD
    return true;
}

} // namespace gl
