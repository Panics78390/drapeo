#include "mcp/McpServer.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <functional>

#include "garment/BodyAxes.h"
#include "garment/GarmentMesher.h"
#include "garment/Pattern.h"
#include "garment/PatternOps.h"
#include "geometry/ClothMesh.h"
#include "geometry/Mannequin.h"
#include "geometry/TriangleBvh.h"
#include "io/GarmentExport.h"
#include "io/MeshImport.h"
#include "io/ProjectFile.h"
#include "io/RasterImage.h"
#include "io/ViewCapture.h"
#include "simulation/GarmentSetup.h"
#include "simulation/Materials.h"
#include "simulation/XpbdSolver.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <spawn.h>
extern char** environ;
#endif

namespace drapeo::mcp {

using garment::Garment;
using garment::Panel;

namespace {

constexpr const char* kProtocolVersion = "2025-06-18";

// --- Chemins UTF-8 -------------------------------------------------------------
std::filesystem::path fsPath(const std::string& utf8)
{
    return std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));
}

std::string utf8Of(const std::filesystem::path& p)
{
    const std::u8string s = p.u8string();
    return std::string(s.begin(), s.end());
}

bool fileExists(const std::string& utf8)
{
    std::error_code ec;
    return std::filesystem::exists(fsPath(utf8), ec);
}

// --- Petits outils -------------------------------------------------------------
double cm(float metres) { return std::round(static_cast<double>(metres) * 1000.0) / 10.0; }   // 0,1 cm près
double round2(double v) { return std::round(v * 100.0) / 100.0; }

std::string lower(std::string s)
{
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string hexColor(glm::vec3 c)
{
    char buf[16];
    auto b = [](float v) { return std::clamp(static_cast<int>(std::lround(v * 255.0f)), 0, 255); };
    std::snprintf(buf, sizeof(buf), "#%02x%02x%02x", b(c.r), b(c.g), b(c.b));
    return buf;
}

bool parseColor(const std::string& s, glm::vec3& out)
{
    std::string h = s;
    if (!h.empty() && h[0] == '#') h.erase(0, 1);
    if (h.size() != 6) return false;
    unsigned v = 0;
    for (const char c : h) {
        v <<= 4;
        if (c >= '0' && c <= '9') v |= static_cast<unsigned>(c - '0');
        else if (c >= 'a' && c <= 'f') v |= static_cast<unsigned>(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') v |= static_cast<unsigned>(c - 'A' + 10);
        else return false;
    }
    out = glm::vec3(static_cast<float>((v >> 16) & 255), static_cast<float>((v >> 8) & 255), static_cast<float>(v & 255)) / 255.0f;
    return true;
}

const char* zoneKey(garment::BodyZone z)
{
    switch (z) {
    case garment::BodyZone::Torso: return "buste";
    case garment::BodyZone::LeftArm: return "bras_gauche";
    case garment::BodyZone::RightArm: return "bras_droit";
    case garment::BodyZone::LeftLeg: return "jambe_gauche";
    case garment::BodyZone::RightLeg: return "jambe_droite";
    }
    return "buste";
}

bool parseZone(const std::string& s, garment::BodyZone& z)
{
    const std::string k = lower(s);
    for (int i = 0; i < garment::kBodyZoneCount; ++i) {
        const auto zone = static_cast<garment::BodyZone>(i);
        if (k == zoneKey(zone)) {
            z = zone;
            return true;
        }
    }
    return false;
}

/// Erreur d'un outil (message lisible renvoyé à l'assistant).
struct ToolError {
    std::string message;
};

[[noreturn]] void fail(const std::string& message) { throw ToolError{message}; }

double num(const Json& args, const char* key, double def)
{
    const Json& v = args.get(key);
    if (v.isNull()) return def;
    if (!v.isNumber()) fail(std::string("« ") + key + " » doit être un nombre.");
    return v.asNumber();
}

double numRequired(const Json& args, const char* key)
{
    if (!args.has(key)) fail(std::string("Paramètre « ") + key + " » manquant.");
    return num(args, key, 0.0);
}

int intRequired(const Json& args, const char* key) { return static_cast<int>(std::lround(numRequired(args, key))); }

std::string str(const Json& args, const char* key, const std::string& def = {})
{
    const Json& v = args.get(key);
    if (v.isNull()) return def;
    if (!v.isString()) fail(std::string("« ") + key + " » doit être un texte.");
    return v.asString();
}

/// Pièce désignée par son index ou par son nom.
int panelRef(const Garment& g, const Json& args, const char* key = "piece")
{
    const Json& v = args.get(key);
    const int n = static_cast<int>(g.panels.size());
    if (v.isNumber()) {
        const int i = static_cast<int>(std::lround(v.asNumber()));
        if (i < 0 || i >= n) fail("Pièce " + std::to_string(i) + " introuvable (le projet a " + std::to_string(n) + " pièce(s)).");
        return i;
    }
    if (v.isString()) {
        const std::string want = lower(v.asString());
        for (int i = 0; i < n; ++i)
            if (lower(g.panels[static_cast<std::size_t>(i)].name) == want) return i;
        fail("Aucune pièce ne s'appelle « " + v.asString() + " ».");
    }
    fail(std::string("Paramètre « ") + key + " » manquant (index ou nom de la pièce).");
}

int pointRef(const Garment& g, int panel, const Json& args, const char* key = "point")
{
    const int i = intRequired(args, key);
    const int n = static_cast<int>(g.panels[static_cast<std::size_t>(panel)].points.size());
    if (i < 0 || i >= n) fail("Point " + std::to_string(i) + " introuvable (la pièce a " + std::to_string(n) + " points, de 0 à " + std::to_string(n - 1) + ").");
    return i;
}

int edgeRef(const Garment& g, int panel, const Json& args, const char* key = "bord")
{
    const int i = intRequired(args, key);
    const int n = static_cast<int>(g.panels[static_cast<std::size_t>(panel)].points.size());
    if (i < 0 || i >= n) fail("Bord " + std::to_string(i) + " introuvable (la pièce a " + std::to_string(n) + " bords, de 0 à " + std::to_string(n - 1) + ").");
    return i;
}

std::vector<glm::vec2> pointsArg(const Json& args, const char* key)
{
    const Json& v = args.get(key);
    if (!v.isArray() || v.size() < 3) fail(std::string("« ") + key + " » : au moins 3 points [x, y] en cm.");
    std::vector<glm::vec2> pts;
    for (const Json& p : v.items()) {
        if (p.isArray() && p.size() == 2 && p.at(0).isNumber() && p.at(1).isNumber())
            pts.emplace_back(static_cast<float>(p.at(0).asNumber() / 100.0), static_cast<float>(p.at(1).asNumber() / 100.0));
        else if (p.isObject() && p.get("x").isNumber() && p.get("y").isNumber())
            pts.emplace_back(static_cast<float>(p.get("x").asNumber() / 100.0), static_cast<float>(p.get("y").asNumber() / 100.0));
        else
            fail(std::string("« ") + key + " » : chaque point s'écrit [x, y] (cm).");
    }
    if (std::abs(garment::signedArea(pts)) < 1e-6f) fail("Le contour est plat (surface nulle).");
    return pts;
}

/// Réglages de placement communs à ajouter_piece / ajouter_rectangle / modifier_piece.
void applyPlacementArgs(Panel& p, const Json& args)
{
    if (args.has("zone")) {
        garment::BodyZone z{};
        if (!parseZone(str(args, "zone"), z)) fail("Zone inconnue : buste, bras_gauche, bras_droit, jambe_gauche ou jambe_droite.");
        p.placement.zone = z;
    }
    if (args.has("angle")) p.placement.angleDeg = static_cast<float>(num(args, "angle", 0.0));
    if (args.has("hauteur_cm")) p.placement.height = static_cast<float>(num(args, "hauteur_cm", 100.0) / 100.0);
    if (args.has("rayon_cm")) p.placement.radius = std::max(0.03f, static_cast<float>(num(args, "rayon_cm", 24.0) / 100.0));
    if (args.has("plate")) p.placement.flat = args.get("plate").asBool();
    if (args.has("couleur")) {
        glm::vec3 c;
        if (!parseColor(str(args, "couleur"), c)) fail("Couleur attendue au format #rrggbb.");
        p.color = c;
    }
}

/// Place une nouvelle pièce à droite des autres dans la vue patron.
void placeBesideOthers(const Garment& g, std::vector<glm::vec2>& pts)
{
    if (g.panels.empty()) return;
    float maxX = -1e9f;
    for (const auto& q : g.panels)
        for (const auto& p : q.points) maxX = std::max(maxX, p.x);
    float minX = 1e9f;
    for (const auto& p : pts) minX = std::min(minX, p.x);
    const float dx = maxX + 0.08f - minX;
    for (auto& p : pts) p.x += dx;
}

// --- Description du projet -----------------------------------------------------
const char* finishKey(garment::FabricFinish f)
{
    switch (f) {
    case garment::FabricFinish::Ripstop: return "ripstop";
    case garment::FabricFinish::Twill: return "serge";
    case garment::FabricFinish::Reflective: return "reflechissant";
    case garment::FabricFinish::Zipper: return "fermeture";
    default: return "uni";
    }
}

garment::FabricFinish finishOf(const std::string& text)
{
    const std::string t = lower(text);
    if (t == "ripstop" || t == "toile" || t == "toile technique") return garment::FabricFinish::Ripstop;
    if (t == "serge" || t == "sergé" || t == "denim") return garment::FabricFinish::Twill;
    if (t.rfind("refl", 0) == 0 || t.rfind("réfl", 0) == 0) return garment::FabricFinish::Reflective;
    if (t.rfind("fermeture", 0) == 0 || t == "zip") return garment::FabricFinish::Zipper;
    if (t == "uni" || t.empty()) return garment::FabricFinish::Plain;
    fail("Aspect inconnu : " + text + " (uni, ripstop, serge, reflechissant ou fermeture).");
}

Json describe(const Garment& g, const std::string& path)
{
    Json root = Json::object();
    root["fichier"] = path;
    root["unites"] = "cm, degrés";
    Json pieces = Json::array();
    std::vector<std::vector<int>> seamOfEdge(g.panels.size());
    for (std::size_t i = 0; i < g.panels.size(); ++i) seamOfEdge[i].assign(g.panels[i].points.size(), -1);
    for (std::size_t s = 0; s < g.seams.size(); ++s)
        for (int side = 0; side < 2; ++side)
            for (const auto& e : g.seamEdges(g.seams[s], side))
                if (e.panel >= 0 && e.panel < static_cast<int>(g.panels.size()) && e.edge >= 0 &&
                    e.edge < static_cast<int>(seamOfEdge[static_cast<std::size_t>(e.panel)].size()))
                    seamOfEdge[static_cast<std::size_t>(e.panel)][static_cast<std::size_t>(e.edge)] = static_cast<int>(s);

    for (std::size_t i = 0; i < g.panels.size(); ++i) {
        const Panel& p = g.panels[i];
        Json o = Json::object();
        o["index"] = static_cast<int>(i);
        o["nom"] = p.name;
        o["zone"] = zoneKey(p.placement.zone);
        o["angle"] = round2(p.placement.angleDeg);
        o["hauteur_cm"] = cm(p.placement.height);
        o["rayon_cm"] = cm(p.placement.radius);
        if (p.placement.flat) o["plate"] = true;
        o["couleur"] = hexColor(p.color);
        o["droit_fil"] = round2(p.grainDeg);
        if (p.symmetric) o["symetrique_axe_x_cm"] = cm(p.symAxisX);
        if (!p.active3D) o["active"] = false;
        if (p.hidden3D) o["masquee"] = true;
        if (p.frozen) o["figee"] = true;
        if (p.stiffness != 1.0f) o["raideur"] = round2(p.stiffness);
        if (p.particleDistance > 0.0f) o["espacement_mm"] = round2(p.particleDistance * 1000.0);
        if (p.layer != 0) o["couche"] = p.layer;
        if (const auto* gr = g.groupOf(static_cast<int>(i))) o["calque"] = gr->name;
        if (p.shrinkWeft != 1.0f) o["retrait_trame"] = round2(p.shrinkWeft * 100.0);
        if (p.shrinkWarp != 1.0f) o["retrait_chaine"] = round2(p.shrinkWarp * 100.0);
        if (p.thickCollision > 0.0f) o["epaisseur_contact_mm"] = round2(p.thickCollision * 1000.0);
        if (p.thickRender > 0.0f) o["epaisseur_affichage_mm"] = round2(p.thickRender * 1000.0);
        if (p.pressure != 0.0f) o["pression"] = round2(p.pressure);
        if (p.finish != garment::FabricFinish::Plain) o["aspect"] = finishKey(p.finish);
        if (!p.prints.empty()) {
            Json prints = Json::array();
            const glm::vec2 bc = garment::panelBoxCenter(p);
            for (std::size_t k = 0; k < p.prints.size(); ++k) {
                const auto& pr = p.prints[k];
                Json q = Json::object();
                q["index"] = static_cast<int>(k);
                q["image"] = pr.image;
                q["x_cm"] = cm(bc.x + pr.offset.x);
                q["y_cm"] = cm(bc.y + pr.offset.y);
                q["largeur_cm"] = cm(pr.width);
                if (pr.height > 0.0f) q["hauteur_cm"] = cm(pr.height);
                if (pr.angleDeg != 0.0f) q["angle"] = round2(pr.angleDeg);
                prints.push(std::move(q));
            }
            o["impressions"] = std::move(prints);
        }
        if (!p.folds.empty()) {
            Json folds = Json::array();
            const glm::vec2 bc = garment::panelBoxCenter(p);
            for (std::size_t k = 0; k < p.folds.size(); ++k) {
                const auto& f = p.folds[k];
                Json q = Json::object();
                q["index"] = static_cast<int>(k);
                q["x1_cm"] = cm(bc.x + f.a.x);
                q["y1_cm"] = cm(bc.y + f.a.y);
                q["x2_cm"] = cm(bc.x + f.b.x);
                q["y2_cm"] = cm(bc.y + f.b.y);
                q["angle"] = round2(f.angleDeg);
                q["force"] = round2(f.strength * 100.0);
                folds.push(std::move(q));
            }
            o["plis"] = std::move(folds);
        }
        const glm::vec2 c = garment::centroid(p.points);
        o["centre_cm"] = Json::array();
        o["centre_cm"].push(cm(c.x));
        o["centre_cm"].push(cm(c.y));
        Json pts = Json::array();
        for (const auto& q : p.points) {
            Json xy = Json::array();
            xy.push(cm(q.x));
            xy.push(cm(q.y));
            pts.push(std::move(xy));
        }
        o["points"] = std::move(pts);
        Json edges = Json::array();
        for (std::size_t e = 0; e < p.points.size(); ++e) {
            Json b = Json::object();
            b["bord"] = static_cast<int>(e);
            b["de"] = static_cast<int>(e);
            b["a"] = static_cast<int>((e + 1) % p.points.size());
            b["longueur_cm"] = cm(g.edgeLength({static_cast<int>(i), static_cast<int>(e)}));
            if (seamOfEdge[i][e] >= 0) b["couture"] = seamOfEdge[i][e];
            edges.push(std::move(b));
        }
        o["bords"] = std::move(edges);
        pieces.push(std::move(o));
    }
    root["pieces"] = std::move(pieces);
    if (!g.groups.empty()) {
        Json calques = Json::array();
        for (const auto& gr : g.groups) {
            Json c = Json::object();
            c["nom"] = gr.name;
            if (!gr.visible) c["masque"] = true;
            if (gr.locked) c["verrouille"] = true;
            calques.push(std::move(c));
        }
        root["calques"] = std::move(calques);
    }

    Json seams = Json::array();
    for (std::size_t s = 0; s < g.seams.size(); ++s) {
        const auto& sm = g.seams[s];
        Json o = Json::object();
        o["index"] = static_cast<int>(s);
        Json a = Json::object();
        a["piece"] = sm.a.panel;
        a["bord"] = sm.a.edge;
        a["nb_bords"] = sm.countA;
        a["longueur_cm"] = cm(g.seamLength(sm, 0));
        Json b = Json::object();
        b["piece"] = sm.b.panel;
        b["bord"] = sm.b.edge;
        b["nb_bords"] = sm.countB;
        b["longueur_cm"] = cm(g.seamLength(sm, 1));
        o["a"] = std::move(a);
        o["b"] = std::move(b);
        if (!sm.enabled) o["active"] = false;
        if (sm.flipped) o["inversee"] = true;
        seams.push(std::move(o));
    }
    root["coutures"] = std::move(seams);
    if (!g.attachments.empty()) {
        Json at = Json::array();
        for (const auto& a : g.attachments) {
            Json o = Json::object();
            o["piece_posee"] = a.patch;
            o["sur"] = a.base;
            const auto k = g.attachKind(a);
            o["type"] = k == garment::AttachKind::Flap ? "rabat" : k == garment::AttachKind::AllRound ? "tout_autour" : "poche";
            at.push(std::move(o));
        }
        root["pieces_posees"] = std::move(at);
    }
    return root;
}

// --- Schémas des outils ----------------------------------------------------------
struct Param {
    const char* name;
    const char* type;   // "number", "integer", "string", "boolean", "points", "piece"
    const char* description;
    bool required = false;
};

Json schemaOf(const std::vector<Param>& params)
{
    Json s = Json::object();
    s["type"] = "object";
    Json props = Json::object();
    Json req = Json::array();
    for (const auto& p : params) {
        Json d = Json::object();
        const std::string t = p.type;
        if (t == "points") {
            d["type"] = "array";
            Json item = Json::object();
            item["type"] = "array";
            Json n = Json::object();
            n["type"] = "number";
            item["items"] = std::move(n);
            item["minItems"] = 2;
            item["maxItems"] = 2;
            d["items"] = std::move(item);
            d["minItems"] = 3;
        } else if (t == "piece") {
            Json any = Json::array();
            Json a = Json::object();
            a["type"] = "integer";
            Json b = Json::object();
            b["type"] = "string";
            any.push(std::move(a));
            any.push(std::move(b));
            d["anyOf"] = std::move(any);
        } else {
            d["type"] = p.type;
        }
        d["description"] = p.description;
        props[p.name] = std::move(d);
        if (p.required) req.push(p.name);
    }
    s["properties"] = std::move(props);
    if (req.size() > 0) s["required"] = std::move(req);
    return s;
}

const std::vector<Param>& placementParams()
{
    static const std::vector<Param> p = {
        {"zone", "string", "Partie du corps : buste (défaut), bras_gauche, bras_droit, jambe_gauche, jambe_droite."},
        {"angle", "number", "Position autour du corps en degrés : 0 = devant, 180 = dos, 90 = côté gauche de l'avatar, -90 = côté droit."},
        {"hauteur_cm", "number", "Hauteur du bas de la pièce au-dessus du sol (cm), pour un avatar de 175 cm. Ex. 95 pour un haut, 60 pour une jupe."},
        {"rayon_cm", "number", "Rayon du cylindre autour du corps (cm, défaut 24)."},
        {"plate", "boolean", "Pièce posée à plat au lieu d'être enroulée autour du corps."},
        {"couleur", "string", "Couleur #rrggbb."},
    };
    return p;
}

std::vector<Param> concat(std::vector<Param> a, const std::vector<Param>& b)
{
    a.insert(a.end(), b.begin(), b.end());
    return a;
}

struct ToolDef {
    const char* name;
    const char* description;
    std::vector<Param> params;
};

const std::vector<ToolDef>& toolDefs()
{
    static const std::vector<ToolDef> defs = {
        {"projet_info",
         "Décrit le projet ouvert : pièces (index, nom, placement autour du corps, points du contour en cm, bords avec leur "
         "longueur et leur couture) et coutures. À appeler d'abord pour connaître les index des pièces, points et bords. "
         "Le bord i va du point i au point i+1 (le dernier bord revient au point 0).",
         {}},
        {"nouveau_projet", "Remplace le projet par un vêtement vide ou par un modèle prêt à l'emploi.",
         {{"modele", "string", "vide (défaut), debardeur, jupe ou robe."}}},
        {"ouvrir_projet",
         "Change de fichier projet (.drapeo). Le fichier est créé vide s'il n'existe pas. Les outils suivants travaillent sur ce fichier.",
         {{"chemin", "string", "Chemin complet du fichier .drapeo.", true}}},
        {"enregistrer_sous", "Enregistre une copie du projet sous un autre nom, puis travaille sur cette copie.",
         {{"chemin", "string", "Chemin complet du nouveau fichier .drapeo.", true}}},
        {"ajouter_piece",
         "Ajoute une pièce de patron à partir de son contour (points en cm, vus de l'extérieur du vêtement, y vers le haut). "
         "Elle est rangée à droite des autres dans la vue patron.",
         concat({{"nom", "string", "Nom de la pièce (ex. « Devant »)."},
                 {"points", "points", "Contour fermé : liste de points [x, y] en cm, au moins 3.", true}},
                placementParams())},
        {"ajouter_rectangle", "Ajoute une pièce rectangulaire.",
         concat({{"nom", "string", "Nom de la pièce."},
                 {"largeur_cm", "number", "Largeur (cm).", true},
                 {"hauteur_piece_cm", "number", "Hauteur de la pièce (cm).", true}},
                placementParams())},
        {"modifier_piece",
         "Change les réglages d'une pièce : nom, placement, droit fil, symétrie, état 3D et propriétés de simulation. "
         "Seuls les paramètres donnés changent.",
         concat({{"piece", "piece", "Index ou nom de la pièce.", true},
                 {"nom", "string", "Nouveau nom."},
                 {"droit_fil", "number", "Direction du droit fil (degrés, 90 = vertical)."},
                 {"symetrique", "boolean", "Édition symétrique autour de l'axe vertical passant par le centre."},
                 {"active", "boolean", "false : la pièce n'est pas simulée."},
                 {"masquee", "boolean", "Simulée mais pas affichée."},
                 {"figee", "boolean", "Les particules de la pièce ne bougent plus."},
                 {"raideur", "number", "1 = normal, > 1 = pièce renforcée."},
                 {"espacement_mm", "number", "Distance entre particules (mm, 0 = celle du vêtement, 20 par défaut)."},
                 {"couche", "integer", "Couche : 1, 2... = posée par-dessus les autres."},
                 {"calque", "string", "Calque (objet séparé : « Veste », « Pantalon »...) ; créé s'il n'existe pas ; \"\" = sans calque."},
                 {"retrait_trame", "number", "Retrait trame en % (100 = aucun, 90 = 10 % plus étroit)."},
                 {"retrait_chaine", "number", "Retrait chaîne en % (100 = aucun)."},
                 {"epaisseur_contact_mm", "number", "Épaisseur ajoutée pour les contacts (mm)."},
                 {"epaisseur_affichage_mm", "number", "Épaisseur ajoutée à l'affichage (mm)."},
                 {"pression", "number", "Pression (Pa) qui gonfle la pièce (doudoune, coussin)."},
                 {"aspect", "string", "Aspect du tissu : uni, ripstop (toile technique quadrillée), serge (denim), reflechissant ou fermeture (dents de fermeture éclair)."}},
                placementParams())},
        {"deplacer_piece", "Déplace, pivote ou retourne une pièce dans la vue patron (le placement 3D ne change pas).",
         {{"piece", "piece", "Index ou nom de la pièce.", true},
          {"dx_cm", "number", "Déplacement horizontal (cm)."},
          {"dy_cm", "number", "Déplacement vertical (cm)."},
          {"rotation", "number", "Rotation (degrés, sens trigonométrique) autour du centre de la pièce."},
          {"retourner", "string", "horizontal ou vertical."}}},
        {"supprimer_piece", "Supprime une pièce (et ses coutures).", {{"piece", "piece", "Index ou nom de la pièce.", true}}},
        {"dupliquer_piece", "Copie une pièce, à côté de l'original. En miroir, la copie est retournée (côté gauche / droit).",
         {{"piece", "piece", "Index ou nom de la pièce.", true}, {"miroir", "boolean", "Copie symétrique."}}},
        {"deplacer_point", "Place un point du contour à une position (cm).",
         {{"piece", "piece", "Index ou nom de la pièce.", true},
          {"point", "integer", "Index du point.", true},
          {"x_cm", "number", "Nouvelle abscisse (cm).", true},
          {"y_cm", "number", "Nouvelle ordonnée (cm).", true}}},
        {"ajouter_point", "Coupe un bord en deux en y ajoutant un point.",
         {{"piece", "piece", "Index ou nom de la pièce.", true},
          {"bord", "integer", "Index du bord.", true},
          {"position", "number", "Position le long du bord, de 0 (début) à 1 (fin). Défaut 0,5."}}},
        {"supprimer_point", "Supprime un point du contour.",
         {{"piece", "piece", "Index ou nom de la pièce.", true}, {"point", "integer", "Index du point.", true}}},
        {"coudre",
         "Coud deux suites de bords ensemble (bord à bord). Une courbe faite de plusieurs bords se coud avec nb_bords > 1 : "
         "les bords bord, bord+1... Le sens est choisi automatiquement ; « inverser » le retourne si la couture se croise.",
         {{"piece_a", "piece", "Pièce du côté A.", true},
          {"bord_a", "integer", "Premier bord du côté A.", true},
          {"nb_bords_a", "integer", "Nombre de bords consécutifs côté A (défaut 1)."},
          {"piece_b", "piece", "Pièce du côté B.", true},
          {"bord_b", "integer", "Premier bord du côté B.", true},
          {"nb_bords_b", "integer", "Nombre de bords consécutifs côté B (défaut 1)."},
          {"inverser", "boolean", "Inverse le sens d'appariement."}}},
        {"supprimer_couture", "Supprime une couture.", {{"couture", "integer", "Index de la couture.", true}}},
        {"inverser_couture",
         "Inverse le sens d'appariement d'une couture (début de A contre fin de B). À essayer quand simuler signale une couture ouverte "
         "ou que le tissu se croise.",
         {{"couture", "integer", "Index de la couture.", true}}},
        {"arrondir_coin", "Remplace un coin par un arc de cercle tangent aux deux bords.",
         {{"piece", "piece", "Index ou nom de la pièce.", true},
          {"point", "integer", "Index du coin.", true},
          {"distance_cm", "number", "Distance du coin où commence l'arrondi sur chaque bord (cm). Absent : arrondi moyen."}}},
        {"couper_bord", "Coupe un bord (ou la courbe qui le contient) en morceaux de même longueur, sans changer la forme.",
         {{"piece", "piece", "Index ou nom de la pièce.", true},
          {"bord", "integer", "Index du bord.", true},
          {"morceaux", "integer", "Nombre de morceaux (défaut 2).", false}}},
        {"ajouter_pince", "Ajoute une pince en un point du contour.",
         {{"piece", "piece", "Index ou nom de la pièce.", true},
          {"point", "integer", "Index du point du contour où placer la pince.", true},
          {"profondeur_cm", "number", "Profondeur (cm, défaut 8)."},
          {"largeur_cm", "number", "Largeur à l'ouverture (cm, défaut 3)."}}},
        {"deplier", "Déplie une demi-pièce autour d'un bord (le pli) : la pièce est complétée par son miroir.",
         {{"piece", "piece", "Index ou nom de la pièce.", true}, {"bord", "integer", "Index du bord qui sert de pli.", true}}},
        {"poser_piece",
         "Pose une pièce sur une autre (poche, rabat, renfort, bande, écusson en tissu) : elle est cousue sur la pièce de base et "
         "suit son drapé. Une pièce posée ne peut pas recevoir une autre pièce posée.",
         {{"piece", "piece", "Pièce à poser (index ou nom).", true},
          {"sur", "piece", "Pièce de base (index ou nom).", true},
          {"x_cm", "number", "Centre de la pièce posée (cm, coordonnées du patron comme les points de la pièce de base : il doit être sur elle).", true},
          {"y_cm", "number", "Centre (cm).", true},
          {"type", "string", "poche (ouverte en haut, défaut), rabat (cousu en haut seulement) ou tout_autour (renfort, bande)."}}},
        {"ajouter_impression",
         "Imprime une image (PNG, JPEG, BMP, TGA) sur une pièce : logo, inscription, drapeau, bande réfléchissante. L'image est "
         "découpée au contour de la pièce, la transparence est respectée, et elle suit le drapé en 3D.",
         {{"piece", "piece", "Index ou nom de la pièce.", true},
          {"image", "string", "Chemin de l'image (absolu, ou relatif au dossier du projet).", true},
          {"x_cm", "number", "Centre de l'image dans le patron (cm). Défaut : centre de la pièce."},
          {"y_cm", "number", "Centre de l'image (cm)."},
          {"largeur_cm", "number", "Largeur (cm, défaut 10)."},
          {"hauteur_cm", "number", "Hauteur (cm, 0 ou absent = proportions de l'image)."},
          {"angle", "number", "Rotation (degrés)."}}},
        {"supprimer_impression", "Retire une impression d'une pièce.",
         {{"piece", "piece", "Index ou nom de la pièce.", true}, {"index", "integer", "Index de l'impression (voir projet_info).", true}}},
        {"allonger_piece",
         "Allonge (longueur > 0) ou raccourcit (< 0) une pièce à une hauteur du patron : tout ce qui est au-dessus de la ligne se "
         "déplace verticalement. Les pièces posées et les impressions suivent ; les coutures sont gardées.",
         {{"piece", "piece", "Index ou nom de la pièce.", true},
          {"a_hauteur_cm", "number", "Ligne d'allongement (y du patron, cm).", true},
          {"longueur_cm", "number", "Allongement (cm, négatif pour raccourcir).", true}}},
        {"couper_piece",
         "Coupe une pièce en deux morceaux le long d'une ligne (par exemple pour séparer le haut et le bas d'une poche, ou faire "
         "un empiècement). Donner soit hauteur_cm (ligne horizontale), soit x_cm (ligne verticale), soit deux points de la ligne. "
         "Les coutures sont gardées (une couture coupée est partagée entre les deux morceaux), les poches et impressions suivent leur morceau.",
         {{"piece", "piece", "Index ou nom de la pièce.", true},
          {"hauteur_cm", "number", "Ligne horizontale y = hauteur (cm, coordonnées du patron)."},
          {"x_cm", "number", "Ligne verticale x = valeur (cm)."},
          {"x1_cm", "number", "Ligne quelconque : premier point (cm)."},
          {"y1_cm", "number", "Premier point (cm)."},
          {"x2_cm", "number", "Second point (cm)."},
          {"y2_cm", "number", "Second point (cm)."},
          {"recoudre", "boolean", "Recoudre les deux morceaux le long de la coupe (défaut : non, deux pièces séparées)."}}},
        {"ajouter_soufflet",
         "Ajoute une bande de soufflet (épaisseur d'une poche ou d'un sac) : une bande aussi longue que les bords choisis, cousue "
         "dessus et dressée en 3D. Son autre côté peut être cousu sur les bords d'une autre pièce (fond de la poche).",
         {{"piece", "piece", "Pièce qui reçoit la bande (index ou nom).", true},
          {"bord", "integer", "Premier bord de la portion.", true},
          {"nb_bords", "integer", "Nombre de bords consécutifs (défaut 1)."},
          {"largeur_cm", "number", "Largeur de la bande (cm, défaut 3)."},
          {"piece_b", "piece", "Autre pièce où coudre l'autre côté de la bande (facultatif)."},
          {"bord_b", "integer", "Premier bord côté B."},
          {"nb_bords_b", "integer", "Nombre de bords côté B (défaut 1)."},
          {"inverser_b", "boolean", "Parcourt les bords B dans l'autre sens (si la bande se vrille)."}}},
        {"ajouter_pli",
         "Ajoute une ligne de pli sur une pièce : le tissu s'y plie de l'angle donné en 3D (passepoil, bande de fermeture repliée, "
         "bord de poche retourné). La ligne est coupée au contour de la pièce.",
         {{"piece", "piece", "Index ou nom de la pièce.", true},
          {"x1_cm", "number", "Début de la ligne (cm, coordonnées du patron).", true},
          {"y1_cm", "number", "Début (cm).", true},
          {"x2_cm", "number", "Fin de la ligne (cm).", true},
          {"y2_cm", "number", "Fin (cm).", true},
          {"angle", "number", "Angle du pli (degrés, défaut 90 ; 180 = replié à plat). > 0 : vers l'endroit, < 0 : vers l'envers."},
          {"force", "number", "Force du pli de 0 (souple) à 100 (marqué), défaut 80."}}},
        {"supprimer_pli", "Retire une ligne de pli d'une pièce.",
         {{"piece", "piece", "Index ou nom de la pièce.", true}, {"index", "integer", "Index de la ligne de pli (voir projet_info).", true}}},
        {"ajouter_fermeture",
         "Pose une fermeture éclair sur une pièce, le long d'une ligne : bande à dents (aspect « fermeture ») cousue dessus, "
         "et curseur avec tirette au bout de la ligne (x2, y2).",
         {{"piece", "piece", "Pièce qui reçoit la fermeture (index ou nom).", true},
          {"x1_cm", "number", "Début de la fermeture (cm, coordonnées du patron de la pièce).", true},
          {"y1_cm", "number", "Début (cm).", true},
          {"x2_cm", "number", "Bout de la fermeture, côté curseur (cm).", true},
          {"y2_cm", "number", "Bout (cm).", true},
          {"largeur_cm", "number", "Largeur de la bande (cm, défaut 2,5)."}}},
        {"ajouter_poche",
         "Trace une poche directement sur une pièce (rectangle centré en x, y) et la coud à sa place : poche ouverte en haut, "
         "rabat cousu en haut, ou renfort cousu tout le tour. Option : rabat ajouté au-dessus de l'ouverture.",
         {{"piece", "piece", "Pièce qui reçoit la poche (index ou nom).", true},
          {"x_cm", "number", "Centre de la poche (cm, coordonnées du patron de la pièce).", true},
          {"y_cm", "number", "Centre (cm).", true},
          {"largeur_cm", "number", "Largeur de la poche (cm).", true},
          {"hauteur_cm", "number", "Hauteur de la poche (cm).", true},
          {"type", "string", "« poche » (défaut), « rabat » ou « tout_le_tour »."},
          {"avec_rabat", "boolean", "Poche seulement : ajoute un rabat au-dessus (défaut false)."}}},
        {"agrandir_piece", "Agrandit (distance > 0) ou rétrécit (< 0) tout le contour d'une pièce. Les coutures sont gardées.",
         {{"piece", "piece", "Index ou nom de la pièce.", true}, {"distance_cm", "number", "Décalage du contour (cm).", true}}},
        {"simuler",
         "Simule le drapé du vêtement sur l'avatar du projet (mannequin intégré par défaut) et donne un bilan : coutures fermées, "
         "étirement, encombrement. Peut exporter le vêtement drapé en OBJ.",
         {{"secondes", "number", "Durée simulée (défaut 4, max 30)."},
          {"matiere", "string", "Coton (défaut), Soie, Denim ou Cuir."},
          {"exporter_obj", "string", "Chemin d'un fichier .obj où enregistrer le vêtement drapé (facultatif)."}}},
        {"capture_vue",
         "Simule le drapé puis renvoie une image PNG de la vue 3D (vêtement sur l'avatar) : face, profil, dos, trois quarts "
         "ou un angle en degrés ; « toutes » met face, profil et dos côte à côte. L'image est jointe à la réponse et peut "
         "être enregistrée.",
         {{"angle", "string", "face (défaut), profil, dos, profil_droit, trois_quarts, trois_quarts_dos, toutes, ou degrés (0 = face, 90 = profil gauche, 180 = dos)."},
          {"chemin_png", "string", "Fichier .png où enregistrer l'image (facultatif)."},
          {"secondes", "number", "Durée simulée avant la capture (défaut 4, max 30)."},
          {"matiere", "string", "Coton (défaut), Soie, Denim ou Cuir."},
          {"largeur_px", "number", "Largeur totale de l'image (défaut 600 pour une vue, 1200 pour plusieurs ; max 2400)."},
          {"cadrage", "string", "« avatar » (défaut) ou « vetement » (zoom sur le vêtement)."},
          {"avec_avatar", "boolean", "Afficher l'avatar (défaut true)."},
          {"plongee", "number", "Inclinaison de la caméra vers le bas, en degrés (défaut 0)."}}},
        {"ouvrir_dans_drapeo",
         "Ouvre le projet dans la fenêtre de Drapeo. Ensuite, chaque modification faite par les outils s'affiche en direct.", {}},
    };
    return defs;
}

// --- Simulation hors écran ---------------------------------------------------------
/// Résultat géométrique d'une simulation (pour capture_vue).
struct SimScene {
    std::vector<glm::vec3> cloth;
    std::vector<uint32_t> clothIndices;
    std::vector<glm::vec3> clothColors;
    std::vector<int> clothParts;
    std::vector<glm::vec3> body;
    std::vector<uint32_t> bodyIndices;
};

std::string simulate(const Garment& g, const io::ProjectAvatar& av, const std::string& projectPath, double seconds,
                     const std::string& materialName, const std::string& objPath, SimScene* scene = nullptr)
{
    if (g.empty()) fail("Le projet est vide : ajoutez des pièces d'abord.");
    // Avatar : celui du projet, sinon le mannequin intégré.
    io::ImportedMesh body;
    std::string note;
    bool builtin = true;
    if (av.present && !av.path.empty()) {
        std::string err;
        std::string candidate = av.path;
        if (!fileExists(candidate)) {
            const auto cut = av.path.find_last_of("/\\");
            const auto dir = projectPath.find_last_of("/\\");
            candidate = (dir == std::string::npos ? std::string() : projectPath.substr(0, dir + 1)) +
                        (cut == std::string::npos ? av.path : av.path.substr(cut + 1));
        }
        if (io::importMesh(candidate, body, err)) builtin = false;
        else note = "Avatar introuvable (" + av.path + ") : mannequin intégré utilisé. ";
    }
    if (builtin) geometry::makeMannequin(body.positions, body.indices);
    io::FitOptions fit;
    fit.targetHeight = av.present ? std::clamp(av.height, 0.5f, 2.5f) : 1.75f;
    fit.rotationYDegrees = av.present ? av.rotationDeg : 0.0f;
    io::fitToGround(body.positions, fit);
    geometry::TriangleBvh bvh;
    bvh.build(body.positions, body.indices);

    garment::MeshingOptions mo;
    mo.spacing = 0.02f;
    mo.bodyScale = fit.targetHeight / 1.75f;
    mo.axes = builtin ? garment::BodyAxes{} : garment::estimateBodyAxes(body.positions);
    garment::GarmentMesh gm;
    std::string err;
    if (!garment::buildGarmentMesh(g, mo, gm, err)) fail("Maillage impossible : " + err);

    simulation::FabricMaterial material = simulation::fabricPresets()[0];
    bool found = materialName.empty();
    for (const auto& m : simulation::fabricPresets())
        if (lower(m.name) == lower(materialName)) {
            material = m;
            found = true;
        }
    if (!found) fail("Matière inconnue : Coton, Soie, Denim ou Cuir.");

    simulation::XpbdSolver solver;
    solver.build(gm.mesh, material);
    simulation::applyShrinkage(solver, g, gm);
    simulation::applyFolds(solver, gm);
    solver.spheres().clear();
    solver.setMeshCollider(&bvh);
    solver.setSeams(gm.seamPairs, 1.0f);
    simulation::applyPanelProperties(solver, g, gm);

    const auto t0 = std::chrono::steady_clock::now();
    const int frames = static_cast<int>(std::ceil(std::clamp(seconds, 0.1, 30.0) * 60.0));
    for (int f = 0; f < frames; ++f) solver.step(1.0f / 60.0f);
    const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();

    const auto& x = solver.positions();
    glm::vec3 lo(1e9f), hi(-1e9f);
    bool finite = true;
    for (const auto& p : x) {
        if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) {
            finite = false;
            continue;
        }
        lo = glm::min(lo, p);
        hi = glm::max(hi, p);
    }
    // Écart restant sur les coutures (sommets qui devraient être réunis).
    float seamGap = 0.0f;
    std::vector<float> gapOfSeam(g.seams.size(), 0.0f);
    for (std::size_t k = 0; k < gm.allSeamPairs.size() && k < gm.seamOfPair.size(); ++k) {
        const int s = gm.seamOfPair[k];
        if (s < 0 || s >= static_cast<int>(g.seams.size()) || !g.seams[static_cast<std::size_t>(s)].enabled) continue;
        const auto& pr = gm.allSeamPairs[k];
        const float d = glm::distance(x[pr[0]], x[pr[1]]);
        gapOfSeam[static_cast<std::size_t>(s)] = std::max(gapOfSeam[static_cast<std::size_t>(s)], d);
        seamGap = std::max(seamGap, d);
    }
    // Pièces tombées au sol.
    std::vector<int> onFloor(g.panels.size(), 0), count(g.panels.size(), 0);
    for (std::size_t v = 0; v < x.size() && v < gm.panelOfVertex.size(); ++v) {
        const int p = gm.panelOfVertex[v];
        if (p < 0 || p >= static_cast<int>(g.panels.size())) continue;
        ++count[static_cast<std::size_t>(p)];
        if (x[v].y < 0.05f) ++onFloor[static_cast<std::size_t>(p)];
    }

    Json r = Json::object();
    r["secondes_simulees"] = round2(frames / 60.0);
    r["calcul_secondes"] = round2(elapsed);
    r["matiere"] = material.name;
    r["avatar"] = builtin ? std::string("mannequin intégré") : av.path;
    r["particules"] = x.size();
    r["triangles"] = gm.mesh.indices.size() / 3;
    r["coutures_points"] = gm.seamPairs.size();
    r["ecart_max_coutures_cm"] = cm(seamGap);
    r["etirement_moyen_pct"] = round2(solver.averageStretchError() * 100.0);
    r["contacts_avatar"] = solver.meshContacts();
    if (finite) {
        Json box = Json::object();
        box["x_cm"] = Json::array();
        box["x_cm"].push(cm(lo.x));
        box["x_cm"].push(cm(hi.x));
        box["y_cm"] = Json::array();
        box["y_cm"].push(cm(lo.y));
        box["y_cm"].push(cm(hi.y));
        box["z_cm"] = Json::array();
        box["z_cm"].push(cm(lo.z));
        box["z_cm"].push(cm(hi.z));
        r["encombrement"] = std::move(box);
    } else {
        r["probleme"] = "La simulation a divergé (positions non finies).";
    }
    Json fallen = Json::array();
    for (std::size_t p = 0; p < g.panels.size(); ++p)
        if (count[p] > 0 && onFloor[p] * 2 > count[p]) fallen.push(g.panels[p].name);
    if (fallen.size() > 0) r["pieces_tombees_au_sol"] = std::move(fallen);
    Json open = Json::array();
    for (std::size_t sm = 0; sm < gapOfSeam.size(); ++sm)
        if (gapOfSeam[sm] > 0.03f) {
            Json o = Json::object();
            o["couture"] = static_cast<int>(sm);
            o["ecart_cm"] = cm(gapOfSeam[sm]);
            open.push(std::move(o));
        }
    if (open.size() > 0) r["coutures_ouvertes"] = std::move(open);
    if (seamGap > 0.03f) note += "Certaines coutures ne sont pas fermées (voir coutures_ouvertes) : essayez inverser_couture, ou vérifiez qu'elles "
                "relient les bons bords et que les pièces sont placées du bon côté du corps (zone, angle). ";

    if (!objPath.empty()) {
        std::vector<glm::vec3> normals;
        geometry::computeVertexNormals(x, solver.indices(), normals);
        io::ExportMesh em;
        em.positions = &x;
        em.normals = &normals;
        em.uvs = &gm.mesh.uvs;
        em.indices = &solver.indices();
        em.panelOfVertex = &gm.panelOfVertex;
        for (const auto& p : g.panels) em.panelNames.push_back(p.name);
        std::string e2;
        if (!io::exportObj(objPath, em, e2)) fail("Export OBJ impossible : " + e2);
        r["obj"] = objPath;
    }
    if (!note.empty()) r["remarque"] = note;
    if (scene) {
        scene->cloth = x;
        scene->clothIndices = solver.indices();
        scene->clothColors.assign(x.size(), glm::vec3(0.8f));
        for (std::size_t v = 0; v < x.size() && v < gm.panelOfVertex.size(); ++v) {
            const int p = gm.panelOfVertex[v];
            if (p >= 0 && p < static_cast<int>(g.panels.size())) scene->clothColors[v] = g.panels[static_cast<std::size_t>(p)].color;
        }
        scene->clothParts = gm.panelOfVertex;
        scene->body = std::move(body.positions);
        scene->bodyIndices = std::move(body.indices);
    }
    return r.dump();
}

// --- Capture de la vue 3D ---------------------------------------------------------
float angleOf(const std::string& name)
{
    const std::string a = lower(name);
    if (a.empty() || a == "face" || a == "devant") return 0.0f;
    if (a == "profil" || a == "profil_gauche" || a == "gauche") return 90.0f;
    if (a == "dos" || a == "derriere" || a == "derrière") return 180.0f;
    if (a == "profil_droit" || a == "droite") return 270.0f;
    if (a == "3/4" || a == "trois_quarts") return 35.0f;
    if (a == "3/4_dos" || a == "trois_quarts_dos") return 145.0f;
    char* end = nullptr;
    const double v = std::strtod(a.c_str(), &end);
    if (end && end != a.c_str() && *end == '\0') return static_cast<float>(v);
    fail("angle : face, profil, dos, profil_droit, trois_quarts, trois_quarts_dos, toutes, ou un nombre de degrés.");
}

std::string captureView(const Garment& g, const io::ProjectAvatar& av, const std::string& projectPath, const Json& args,
                        std::vector<uint8_t>& png)
{
    // angle : texte, nombre, ou liste ; « toutes » = face + profil + dos côte à côte.
    std::vector<float> angles;
    std::vector<std::string> names;
    const Json& a = args.get("angle");
    auto addOne = [&](const Json& v) {
        if (v.isNumber()) {
            angles.push_back(static_cast<float>(v.asNumber()));
            names.push_back(std::to_string(static_cast<int>(std::lround(v.asNumber()))) + "°");
        } else if (lower(v.asString()) == "toutes" || lower(v.asString()) == "tout") {
            for (const char* n : {"face", "profil", "dos"}) {
                angles.push_back(angleOf(n));
                names.push_back(n);
            }
        } else {
            angles.push_back(angleOf(v.asString()));
            names.push_back(v.asString().empty() ? std::string("face") : v.asString());
        }
    };
    if (a.isArray()) for (const Json& v : a.items()) addOne(v);
    else addOne(a);
    if (angles.empty()) addOne(Json("face"));
    if (angles.size() > 4) fail("4 angles au plus par capture.");

    SimScene scene;
    const std::string report = simulate(g, av, projectPath, num(args, "secondes", 4.0), str(args, "matiere"), {}, &scene);
    const bool withBody = args.get("avec_avatar").asBool(true);
    const std::string frame = lower(str(args, "cadrage", "avatar"));
    if (frame != "avatar" && frame != "vetement" && frame != "vêtement") fail("cadrage : « avatar » ou « vetement ».");

    std::vector<io::CaptureMesh> meshes;
    io::CaptureMesh body;
    body.positions = &scene.body;
    body.indices = &scene.bodyIndices;
    body.color = glm::vec3(0.62f, 0.60f, 0.58f);
    body.frame = frame == "avatar";
    if (withBody) meshes.push_back(body);
    io::CaptureMesh cloth;
    cloth.positions = &scene.cloth;
    cloth.indices = &scene.clothIndices;
    cloth.vertexColors = &scene.clothColors;
    cloth.partOfVertex = &scene.clothParts;
    cloth.depthBias = 0.002f;
    meshes.push_back(cloth);

    const int total = std::clamp(static_cast<int>(num(args, "largeur_px", angles.size() > 1 ? 1200.0 : 600.0)), 200, 2400);
    io::CaptureOptions opt;
    opt.width = std::max(160, total / static_cast<int>(angles.size()));
    opt.pitchDeg = static_cast<float>(num(args, "plongee", 0.0));
    std::vector<std::vector<uint8_t>> images;
    std::vector<int> widths;
    int w = 0, h = 0;
    for (const float ang : angles) {
        opt.yawDeg = ang;
        images.emplace_back();
        io::renderView(meshes, opt, w, h, images.back());
        widths.push_back(w);
    }
    std::vector<uint8_t> rgb;
    int width = 0;
    io::joinSideBySide(images, widths, h, rgb, width);
    if (!io::encodePng(rgb, width, h, png)) fail("Encodage PNG impossible.");

    Json r = Json::object();
    Json views = Json::array();
    for (const auto& n : names) views.push(n);
    r["vues"] = std::move(views);
    r["image_px"] = std::to_string(width) + " x " + std::to_string(h);
    const std::string path = str(args, "chemin_png");
    if (!path.empty()) {
        std::string err;
        if (!io::writeBytes(path, png, err)) fail(err);
        r["png"] = path;
    }
    Json sim;
    std::string e;
    if (Json::parse(report, sim, e)) r["simulation"] = std::move(sim);
    r["lecture"] = "Vues côte à côte de gauche à droite, dans l'ordre de « vues ». Profil = côté gauche du personnage. "
                   "Tissu aux couleurs des pièces (envers plus sombre), avatar en gris, traits sombres = bords des pièces. Les impressions (logos) ne sont pas dessinées.";
    return r.dump();
}

bool launchDrapeo(const std::string& exe, const std::string& project, std::string& error)
{
    if (exe.empty() || !fileExists(exe)) {
        error = "drapeo introuvable à côté du serveur MCP (" + exe + "). Ouvrez le projet à la main : " + project;
        return false;
    }
#ifdef _WIN32
    auto wide = [](const std::string& s) {
        const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
        std::wstring w(n > 0 ? static_cast<std::size_t>(n) : 1u, L'\0');
        if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
        w.resize(w.size() - 1);
        return w;
    };
    std::wstring cmd = L"\"" + wide(exe) + L"\" \"" + wide(project) + L"\"";
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, DETACHED_PROCESS, nullptr, nullptr, &si, &pi)) {
        error = "Lancement de Drapeo impossible (erreur " + std::to_string(GetLastError()) + ").";
        return false;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
#else
    pid_t pid = 0;
    std::string a0 = exe, a1 = project;
    char* argv[] = {a0.data(), a1.data(), nullptr};
    if (posix_spawn(&pid, exe.c_str(), nullptr, nullptr, argv, environ) != 0) {
        error = "Lancement de Drapeo impossible.";
        return false;
    }
    return true;
#endif
}

} // namespace

// =============================================================================
std::string defaultProjectPath()
{
    std::filesystem::path dir;
#ifdef _WIN32
    if (const wchar_t* home = _wgetenv(L"USERPROFILE")) dir = std::filesystem::path(home) / L"Documents" / L"Drapeo";
#else
    if (const char* home = std::getenv("HOME")) dir = std::filesystem::path(home) / "Documents" / "Drapeo";
#endif
    if (dir.empty()) dir = std::filesystem::current_path();
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return utf8Of(dir / "mcp.drapeo");
}

Server::Server(std::string projectPath, std::string drapeoExe) : projectPath_(std::move(projectPath)), drapeoExe_(std::move(drapeoExe)) {}

Json Server::toolList()
{
    Json tools = Json::array();
    for (const auto& d : toolDefs()) {
        Json t = Json::object();
        t["name"] = d.name;
        t["description"] = d.description;
        t["inputSchema"] = schemaOf(d.params);
        tools.push(std::move(t));
    }
    return tools;
}

bool Server::callTool(const std::string& name, const Json& argsIn, std::string& text, std::string& error)
{
    image_.clear();
    const Json args = argsIn.isObject() ? argsIn : Json::object();
    try {
        // Projet courant (vide s'il n'existe pas encore).
        Garment g;
        io::ProjectAvatar av;
        if (fileExists(projectPath_)) {
            std::string err;
            if (!io::loadProject(projectPath_, g, err, &av)) fail("Lecture du projet impossible (" + projectPath_ + ") : " + err);
        }
        const std::size_t seamsBefore = g.seams.size();
        auto save = [&](const std::string& summary) {
            text = summary;
            if (name != "supprimer_couture" && name != "nouveau_projet" && name != "ouvrir_projet" && name != "supprimer_piece" &&
                name != "coudre" && g.seams.size() < seamsBefore)
                text += " Attention : " + std::to_string(seamsBefore - g.seams.size()) +
                        " couture(s) retirée(s) car leurs bords ont changé (voir projet_info, puis coudre à nouveau).";
            std::string err;
            io::ProjectAvatar a = av;
            a.present = true;
            if (!io::saveProject(projectPath_, g, err, &a)) fail("Enregistrement impossible (" + projectPath_ + ") : " + err);
        };
        auto P = [&](int i) -> Panel& { return g.panels[static_cast<std::size_t>(i)]; };
        auto pointText = [&](int panel) {
            std::string s;
            const auto& pts = P(panel).points;
            for (std::size_t i = 0; i < pts.size(); ++i) {
                char buf[64];
                std::snprintf(buf, sizeof(buf), "%s%zu:(%.1f, %.1f)", i ? " " : "", i, cm(pts[i].x), cm(pts[i].y));
                s += buf;
            }
            return s;
        };

        if (name == "projet_info") {
            text = describe(g, projectPath_).dump();
            return true;
        }
        if (name == "nouveau_projet") {
            const std::string m = lower(str(args, "modele", "vide"));
            if (m == "vide") g = {};
            else if (m == "debardeur" || m == "débardeur") g = garment::makeTankTop();
            else if (m == "jupe") g = garment::makeSkirt();
            else if (m == "robe") g = garment::makeDress();
            else fail("Modèle inconnu : vide, debardeur, jupe ou robe.");
            save("Nouveau projet (" + m + ") : " + std::to_string(g.panels.size()) + " pièce(s), " + std::to_string(g.seams.size()) +
                 " couture(s). Fichier : " + projectPath_);
            return true;
        }
        if (name == "ouvrir_projet" || name == "enregistrer_sous") {
            std::string path = str(args, "chemin");
            if (path.empty()) fail("Paramètre « chemin » manquant.");
            if (lower(utf8Of(fsPath(path).extension())) != ".drapeo") path += ".drapeo";
            if (name == "ouvrir_projet") {
                projectPath_ = path;
                if (fileExists(path)) {
                    Garment g2;
                    std::string err;
                    if (!io::loadProject(path, g2, err)) fail("Lecture impossible : " + err);
                    text = "Projet ouvert : " + path + " (" + std::to_string(g2.panels.size()) + " pièce(s), " +
                           std::to_string(g2.seams.size()) + " couture(s)).";
                } else {
                    g = {};
                    save("Nouveau projet vide créé : " + path);
                }
                return true;
            }
            projectPath_ = path;
            save("Projet enregistré sous " + path + " ; les outils travaillent maintenant sur ce fichier.");
            return true;
        }
        if (name == "ajouter_piece" || name == "ajouter_rectangle") {
            Panel p;
            if (name == "ajouter_piece") {
                p.points = pointsArg(args, "points");
            } else {
                const float w = static_cast<float>(numRequired(args, "largeur_cm") / 100.0);
                const float h = static_cast<float>(numRequired(args, "hauteur_piece_cm") / 100.0);
                if (w <= 0.005f || h <= 0.005f) fail("Largeur et hauteur doivent être positives.");
                p.points = {{-w / 2, 0.0f}, {w / 2, 0.0f}, {w / 2, h}, {-w / 2, h}};
            }
            p.name = str(args, "nom", "Pièce " + std::to_string(g.panels.size() + 1));
            p.color = {0.82f, 0.80f, 0.74f};
            applyPlacementArgs(p, args);
            placeBesideOthers(g, p.points);
            g.panels.push_back(std::move(p));
            const int i = static_cast<int>(g.panels.size()) - 1;
            save("Pièce " + std::to_string(i) + " « " + P(i).name + " » ajoutée. Points (cm) : " + pointText(i) +
                 ". Le bord k va du point k au point k+1.");
            return true;
        }
        if (name == "modifier_piece") {
            const int i = panelRef(g, args);
            Panel& p = P(i);
            if (args.has("nom")) p.name = str(args, "nom");
            applyPlacementArgs(p, args);
            if (args.has("droit_fil")) p.grainDeg = static_cast<float>(num(args, "droit_fil", 90.0));
            if (args.has("symetrique")) {
                p.symmetric = args.get("symetrique").asBool();
                if (p.symmetric) {
                    float lo = 1e9f, hi = -1e9f;
                    for (const auto& q : p.points) {
                        lo = std::min(lo, q.x);
                        hi = std::max(hi, q.x);
                    }
                    p.symAxisX = 0.5f * (lo + hi);
                }
            }
            if (args.has("active")) p.active3D = args.get("active").asBool(true);
            if (args.has("masquee")) p.hidden3D = args.get("masquee").asBool();
            if (args.has("figee")) p.frozen = args.get("figee").asBool();
            if (args.has("raideur")) p.stiffness = std::clamp(static_cast<float>(num(args, "raideur", 1.0)), 0.1f, 20.0f);
            if (args.has("espacement_mm")) {
                const float d = static_cast<float>(num(args, "espacement_mm", 0.0));
                p.particleDistance = d <= 0.0f ? 0.0f : std::clamp(d, 5.0f, 100.0f) / 1000.0f;
            }
            if (args.has("couche")) p.layer = std::clamp(static_cast<int>(std::lround(num(args, "couche", 0.0))), 0, 10);
            if (args.has("calque")) {
                const std::string cname = str(args, "calque", "");
                int k = -1;
                for (int j = 0; j < static_cast<int>(g.groups.size()) && !cname.empty(); ++j)
                    if (g.groups[static_cast<std::size_t>(j)].name == cname) k = j;
                if (!cname.empty() && k < 0) {
                    garment::PieceGroup gr;
                    gr.name = cname;
                    g.groups.push_back(gr);
                    k = static_cast<int>(g.groups.size()) - 1;
                }
                p.group = k;
            }
            if (args.has("retrait_trame")) p.shrinkWeft = std::clamp(static_cast<float>(num(args, "retrait_trame", 100.0)), 50.0f, 150.0f) / 100.0f;
            if (args.has("retrait_chaine")) p.shrinkWarp = std::clamp(static_cast<float>(num(args, "retrait_chaine", 100.0)), 50.0f, 150.0f) / 100.0f;
            if (args.has("epaisseur_contact_mm"))
                p.thickCollision = std::clamp(static_cast<float>(num(args, "epaisseur_contact_mm", 0.0)), 0.0f, 50.0f) / 1000.0f;
            if (args.has("epaisseur_affichage_mm"))
                p.thickRender = std::clamp(static_cast<float>(num(args, "epaisseur_affichage_mm", 0.0)), 0.0f, 50.0f) / 1000.0f;
            if (args.has("pression")) p.pressure = std::clamp(static_cast<float>(num(args, "pression", 0.0)), -500.0f, 500.0f);
            if (args.has("aspect")) p.finish = finishOf(str(args, "aspect"));
            save("Pièce " + std::to_string(i) + " « " + p.name + " » modifiée.");
            return true;
        }
        if (name == "deplacer_piece") {
            const int i = panelRef(g, args);
            const glm::vec2 d(static_cast<float>(num(args, "dx_cm", 0.0) / 100.0), static_cast<float>(num(args, "dy_cm", 0.0) / 100.0));
            for (auto& q : P(i).points) q += d;
            if (P(i).symmetric) P(i).symAxisX += d.x;
            for (auto& a : g.attachments)
                if (a.patch == i) a.at += d;
            if (const double r = num(args, "rotation", 0.0); r != 0.0) garment::rotatePanels(g, {i}, static_cast<float>(r));
            const std::string f = lower(str(args, "retourner"));
            if (f == "horizontal") garment::flipPanels(g, {i}, true);
            else if (f == "vertical") garment::flipPanels(g, {i}, false);
            else if (!f.empty()) fail("« retourner » : horizontal ou vertical.");
            save("Pièce " + std::to_string(i) + " déplacée. Points (cm) : " + pointText(i));
            return true;
        }
        if (name == "supprimer_piece") {
            const int i = panelRef(g, args);
            const std::string n = P(i).name;
            g.removePanel(i);
            save("Pièce « " + n + " » supprimée. Attention : les index des pièces suivantes ont diminué de 1.");
            return true;
        }
        if (name == "dupliquer_piece") {
            const int i = panelRef(g, args);
            garment::CloneOptions o;
            o.mirror = args.get("miroir").asBool();
            const auto ids = garment::clonePanels(g, {i}, o);
            if (ids.empty()) fail("Copie impossible.");
            save("Copie créée : pièce " + std::to_string(ids[0]) + " « " + P(ids[0]).name + " ». Points (cm) : " + pointText(ids[0]));
            return true;
        }
        if (name == "deplacer_point") {
            const int i = panelRef(g, args);
            const int k = pointRef(g, i, args);
            P(i).points[static_cast<std::size_t>(k)] = {static_cast<float>(numRequired(args, "x_cm") / 100.0),
                                                         static_cast<float>(numRequired(args, "y_cm") / 100.0)};
            save("Point " + std::to_string(k) + " déplacé. Points (cm) : " + pointText(i));
            return true;
        }
        if (name == "ajouter_point") {
            const int i = panelRef(g, args);
            const int e = edgeRef(g, i, args);
            const float t = std::clamp(static_cast<float>(num(args, "position", 0.5)), 0.01f, 0.99f);
            const garment::EdgeRef er{i, e};
            g.insertPoint(i, e, glm::mix(g.edgeStart(er), g.edgeEnd(er), t));
            save("Point ajouté (index " + std::to_string(e + 1) + "). Points (cm) : " + pointText(i));
            return true;
        }
        if (name == "supprimer_point") {
            const int i = panelRef(g, args);
            const int k = pointRef(g, i, args);
            if (P(i).points.size() <= 3) fail("Une pièce garde au moins 3 points.");
            g.removePoint(i, k);
            save("Point supprimé. Points (cm) : " + pointText(i));
            return true;
        }
        if (name == "coudre") {
            const int a = panelRef(g, args, "piece_a");
            const int b = panelRef(g, args, "piece_b");
            const int ea = edgeRef(g, a, args, "bord_a");
            const int eb = edgeRef(g, b, args, "bord_b");
            const int na = static_cast<int>(std::lround(num(args, "nb_bords_a", 1.0)));
            const int nb = static_cast<int>(std::lround(num(args, "nb_bords_b", 1.0)));
            const int sa = static_cast<int>(P(a).points.size()), sb = static_cast<int>(P(b).points.size());
            if (na < 1 || na >= sa || nb < 1 || nb >= sb) fail("nb_bords doit être entre 1 et le nombre de bords de la pièce moins 1.");
            garment::Seam s;
            s.a = {a, ea};
            s.b = {b, eb};
            s.countA = na;
            s.countB = nb;
            s.flipped = args.get("inverser").asBool();
            // Les côtés ne doivent pas se chevaucher sur une même pièce.
            if (a == b) {
                for (int i = 0; i < na; ++i)
                    for (int j = 0; j < nb; ++j)
                        if ((ea + i) % sa == (eb + j) % sb) fail("Les deux côtés de la couture se chevauchent.");
            }
            for (int side = 0; side < 2; ++side)
                for (const auto& e : g.seamEdges(s, side)) g.removeSeamsOn(e);
            g.seams.push_back(s);
            char buf[160];
            std::snprintf(buf, sizeof(buf), "Couture %zu ajoutée : %.1f cm côté A, %.1f cm côté B.", g.seams.size() - 1, cm(g.seamLength(s, 0)),
                          cm(g.seamLength(s, 1)));
            std::string msg = buf;
            if (std::abs(g.seamLength(s, 0) - g.seamLength(s, 1)) > 0.01f) msg += " Les longueurs diffèrent : le côté le plus long sera froncé.";
            save(msg);
            return true;
        }
        if (name == "supprimer_couture") {
            const int s = intRequired(args, "couture");
            if (s < 0 || s >= static_cast<int>(g.seams.size())) fail("Couture " + std::to_string(s) + " introuvable.");
            g.seams.erase(g.seams.begin() + s);
            save("Couture supprimée. Les index des coutures suivantes ont diminué de 1.");
            return true;
        }
        if (name == "inverser_couture") {
            const int s = intRequired(args, "couture");
            if (s < 0 || s >= static_cast<int>(g.seams.size())) fail("Couture " + std::to_string(s) + " introuvable.");
            auto& sm = g.seams[static_cast<std::size_t>(s)];
            if (sm.order != 0) sm.order = sm.order == 1 ? 2 : 1;
            else sm.flipped = !sm.flipped;
            save("Couture " + std::to_string(s) + " inversée.");
            return true;
        }
        if (name == "arrondir_coin") {
            const int i = panelRef(g, args);
            const int k = pointRef(g, i, args);
            const float maxT = garment::maxCornerTangent(g, i, k);
            float t = args.has("distance_cm") ? static_cast<float>(num(args, "distance_cm", 0.0) / 100.0) : 0.5f * maxT;
            t = std::min(t, maxT);
            if (t <= 0.001f) fail("Ce point ne peut pas être arrondi (bords trop courts ou alignés).");
            if (garment::roundCorner(g, i, k, t) < 0) fail("Arrondi impossible à ce point.");
            save("Coin arrondi. Points (cm) : " + pointText(i));
            return true;
        }
        if (name == "couper_bord") {
            const int i = panelRef(g, args);
            const int e = edgeRef(g, i, args);
            const int parts = std::clamp(static_cast<int>(std::lround(num(args, "morceaux", 2.0))), 2, 50);
            if (!garment::splitCurve(g, i, e, parts)) fail("Aucun point ajouté (bord trop court ?).");
            save("Bord coupé en " + std::to_string(parts) + " morceaux. Points (cm) : " + pointText(i));
            return true;
        }
        if (name == "ajouter_pince") {
            const int i = panelRef(g, args);
            const int k = pointRef(g, i, args);
            const float depth = static_cast<float>(num(args, "profondeur_cm", 8.0) / 100.0);
            const float width = static_cast<float>(num(args, "largeur_cm", 3.0) / 100.0);
            const int tip = garment::addDart(g, i, k, depth, width);
            if (tip < 0) fail("Pince impossible à ce point (bords trop courts ?).");
            save("Pince ajoutée, pointe = point " + std::to_string(tip) + ". Points (cm) : " + pointText(i));
            return true;
        }
        if (name == "deplier") {
            const int i = panelRef(g, args);
            const int e = edgeRef(g, i, args);
            if (!garment::unfoldPanel(g, i, e)) fail("Dépliage impossible autour de ce bord.");
            save("Pièce dépliée. Points (cm) : " + pointText(i));
            return true;
        }
        if (name == "poser_piece") {
            const int i = panelRef(g, args);
            Json baseArgs = Json::object();
            baseArgs["piece"] = args.get("sur");
            const int b = panelRef(g, baseArgs);
            if (i == b) fail("Une pièce ne peut pas être posée sur elle-même.");
            const std::string t = lower(str(args, "type", "poche"));
            const garment::AttachKind kind = t == "rabat" ? garment::AttachKind::Flap
                                             : (t == "tout_autour" || t == "tout autour") ? garment::AttachKind::AllRound
                                                                                           : garment::AttachKind::Pocket;
            const glm::vec2 at(static_cast<float>(numRequired(args, "x_cm") / 100.0), static_cast<float>(numRequired(args, "y_cm") / 100.0));
            {   // le centre doit tomber sur la pièce de base (test pair-impair)
                const auto& poly = P(b).points;
                bool inside = false;
                for (std::size_t k = 0, j = poly.size() - 1; k < poly.size(); j = k++)
                    if ((poly[k].y > at.y) != (poly[j].y > at.y) &&
                        at.x < (poly[j].x - poly[k].x) * (at.y - poly[k].y) / (poly[j].y - poly[k].y) + poly[k].x)
                        inside = !inside;
                if (!inside) {
                    const glm::vec2 c = garment::panelBoxCenter(P(b));
                    char buf[160];
                    std::snprintf(buf, sizeof(buf), "Le point (%.1f, %.1f) n'est pas sur la pièce « %s » (son centre : %.1f, %.1f cm).", cm(at.x),
                                  cm(at.y), P(b).name.c_str(), cm(c.x), cm(c.y));
                    fail(buf);
                }
            }
            if (g.attachPatch(i, b, at, kind) < 0)
                fail("Pose impossible (la base est elle-même posée, ou la pièce est déjà posée ailleurs).");
            save("Pièce « " + P(i).name + " » posée sur « " + P(b).name + " ».");
            return true;
        }
        if (name == "ajouter_impression") {
            const int i = panelRef(g, args);
            garment::Print pr;
            pr.image = str(args, "image");
            if (pr.image.empty()) fail("Paramètre image manquant.");
            const std::filesystem::path img = fsPath(pr.image);
            const std::filesystem::path dir = fsPath(projectPath_).parent_path();
            std::error_code ec;
            const bool found = std::filesystem::exists(img.is_absolute() ? img : dir / img, ec);
            if (img.is_absolute()) {   // chemin relatif au projet si l'image est dans son dossier
                const auto rel = std::filesystem::relative(img, dir, ec);
                if (!ec && !rel.empty() && rel.begin()->string() != "..") pr.image = utf8Of(rel);
            }
            const glm::vec2 c = garment::panelBoxCenter(P(i));
            const glm::vec2 at(args.has("x_cm") ? static_cast<float>(num(args, "x_cm", 0.0) / 100.0) : c.x,
                               args.has("y_cm") ? static_cast<float>(num(args, "y_cm", 0.0) / 100.0) : c.y);
            pr.offset = at - c;
            pr.width = std::clamp(static_cast<float>(num(args, "largeur_cm", 10.0) / 100.0), 0.002f, 3.0f);
            pr.height = std::clamp(static_cast<float>(num(args, "hauteur_cm", 0.0) / 100.0), 0.0f, 3.0f);
            pr.angleDeg = static_cast<float>(num(args, "angle", 0.0));
            P(i).prints.push_back(pr);
            save("Impression " + std::to_string(P(i).prints.size() - 1) + " ajoutée sur « " + P(i).name + " »." +
                 (found ? "" : " Attention : image introuvable pour l'instant (" + pr.image + ")."));
            return true;
        }
        if (name == "supprimer_impression") {
            const int i = panelRef(g, args);
            const int k = intRequired(args, "index");
            if (k < 0 || k >= static_cast<int>(P(i).prints.size())) fail("Impression " + std::to_string(k) + " introuvable.");
            P(i).prints.erase(P(i).prints.begin() + k);
            save("Impression retirée.");
            return true;
        }
        if (name == "allonger_piece") {
            const int i = panelRef(g, args);
            if (!garment::lengthenPanel(g, i, static_cast<float>(numRequired(args, "a_hauteur_cm") / 100.0),
                                        static_cast<float>(numRequired(args, "longueur_cm") / 100.0)))
                fail("Allongement impossible (ligne hors de la pièce ?).");
            save("Pièce « " + P(i).name + " » modifiée. Points (cm) : " + pointText(i));
            return true;
        }
        if (name == "couper_piece") {
            const int i = panelRef(g, args);
            glm::vec2 lo(1e9f), hi(-1e9f);
            for (const auto& q : P(i).points) {
                lo = glm::min(lo, q);
                hi = glm::max(hi, q);
            }
            glm::vec2 a, b;
            if (args.has("hauteur_cm")) {
                const float y = static_cast<float>(num(args, "hauteur_cm", 0.0) / 100.0);
                a = {lo.x - 0.01f, y};
                b = {hi.x + 0.01f, y};
            } else if (args.has("x_cm")) {
                const float x = static_cast<float>(num(args, "x_cm", 0.0) / 100.0);
                a = {x, lo.y - 0.01f};
                b = {x, hi.y + 0.01f};
            } else {
                a = {static_cast<float>(numRequired(args, "x1_cm") / 100.0), static_cast<float>(numRequired(args, "y1_cm") / 100.0)};
                b = {static_cast<float>(numRequired(args, "x2_cm") / 100.0), static_cast<float>(numRequired(args, "y2_cm") / 100.0)};
            }
            std::string err;
            const int np = garment::splitPanelAlongLine(g, i, a, b, args.get("recoudre").asBool(), err);
            if (np < 0) fail(err);
            save("Pièce coupée : « " + P(i).name + " » (pièce " + std::to_string(i) + ", points (cm) : " + pointText(i) + ") et « " +
                 P(np).name + " » (pièce " + std::to_string(np) + ", points (cm) : " + pointText(np) + ").");
            return true;
        }
        if (name == "ajouter_soufflet") {
            const int i = panelRef(g, args);
            auto portion = [&](int panel, const char* edgeKey, const char* countKey, bool reverse, float& s0, float& len) {
                const int e = edgeRef(g, panel, args, edgeKey);
                const int n = static_cast<int>(P(panel).points.size());
                const int c = static_cast<int>(std::lround(num(args, countKey, 1.0)));
                if (c < 1 || c > n) fail(std::string("« ") + countKey + " » doit être entre 1 et le nombre de bords de la pièce.");
                s0 = garment::contourParam(g, panel, e, 0.0f);
                len = 0.0f;
                for (int k = 0; k < c; ++k) len += g.edgeLength({panel, (e + k) % n});
                if (reverse) {
                    s0 += len;
                    len = -len;
                }
            };
            float s0 = 0, len = 0;
            portion(i, "bord", "nb_bords", false, s0, len);
            int b = -1;
            float sb = 0, lb = 0;
            if (args.has("piece_b")) {
                b = panelRef(g, args, "piece_b");
                if (b == i) fail("L'autre côté de la bande doit aller sur une autre pièce.");
                portion(b, "bord_b", "nb_bords_b", args.get("inverser_b").asBool(), sb, lb);
            }
            std::string err;
            const int strip = garment::addGussetStrip(g, i, s0, len, static_cast<float>(num(args, "largeur_cm", 3.0) / 100.0), err);
            if (strip < 0) fail(err);
            std::string more;
            if (b >= 0) {
                if (garment::sewGussetTop(g, strip, b, sb, lb) < 0) fail("L'autre côté de la bande n'a pas pu être cousu.");
                more = " Autre côté cousu sur « " + P(b).name + " ».";
            }
            char lenText[32];
            std::snprintf(lenText, sizeof(lenText), "%.1f cm", static_cast<double>(std::abs(len) * 100.0f));
            save("Bande de soufflet « " + P(strip).name + " » (pièce " + std::to_string(strip) + ", " + lenText + ") cousue." + more);
            return true;
        }
        if (name == "ajouter_fermeture") {
            const int i = panelRef(g, args);
            const glm::vec2 a(static_cast<float>(numRequired(args, "x1_cm") / 100.0), static_cast<float>(numRequired(args, "y1_cm") / 100.0));
            const glm::vec2 b(static_cast<float>(numRequired(args, "x2_cm") / 100.0), static_cast<float>(numRequired(args, "y2_cm") / 100.0));
            std::string err;
            const int zi = garment::addZipper(g, i, a, b, static_cast<float>(num(args, "largeur_cm", 2.5) / 100.0), err);
            if (zi < 0) fail(err);
            const int puller = static_cast<int>(g.panels.size()) - 1;
            save("Fermeture éclair posée sur « " + P(i).name + " » : bande = pièce " + std::to_string(zi) +
                 (puller != zi ? ", curseur = pièce " + std::to_string(puller) : std::string()) + ".");
            return true;
        }
        if (name == "ajouter_poche") {
            const int i = panelRef(g, args);
            const glm::vec2 c(static_cast<float>(numRequired(args, "x_cm") / 100.0), static_cast<float>(numRequired(args, "y_cm") / 100.0));
            const glm::vec2 h(static_cast<float>(numRequired(args, "largeur_cm") / 200.0), static_cast<float>(numRequired(args, "hauteur_cm") / 200.0));
            const std::string type = str(args, "type", "poche");
            garment::AttachKind kind = garment::AttachKind::Pocket;
            if (type == "rabat") kind = garment::AttachKind::Flap;
            else if (type == "tout_le_tour") kind = garment::AttachKind::AllRound;
            else if (type != "poche") fail("type : « poche », « rabat » ou « tout_le_tour ».");
            std::string err;
            const int pi = garment::addPocket(g, i, c - glm::abs(h), c + glm::abs(h), kind, args.get("avec_rabat").asBool(false), err);
            if (pi < 0) fail(err);
            const int last = static_cast<int>(g.panels.size()) - 1;
            save("Poche cousue sur « " + P(i).name + " » : pièce " + std::to_string(pi) + (last != pi ? ", rabat = pièce " + std::to_string(last) : std::string()) + ".");
            return true;
        }
        if (name == "ajouter_pli") {
            const int i = panelRef(g, args);
            const glm::vec2 a(static_cast<float>(numRequired(args, "x1_cm") / 100.0), static_cast<float>(numRequired(args, "y1_cm") / 100.0));
            const glm::vec2 b(static_cast<float>(numRequired(args, "x2_cm") / 100.0), static_cast<float>(numRequired(args, "y2_cm") / 100.0));
            if (glm::distance(a, b) < 0.005f) fail("Ligne de pli trop courte.");
            glm::vec2 p0, p1;
            if (!garment::lineCut(P(i), a, b, p0, p1)) fail("La ligne ne passe pas sur la pièce.");
            garment::FoldLine f;
            f.a = a - garment::panelBoxCenter(P(i));
            f.b = b - garment::panelBoxCenter(P(i));
            f.angleDeg = std::clamp(static_cast<float>(num(args, "angle", 90.0)), -180.0f, 180.0f);
            f.strength = std::clamp(static_cast<float>(num(args, "force", 80.0) / 100.0), 0.0f, 1.0f);
            P(i).folds.push_back(f);
            save("Ligne de pli " + std::to_string(P(i).folds.size() - 1) + " ajoutée sur « " + P(i).name + " ».");
            return true;
        }
        if (name == "supprimer_pli") {
            const int i = panelRef(g, args);
            const int k = intRequired(args, "index");
            if (k < 0 || k >= static_cast<int>(P(i).folds.size())) fail("Ligne de pli " + std::to_string(k) + " introuvable.");
            P(i).folds.erase(P(i).folds.begin() + k);
            save("Ligne de pli retirée.");
            return true;
        }
        if (name == "agrandir_piece") {
            const int i = panelRef(g, args);
            if (!garment::offsetPanel(g, i, static_cast<float>(numRequired(args, "distance_cm") / 100.0))) fail("Décalage impossible.");
            save("Contour décalé. Points (cm) : " + pointText(i));
            return true;
        }
        if (name == "simuler") {
            text = simulate(g, av, projectPath_, num(args, "secondes", 4.0), str(args, "matiere"), str(args, "exporter_obj"));
            return true;
        }
        if (name == "capture_vue") {
            text = captureView(g, av, projectPath_, args, image_);
            return true;
        }
        if (name == "ouvrir_dans_drapeo") {
            if (!fileExists(projectPath_)) save("");
            std::string err;
            if (!launchDrapeo(drapeoExe_, projectPath_, err)) fail(err);
            text = "Drapeo s'ouvre avec " + projectPath_ + ". Les modifications suivantes s'y afficheront en direct.";
            return true;
        }
        fail("Outil inconnu : " + name);
    } catch (const ToolError& e) {
        error = e.message;
        return false;
    } catch (const std::exception& e) {
        error = std::string("Erreur interne : ") + e.what();
        return false;
    }
}

namespace {
Json rpcError(const Json& id, int code, const std::string& message)
{
    Json r = Json::object();
    r["jsonrpc"] = "2.0";
    r["id"] = id;
    Json e = Json::object();
    e["code"] = code;
    e["message"] = message;
    r["error"] = std::move(e);
    return r;
}
} // namespace

bool Server::handle(const Json& msg, Json& response)
{
    if (!msg.isObject()) {
        response = rpcError(Json(), -32600, "Requête invalide");
        return true;
    }
    const Json& id = msg.get("id");
    const bool isRequest = msg.has("id");
    const std::string method = msg.get("method").asString();
    if (!isRequest) return false;   // notification (notifications/initialized...) : pas de réponse

    Json result = Json::object();
    if (method == "initialize") {
        const Json& requested = msg.get("params").get("protocolVersion");
        result["protocolVersion"] = requested.isString() ? requested.asString() : std::string(kProtocolVersion);
        Json caps = Json::object();
        caps["tools"] = Json::object();
        result["capabilities"] = std::move(caps);
        Json info = Json::object();
        info["name"] = "drapeo";
        info["version"] = "0.1.0";
        result["serverInfo"] = std::move(info);
        result["instructions"] =
            "Drapeo crée des vêtements 3D à partir de patrons 2D. Commencez par projet_info. Les pièces sont des contours en cm "
            "(y vers le haut), placées autour d'un avatar de 175 cm (zone, angle : 0 devant / 180 dos, hauteur du bas de la pièce). "
            "Cousez les bords avec coudre, puis vérifiez avec simuler. ouvrir_dans_drapeo montre le résultat en direct.";
    } else if (method == "ping") {
        // résultat vide
    } else if (method == "tools/list") {
        result["tools"] = toolList();
    } else if (method == "tools/call") {
        const Json& params = msg.get("params");
        std::string text, error;
        const bool ok = callTool(params.get("name").asString(), params.get("arguments"), text, error);
        Json content = Json::array();
        Json item = Json::object();
        item["type"] = "text";
        item["text"] = ok ? text : error;
        content.push(std::move(item));
        if (ok && !image_.empty()) {
            Json img = Json::object();
            img["type"] = "image";
            img["data"] = io::base64(image_);
            img["mimeType"] = "image/png";
            content.push(std::move(img));
        }
        result["content"] = std::move(content);
        if (!ok) result["isError"] = true;
    } else if (method == "resources/list") {
        result["resources"] = Json::array();
    } else if (method == "prompts/list") {
        result["prompts"] = Json::array();
    } else {
        response = rpcError(id, -32601, "Méthode inconnue : " + method);
        return true;
    }
    response = Json::object();
    response["jsonrpc"] = "2.0";
    response["id"] = id;
    response["result"] = std::move(result);
    return true;
}

std::string Server::handleLine(const std::string& line)
{
    std::string t = line;
    while (!t.empty() && (t.back() == '\r' || t.back() == '\n' || t.back() == ' ')) t.pop_back();
    if (t.empty()) return {};
    Json msg;
    std::string error;
    if (!Json::parse(t, msg, error)) return rpcError(Json(), -32700, "JSON invalide : " + error).dump();
    if (msg.isArray()) {   // lot de messages
        Json out = Json::array();
        for (const Json& m : msg.items()) {
            Json r;
            if (handle(m, r)) out.push(std::move(r));
        }
        return out.size() > 0 ? out.dump() : std::string();
    }
    Json response;
    return handle(msg, response) ? response.dump() : std::string();
}

} // namespace drapeo::mcp
