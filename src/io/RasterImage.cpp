#include "io/RasterImage.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_HDR
#define STBI_NO_PIC
#define STBI_NO_PNM
#define STBI_FAILURE_USERMSG
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wpedantic"
#endif
#include <stb_image.h>
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include "garment/Pattern.h"

namespace drapeo::io {

bool readFileBytes(const std::string& utf8Path, std::string& out, std::string& error)
{
#if defined(_WIN32)
    const std::filesystem::path path(std::u8string(utf8Path.begin(), utf8Path.end()));
#else
    const std::filesystem::path path(utf8Path);
#endif
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = "Impossible d'ouvrir le fichier.";
        return false;
    }
    std::stringstream ss;
    ss << in.rdbuf();
    out = ss.str();
    return true;
}

float detectDpi(const std::string& b)
{
    const auto u8 = [&](std::size_t i) { return static_cast<uint32_t>(static_cast<unsigned char>(b[i])); };
    // PNG : signature puis blocs [longueur][type][données][CRC] ; pHYs = pixels par mètre.
    if (b.size() > 8 && b.compare(1, 3, "PNG") == 0) {
        std::size_t p = 8;
        while (p + 8 <= b.size()) {
            const uint32_t len = (u8(p) << 24) | (u8(p + 1) << 16) | (u8(p + 2) << 8) | u8(p + 3);
            const std::string type = b.substr(p + 4, 4);
            if (type == "pHYs" && len >= 9 && p + 17 <= b.size()) {
                const uint32_t ppm = (u8(p + 8) << 24) | (u8(p + 9) << 16) | (u8(p + 10) << 8) | u8(p + 11);
                if (u8(p + 16) == 1 && ppm > 0) return static_cast<float>(ppm) * 0.0254f;
                return 0.0f;
            }
            if (type == "IDAT" || type == "IEND") break;
            p += 12 + len;
        }
        return 0.0f;
    }
    // JPEG : segment APP0 « JFIF » ; unité 1 = ppp, 2 = points par cm.
    if (b.size() > 20 && u8(0) == 0xFF && u8(1) == 0xD8) {
        std::size_t p = 2;
        while (p + 4 < b.size() && u8(p) == 0xFF) {
            const uint32_t marker = u8(p + 1);
            const uint32_t len = (u8(p + 2) << 8) | u8(p + 3);
            if (marker == 0xE0 && p + 16 < b.size() && b.compare(p + 4, 4, "JFIF") == 0) {
                const uint32_t unit = u8(p + 11);
                const uint32_t xd = (u8(p + 12) << 8) | u8(p + 13);
                if (unit == 1 && xd > 1) return static_cast<float>(xd);
                if (unit == 2 && xd > 1) return static_cast<float>(xd) * 2.54f;
                return 0.0f;
            }
            if (marker == 0xDA) break;   // début des données d'image
            p += 2 + len;
        }
    }
    return 0.0f;
}

bool decodeRasterImage(const std::string& bytes, RasterImage& out, std::string& error)
{
    int w = 0, h = 0, n = 0;
    unsigned char* px = stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(bytes.data()), static_cast<int>(bytes.size()),
                                              &w, &h, &n, 4);
    if (px == nullptr) {
        error = std::string("Image illisible : ") + stbi_failure_reason();
        return false;
    }
    out.width = w;
    out.height = h;
    out.rgba.assign(px, px + static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4);
    stbi_image_free(px);
    out.dpi = detectDpi(bytes);
    return true;
}

bool loadRasterImage(const std::string& utf8Path, RasterImage& out, std::string& error)
{
    std::string bytes;
    if (!readFileBytes(utf8Path, bytes, error)) return false;
    return decodeRasterImage(bytes, out, error);
}

// --- Simplification et filtrage ---------------------------------------------
namespace {
void dpRange(const std::vector<glm::vec2>& p, std::size_t a, std::size_t b, float tol, std::vector<char>& keep)
{
    if (b <= a + 1) return;
    float best = -1.0f;
    std::size_t idx = a;
    for (std::size_t i = a + 1; i < b; ++i) {
        const float d = garment::distanceToSegment(p[i], p[a], p[b]);
        if (d > best) {
            best = d;
            idx = i;
        }
    }
    if (best > tol) {
        keep[idx] = 1;
        dpRange(p, a, idx, tol, keep);
        dpRange(p, idx, b, tol, keep);
    }
}
} // namespace

std::vector<glm::vec2> simplifyClosed(const std::vector<glm::vec2>& poly, float tolerance)
{
    if (poly.size() <= 4) return poly;
    // Coupe le polygone en deux au point le plus éloigné du premier.
    std::size_t far = 0;
    float best = -1.0f;
    for (std::size_t i = 1; i < poly.size(); ++i) {
        const float d = glm::distance(poly[0], poly[i]);
        if (d > best) {
            best = d;
            far = i;
        }
    }
    std::vector<glm::vec2> ring(poly);
    ring.push_back(poly[0]);
    std::vector<char> keep(ring.size(), 0);
    keep[0] = keep[far] = keep[ring.size() - 1] = 1;
    dpRange(ring, 0, far, tolerance, keep);
    dpRange(ring, far, ring.size() - 1, tolerance, keep);
    std::vector<glm::vec2> out;
    for (std::size_t i = 0; i + 1 < ring.size(); ++i)
        if (keep[i]) out.push_back(ring[i]);
    return out;
}

void keepOutermost(std::vector<std::vector<glm::vec2>>& outlines)
{
    std::sort(outlines.begin(), outlines.end(), [](const auto& a, const auto& b) {
        return std::abs(garment::signedArea(a)) > std::abs(garment::signedArea(b));
    });
    std::vector<std::vector<glm::vec2>> kept;
    for (auto& o : outlines) {
        const glm::vec2 c = garment::centroid(o);
        bool inside = false;
        for (const auto& k : kept)
            if (garment::pointInPolygon(k, c) || garment::pointInPolygon(k, o[0])) {
                inside = true;
                break;
            }
        if (!inside) kept.push_back(std::move(o));
    }
    outlines = std::move(kept);
}

// --- Détection des pièces ---------------------------------------------------
std::vector<std::vector<glm::vec2>> traceOutlines(const RasterImage& image, const TraceOptions& opt)
{
    std::vector<std::vector<glm::vec2>> result;
    if (image.width <= 2 || image.height <= 2) return result;

    // 1. Luminance, réduite si l'image est très grande (vitesse).
    const int f = std::max(1, static_cast<int>(std::ceil(std::max(image.width, image.height) / 1600.0)));
    const int W = image.width / f, H = image.height / f;
    std::vector<uint8_t> lum(static_cast<std::size_t>(W) * H);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            float sum = 0.0f;
            for (int j = 0; j < f; ++j)
                for (int i = 0; i < f; ++i) {
                    const unsigned char* p = &image.rgba[(static_cast<std::size_t>(y * f + j) * image.width + (x * f + i)) * 4];
                    const float a = p[3] / 255.0f;   // transparent = papier blanc
                    const float l = 0.299f * p[0] + 0.587f * p[1] + 0.114f * p[2];
                    sum += l * a + 255.0f * (1.0f - a);
                }
            lum[static_cast<std::size_t>(y) * W + x] = static_cast<uint8_t>(sum / static_cast<float>(f * f));
        }

    // 2. Seuil (Otsu) et sens papier clair / sombre (moyenne du bord de l'image).
    int thr = opt.threshold;
    if (thr < 0) {
        double hist[256] = {};
        for (const auto v : lum) hist[v] += 1.0;
        const double total = static_cast<double>(lum.size());
        double sumAll = 0.0;
        for (int i = 0; i < 256; ++i) sumAll += i * hist[i];
        double wB = 0.0, sumB = 0.0, bestVar = -1.0;
        thr = 128;
        for (int t = 0; t < 256; ++t) {
            wB += hist[t];
            if (wB <= 0.0 || wB >= total) continue;
            sumB += t * hist[t];
            const double mB = sumB / wB, mF = (sumAll - sumB) / (total - wB);
            const double v = wB * (total - wB) * (mB - mF) * (mB - mF);
            if (v > bestVar) {
                bestVar = v;
                thr = t;
            }
        }
    }
    double border = 0.0;
    int nb = 0;
    for (int x = 0; x < W; ++x) { border += lum[x] + lum[static_cast<std::size_t>(H - 1) * W + x]; nb += 2; }
    for (int y = 0; y < H; ++y) { border += lum[static_cast<std::size_t>(y) * W] + lum[static_cast<std::size_t>(y) * W + W - 1]; nb += 2; }
    const bool lightPaper = border / nb > thr;

    const auto at = [W](int x, int y) { return static_cast<std::size_t>(y) * W + x; };
    std::vector<uint8_t> fg(lum.size());
    for (std::size_t i = 0; i < lum.size(); ++i) fg[i] = lightPaper ? (lum[i] <= thr) : (lum[i] > thr);

    // 3. Dilatation 3x3 : referme les coupures d'un pixel dans les traits.
    std::vector<uint8_t> dil(fg.size(), 0);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            if (!fg[at(x, y)]) continue;
            for (int j = -1; j <= 1; ++j)
                for (int i = -1; i <= 1; ++i) {
                    const int xx = x + i, yy = y + j;
                    if (xx >= 0 && yy >= 0 && xx < W && yy < H) dil[at(xx, yy)] = 1;
                }
        }

    // 4. Remplissage du fond depuis le bord : le reste est « plein ».
    std::vector<uint8_t> outside(fg.size(), 0);
    std::vector<int> stack;
    const auto pushIf = [&](int x, int y) {
        if (x < 0 || y < 0 || x >= W || y >= H) return;
        const std::size_t i = at(x, y);
        if (outside[i] || dil[i]) return;
        outside[i] = 1;
        stack.push_back(static_cast<int>(i));
    };
    for (int x = 0; x < W; ++x) { pushIf(x, 0); pushIf(x, H - 1); }
    for (int y = 0; y < H; ++y) { pushIf(0, y); pushIf(W - 1, y); }
    while (!stack.empty()) {
        const int i = stack.back();
        stack.pop_back();
        const int x = i % W, y = i / W;
        pushIf(x + 1, y); pushIf(x - 1, y); pushIf(x, y + 1); pushIf(x, y - 1);
    }

    // 5. Composantes connexes (4-voisinage) de la zone pleine.
    std::vector<int> label(fg.size(), 0);
    const float mpp = opt.metersPerPixel * static_cast<float>(f);
    const double minPixels = std::max(30.0, static_cast<double>(opt.minArea) / (mpp * mpp));
    int next = 0;
    for (int sy = 0; sy < H; ++sy)
        for (int sx = 0; sx < W; ++sx) {
            const std::size_t s = at(sx, sy);
            if (outside[s] || label[s]) continue;
            ++next;
            int minX = sx, maxX = sx, minY = sy, maxY = sy;
            std::size_t count = 0;
            stack.assign(1, static_cast<int>(s));
            label[s] = next;
            while (!stack.empty()) {
                const int i = stack.back();
                stack.pop_back();
                ++count;
                const int x = i % W, y = i / W;
                minX = std::min(minX, x); maxX = std::max(maxX, x);
                minY = std::min(minY, y); maxY = std::max(maxY, y);
                const int nx[4] = {x + 1, x - 1, x, x}, ny[4] = {y, y, y + 1, y - 1};
                for (int k = 0; k < 4; ++k) {
                    if (nx[k] < 0 || ny[k] < 0 || nx[k] >= W || ny[k] >= H) continue;
                    const std::size_t n = at(nx[k], ny[k]);
                    if (outside[n] || label[n]) continue;
                    label[n] = next;
                    stack.push_back(static_cast<int>(n));
                }
            }
            if (static_cast<double>(count) < minPixels) continue;
            if (maxX - minX >= W * 97 / 100 && maxY - minY >= H * 97 / 100) continue;   // cadre de la page

            // 6. Bord extérieur : arêtes de pixels orientées, chaînées en boucle.
            //    Le premier pixel rencontré (sx, sy) a forcément son bord haut libre.
            const auto inComp = [&](int x, int y) {
                return x >= 0 && y >= 0 && x < W && y < H && label[at(x, y)] == next;
            };
            const int dxs[4] = {1, 0, -1, 0}, dys[4] = {0, 1, 0, -1};   // E, S, O, N (y vers le bas)
            int vx = sx, vy = sy, dir = 0;                                // arête haute, vers l'est
            std::vector<glm::ivec2> corners;
            const std::size_t limit = count * 4 + 16;
            for (std::size_t step = 0; step < limit; ++step) {
                vx += dxs[dir];
                vy += dys[dir];
                if (vx == sx && vy == sy) break;
                // À chaque sommet, tourner à droite si possible (contour serré), sinon tout droit, sinon à gauche.
                const auto edgeFree = [&](int d) {
                    // Arête partant de (vx, vy) dans la direction d : le pixel à droite est dans la
                    // composante, celui à gauche non.
                    int rx = 0, ry = 0, lx = 0, ly = 0;
                    switch (d) {
                    case 0: rx = vx; ry = vy; lx = vx; ly = vy - 1; break;
                    case 1: rx = vx - 1; ry = vy; lx = vx; ly = vy; break;
                    case 2: rx = vx - 1; ry = vy - 1; lx = vx - 1; ly = vy; break;
                    default: rx = vx; ry = vy - 1; lx = vx - 1; ly = vy - 1; break;
                    }
                    return inComp(rx, ry) && !inComp(lx, ly);
                };
                const int order[3] = {(dir + 1) % 4, dir, (dir + 3) % 4};
                int nd = -1;
                for (const int d : order)
                    if (edgeFree(d)) {
                        nd = d;
                        break;
                    }
                if (nd < 0) break;
                if (nd != dir) corners.push_back({vx, vy});
                dir = nd;
            }
            corners.push_back({sx, sy});
            if (corners.size() < 3) continue;

            std::vector<glm::vec2> poly;
            poly.reserve(corners.size());
            for (const auto& c : corners)
                poly.push_back(opt.origin + glm::vec2(static_cast<float>(c.x) * mpp,
                                                      static_cast<float>(image.height - c.y * f) * opt.metersPerPixel));
            poly = simplifyClosed(poly, std::max(opt.simplify, mpp * 1.5f));
            if (poly.size() >= 3 && std::abs(garment::signedArea(poly)) >= opt.minArea) result.push_back(std::move(poly));
        }
    keepOutermost(result);
    return result;
}

} // namespace drapeo::io
