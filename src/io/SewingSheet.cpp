#include "io/SewingSheet.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>
#include <string>

#include "io/ViewCapture.h"

namespace drapeo::io {

namespace {
constexpr float kPtPerMm = 72.0f / 25.4f;
constexpr float kA4W = 210.0f;
constexpr float kA4H = 297.0f;

float signedArea(const std::vector<glm::vec2>& p)
{
    double a = 0.0;
    for (std::size_t i = 0; i < p.size(); ++i) {
        const auto& q = p[(i + 1) % p.size()];
        a += static_cast<double>(p[i].x) * q.y - static_cast<double>(q.x) * p[i].y;
    }
    return static_cast<float>(a * 0.5);
}

bool nearlyParallel(glm::vec2 a, glm::vec2 b)
{
    const float la = glm::length(a), lb = glm::length(b);
    if (la < 1e-8f || lb < 1e-8f) return true;
    return std::abs(a.x * b.y - a.y * b.x) < 0.04f * la * lb;
}

glm::vec2 lineHit(glm::vec2 p, glm::vec2 d, glm::vec2 q, glm::vec2 e)
{
    const float den = d.x * e.y - d.y * e.x;
    if (std::abs(den) < 1e-8f) return p;
    const float t = ((q.x - p.x) * e.y - (q.y - p.y) * e.x) / den;
    return p + d * t;
}

/// Décale un contour fermé vers l'extérieur (mètres). Les pointes trop longues sont rabotées.
std::vector<glm::vec2> offsetOutward(const std::vector<glm::vec2>& poly, float dist)
{
    const int n = static_cast<int>(poly.size());
    if (n < 3 || dist <= 0.0f) return poly;
    const float area = signedArea(poly);
    const float sign = area >= 0.0f ? -1.0f : 1.0f;   // extérieur
    std::vector<glm::vec2> a(n), dir(n);
    for (int i = 0; i < n; ++i) {
        const glm::vec2 e = poly[(i + 1) % n] - poly[i];
        const float len = glm::length(e);
        const glm::vec2 t = len > 1e-8f ? e / len : glm::vec2(1.0f, 0.0f);
        const glm::vec2 nrm = sign * glm::vec2(-t.y, t.x);
        a[i] = poly[i] + nrm * dist;
        dir[i] = t;
    }
    std::vector<glm::vec2> out(n);
    for (int i = 0; i < n; ++i) {
        const int j = (i + n - 1) % n;
        glm::vec2 p = lineHit(a[j], dir[j], a[i], dir[i]);
        const glm::vec2 mid = (poly[i] + poly[(i + 1) % n]) * 0.5f;
        if (glm::length(p - poly[i]) > dist * 6.0f || nearlyParallel(dir[j], dir[i])) p = poly[i] + (a[i] - poly[i]);
        (void)mid;
        out[i] = p;
    }
    return out;
}

struct PieceDraw {
    std::string name;
    std::vector<glm::vec2> cut;      // avec valeur de couture, mètres
    std::vector<glm::vec2> seam;     // ligne de couture
    std::vector<std::pair<glm::vec2, glm::vec2>> marks;
    glm::vec2 min{0.0f}, max{0.0f};
};

void boundsOf(const std::vector<glm::vec2>& p, glm::vec2& lo, glm::vec2& hi)
{
    lo = glm::vec2(1e9f);
    hi = glm::vec2(-1e9f);
    for (const auto& q : p) {
        lo = glm::min(lo, q);
        hi = glm::max(hi, q);
    }
}

std::vector<PieceDraw> layoutPieces(const garment::Garment& g, const SewingOptions& opt)
{
    std::vector<PieceDraw> pieces;
    const float allow = opt.allowanceMm / 1000.0f;
    for (std::size_t pi = 0; pi < g.panels.size(); ++pi) {
        const auto& panel = g.panels[pi];
        if (panel.points.size() < 3) continue;
        PieceDraw d;
        d.name = panel.name.empty() ? "Pièce " + std::to_string(pi + 1) : panel.name;
        d.seam = panel.points;
        d.cut = offsetOutward(panel.points, allow);
        boundsOf(d.cut, d.min, d.max);
        if (opt.grain) {
            glm::vec2 a, b;
            garment::grainLine(panel, a, b);
            d.marks.push_back({a, b});
        }
        if (opt.notches) {
            const int n = static_cast<int>(panel.points.size());
            for (const auto& s : g.seams) {
                auto side = [&](const garment::EdgeRef& e, int count) {
                    if (e.panel != static_cast<int>(pi) || count <= 0) return;
                    for (int k = 0; k < count; ++k) {
                        const int i0 = (e.edge + k) % n;
                        const glm::vec2 p0 = panel.points[static_cast<std::size_t>(i0)];
                        const glm::vec2 p1 = panel.points[static_cast<std::size_t>((i0 + 1) % n)];
                        const glm::vec2 mid = (p0 + p1) * 0.5f;
                        glm::vec2 t = p1 - p0;
                        const float len = glm::length(t);
                        if (len < 1e-6f) continue;
                        t /= len;
                        const glm::vec2 nrm(-t.y, t.x);
                        d.marks.push_back({mid, mid + nrm * (allow > 0.0f ? allow : 0.006f)});
                    }
                };
                side(s.a, s.countA);
                side(s.b, s.countB);
            }
        }
        pieces.push_back(std::move(d));
    }
    // Étagère : rangées de 70 cm, pour que l'A4 et le traceur partagent la même mise en page.
    float x = 0.015f, y = 0.015f, rowH = 0.0f;
    const float rowW = 0.70f;
    for (auto& d : pieces) {
        const glm::vec2 size = d.max - d.min;
        if (x > 0.02f && x + size.x > rowW) {
            x = 0.015f;
            y += rowH + 0.02f;
            rowH = 0.0f;
        }
        const glm::vec2 shift(x - d.min.x, y - d.min.y);
        auto move = [&](glm::vec2 p) { return p + shift; };
        for (auto& p : d.cut) p = move(p);
        for (auto& p : d.seam) p = move(p);
        for (auto& m : d.marks) {
            m.first = move(m.first);
            m.second = move(m.second);
        }
        d.min += shift;
        d.max += shift;
        x += size.x + 0.015f;
        rowH = std::max(rowH, size.y);
    }
    return pieces;
}

struct Page {
    float x0, y0, w, h;   // millimètres, origine en bas à gauche de la planche
    bool a4 = true;
};

std::vector<Page> pagesFor(const std::vector<PieceDraw>& pieces, bool a4)
{
    glm::vec2 hi(0.02f);
    for (const auto& d : pieces) hi = glm::max(hi, d.max + glm::vec2(0.015f));
    const float mmW = hi.x * 1000.0f, mmH = hi.y * 1000.0f;
    std::vector<Page> pages;
    if (!a4) {
        pages.push_back({0.0f, 0.0f, std::max(mmW, 40.0f), std::max(mmH, 40.0f), false});
        return pages;
    }
    const int nx = std::max(1, static_cast<int>(std::ceil(mmW / kA4W)));
    const int ny = std::max(1, static_cast<int>(std::ceil(mmH / kA4H)));
    for (int iy = 0; iy < ny; ++iy)
        for (int ix = 0; ix < nx; ++ix)
            pages.push_back({ix * kA4W, iy * kA4H, kA4W, kA4H, true});
    return pages;
}

std::string winAnsi(const std::string& utf8)
{
    std::string o;
    for (std::size_t i = 0; i < utf8.size();) {
        const unsigned char c = static_cast<unsigned char>(utf8[i]);
        if (c < 0x80) {
            if (c == '(' || c == ')' || c == '\\') o.push_back('\\');
            o.push_back(static_cast<char>(c));
            ++i;
            continue;
        }
        // UTF-8 sur deux octets → WinAnsi pour les lettres françaises courantes.
        if ((c & 0xE0) == 0xC0 && i + 1 < utf8.size()) {
            const unsigned cp = (static_cast<unsigned>(c & 0x1F) << 6) | (static_cast<unsigned char>(utf8[i + 1]) & 0x3F);
            const unsigned char map = cp <= 255 ? static_cast<unsigned char>(cp) : '?';
            o.push_back(static_cast<char>(map));
            i += 2;
            continue;
        }
        o.push_back('?');
        ++i;
    }
    return o;
}

std::string buildPdf(const std::vector<PieceDraw>& pieces, const std::vector<Page>& pages)
{
    std::vector<std::string> streams;
    streams.reserve(pages.size());
    for (std::size_t pi = 0; pi < pages.size(); ++pi) {
        const auto& page = pages[pi];
        std::ostringstream s;
        s.setf(std::ios::fixed);
        s.precision(2);
        auto X = [&](float metersX) { return (metersX * 1000.0f - page.x0) * kPtPerMm; };
        auto Y = [&](float metersY) { return (metersY * 1000.0f - page.y0) * kPtPerMm; };
        s << "0.5 w\n0 0 0 RG\n";
        // Repères d'angle.
        const float W = page.w * kPtPerMm, H = page.h * kPtPerMm;
        s << "0.4 w\n";
        s << "8 8 m 22 8 l S\n8 8 m 8 22 l S\n";
        s << W - 8 << " 8 m " << W - 22 << " 8 l S\n" << W - 8 << " 8 m " << W - 8 << " 22 l S\n";
        s << "8 " << H - 8 << " m 22 " << H - 8 << " l S\n8 " << H - 8 << " m 8 " << H - 22 << " l S\n";
        s << "BT /F1 9 Tf 28 12 Td (" << winAnsi("Drapeo  ·  échelle 1:1  ·  page " + std::to_string(pi + 1) + "/" + std::to_string(pages.size()))
          << ") Tj ET\n";
        for (const auto& d : pieces) {
            auto path = [&](const std::vector<glm::vec2>& poly) {
                if (poly.empty()) return;
                s << X(poly[0].x) << ' ' << Y(poly[0].y) << " m\n";
                for (std::size_t i = 1; i < poly.size(); ++i) s << X(poly[i].x) << ' ' << Y(poly[i].y) << " l\n";
                s << "h S\n";
            };
            s << "0.9 w\n";
            path(d.cut);
            s << "0.35 w [2 2] 0 d\n";
            path(d.seam);
            s << "[] 0 d\n0.6 w\n";
            for (const auto& m : d.marks) s << X(m.first.x) << ' ' << Y(m.first.y) << " m " << X(m.second.x) << ' ' << Y(m.second.y) << " l S\n";
            if (!d.cut.empty()) {
                s << "BT /F1 8 Tf " << X(d.min.x) << ' ' << Y(d.max.y) + 4 << " Td (" << winAnsi(d.name) << ") Tj ET\n";
            }
        }
        streams.push_back(s.str());
    }

    std::ostringstream pdf;
    pdf << "%PDF-1.4\n";
    std::vector<int> offsets;
    auto obj = [&](const std::string& body) {
        offsets.push_back(static_cast<int>(pdf.tellp()));
        pdf << offsets.size() << " 0 obj\n" << body << "\nendobj\n";
    };
    const int nPages = static_cast<int>(pages.size());
    const int fontId = 3 + nPages * 2;
    std::string kids;
    for (int i = 0; i < nPages; ++i) kids += std::to_string(4 + i * 2) + " 0 R ";
    obj("<< /Type /Catalog /Pages 2 0 R >>");
    obj("<< /Type /Pages /Count " + std::to_string(nPages) + " /Kids [" + kids + "] >>");
    obj("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding >>");
    // L'objet police est le 3e, mais les pages le référencent par son numéro final.
    // On le réécrit au bon endroit : plus simple de le laisser en 3 et de référencer 3 0 R.
    // (fontId ci-dessus n'est pas utilisé : Helvetica est l'objet 3.)
    (void)fontId;
    for (int i = 0; i < nPages; ++i) {
        const float W = pages[static_cast<std::size_t>(i)].w * kPtPerMm;
        const float H = pages[static_cast<std::size_t>(i)].h * kPtPerMm;
        const int content = 5 + i * 2;
        std::ostringstream page;
        page.setf(std::ios::fixed);
        page.precision(2);
        page << "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 " << W << ' ' << H << "] /Contents " << content
             << " 0 R /Resources << /Font << /F1 3 0 R >> >> >>";
        obj(page.str());
        obj("<< /Length " + std::to_string(streams[static_cast<std::size_t>(i)].size() + 1) + " >>\nstream\n" + streams[static_cast<std::size_t>(i)] + "\nendstream");
    }
    const int xref = static_cast<int>(pdf.tellp());
    pdf << "xref\n0 " << offsets.size() + 1 << "\n0000000000 65535 f \n";
    for (int off : offsets) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%010d 00000 n \n", off);
        pdf << buf;
    }
    pdf << "trailer << /Size " << offsets.size() + 1 << " /Root 1 0 R >>\nstartxref\n" << xref << "\n%%EOF\n";
    return pdf.str();
}

void paintPoly(std::vector<unsigned char>& rgb, int W, int H, const std::vector<glm::vec2>& poly, unsigned char r, unsigned char gch, unsigned char b,
               float x0mm, float y0mm, float mmPerPx, bool dashed)
{
    auto pix = [&](glm::vec2 meters) {
        const float x = (meters.x * 1000.0f - x0mm) / mmPerPx;
        const float y = (meters.y * 1000.0f - y0mm) / mmPerPx;
        return glm::ivec2(static_cast<int>(std::lround(x)), H - 1 - static_cast<int>(std::lround(y)));
    };
    auto plot = [&](int x, int y) {
        if (x < 0 || y < 0 || x >= W || y >= H) return;
        const std::size_t i = (static_cast<std::size_t>(y) * W + x) * 3;
        rgb[i] = r;
        rgb[i + 1] = gch;
        rgb[i + 2] = b;
    };
    auto line = [&](glm::ivec2 a, glm::ivec2 b, bool dash) {
        int x0 = a.x, y0 = a.y, x1 = b.x, y1 = b.y;
        const int dx = std::abs(x1 - x0), dy = std::abs(y1 - y0);
        const int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
        int err = dx - dy, step = 0;
        for (;;) {
            if (!dash || ((step / 4) % 2) == 0) plot(x0, y0);
            if (x0 == x1 && y0 == y1) break;
            const int e2 = 2 * err;
            if (e2 > -dy) { err -= dy; x0 += sx; }
            if (e2 < dx) { err += dx; y0 += sy; }
            ++step;
        }
    };
    for (std::size_t i = 0; i < poly.size(); ++i) line(pix(poly[i]), pix(poly[(i + 1) % poly.size()]), dashed);
}
} // namespace

bool writeSewingPdf(const garment::Garment& g, const SewingOptions& options, std::vector<unsigned char>& pdf, std::string& error)
{
    const auto pieces = layoutPieces(g, options);
    if (pieces.empty()) {
        error = "Aucune pièce à exporter.";
        return false;
    }
    const auto pages = pagesFor(pieces, options.a4);
    const std::string text = buildPdf(pieces, pages);
    pdf.assign(text.begin(), text.end());
    return true;
}

void fillPoly(std::vector<unsigned char>& rgb, int W, int H, const std::vector<glm::vec2>& poly, unsigned char r, unsigned char gch,
              unsigned char b, float x0mm, float y0mm, float mmPerPx)
{
    if (poly.size() < 3) return;
    float minY = 1e9f, maxY = -1e9f;
    for (const auto& p : poly) {
        minY = std::min(minY, p.y);
        maxY = std::max(maxY, p.y);
    }
    auto py = [&](float meters) { return H - 1 - static_cast<int>(std::lround((meters * 1000.0f - y0mm) / mmPerPx)); };
    const int y0 = std::clamp(py(maxY), 0, H - 1);
    const int y1 = std::clamp(py(minY), 0, H - 1);
    for (int y = y0; y <= y1; ++y) {
        const float my = (static_cast<float>(H - 1 - y) * mmPerPx + y0mm) / 1000.0f;
        std::vector<float> xs;
        for (std::size_t i = 0; i < poly.size(); ++i) {
            const glm::vec2 a = poly[i], c = poly[(i + 1) % poly.size()];
            if ((a.y <= my && c.y > my) || (c.y <= my && a.y > my)) {
                const float t = (my - a.y) / (c.y - a.y);
                xs.push_back((a.x + t * (c.x - a.x)) * 1000.0f);
            }
        }
        std::sort(xs.begin(), xs.end());
        for (std::size_t i = 0; i + 1 < xs.size(); i += 2) {
            int x0 = static_cast<int>(std::lround((xs[i] - x0mm) / mmPerPx));
            int x1 = static_cast<int>(std::lround((xs[i + 1] - x0mm) / mmPerPx));
            if (x0 > x1) std::swap(x0, x1);
            x0 = std::max(0, x0);
            x1 = std::min(W - 1, x1);
            for (int x = x0; x <= x1; ++x) {
                const std::size_t k = (static_cast<std::size_t>(y) * W + x) * 3;
                rgb[k] = r;
                rgb[k + 1] = gch;
                rgb[k + 2] = b;
            }
        }
    }
}

bool writeSewingPreviewPng(const garment::Garment& g, const SewingOptions& options, std::vector<unsigned char>& png, std::string& error)
{
    const auto pieces = layoutPieces(g, options);
    if (pieces.empty()) {
        error = "Aucune pièce à exporter.";
        return false;
    }
    glm::vec2 hi(0.02f);
    for (const auto& d : pieces) hi = glm::max(hi, d.max + glm::vec2(0.012f));
    const float mmW = hi.x * 1000.0f, mmH = hi.y * 1000.0f;
    const int W = 1400;
    const int H = std::clamp(static_cast<int>(W * mmH / std::max(mmW, 1.0f)), 400, 2000);
    const float mmPerPx = mmW / static_cast<float>(W);
    std::vector<unsigned char> rgb(static_cast<std::size_t>(W) * H * 3, 246);
    for (const auto& d : pieces) {
        fillPoly(rgb, W, H, d.cut, 236, 228, 214, 0.0f, 0.0f, mmPerPx);
        paintPoly(rgb, W, H, d.cut, 40, 32, 24, 0.0f, 0.0f, mmPerPx, false);
        paintPoly(rgb, W, H, d.seam, 176, 122, 48, 0.0f, 0.0f, mmPerPx, true);
        for (const auto& m : d.marks) paintPoly(rgb, W, H, {m.first, m.second}, 36, 110, 78, 0.0f, 0.0f, mmPerPx, false);
    }
    return encodePng(rgb, W, H, png);
}

} // namespace drapeo::io
