#include "io/PatternImport.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace drapeo::io {

namespace {

std::string lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// ---------------------------------------------------------------- SVG ------
std::string attribute(const std::string& tag, const std::string& name)
{
    std::size_t pos = 0;
    while ((pos = tag.find(name, pos)) != std::string::npos) {
        const bool boundary = pos == 0 || std::isspace(static_cast<unsigned char>(tag[pos - 1]));
        std::size_t p = pos + name.size();
        while (p < tag.size() && std::isspace(static_cast<unsigned char>(tag[p]))) ++p;
        if (boundary && p < tag.size() && tag[p] == '=') {
            ++p;
            while (p < tag.size() && std::isspace(static_cast<unsigned char>(tag[p]))) ++p;
            if (p < tag.size() && (tag[p] == '"' || tag[p] == '\'')) {
                const char q = tag[p];
                const std::size_t end = tag.find(q, p + 1);
                if (end != std::string::npos) return tag.substr(p + 1, end - p - 1);
            }
        }
        pos += name.size();
    }
    return {};
}

std::vector<float> numbers(const std::string& s)
{
    std::vector<float> v;
    const char* c = s.c_str();
    while (*c) {
        if (std::isdigit(static_cast<unsigned char>(*c)) || *c == '-' || *c == '+' || *c == '.') {
            char* end = nullptr;
            const float f = std::strtof(c, &end);
            if (end == c) { ++c; continue; }
            v.push_back(f);
            c = end;
        } else {
            ++c;
        }
    }
    return v;
}

float lengthToMeters(const std::string& value, bool* hasUnit)
{
    const float n = std::strtof(value.c_str(), nullptr);
    const std::string v = lower(value);
    *hasUnit = true;
    if (v.find("mm") != std::string::npos) return n * 0.001f;
    if (v.find("cm") != std::string::npos) return n * 0.01f;
    if (v.find("in") != std::string::npos) return n * 0.0254f;
    if (v.find("pt") != std::string::npos) return n * 0.0254f / 72.0f;
    *hasUnit = v.find("px") != std::string::npos;
    return n * 0.0254f / 96.0f;   // px CSS
}

void parsePath(const std::string& d, std::vector<std::vector<glm::vec2>>& out)
{
    std::vector<glm::vec2> cur;
    glm::vec2 pos(0), start(0), lastCtrl(0);
    char cmd = 0, prevCmd = 0;
    std::size_t i = 0;
    auto skip = [&] { while (i < d.size() && (std::isspace(static_cast<unsigned char>(d[i])) || d[i] == ',')) ++i; };
    auto num = [&]() -> float {
        skip();
        const char* b = d.c_str() + i;
        char* e = nullptr;
        const float f = std::strtof(b, &e);
        i += static_cast<std::size_t>(e - b);
        return f;
    };
    auto flush = [&](bool closed) {
        if (closed && cur.size() >= 3) out.push_back(cur);
        cur.clear();
    };
    auto cubic = [&](glm::vec2 p0, glm::vec2 c1, glm::vec2 c2, glm::vec2 p3) {
        for (int k = 1; k <= 12; ++k) {
            const float t = k / 12.0f, s = 1 - t;
            cur.push_back(s * s * s * p0 + 3 * s * s * t * c1 + 3 * s * t * t * c2 + t * t * t * p3);
        }
    };
    while (true) {
        skip();
        if (i >= d.size()) break;
        if (std::isalpha(static_cast<unsigned char>(d[i]))) cmd = d[i++];
        else if (cmd == 'M') cmd = 'L';   // coordonnées implicites après M = L
        else if (cmd == 'm') cmd = 'l';
        const bool rel = std::islower(static_cast<unsigned char>(cmd));
        const glm::vec2 o = rel ? pos : glm::vec2(0);
        switch (std::toupper(static_cast<unsigned char>(cmd))) {
        case 'M': {
            flush(false);
            float x = num(), y = num();
            pos = o + glm::vec2(x, y);
            start = pos;
            cur.push_back(pos);
            break;
        }
        case 'L': { float x = num(), y = num(); pos = o + glm::vec2(x, y); cur.push_back(pos); break; }
        case 'H': { float x = num(); pos.x = (rel ? pos.x : 0) + x; cur.push_back(pos); break; }
        case 'V': { float y = num(); pos.y = (rel ? pos.y : 0) + y; cur.push_back(pos); break; }
        case 'C': {
            float a = num(), b = num(), c = num(), e = num(), f = num(), g = num();
            const glm::vec2 c1 = o + glm::vec2(a, b), c2 = o + glm::vec2(c, e), p = o + glm::vec2(f, g);
            cubic(pos, c1, c2, p);
            lastCtrl = c2;
            pos = p;
            break;
        }
        case 'S': {
            float c = num(), e = num(), f = num(), g = num();
            const char pc = static_cast<char>(std::toupper(static_cast<unsigned char>(prevCmd)));
            const glm::vec2 c1 = (pc == 'C' || pc == 'S') ? 2.0f * pos - lastCtrl : pos;
            const glm::vec2 c2 = o + glm::vec2(c, e), p = o + glm::vec2(f, g);
            cubic(pos, c1, c2, p);
            lastCtrl = c2;
            pos = p;
            break;
        }
        case 'Q': case 'T': {
            glm::vec2 q;
            if (std::toupper(static_cast<unsigned char>(cmd)) == 'Q') { float a = num(), b = num(); q = o + glm::vec2(a, b); }
            else { const char pc = static_cast<char>(std::toupper(static_cast<unsigned char>(prevCmd))); q = (pc == 'Q' || pc == 'T') ? 2.0f * pos - lastCtrl : pos; }
            float f = num(), g = num();
            const glm::vec2 p = o + glm::vec2(f, g);
            cubic(pos, pos + (q - pos) * (2.0f / 3.0f), p + (q - p) * (2.0f / 3.0f), p);
            lastCtrl = q;
            pos = p;
            break;
        }
        case 'A': {   // arc : approximé par un segment jusqu'au point final
            num(); num(); num(); num(); num();
            float x = num(), y = num();
            pos = o + glm::vec2(x, y);
            cur.push_back(pos);
            break;
        }
        case 'Z':
            pos = start;
            flush(true);
            break;
        default:
            ++i;
            break;
        }
        prevCmd = cmd;
    }
    // Contour non fermé par Z mais dont le dernier point rejoint le premier
    if (cur.size() >= 4 && glm::distance(cur.front(), cur.back()) < 1e-3f * (1 + glm::length(cur.front()))) {
        cur.pop_back();
        flush(true);
    }
}

// ---------------------------------------------------------------- DXF ------
struct Group {
    int code;
    std::string value;
};

std::vector<Group> readGroups(const std::string& text)
{
    std::vector<Group> g;
    std::istringstream in(text);
    std::string a, b;
    while (std::getline(in, a) && std::getline(in, b)) {
        auto trim = [](std::string& s) {
            while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
            std::size_t k = 0;
            while (k < s.size() && std::isspace(static_cast<unsigned char>(s[k]))) ++k;
            s.erase(0, k);
        };
        trim(a);
        trim(b);
        g.push_back({std::atoi(a.c_str()), b});
    }
    return g;
}

} // namespace

std::vector<glm::vec2> simplifyOutline(const std::vector<glm::vec2>& poly, float tolerance)
{
    std::vector<glm::vec2> pts;
    for (const auto& p : poly)
        if (pts.empty() || glm::distance(pts.back(), p) > tolerance) pts.push_back(p);
    if (pts.size() > 3 && glm::distance(pts.front(), pts.back()) <= tolerance) pts.pop_back();
    // Retire les points presque alignés avec leurs voisins.
    bool changed = true;
    while (changed && pts.size() > 3) {
        changed = false;
        for (std::size_t i = 0; i < pts.size() && pts.size() > 3; ++i) {
            const glm::vec2 a = pts[(i + pts.size() - 1) % pts.size()], b = pts[i], c = pts[(i + 1) % pts.size()];
            const glm::vec2 ac = c - a;
            const float len = glm::length(ac);
            const float dist = len > 0 ? std::abs(ac.x * (b.y - a.y) - ac.y * (b.x - a.x)) / len : glm::distance(a, b);
            if (dist < tolerance * 0.5f) {
                pts.erase(pts.begin() + static_cast<std::ptrdiff_t>(i));
                changed = true;
                break;
            }
        }
    }
    return pts;
}

bool parseSvg(const std::string& text, std::vector<std::vector<glm::vec2>>& out, std::string& error)
{
    std::vector<std::vector<glm::vec2>> raw;
    float unit = 0.0254f / 96.0f;
    std::size_t pos = 0;
    while ((pos = text.find('<', pos)) != std::string::npos) {
        const std::size_t end = text.find('>', pos);
        if (end == std::string::npos) break;
        const std::string tag = text.substr(pos + 1, end - pos - 1);
        pos = end;
        std::size_t k = 0;
        while (k < tag.size() && !std::isspace(static_cast<unsigned char>(tag[k])) && tag[k] != '/') ++k;
        const std::string name = lower(tag.substr(0, k));
        if (name == "svg") {
            const std::string w = attribute(tag, "width");
            const auto vb = numbers(attribute(tag, "viewBox"));
            bool hasUnit = false;
            if (!w.empty()) {
                const float wm = lengthToMeters(w, &hasUnit);
                if (vb.size() == 4 && vb[2] > 0) unit = wm / vb[2];
            }
        } else if (name == "polygon" || name == "polyline") {
            const auto n = numbers(attribute(tag, "points"));
            std::vector<glm::vec2> poly;
            for (std::size_t i = 0; i + 1 < n.size(); i += 2) poly.emplace_back(n[i], n[i + 1]);
            if (poly.size() >= 3) raw.push_back(poly);
        } else if (name == "rect") {
            const float x = std::strtof(attribute(tag, "x").c_str(), nullptr), y = std::strtof(attribute(tag, "y").c_str(), nullptr);
            const float w = std::strtof(attribute(tag, "width").c_str(), nullptr), h = std::strtof(attribute(tag, "height").c_str(), nullptr);
            if (w > 0 && h > 0) raw.push_back({{x, y}, {x + w, y}, {x + w, y + h}, {x, y + h}});
        } else if (name == "path") {
            parsePath(attribute(tag, "d"), raw);
        }
    }
    for (auto& poly : raw) {
        for (auto& p : poly) p = glm::vec2(p.x, -p.y) * unit;   // SVG : Y vers le bas
        auto s = simplifyOutline(poly, 0.002f);
        if (s.size() >= 3) out.push_back(std::move(s));
    }
    if (out.empty()) error = "Aucun contour fermé trouvé dans le SVG.";
    return !out.empty();
}

bool parseDxf(const std::string& text, std::vector<std::vector<glm::vec2>>& out, std::string& error)
{
    const auto g = readGroups(text);
    float unit = 0.001f;   // mm par défaut
    std::vector<std::vector<glm::vec2>> closed;
    std::vector<std::pair<glm::vec2, glm::vec2>> lines;

    for (std::size_t i = 0; i < g.size(); ++i) {
        if (g[i].code == 9 && g[i].value == "$INSUNITS" && i + 1 < g.size()) {
            switch (std::atoi(g[i + 1].value.c_str())) {
            case 1: unit = 0.0254f; break;
            case 4: unit = 0.001f; break;
            case 5: unit = 0.01f; break;
            case 6: unit = 1.0f; break;
            default: break;
            }
        }
        if (g[i].code != 0) continue;
        const std::string& type = g[i].value;
        if (type == "LWPOLYLINE") {
            std::vector<glm::vec2> poly;
            bool isClosed = false;
            float x = 0;
            std::size_t j = i + 1;
            for (; j < g.size() && g[j].code != 0; ++j) {
                if (g[j].code == 70) isClosed = (std::atoi(g[j].value.c_str()) & 1) != 0;
                if (g[j].code == 10) x = std::strtof(g[j].value.c_str(), nullptr);
                if (g[j].code == 20) poly.emplace_back(x, std::strtof(g[j].value.c_str(), nullptr));
            }
            if (poly.size() >= 3 && (isClosed || glm::distance(poly.front(), poly.back()) < 1e-3f)) closed.push_back(poly);
            else for (std::size_t k = 0; k + 1 < poly.size(); ++k) lines.emplace_back(poly[k], poly[k + 1]);
            i = j - 1;
        } else if (type == "POLYLINE") {
            bool isClosed = false;
            std::size_t j = i + 1;
            for (; j < g.size() && g[j].code != 0; ++j)
                if (g[j].code == 70) isClosed = (std::atoi(g[j].value.c_str()) & 1) != 0;
            std::vector<glm::vec2> poly;
            while (j < g.size() && g[j].code == 0 && g[j].value == "VERTEX") {
                float x = 0, y = 0;
                std::size_t k = j + 1;
                for (; k < g.size() && g[k].code != 0; ++k) {
                    if (g[k].code == 10) x = std::strtof(g[k].value.c_str(), nullptr);
                    if (g[k].code == 20) y = std::strtof(g[k].value.c_str(), nullptr);
                }
                poly.emplace_back(x, y);
                j = k;
            }
            if (poly.size() >= 3 && (isClosed || glm::distance(poly.front(), poly.back()) < 1e-3f)) closed.push_back(poly);
            else for (std::size_t k = 0; k + 1 < poly.size(); ++k) lines.emplace_back(poly[k], poly[k + 1]);
            i = j - 1;
        } else if (type == "LINE") {
            glm::vec2 a(0), b(0);
            std::size_t j = i + 1;
            for (; j < g.size() && g[j].code != 0; ++j) {
                const float v = std::strtof(g[j].value.c_str(), nullptr);
                if (g[j].code == 10) a.x = v;
                if (g[j].code == 20) a.y = v;
                if (g[j].code == 11) b.x = v;
                if (g[j].code == 21) b.y = v;
            }
            lines.emplace_back(a, b);
            i = j - 1;
        }
    }

    // Chaînage des segments isolés en contours fermés.
    std::vector<bool> used(lines.size(), false);
    for (std::size_t s = 0; s < lines.size(); ++s) {
        if (used[s]) continue;
        used[s] = true;
        std::vector<glm::vec2> chain{lines[s].first, lines[s].second};
        bool grew = true;
        while (grew) {
            grew = false;
            for (std::size_t t = 0; t < lines.size(); ++t) {
                if (used[t]) continue;
                const float tol = 1e-3f * (1.0f + glm::length(chain.back()));
                if (glm::distance(lines[t].first, chain.back()) < tol) { chain.push_back(lines[t].second); used[t] = grew = true; }
                else if (glm::distance(lines[t].second, chain.back()) < tol) { chain.push_back(lines[t].first); used[t] = grew = true; }
            }
        }
        if (chain.size() >= 4 && glm::distance(chain.front(), chain.back()) < 1e-3f * (1.0f + glm::length(chain.front()))) {
            chain.pop_back();
            closed.push_back(chain);
        }
    }

    for (auto& poly : closed) {
        for (auto& p : poly) p *= unit;
        auto s = simplifyOutline(poly, 0.002f);
        if (s.size() >= 3) out.push_back(std::move(s));
    }
    if (out.empty()) error = "Aucun contour fermé trouvé dans le DXF.";
    return !out.empty();
}

bool importPatternFile(const std::string& utf8Path, std::vector<std::vector<glm::vec2>>& outline, std::string& error)
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
    const std::string ext = lower(path.extension().string());
    if (ext == ".svg") return parseSvg(ss.str(), outline, error);
    if (ext == ".dxf") return parseDxf(ss.str(), outline, error);
    error = "Format non reconnu (attendu : .svg ou .dxf).";
    return false;
}

} // namespace drapeo::io
