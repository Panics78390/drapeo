#include "io/ProjectFile.h"

#include <algorithm>
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
    for (const auto& gr : g.groups)   // calque <visible> <verrouillé> <nom>
        o << "calque " << (gr.visible ? 1 : 0) << ' ' << (gr.locked ? 1 : 0) << ' ' << gr.name << "\n";
    for (const auto& p : g.panels) {
        o << "piece\n";
        o << "nom " << p.name << "\n";
        o << "couleur " << p.color.r << ' ' << p.color.g << ' ' << p.color.b << "\n";
        o << "droitfil " << p.grainDeg << "\n";
        if (p.symmetric) o << "symetrie " << p.symAxisX << "\n";
        if (!p.active3D || p.hidden3D || p.frozen || p.stiffness != 1.0f)   // etat3d <active> <masquée> <figée> <raideur>
            o << "etat3d " << (p.active3D ? 1 : 0) << ' ' << (p.hidden3D ? 1 : 0) << ' ' << (p.frozen ? 1 : 0) << ' ' << p.stiffness << "\n";
        if (p.placement.flat) o << "plate\n";
        if (p.finish != garment::FabricFinish::Plain) o << "fini " << static_cast<int>(p.finish) << "\n";
        for (const auto& pr : p.prints)   // impression <dx> <dy> <largeur> <hauteur> <angle> <image>
            o << "impression " << pr.offset.x << ' ' << pr.offset.y << ' ' << pr.width << ' ' << pr.height << ' ' << pr.angleDeg << ' '
              << pr.image << "\n";
        for (const auto& f : p.folds)   // pli <ax> <ay> <bx> <by> <angle> <force> (par rapport au centre du cadre)
            o << "pli " << f.a.x << ' ' << f.a.y << ' ' << f.b.x << ' ' << f.b.y << ' ' << f.angleDeg << ' ' << f.strength << "\n";
        if (p.particleDistance != 0.0f || p.layer != 0 || p.shrinkWeft != 1.0f || p.shrinkWarp != 1.0f || p.thickCollision != 0.0f ||
            p.thickRender != 0.0f || p.pressure != 0.0f)   // simu <espacement> <couche> <trame> <chaîne> <ép. contact> <ép. affichage> <pression>
            o << "simu " << p.particleDistance << ' ' << p.layer << ' ' << p.shrinkWeft << ' ' << p.shrinkWarp << ' ' << p.thickCollision << ' '
              << p.thickRender << ' ' << p.pressure << "\n";
        if (p.group >= 0) o << "groupe " << p.group << "\n";
        if (p.linkGroup > 0) o << "lien " << p.linkGroup << ' ' << (p.linkMirror ? 1 : 0) << ' ' << p.linkAngle << "\n";
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
          << s.countB << ' ' << (s.flipped ? 1 : 0) << ' ' << (s.enabled ? 1 : 0) << ' ' << s.order << "\n";
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
        } else if (key == "calque") {
            garment::PieceGroup gr;
            int visible = 1, locked = 0;
            ls >> visible >> locked;
            std::string rest;
            std::getline(ls, rest);
            const auto first = rest.find_first_not_of(' ');
            gr.name = first == std::string::npos ? "Calque" : rest.substr(first);
            gr.visible = visible != 0;
            gr.locked = locked != 0;
            g.groups.push_back(gr);
        } else if (key == "groupe" && !g.panels.empty()) {
            ls >> g.panels.back().group;
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
        } else if (key == "etat3d" && !g.panels.empty()) {
            auto& pn = g.panels.back();
            int active = 1, hidden = 0, frozen = 0;
            ls >> active >> hidden >> frozen >> pn.stiffness;
            pn.active3D = active != 0;
            pn.hidden3D = hidden != 0;
            pn.frozen = frozen != 0;
            if (!(pn.stiffness > 0.0f)) pn.stiffness = 1.0f;
        } else if (key == "simu" && !g.panels.empty()) {
            auto& pn = g.panels.back();
            ls >> pn.particleDistance >> pn.layer >> pn.shrinkWeft >> pn.shrinkWarp >> pn.thickCollision >> pn.thickRender >> pn.pressure;
            pn.particleDistance = std::clamp(pn.particleDistance, 0.0f, 0.2f);
            pn.layer = std::clamp(pn.layer, -10, 10);
            pn.shrinkWeft = std::clamp(pn.shrinkWeft, 0.5f, 1.5f);
            pn.shrinkWarp = std::clamp(pn.shrinkWarp, 0.5f, 1.5f);
            pn.thickCollision = std::clamp(pn.thickCollision, 0.0f, 0.05f);
            pn.thickRender = std::clamp(pn.thickRender, 0.0f, 0.05f);
            pn.pressure = std::clamp(pn.pressure, -1000.0f, 1000.0f);
        } else if (key == "fini" && !g.panels.empty()) {
            int f = 0;
            ls >> f;
            if (f >= 0 && f < garment::kFabricFinishCount) g.panels.back().finish = static_cast<garment::FabricFinish>(f);
        } else if (key == "impression" && !g.panels.empty()) {
            garment::Print pr;
            ls >> pr.offset.x >> pr.offset.y >> pr.width >> pr.height >> pr.angleDeg;
            std::string rest;
            std::getline(ls, rest);
            const auto first = rest.find_first_not_of(' ');
            pr.image = first == std::string::npos ? "" : rest.substr(first);
            pr.width = std::clamp(pr.width, 0.002f, 3.0f);
            pr.height = std::clamp(pr.height, 0.0f, 3.0f);
            if (!pr.image.empty()) g.panels.back().prints.push_back(pr);
        } else if (key == "pli" && !g.panels.empty()) {
            garment::FoldLine f;
            ls >> f.a.x >> f.a.y >> f.b.x >> f.b.y >> f.angleDeg >> f.strength;
            f.angleDeg = std::clamp(f.angleDeg, -180.0f, 180.0f);
            f.strength = std::clamp(f.strength, 0.0f, 1.0f);
            if (ls && glm::distance(f.a, f.b) > 1e-4f) g.panels.back().folds.push_back(f);
        } else if (key == "plate" && !g.panels.empty()) {
            g.panels.back().placement.flat = true;
        } else if (key == "lien" && !g.panels.empty()) {
            auto& pn = g.panels.back();
            int mirror = 0;
            ls >> pn.linkGroup >> mirror >> pn.linkAngle;
            pn.linkMirror = mirror != 0;
            if (pn.linkGroup < 0) pn.linkGroup = 0;
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
            int order = 0;
            if (ls >> order) s.order = std::clamp(order, 0, 2);
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
    for (auto& p : g.panels)
        if (p.group < -1 || p.group >= static_cast<int>(g.groups.size())) p.group = -1;
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
