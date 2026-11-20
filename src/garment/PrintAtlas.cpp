#include "garment/PrintAtlas.h"

#include <algorithm>
#include <cmath>

namespace drapeo::garment {

glm::vec2 printSize(const Print& print, const io::RasterImage* image)
{
    float h = print.height;
    if (h <= 0.0f) h = image != nullptr && image->width > 0 ? print.width * static_cast<float>(image->height) / static_cast<float>(image->width) : print.width;
    return {print.width, h};
}

void printCorners(const Panel& panel, const Print& print, const io::RasterImage* image, glm::vec2 corners[4])
{
    const glm::vec2 c = printCenter(panel, print);
    const glm::vec2 half = printSize(print, image) * 0.5f;
    const float a = glm::radians(print.angleDeg);
    const glm::vec2 ax(std::cos(a), std::sin(a)), ay(-std::sin(a), std::cos(a));
    corners[0] = c - ax * half.x - ay * half.y;
    corners[1] = c + ax * half.x - ay * half.y;
    corners[2] = c + ax * half.x + ay * half.y;
    corners[3] = c - ax * half.x + ay * half.y;
}

glm::vec3 finishColor(glm::vec3 base, FabricFinish finish, glm::vec2 p)
{
    switch (finish) {
    case FabricFinish::Plain: return base;
    case FabricFinish::Ripstop: {
        // Quadrillage de renfort tous les 6 mm, fils légèrement plus clairs.
        const glm::vec2 f = glm::fract(p / 0.006f);
        const float line = (f.x < 0.12f || f.y < 0.12f) ? 1.0f : 0.0f;
        return glm::clamp(base * (1.0f + 0.10f * line), 0.0f, 1.0f);
    }
    case FabricFinish::Twill: {
        const float d = std::fmod(std::abs(p.x + p.y) / 0.002f, 1.0f);
        return base * (d < 0.5f ? 1.05f : 0.93f);
    }
    case FabricFinish::Reflective: return glm::mix(base, glm::vec3(0.82f, 0.84f, 0.86f), 0.7f);
    case FabricFinish::Zipper: return base;   // dents peintes par buildPatternTexture
    }
    return base;
}

namespace {

glm::vec4 sampleImage(const io::RasterImage& img, float u, float v)
{
    // Bilinéaire ; v = 0 en bas de l'image.
    const float x = std::clamp(u * static_cast<float>(img.width) - 0.5f, 0.0f, static_cast<float>(img.width - 1));
    const float y = std::clamp((1.0f - v) * static_cast<float>(img.height) - 0.5f, 0.0f, static_cast<float>(img.height - 1));
    const int x0 = static_cast<int>(x), y0 = static_cast<int>(y);
    const int x1 = std::min(x0 + 1, img.width - 1), y1 = std::min(y0 + 1, img.height - 1);
    const float fx = x - static_cast<float>(x0), fy = y - static_cast<float>(y0);
    auto px = [&](int xx, int yy) {
        const unsigned char* q = &img.rgba[(static_cast<std::size_t>(yy) * static_cast<std::size_t>(img.width) + static_cast<std::size_t>(xx)) * 4];
        return glm::vec4(q[0], q[1], q[2], q[3]) / 255.0f;
    };
    return glm::mix(glm::mix(px(x0, y0), px(x1, y0), fx), glm::mix(px(x0, y1), px(x1, y1), fx), fy);
}

/// Intervalles [x0, x1) d'une ligne horizontale à l'intérieur d'un polygone (règle pair-impair).
void scanline(const std::vector<glm::vec2>& poly, float y, std::vector<float>& xs)
{
    xs.clear();
    for (std::size_t i = 0, n = poly.size(); i < n; ++i) {
        const glm::vec2 a = poly[i], b = poly[(i + 1) % n];
        if ((a.y <= y) == (b.y <= y)) continue;
        xs.push_back(a.x + (y - a.y) / (b.y - a.y) * (b.x - a.x));
    }
    std::sort(xs.begin(), xs.end());
}

} // namespace

bool buildPatternTexture(const Garment& g, const PrintImageLoader& load, const PatternTextureOptions& options, PatternTexture& out)
{
    out = {};
    bool anyPrint = false;
    glm::vec2 lo(1e9f), hi(-1e9f);
    for (const auto& p : g.panels) {
        for (const auto& q : p.points) {
            lo = glm::min(lo, q);
            hi = glm::max(hi, q);
        }
        if (!p.prints.empty() || p.finish == FabricFinish::Zipper) anyPrint = true;
    }
    if (g.panels.empty() || (!anyPrint && !options.fillPanels && !options.stitches)) return false;
    lo -= glm::vec2(0.01f);
    hi += glm::vec2(0.01f);
    const glm::vec2 ext = hi - lo;
    float mpp = std::max(options.metersPerPixel, 1e-4f);
    const float maxDim = std::max(ext.x, ext.y);
    if (maxDim / mpp > static_cast<float>(options.maxSize)) mpp = maxDim / static_cast<float>(options.maxSize);
    out.metersPerPixel = mpp;
    out.origin = lo;
    out.width = std::max(1, static_cast<int>(std::ceil(ext.x / mpp)));
    out.height = std::max(1, static_cast<int>(std::ceil(ext.y / mpp)));
    out.rgba.assign(static_cast<std::size_t>(out.width) * static_cast<std::size_t>(out.height) * 4, 0);
    auto pixelCenter = [&](int x, int y) { return lo + (glm::vec2(static_cast<float>(x), static_cast<float>(y)) + 0.5f) * mpp; };
    auto at = [&](int x, int y) { return &out.rgba[(static_cast<std::size_t>(y) * static_cast<std::size_t>(out.width) + static_cast<std::size_t>(x)) * 4]; };

    std::vector<float> xs;
    // Fond : couleur et aspect de chaque pièce (export), avec une marge pour le filtrage aux bords.
    if (options.fillPanels) {
        for (std::size_t pv = 0; pv < g.panels.size(); ++pv) {
            const auto& p = g.panels[pv];
            if (p.points.size() < 3 || !g.panelVisible(static_cast<int>(pv))) continue;   // calque masqué
            std::vector<glm::vec2> poly = p.points;
            glm::vec2 plo(1e9f), phi(-1e9f);
            for (const auto& q : poly) {
                plo = glm::min(plo, q);
                phi = glm::max(phi, q);
            }
            const int y0 = std::max(0, static_cast<int>((plo.y - lo.y) / mpp) - 3);
            const int y1 = std::min(out.height - 1, static_cast<int>((phi.y - lo.y) / mpp) + 3);
            const float pad = 3.0f * mpp;
            for (int y = y0; y <= y1; ++y) {
                const float wy = lo.y + (static_cast<float>(y) + 0.5f) * mpp;
                scanline(poly, std::clamp(wy, plo.y + 1e-5f, phi.y - 1e-5f), xs);
                for (std::size_t k = 0; k + 1 < xs.size(); k += 2) {
                    const int x0 = std::max(0, static_cast<int>((xs[k] - pad - lo.x) / mpp));
                    const int x1 = std::min(out.width - 1, static_cast<int>((xs[k + 1] + pad - lo.x) / mpp));
                    for (int x = x0; x <= x1; ++x) {
                        const glm::vec3 c = finishColor(p.color, p.finish, pixelCenter(x, y));
                        unsigned char* d = at(x, y);
                        d[0] = static_cast<unsigned char>(std::lround(std::clamp(c.r, 0.0f, 1.0f) * 255.0f));
                        d[1] = static_cast<unsigned char>(std::lround(std::clamp(c.g, 0.0f, 1.0f) * 255.0f));
                        d[2] = static_cast<unsigned char>(std::lround(std::clamp(c.b, 0.0f, 1.0f) * 255.0f));
                        d[3] = 255;
                    }
                }
            }
        }
    }

    // Coutures : bord de chaque pièce légèrement assombri ; surpiqûre en tirets sur les pièces posées
    // (poches, rabats, renforts), à 4 mm du bord. Le tout reste découpé au contour de la pièce.
    if (options.stitches) {
        auto blend = [&](int x, int y, glm::vec3 c, float a) {
            unsigned char* dst = at(x, y);
            const float da = static_cast<float>(dst[3]) / 255.0f;
            const float oa = a + da * (1.0f - a);
            for (int k = 0; k < 3; ++k) {
                const float dc = static_cast<float>(dst[k]) / 255.0f;
                const float oc = oa > 0.0f ? (c[k] * a + dc * da * (1.0f - a)) / oa : 0.0f;
                dst[k] = static_cast<unsigned char>(std::lround(std::clamp(oc, 0.0f, 1.0f) * 255.0f));
            }
            dst[3] = static_cast<unsigned char>(std::lround(oa * 255.0f));
        };
        const float edgeW = std::max(0.0012f, mpp * 0.9f), stitchAt = 0.004f, stitchW = std::max(0.0005f, mpp * 0.45f);
        for (std::size_t pi = 0; pi < g.panels.size(); ++pi) {
            const auto& p = g.panels[pi];
            const std::size_t n = p.points.size();
            if (n < 3 || !g.panelVisible(static_cast<int>(pi))) continue;
            const bool patch = g.attachmentOf(static_cast<int>(pi)) >= 0;
            const float orient = signedArea(p.points) > 0.0f ? 1.0f : -1.0f;
            const glm::vec3 dark = p.color * 0.45f, thread = glm::min(p.color * 1.9f + 0.06f, glm::vec3(1.0f));
            float along = 0.0f;   // abscisse curviligne (tirets continus d'un bord à l'autre)
            for (std::size_t e = 0; e < n; ++e) {
                const glm::vec2 a = p.points[e], b = p.points[(e + 1) % n];
                const float L = glm::distance(a, b);
                if (L < 1e-6f) continue;
                const glm::vec2 dir = (b - a) / L, nrm = glm::vec2(-dir.y, dir.x) * orient;   // vers l'intérieur
                const float reach = patch ? stitchAt + stitchW + mpp : edgeW + mpp;
                const glm::vec2 blo = glm::min(a, b) - reach, bhi = glm::max(a, b) + reach;
                const int x0 = std::max(0, static_cast<int>((blo.x - lo.x) / mpp)), x1 = std::min(out.width - 1, static_cast<int>((bhi.x - lo.x) / mpp));
                const int y0 = std::max(0, static_cast<int>((blo.y - lo.y) / mpp)), y1 = std::min(out.height - 1, static_cast<int>((bhi.y - lo.y) / mpp));
                for (int y = y0; y <= y1; ++y)
                    for (int x = x0; x <= x1; ++x) {
                        const glm::vec2 w = pixelCenter(x, y);
                        const float t = glm::dot(w - a, dir);
                        if (t < -edgeW || t > L + edgeW) continue;
                        const float dn = glm::dot(w - a, nrm);
                        if (dn < 0.0f || !pointInPolygon(p.points, w)) continue;
                        if (dn < edgeW && t >= 0.0f && t <= L) blend(x, y, dark, 0.55f * (1.0f - dn / edgeW) + 0.15f);
                        else if (patch && t >= stitchAt * 0.5f && t <= L - stitchAt * 0.5f && std::abs(dn - stitchAt) < stitchW &&
                                 std::fmod(along + t, 0.004f) < 0.0026f)
                            blend(x, y, thread, 0.7f);
                    }
                along += L;
            }
        }
    }

    // Fermetures éclair : ruban tissé, deux rangées de dents qui s'emboîtent au milieu, piqûres le long des bords.
    for (std::size_t pz = 0; pz < g.panels.size(); ++pz) {
        const auto& p = g.panels[pz];
        if (p.finish != FabricFinish::Zipper || p.points.size() < 3 || !g.panelVisible(static_cast<int>(pz))) continue;
        const std::size_t n = p.points.size();
        glm::vec2 d(1.0f, 0.0f);
        float longest = 0.0f;
        for (std::size_t e = 0; e < n; ++e)
            if (const float l = glm::distance(p.points[e], p.points[(e + 1) % n]); l > longest) {
                longest = l;
                d = (p.points[(e + 1) % n] - p.points[e]) / l;
            }
        const glm::vec2 nrm(-d.y, d.x), c = centroid(p.points);
        float halfW = 0.0f;
        glm::vec2 plo(1e9f), phi(-1e9f);
        for (const auto& q : p.points) {
            halfW = std::max(halfW, std::abs(glm::dot(q - c, nrm)));
            plo = glm::min(plo, q);
            phi = glm::max(phi, q);
        }
        const float teeth = std::min(0.0035f, halfW * 0.45f), pitch = 0.003f;
        const glm::vec3 tape = p.color, metal(0.78f, 0.77f, 0.72f);
        const int x0 = std::max(0, static_cast<int>((plo.x - lo.x) / mpp)), x1 = std::min(out.width - 1, static_cast<int>((phi.x - lo.x) / mpp) + 1);
        const int y0 = std::max(0, static_cast<int>((plo.y - lo.y) / mpp)), y1 = std::min(out.height - 1, static_cast<int>((phi.y - lo.y) / mpp) + 1);
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x) {
                const glm::vec2 w = pixelCenter(x, y);
                if (!pointInPolygon(p.points, w)) continue;
                const float s = glm::dot(w - c, d), t = glm::dot(w - c, nrm);
                glm::vec3 col = tape * (0.94f + 0.06f * std::sin(t * 6283.0f));            // ruban : fils dans la longueur
                if (std::abs(std::abs(t) - (halfW - 0.003f)) < std::max(0.0003f, mpp * 0.5f) && std::fmod(std::abs(s), 0.004f) < 0.0026f)
                    col = glm::min(tape * 1.8f + 0.08f, glm::vec3(1.0f));                    // piqûre
                if (std::abs(t) < teeth) {
                    // Dents : une rangée de chaque côté, décalées d'un demi-pas pour s'emboîter.
                    const float phase = glm::fract(s / pitch + (t < 0.0f ? 0.5f : 0.0f));
                    const bool overlap = std::abs(t) < teeth * 0.35f;   // milieu : les dents des deux côtés se croisent
                    if (phase < 0.62f || overlap) {
                        const float shade = 0.82f + 0.25f * std::sin(phase / 0.62f * 3.14159f);
                        col = metal * (overlap ? 0.78f : shade);
                    } else {
                        col = tape * 0.55f;
                    }
                }
                unsigned char* dst = at(x, y);
                dst[0] = static_cast<unsigned char>(std::lround(std::clamp(col.r, 0.0f, 1.0f) * 255.0f));
                dst[1] = static_cast<unsigned char>(std::lround(std::clamp(col.g, 0.0f, 1.0f) * 255.0f));
                dst[2] = static_cast<unsigned char>(std::lround(std::clamp(col.b, 0.0f, 1.0f) * 255.0f));
                dst[3] = 255;
            }
    }

    // Impressions, découpées au contour de leur pièce.
    for (std::size_t pp = 0; pp < g.panels.size(); ++pp) {
        const auto& p = g.panels[pp];
        if (!g.panelVisible(static_cast<int>(pp))) continue;
        for (const auto& pr : p.prints) {
            const io::RasterImage* img = load ? load(pr.image) : nullptr;
            if (img == nullptr || img->width <= 0 || img->height <= 0) continue;
            glm::vec2 c4[4];
            printCorners(p, pr, img, c4);
            glm::vec2 blo(1e9f), bhi(-1e9f);
            for (const auto& q : c4) {
                blo = glm::min(blo, q);
                bhi = glm::max(bhi, q);
            }
            const glm::vec2 c = printCenter(p, pr);
            const glm::vec2 sz = printSize(pr, img);
            const float a = glm::radians(pr.angleDeg);
            const glm::vec2 ax(std::cos(a), std::sin(a)), ay(-std::sin(a), std::cos(a));
            const int x0 = std::max(0, static_cast<int>((blo.x - lo.x) / mpp)), x1 = std::min(out.width - 1, static_cast<int>((bhi.x - lo.x) / mpp) + 1);
            const int y0 = std::max(0, static_cast<int>((blo.y - lo.y) / mpp)), y1 = std::min(out.height - 1, static_cast<int>((bhi.y - lo.y) / mpp) + 1);
            for (int y = y0; y <= y1; ++y)
                for (int x = x0; x <= x1; ++x) {
                    const glm::vec2 w = pixelCenter(x, y);
                    const glm::vec2 d = w - c;
                    const float u = glm::dot(d, ax) / sz.x + 0.5f, v = glm::dot(d, ay) / sz.y + 0.5f;
                    if (u < 0.0f || u > 1.0f || v < 0.0f || v > 1.0f) continue;
                    if (!pointInPolygon(p.points, w)) continue;
                    const glm::vec4 s = sampleImage(*img, u, v);
                    if (s.a <= 0.0f) continue;
                    unsigned char* dst = at(x, y);
                    const float da = static_cast<float>(dst[3]) / 255.0f;
                    const float oa = s.a + da * (1.0f - s.a);
                    for (int k = 0; k < 3; ++k) {
                        const float dc = static_cast<float>(dst[k]) / 255.0f;
                        const float oc = oa > 0.0f ? (s[k] * s.a + dc * da * (1.0f - s.a)) / oa : 0.0f;
                        dst[k] = static_cast<unsigned char>(std::lround(std::clamp(oc, 0.0f, 1.0f) * 255.0f));
                    }
                    dst[3] = static_cast<unsigned char>(std::lround(oa * 255.0f));
                }
        }
    }
    return true;
}

} // namespace drapeo::garment
