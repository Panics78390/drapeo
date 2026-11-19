#include "io/PdfImport.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>

#include <stb_image.h>   // décompression zlib (stbi_zlib_decode_malloc)

#include "garment/Pattern.h"
#include "io/RasterImage.h"

namespace drapeo::io {

namespace {
constexpr float kPoint = 0.0254f / 72.0f;   // 1 point PDF en mètres

struct Matrix {
    float a = 1, b = 0, c = 0, d = 1, e = 0, f = 0;
    [[nodiscard]] glm::vec2 apply(float x, float y) const { return {a * x + c * y + e, b * x + d * y + f}; }
    // this = m × this (m appliquée en premier)
    void preMultiply(const Matrix& m)
    {
        const Matrix t = *this;
        a = m.a * t.a + m.b * t.c;
        b = m.a * t.b + m.b * t.d;
        c = m.c * t.a + m.d * t.c;
        d = m.c * t.b + m.d * t.d;
        e = m.e * t.a + m.f * t.c + t.e;
        f = m.e * t.b + m.f * t.d + t.f;
    }
};

/// Filtre ASCII85Decode (fréquent dans les PDF générés par des scripts).
std::string decodeAscii85(const std::string& in)
{
    std::string out;
    uint32_t group = 0;
    int count = 0;
    for (std::size_t i = 0; i < in.size(); ++i) {
        const char ch = in[i];
        if (ch == '~') break;   // fin « ~> »
        if (std::isspace(static_cast<unsigned char>(ch))) continue;
        if (ch == 'z' && count == 0) {
            out.append(4, '\0');
            continue;
        }
        if (ch < '!' || ch > 'u') continue;
        group = group * 85 + static_cast<uint32_t>(ch - '!');
        if (++count == 5) {
            for (int k = 3; k >= 0; --k) out += static_cast<char>((group >> (k * 8)) & 0xFF);
            group = 0;
            count = 0;
        }
    }
    if (count > 1) {   // dernier groupe incomplet : compléter avec 'u'
        for (int k = count; k < 5; ++k) group = group * 85 + 84;
        for (int k = 0; k < count - 1; ++k) out += static_cast<char>((group >> ((3 - k) * 8)) & 0xFF);
    }
    return out;
}

bool isDelimiter(char ch)
{
    return std::isspace(static_cast<unsigned char>(ch)) || ch == '(' || ch == ')' || ch == '<' || ch == '>' || ch == '[' ||
           ch == ']' || ch == '{' || ch == '}' || ch == '/' || ch == '%';
}
} // namespace

void parsePdfContent(const std::string& s, std::vector<std::vector<glm::vec2>>& closedPaths, std::size_t& pathCount)
{
    std::vector<float> stack;
    std::vector<Matrix> saved;
    Matrix ctm;
    struct Sub {
        std::vector<glm::vec2> pts;
        bool closed = false;
    };
    std::vector<Sub> path;
    glm::vec2 cur(0.0f);   // point courant (espace utilisateur, avant ctm)

    const auto lineTo = [&](float x, float y) {
        if (path.empty()) path.push_back({});
        path.back().pts.push_back(ctm.apply(x, y));
        cur = {x, y};
    };
    const auto curveTo = [&](glm::vec2 p1, glm::vec2 p2, glm::vec2 p3) {
        const glm::vec2 p0 = cur;
        for (int i = 1; i <= 8; ++i) {
            const float t = static_cast<float>(i) / 8.0f, u = 1.0f - t;
            const glm::vec2 q = u * u * u * p0 + 3 * u * u * t * p1 + 3 * u * t * t * p2 + t * t * t * p3;
            lineTo(q.x, q.y);
        }
    };
    const auto paint = [&](bool forceClose) {
        for (auto& sub : path) {
            ++pathCount;
            if (sub.pts.size() < 3) continue;
            const bool closed = sub.closed || forceClose || glm::distance(sub.pts.front(), sub.pts.back()) < 0.5f;
            if (!closed) continue;
            std::vector<glm::vec2> poly;
            for (const auto& p : sub.pts) poly.push_back(p * kPoint);
            if (glm::distance(poly.front(), poly.back()) < 1e-5f) poly.pop_back();
            if (poly.size() >= 3) closedPaths.push_back(std::move(poly));
        }
        path.clear();
    };
    const auto arg = [&](std::size_t i, std::size_t n) { return stack[stack.size() - n + i]; };

    std::size_t i = 0;
    const std::size_t n = s.size();
    while (i < n) {
        const char ch = s[i];
        if (std::isspace(static_cast<unsigned char>(ch))) { ++i; continue; }
        if (ch == '%') { while (i < n && s[i] != '\n' && s[i] != '\r') ++i; continue; }
        if (ch == '(') {   // chaîne littérale (parenthèses imbriquées, échappements)
            int depth = 0;
            for (; i < n; ++i) {
                if (s[i] == '\\') { ++i; continue; }
                if (s[i] == '(') ++depth;
                else if (s[i] == ')' && --depth == 0) { ++i; break; }
            }
            stack.clear();
            continue;
        }
        if (ch == '<') {   // chaîne hexadécimale ou dictionnaire : ignorés
            if (i + 1 < n && s[i + 1] == '<') { i += 2; continue; }
            while (i < n && s[i] != '>') ++i;
            ++i;
            continue;
        }
        if (ch == '>' || ch == '[' || ch == ']' || ch == '{' || ch == '}' || ch == ')') { ++i; continue; }
        if (ch == '/') {   // nom : opérande non numérique
            ++i;
            while (i < n && !isDelimiter(s[i])) ++i;
            continue;
        }
        // Nombre ou opérateur
        const std::size_t start = i;
        while (i < n && !isDelimiter(s[i])) ++i;
        const std::string tok = s.substr(start, i - start);
        if (tok.empty()) { ++i; continue; }
        const char c0 = tok[0];
        if (std::isdigit(static_cast<unsigned char>(c0)) || c0 == '-' || c0 == '+' || c0 == '.') {
            stack.push_back(std::strtof(tok.c_str(), nullptr));
            if (stack.size() > 64) stack.erase(stack.begin());
            continue;
        }
        const std::size_t k = stack.size();
        if (tok == "q") saved.push_back(ctm);
        else if (tok == "Q") { if (!saved.empty()) { ctm = saved.back(); saved.pop_back(); } }
        else if (tok == "cm" && k >= 6) ctm.preMultiply({arg(0, 6), arg(1, 6), arg(2, 6), arg(3, 6), arg(4, 6), arg(5, 6)});
        else if (tok == "m" && k >= 2) { path.push_back({}); lineTo(arg(0, 2), arg(1, 2)); }
        else if (tok == "l" && k >= 2) lineTo(arg(0, 2), arg(1, 2));
        else if (tok == "c" && k >= 6) curveTo({arg(0, 6), arg(1, 6)}, {arg(2, 6), arg(3, 6)}, {arg(4, 6), arg(5, 6)});
        else if (tok == "v" && k >= 4) curveTo(cur, {arg(0, 4), arg(1, 4)}, {arg(2, 4), arg(3, 4)});
        else if (tok == "y" && k >= 4) curveTo({arg(0, 4), arg(1, 4)}, {arg(2, 4), arg(3, 4)}, {arg(2, 4), arg(3, 4)});
        else if (tok == "h") { if (!path.empty()) path.back().closed = true; }
        else if (tok == "re" && k >= 4) {
            const float x = arg(0, 4), y = arg(1, 4), w = arg(2, 4), h = arg(3, 4);
            path.push_back({});
            lineTo(x, y); lineTo(x + w, y); lineTo(x + w, y + h); lineTo(x, y + h);
            path.back().closed = true;
        }
        else if (tok == "S") paint(false);
        else if (tok == "s" || tok == "f" || tok == "F" || tok == "f*" || tok == "B" || tok == "B*" || tok == "b" || tok == "b*") paint(true);
        else if (tok == "n") path.clear();   // tracé de découpe (clip) : non dessiné
        else if (tok == "BI") {              // image en ligne : sauter jusqu'à EI
            const std::size_t e = s.find("EI", i);
            i = e == std::string::npos ? n : e + 2;
        }
        stack.clear();
    }
}

bool parsePdf(const std::string& bytes, PdfPatternResult& out, std::string& error)
{
    if (bytes.compare(0, 5, "%PDF-") != 0) {
        error = "Ce fichier n'est pas un PDF.";
        return false;
    }
    std::vector<std::vector<glm::vec2>> closed;
    std::size_t pos = 0;
    while ((pos = bytes.find("stream", pos)) != std::string::npos) {
        // « endstream » contient aussi « stream » : on l'ignore.
        if (pos >= 3 && bytes.compare(pos - 3, 3, "end") == 0) { pos += 6; continue; }
        const std::size_t objStart = bytes.rfind(" obj", pos);
        const std::size_t dictStart = objStart == std::string::npos || pos - objStart > 4000 ? (pos > 2000 ? pos - 2000 : 0) : objStart;
        const std::string dict = bytes.substr(dictStart, pos - dictStart);
        std::size_t dataStart = pos + 6;
        if (dataStart < bytes.size() && bytes[dataStart] == '\r') ++dataStart;
        if (dataStart < bytes.size() && bytes[dataStart] == '\n') ++dataStart;
        std::size_t dataEnd = bytes.find("endstream", dataStart);
        if (dataEnd == std::string::npos) break;
        pos = dataEnd + 9;
        std::size_t len = dataEnd - dataStart;
        while (len > 0 && (bytes[dataStart + len - 1] == '\n' || bytes[dataStart + len - 1] == '\r')) --len;

        const bool image = dict.find("/Image") != std::string::npos;
        if (image) {
            if (out.jpeg.empty() && dict.find("/DCTDecode") != std::string::npos) out.jpeg = bytes.substr(dataStart, len);
            continue;
        }
        // Flux qui ne sont pas du contenu de page : polices, métadonnées, tables...
        if (dict.find("/Length1") != std::string::npos || dict.find("/Length2") != std::string::npos ||
            dict.find("/ObjStm") != std::string::npos || dict.find("/XRef") != std::string::npos ||
            dict.find("/Metadata") != std::string::npos || dict.find("/FontFile") != std::string::npos ||
            dict.find("/CIDFont") != std::string::npos || dict.find("/ICCBased") != std::string::npos ||
            dict.find("/N 3") != std::string::npos || dict.find("/N 4") != std::string::npos)
            continue;
        std::string content = bytes.substr(dataStart, len);
        bool supported = true;
        if (dict.find("/ASCII85Decode") != std::string::npos) content = decodeAscii85(content);
        if (dict.find("/FlateDecode") != std::string::npos) {
            int outLen = 0;
            char* raw = stbi_zlib_decode_malloc(content.data(), static_cast<int>(content.size()), &outLen);
            if (raw == nullptr) continue;
            content.assign(raw, raw + outLen);
            std::free(raw);
        }
        if (dict.find("/LZWDecode") != std::string::npos || dict.find("/RunLengthDecode") != std::string::npos ||
            dict.find("/ASCIIHexDecode") != std::string::npos)
            supported = false;   // filtres rares, non gérés
        if (!supported) continue;
        parsePdfContent(content, closed, out.paths);
    }

    // Nettoyage : simplification, petits tracés (crans, texte), cadre de page, doublons.
    // Format de page (MediaBox) : un rectangle de cette taille est le cadre de la page, pas une pièce.
    glm::vec2 page(0.0f);
    if (const std::size_t mb = bytes.find("/MediaBox"); mb != std::string::npos) {
        const std::size_t open = bytes.find('[', mb);
        if (open != std::string::npos && open - mb < 16) {
            float v[4] = {};
            const char* q = bytes.c_str() + open + 1;
            for (float& x : v) {
                char* endp = nullptr;
                x = std::strtof(q, &endp);
                q = endp;
            }
            page = glm::vec2(v[2] - v[0], v[3] - v[1]) * kPoint;
            out.pageSize = page;
        }
    }
    for (auto& p : closed) {
        p = simplifyClosed(p, 0.003f);
        if (p.size() < 3) continue;
        glm::vec2 a(1e9f), b(-1e9f);
        for (const auto& q : p) { a = glm::min(a, q); b = glm::max(b, q); }
        const glm::vec2 size = b - a;
        if (std::max(size.x, size.y) < 0.04f || std::abs(garment::signedArea(p)) < 0.0016f) continue;
        if (p.size() <= 5 && page.x > 0.0f && size.x > 0.97f * page.x && size.y > 0.97f * page.y) continue;   // cadre de page
        out.outlines.push_back(std::move(p));
    }
    keepOutermost(out.outlines);
    if (out.outlines.empty() && out.jpeg.empty()) {
        error = out.paths > 0 ? "Le PDF contient des tracés, mais aucun contour fermé assez grand pour être une pièce."
                              : "Aucun tracé ni image exploitable dans ce PDF.";
        return false;
    }
    return true;
}

} // namespace drapeo::io
