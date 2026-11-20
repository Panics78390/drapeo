#include "io/ViewCapture.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>

#include "geometry/ClothMesh.h"

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wsign-compare"
#endif
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_STATIC
#define STBI_WRITE_NO_STDIO
#include <stb_image_write.h>
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace drapeo::io {

namespace {
constexpr int kSuper = 2;   // suréchantillonnage (anticrénelage)
}

void renderView(const std::vector<CaptureMesh>& meshes, const CaptureOptions& o, int& width, int& height,
                std::vector<uint8_t>& rgb)
{
    width = std::clamp(o.width, 64, 2000);
    height = static_cast<int>(std::lround(width * 1.4));
    const int W = width * kSuper, H = height * kSuper;

    const float yaw = glm::radians(o.yawDeg), pitch = glm::radians(std::clamp(o.pitchDeg, -80.0f, 80.0f));
    const glm::vec3 d(std::sin(yaw) * std::cos(pitch), std::sin(pitch), std::cos(yaw) * std::cos(pitch));   // vers la caméra
    const glm::vec3 r(std::cos(yaw), 0.0f, -std::sin(yaw));
    const glm::vec3 u = glm::cross(d, r);

    // Cadrage orthographique sur les maillages marqués `frame`.
    glm::vec2 lo(std::numeric_limits<float>::max()), hi(-std::numeric_limits<float>::max());
    for (const auto& m : meshes) {
        if (!m.frame || !m.positions) continue;
        for (const auto& p : *m.positions) {
            if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) continue;
            const glm::vec2 s(glm::dot(p, r), glm::dot(p, u));
            lo = glm::min(lo, s);
            hi = glm::max(hi, s);
        }
    }
    if (lo.x > hi.x) lo = glm::vec2(-1.0f), hi = glm::vec2(1.0f);
    const glm::vec2 ext = glm::max(hi - lo, glm::vec2(1e-3f));
    const float m2 = 1.0f - 2.0f * std::clamp(o.margin, 0.0f, 0.4f);
    const float scale = std::min(W / ext.x, H / ext.y) * m2;
    const glm::vec2 mid = (lo + hi) * 0.5f;
    auto toScreen = [&](const glm::vec3& p) {
        return glm::vec3(W * 0.5f + (glm::dot(p, r) - mid.x) * scale, H * 0.5f - (glm::dot(p, u) - mid.y) * scale, glm::dot(p, d));
    };

    std::vector<float> depth(static_cast<std::size_t>(W) * H, -std::numeric_limits<float>::max());
    std::vector<glm::vec3> color(static_cast<std::size_t>(W) * H);
    std::vector<int> id(static_cast<std::size_t>(W) * H, -1);
    for (int y = 0; y < H; ++y) {   // fond : dégradé gris clair
        const float t = static_cast<float>(y) / H;
        const glm::vec3 bg = glm::mix(glm::vec3(0.95f, 0.95f, 0.96f), glm::vec3(0.80f, 0.81f, 0.83f), t);
        std::fill(color.begin() + static_cast<std::ptrdiff_t>(y) * W, color.begin() + static_cast<std::ptrdiff_t>(y + 1) * W, bg);
    }
    const glm::vec3 light = glm::normalize(d + 0.35f * r + glm::vec3(0.0f, 0.6f, 0.0f));

    for (std::size_t mi = 0; mi < meshes.size(); ++mi) {
        const auto& m = meshes[mi];
        if (!m.positions || !m.indices) continue;
        const auto& P = *m.positions;
        const auto& I = *m.indices;
        std::vector<glm::vec3> normals;
        geometry::computeVertexNormals(P, I, normals);
        std::vector<glm::vec3> S(P.size());
        for (std::size_t v = 0; v < P.size(); ++v) S[v] = toScreen(P[v]);
        const bool perVertex = m.vertexColors && m.vertexColors->size() == P.size();
        for (std::size_t t = 0; t + 2 < I.size(); t += 3) {
            const uint32_t a = I[t], b = I[t + 1], c = I[t + 2];
            if (a >= P.size() || b >= P.size() || c >= P.size()) continue;
            const glm::vec3 A = S[a], B = S[b], C = S[c];
            if (!std::isfinite(A.x + A.y + B.x + B.y + C.x + C.y)) continue;
            const float area = (B.x - A.x) * (C.y - A.y) - (B.y - A.y) * (C.x - A.x);
            if (std::abs(area) < 1e-9f) continue;
            const int x0 = std::max(0, static_cast<int>(std::floor(std::min({A.x, B.x, C.x}))));
            const int x1 = std::min(W - 1, static_cast<int>(std::ceil(std::max({A.x, B.x, C.x}))));
            const int y0 = std::max(0, static_cast<int>(std::floor(std::min({A.y, B.y, C.y}))));
            const int y1 = std::min(H - 1, static_cast<int>(std::ceil(std::max({A.y, B.y, C.y}))));
            if (x0 > x1 || y0 > y1) continue;
            const bool parts = m.partOfVertex && m.partOfVertex->size() == P.size();
            const int pid = static_cast<int>(mi) * 100000 + (parts ? 1 + std::max(-1, (*m.partOfVertex)[a]) : 0);
            for (int y = y0; y <= y1; ++y)
                for (int x = x0; x <= x1; ++x) {
                    const float px = x + 0.5f, py = y + 0.5f;
                    const float w0 = ((B.x - px) * (C.y - py) - (B.y - py) * (C.x - px)) / area;
                    const float w1 = ((C.x - px) * (A.y - py) - (C.y - py) * (A.x - px)) / area;
                    const float w2 = 1.0f - w0 - w1;
                    if (w0 < 0.0f || w1 < 0.0f || w2 < 0.0f) continue;
                    const float z = w0 * A.z + w1 * B.z + w2 * C.z + m.depthBias;
                    const std::size_t k = static_cast<std::size_t>(y) * W + x;
                    if (z <= depth[k]) continue;
                    depth[k] = z;
                    glm::vec3 n = w0 * normals[a] + w1 * normals[b] + w2 * normals[c];
                    const float len = glm::length(n);
                    n = len > 1e-8f ? n / len : d;
                    float tone = 1.0f;
                    if (glm::dot(n, d) < 0.0f) {   // envers du tissu
                        n = -n;
                        tone = 0.72f;
                    }
                    const glm::vec3 base = perVertex ? w0 * (*m.vertexColors)[a] + w1 * (*m.vertexColors)[b] + w2 * (*m.vertexColors)[c]
                                                     : m.color;
                    const float diffuse = std::max(0.0f, glm::dot(n, light));
                    color[k] = base * tone * (0.32f + 0.68f * diffuse);
                    id[k] = pid;
                }
        }
    }

    // Contours : changement de pièce ou saut de profondeur.
    std::vector<glm::vec3> out = color;
    for (int y = 1; y < H - 1; ++y)
        for (int x = 1; x < W - 1; ++x) {
            const std::size_t k = static_cast<std::size_t>(y) * W + x;
            if (id[k] < 0) continue;
            bool edge = false;
            for (const std::size_t q : {k - 1, k + 1, k - W, k + W}) {
                if (id[q] != id[k] && (id[q] < 0 || depth[q] < depth[k] + 0.02f)) edge = true;
                else if (id[q] >= 0 && std::abs(depth[q] - depth[k]) > 0.04f && depth[q] < depth[k]) edge = true;
            }
            if (edge) out[k] *= 0.45f;
        }

    rgb.assign(static_cast<std::size_t>(width) * height * 3, 0);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
            glm::vec3 s(0.0f);
            for (int j = 0; j < kSuper; ++j)
                for (int i = 0; i < kSuper; ++i) s += out[static_cast<std::size_t>(y * kSuper + j) * W + x * kSuper + i];
            s /= static_cast<float>(kSuper * kSuper);
            const std::size_t o3 = (static_cast<std::size_t>(y) * width + x) * 3;
            for (int c = 0; c < 3; ++c) rgb[o3 + c] = static_cast<uint8_t>(std::lround(std::clamp(s[c], 0.0f, 1.0f) * 255.0f));
        }
}

void joinSideBySide(const std::vector<std::vector<uint8_t>>& images, const std::vector<int>& widths, int height,
                    std::vector<uint8_t>& rgb, int& width)
{
    width = 0;
    for (const int w : widths) width += w;
    rgb.assign(static_cast<std::size_t>(width) * height * 3, 0);
    int x0 = 0;
    for (std::size_t i = 0; i < images.size() && i < widths.size(); ++i) {
        const int w = widths[i];
        for (int y = 0; y < height; ++y)
            std::copy_n(images[i].begin() + static_cast<std::ptrdiff_t>(y) * w * 3, w * 3,
                        rgb.begin() + (static_cast<std::ptrdiff_t>(y) * width + x0) * 3);
        if (x0 > 0)   // filet de séparation
            for (int y = 0; y < height; ++y)
                for (int c = 0; c < 3; ++c) rgb[(static_cast<std::size_t>(y) * width + x0) * 3 + c] = 150;
        x0 += w;
    }
}

bool encodePng(const std::vector<uint8_t>& rgb, int width, int height, std::vector<uint8_t>& png)
{
    png.clear();
    if (width <= 0 || height <= 0 || rgb.size() < static_cast<std::size_t>(width) * height * 3) return false;
    auto sink = [](void* ctx, void* data, int size) {
        auto* v = static_cast<std::vector<uint8_t>*>(ctx);
        v->insert(v->end(), static_cast<uint8_t*>(data), static_cast<uint8_t*>(data) + size);
    };
    return stbi_write_png_to_func(sink, &png, width, height, 3, rgb.data(), width * 3) != 0 && !png.empty();
}

bool writeBytes(const std::string& utf8Path, const std::vector<uint8_t>& bytes, std::string& error)
{
#if defined(_WIN32)
    const std::filesystem::path path(std::u8string(utf8Path.begin(), utf8Path.end()));
#else
    const std::filesystem::path path(utf8Path);
#endif
    std::error_code ec;
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        error = "Impossible d'écrire « " + utf8Path + " ».";
        return false;
    }
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!out) {
        error = "Écriture incomplète de « " + utf8Path + " ».";
        return false;
    }
    return true;
}

std::string base64(const std::vector<uint8_t>& b)
{
    static const char* T = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string s;
    s.reserve((b.size() + 2) / 3 * 4);
    std::size_t i = 0;
    for (; i + 2 < b.size(); i += 3) {
        const uint32_t v = (uint32_t(b[i]) << 16) | (uint32_t(b[i + 1]) << 8) | b[i + 2];
        s += T[v >> 18];
        s += T[(v >> 12) & 63];
        s += T[(v >> 6) & 63];
        s += T[v & 63];
    }
    if (i < b.size()) {
        uint32_t v = uint32_t(b[i]) << 16;
        if (i + 1 < b.size()) v |= uint32_t(b[i + 1]) << 8;
        s += T[v >> 18];
        s += T[(v >> 12) & 63];
        s += i + 1 < b.size() ? T[(v >> 6) & 63] : '=';
        s += '=';
    }
    return s;
}

} // namespace drapeo::io
