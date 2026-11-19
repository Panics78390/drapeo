// =============================================================================
//  garment/Pattern — modèle de données d'un vêtement
//
//  Un vêtement = des pièces de patron 2D (polygones, en mètres, vues de
//  l'extérieur du vêtement) + des coutures qui relient deux bords + un
//  placement par pièce autour du corps (« point d'arrangement » : la pièce est
//  enroulée sur un cylindre vertical centré sur l'avatar).
// =============================================================================
#pragma once

#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace drapeo::garment {

/// Partie du corps autour de laquelle la pièce est enroulée.
enum class BodyZone { Torso = 0, LeftArm, RightArm, LeftLeg, RightLeg };
inline constexpr int kBodyZoneCount = 5;
const char* bodyZoneName(BodyZone z);

struct Placement {
    float angleDeg = 0.0f;   // 0 = devant, 180 = dos, 90 = côté gauche de l'avatar (+X)
    float height = 1.0f;     // hauteur du bas de la pièce (m), pour un avatar de 1,75 m
    float radius = 0.24f;    // rayon du cylindre d'enroulement (m)
    BodyZone zone = BodyZone::Torso;   // buste, ou cylindre incliné le long d'un bras / d'une jambe
    // Réglage libre en 3D (gizmo) appliqué après l'enroulement : rotation autour
    // du centre de la pièce, puis translation (m).
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 offset{0.0f};
};

struct Panel {
    std::string name;
    std::vector<glm::vec2> points;   // contour fermé, sens quelconque
    glm::vec3 color{0.85f, 0.83f, 0.78f};
    Placement placement;
    float grainDeg = 90.0f;   // droit fil dans le patron (degrés, 90 = vertical)
    bool symmetric = false;   // édition symétrique autour d'un axe vertical
    float symAxisX = 0.0f;    // abscisse de l'axe de symétrie (m)
};

/// Un bord = le segment entre points[edge] et points[(edge + 1) % n] de la pièce.
struct EdgeRef {
    int panel = -1;
    int edge = -1;
    bool operator==(const EdgeRef& o) const { return panel == o.panel && edge == o.edge; }
};

/// Une couture relie deux suites de bords consécutifs (une courbe d'emmanchure,
/// d'entrejambe... est faite de plusieurs bords) : `a.edge`, `a.edge + 1`, ...
/// `countA` bords côté A, `countB` côté B.
struct Seam {
    EdgeRef a, b;
    bool flipped = false;   // inverse le sens d'appariement choisi automatiquement
    int countA = 1, countB = 1;
    bool enabled = true;    // une couture désactivée reste dessinée mais n'est pas cousue
};

/// Pièce posée sur une autre (poche plaquée, rabat, renfort) : le contour de
/// `patch` est cousu à l'intérieur de `base`, centré au point `at` (coordonnées
/// 2D de base). On coud `count` bords à partir de `startEdge` (0 = tous les bords).
struct Attachment {
    int patch = -1, base = -1;
    glm::vec2 at{0.0f};
    int startEdge = 0, count = 0;
};

/// Façon de coudre une pièce posée.
enum class AttachKind { Pocket, Flap, AllRound };   // poche ouverte en haut, rabat cousu en haut, tout le tour

struct Garment {
    std::vector<Panel> panels;
    std::vector<Seam> seams;
    std::vector<Attachment> attachments;

    [[nodiscard]] bool empty() const { return panels.empty(); }
    [[nodiscard]] glm::vec2 edgeStart(const EdgeRef& e) const;
    [[nodiscard]] glm::vec2 edgeEnd(const EdgeRef& e) const;
    [[nodiscard]] float edgeLength(const EdgeRef& e) const;
    /// Bords d'un côté de couture (côté 0 = A, 1 = B), dans l'ordre.
    [[nodiscard]] std::vector<EdgeRef> seamEdges(const Seam& s, int side) const;
    [[nodiscard]] float seamLength(const Seam& s, int side) const;
    /// Ajoute (delta = +1) ou retire (-1) un bord au bout d'un côté de couture
    /// (au début du côté si atStart).
    bool extendSeam(int seam, int side, int delta, bool atStart = false);

    // Édition qui garde les coutures cohérentes.
    void removePanel(int panel);
    void removePoint(int panel, int point);
    void insertPoint(int panel, int edge, glm::vec2 position);   // coupe le bord `edge` en deux
    void removeSeamsOn(const EdgeRef& e);
    int addMirrorCopy(int panel, float gap);                       // copie miroir à droite, renvoie son index
    /// Pièce posée sur `panel`, ou -1.
    [[nodiscard]] int attachmentOf(int panel) const;
    /// Bords cousus d'une pièce posée, dans l'ordre.
    [[nodiscard]] std::vector<int> attachedEdges(const Attachment& a) const;
    /// Coud `patch` sur `base` au point `at` (coordonnées de base). Remplace une pose existante ;
    /// renvoie l'index de la pose, ou -1 si les pièces ne conviennent pas.
    int attachPatch(int patch, int base, glm::vec2 at, AttachKind kind);
    /// Change les bords cousus d'une pose selon `kind`.
    void setAttachKind(Attachment& a, AttachKind kind) const;
    [[nodiscard]] AttachKind attachKind(const Attachment& a) const;
};

/// Cintrage : où la pièce est le plus resserrée (ou élargie) et autour de quel bord.
enum class TaperMode { Uniform, Bottom, Middle, Top };
enum class TaperAnchor { Center, Left, Right };
/// Resserre (amount < 0) ou élargit (amount > 0) une pièce en largeur, progressivement selon
/// `mode` (Bottom : surtout en bas, Middle : surtout à mi-hauteur...). Les poches posées dessus suivent.
void taperPanel(Garment& g, int panel, float amount, TaperMode mode, TaperAnchor anchor);

/// Réunion (subtract = false) ou différence de deux contours simples. Renvoie les contours
/// obtenus (vide si rien ne reste) ; `touching` = false si les contours ne se touchent pas.
std::vector<std::vector<glm::vec2>> polygonBoolean(const std::vector<glm::vec2>& a, const std::vector<glm::vec2>& b,
                                                   bool subtract, bool* touching = nullptr);

/// Ajoute (union) ou découpe (différence) la forme de la pièce `shape` dans la pièce `target`,
/// puis supprime `shape`. Les coutures de `target` dont les bords sont intacts sont gardées.
/// Renvoie false (et `error`) si le résultat n'est pas une seule pièce sans trou.
bool combinePanels(Garment& g, int target, int shape, bool subtract, std::string& error);

/// Index du point symétrique de `point` dans une pièce symétrique (-1 s'il n'y en a pas, ou s'il est sur l'axe).
int symmetricPartner(const Panel& panel, int point);

/// Segment du droit fil d'une pièce (à travers son centre, aux deux tiers de sa longueur).
void grainLine(const Panel& panel, glm::vec2& a, glm::vec2& b);

/// Bord du haut d'un contour (le plus haut parmi les bords plutôt horizontaux).
int topEdge(const std::vector<glm::vec2>& poly);

// --- Géométrie 2D utilitaire -------------------------------------------------
float signedArea(const std::vector<glm::vec2>& poly);
bool pointInPolygon(const std::vector<glm::vec2>& poly, glm::vec2 p);
float distanceToSegment(glm::vec2 p, glm::vec2 a, glm::vec2 b, float* t = nullptr);
glm::vec2 centroid(const std::vector<glm::vec2>& poly);

// --- Modèles prêts à l'emploi (avatar de 1,75 m) ------------------------------
Garment makeTankTop();
Garment makeSkirt();
Garment makeDress();

} // namespace drapeo::garment
