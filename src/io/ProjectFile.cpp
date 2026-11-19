#include "io/ProjectFile.h"

#include <cstdio>
#include <sstream>

#include "io/RasterImage.h"

#ifdef _WIN32
#include <windows.h>
#endif

namespace drapeo::io {

std::string writeProject(const garment::Garment& g, const ProjectAvatar* avatar)
{
    std::ostringstream o;
    o.precision(6);
    o << "drapeo-projet 1\n";
    if (avatar != nullptr)   // avatar <taille> <rotation> <chemin, vide = mannequin intégré>
        o << "avatar " << avatar->height << ' ' << avatar->rotationDeg << ' ' << avatar->path << "\n";
    for (const auto& p : g.panels) {
        o << "piece\n";
        o << "nom " << p.name << "\n";
        o << "couleur " << p.color.r << ' ' << p.color.g << ' ' << p.color.b << "\n";
        o << "droitfil " << p.grainDeg << "\n";
        if (p.symmetric) o << "symetrie " << p.symAxisX << "\n";
        const auto& pl = p.placement;
        o << "placement " << static_cast<int>(pl.zone) << ' ' << pl.angleDeg << ' ' << pl.height << ' ' << pl.radius << ' '
          << pl.offset.x << ' ' << pl.offset.y << ' ' << pl.offset.z << ' ' << pl.rotation.w << ' ' << pl.rotation.x << ' '
          << pl.rotation.y << ' ' << pl.rotation.z << "\n";
        o << "points " << p.points.size() << "\n";
        for (const auto& q : p.points) o << q.x << ' ' << q.y << "\n";
    }
    for (const auto& a : g.attachments)
        o << "posee " << a.patch << ' ' << a.base << ' ' << a.at.x << ' ' << a.at.y << ' ' << a.startEdge << ' ' << a.count << "\n";
    for (const auto& s : g.seams)
        o << "couture " << s.a.panel << ' ' << s.a.edge << ' ' << s.countA << ' ' << s.b.panel << ' ' << s.b.edge << ' '
          << s.countB << ' ' << (s.flipped ? 1 : 0) << ' ' << (s.enabled ? 1 : 0) << "\n";
    return o.str();
}

bool parseProject(const std::string& text, garment::Garment& out, std::string& error, ProjectAvatar* avatar)
{
    std::istringstream in(text);
    std::string line;
    if (!std::getline(in, line) || line.rfind("drapeo-projet", 0) != 0) {
        error = "Ce fichier n'est pas un projet Drapeo.";
        return false;
    }
    garment::Garment g;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::istringstream ls(line);
        std::string key;
        ls >> key;
        if (key == "avatar" && avatar != nullptr) {
            avatar->present = true;
            ls >> avatar->height >> avatar->rotationDeg;
            std::string rest;
            std::getline(ls, rest);
            const auto first = rest.find_first_not_of(' ');
            avatar->path = first == std::string::npos ? "" : rest.substr(first);
        } else if (key == "piece") {
            g.panels.emplace_back();
        } else if (key == "nom" && !g.panels.empty()) {
            g.panels.back().name = line.size() > 4 ? line.substr(4) : "";
        } else if (key == "couleur" && !g.panels.empty()) {
            auto& c = g.panels.back().color;
            ls >> c.r >> c.g >> c.b;
        } else if (key == "symetrie" && !g.panels.empty()) {
            g.panels.back().symmetric = true;
            ls >> g.panels.back().symAxisX;
        } else if (key == "droitfil" && !g.panels.empty()) {
            ls >> g.panels.back().grainDeg;
        } else if (key == "placement" && !g.panels.empty()) {
            auto& pl = g.panels.back().placement;
            int zone = 0;
            ls >> zone >> pl.angleDeg >> pl.height >> pl.radius;
            if (zone < 0 || zone >= garment::kBodyZoneCount) zone = 0;
            glm::vec3 off;
            glm::quat rot;
            if (ls >> off.x >> off.y >> off.z) pl.offset = off;
            if (ls >> rot.w >> rot.x >> rot.y >> rot.z && glm::length(glm::vec4(rot.x, rot.y, rot.z, rot.w)) > 0.5f)
                pl.rotation = glm::normalize(rot);
            pl.zone = static_cast<garment::BodyZone>(zone);
        } else if (key == "points" && !g.panels.empty()) {
            std::size_t n = 0;
            ls >> n;
            auto& pts = g.panels.back().points;
            for (std::size_t k = 0; k < n && std::getline(in, line); ++k) {
                std::istringstream ps(line);
                glm::vec2 q(0.0f);
                ps >> q.x >> q.y;
                pts.push_back(q);
            }
        } else if (key == "posee") {
            garment::Attachment a;
            ls >> a.patch >> a.base >> a.at.x >> a.at.y >> a.startEdge >> a.count;
            g.attachments.push_back(a);
        } else if (key == "couture") {
            garment::Seam s;
            int flipped = 0;
            ls >> s.a.panel >> s.a.edge >> s.countA >> s.b.panel >> s.b.edge >> s.countB >> flipped;
            s.flipped = flipped != 0;
            int enabled = 1;
            if (ls >> enabled) s.enabled = enabled != 0;
            g.seams.push_back(s);
        }
    }
    // Validation : pièces d'au moins 3 points, coutures sur des bords existants.
    for (const auto& p : g.panels)
        if (p.points.size() < 3) {
            error = "Pièce « " + p.name + " » invalide dans le projet.";
            return false;
        }
    std::erase_if(g.seams, [&](const garment::Seam& s) {
        for (const auto& [e, c] : {std::pair{s.a, s.countA}, std::pair{s.b, s.countB}}) {
            if (e.panel < 0 || e.panel >= static_cast<int>(g.panels.size())) return true;
            const int n = static_cast<int>(g.panels[static_cast<std::size_t>(e.panel)].points.size());
            if (e.edge < 0 || e.edge >= n || c < 1 || c >= n) return true;
        }
        return false;
    });
    std::erase_if(g.attachments, [&](const garment::Attachment& a) {
        const int n = static_cast<int>(g.panels.size());
        return a.patch < 0 || a.patch >= n || a.base < 0 || a.base >= n || a.patch == a.base;
    });
    out = std::move(g);
    return true;
}

bool saveProject(const std::string& path, const garment::Garment& g, std::string& error, const ProjectAvatar* avatar)
{
#ifdef _WIN32
    const int wlen = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
    std::wstring wpath(static_cast<std::size_t>(wlen), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, wpath.data(), wlen);
    FILE* f = _wfopen(wpath.c_str(), L"wb");
#else
    FILE* f = std::fopen(path.c_str(), "wb");
#endif
    if (f == nullptr) {
        error = "Impossible d'écrire " + path;
        return false;
    }
    const std::string text = writeProject(g, avatar);
    const bool ok = std::fwrite(text.data(), 1, text.size(), f) == text.size();
    std::fclose(f);
    if (!ok) error = "Écriture incomplète de " + path;
    return ok;
}

bool loadProject(const std::string& path, garment::Garment& out, std::string& error, ProjectAvatar* avatar)
{
    std::string bytes;
    if (!readFileBytes(path, bytes, error)) return false;
    return parseProject(bytes, out, error, avatar);
}

} // namespace drapeo::io
