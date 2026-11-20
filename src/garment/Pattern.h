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
    bool flat = false;       // pièce posée à plat (plan tangent au cylindre) au lieu d'être enroulée
    // Réglage libre en 3D (gizmo) appliqué après l'enroulement : rotation autour
    // du centre de la pièce, puis translation (m).
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 offset{0.0f};
};

/// Impression posée sur une pièce (logo, inscription, drapeau, bande réfléchissante...) :
/// une image PNG / JPEG plaquée sur le tissu, visible en 3D et exportée avec le vêtement.
struct Print {
    std::string image;            // chemin du fichier image (UTF-8), relatif au projet ou absolu
    glm::vec2 offset{0.0f};       // centre de l'image par rapport au centre du cadre de la pièce (m)
    float width = 0.10f;          // largeur (m)
    float height = 0.0f;          // hauteur (m) ; 0 = proportions de l'image
    float angleDeg = 0.0f;        // rotation dans le patron (degrés)
};

/// Ligne de pli à l'intérieur d'une pièce (pli de passepoil, de bande de fermeture, bord de poche
/// replié) : le tissu se plie le long de la ligne d'un angle donné.
struct FoldLine {
    glm::vec2 a{0.0f}, b{0.0f};   // extrémités, par rapport au centre du cadre de la pièce (m), comme les impressions
    float angleDeg = 90.0f;       // 0 = à plat ; > 0 : les deux côtés se replient vers l'endroit, < 0 : vers l'envers
    float strength = 0.8f;        // force du pli, de 0 (souple) à 1 (marqué)
};

/// Aspect du tissu (rendu 3D et texture exportée).
enum class FabricFinish { Plain = 0, Ripstop, Twill, Reflective, Zipper };   // Zipper : bande de fermeture éclair (dents dessinées)
inline constexpr int kFabricFinishCount = 5;
const char* fabricFinishName(FabricFinish f);

struct Panel {
    std::string name;
    std::vector<glm::vec2> points;   // contour fermé, sens quelconque
    glm::vec3 color{0.85f, 0.83f, 0.78f};
    Placement placement;
    float grainDeg = 90.0f;   // droit fil dans le patron (degrés, 90 = vertical)
    bool symmetric = false;   // édition symétrique autour d'un axe vertical
    float symAxisX = 0.0f;    // abscisse de l'axe de symétrie (m)
    // État de la pièce dans la simulation 3D (menu clic droit de la vue patron)
    bool active3D = true;     // false : la pièce n'est pas simulée (absente de la vue 3D)
    bool hidden3D = false;    // simulée mais pas affichée
    bool frozen = false;      // figée : ses particules ne bougent plus
    float stiffness = 1.0f;   // > 1 : pièce renforcée (moins extensible, plus raide)
    // Clones liés : les pièces d'un même groupe (> 0) reçoivent les modifications de forme
    // des autres. Repère de la pièce par rapport à la forme commune : rotation puis miroir.
    int linkGroup = 0;
    bool linkMirror = false;
    float linkAngle = 0.0f;   // degrés
    // Propriétés de simulation de la pièce (onglet Simulation)
    float particleDistance = 0.0f;   // espacement des particules (m) ; 0 = celui du vêtement
    int layer = 0;                   // couche : > 0 posée par-dessus, prioritaire dans les contacts
    float shrinkWeft = 1.0f;         // rétrécissement trame (perpendiculaire au droit fil), 1 = 100 %
    float shrinkWarp = 1.0f;         // rétrécissement chaîne (le long du droit fil)
    float thickCollision = 0.0f;     // épaisseur ajoutée pour les contacts (m)
    float thickRender = 0.0f;        // épaisseur ajoutée à l'affichage (m)
    float pressure = 0.0f;           // pression (Pa) qui pousse la pièce vers son endroit (volume gonflé)
    FabricFinish finish = FabricFinish::Plain;   // aspect du tissu
    std::vector<Print> prints;       // impressions posées sur la pièce
    std::vector<FoldLine> folds;     // lignes de pli
};

/// Centre du cadre (boîte englobante) d'une pièce : repère des impressions.
glm::vec2 panelBoxCenter(const Panel& panel);
/// Position du centre d'une impression dans les coordonnées du patron (m).
inline glm::vec2 printCenter(const Panel& panel, const Print& print) { return panelBoxCenter(panel) + print.offset; }

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
    int order = 0;          // 0 : sens choisi automatiquement ; 1 : début de A contre début de B ;
                            // 2 : début de A contre fin de B (couture libre : sens donné à la souris)
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

/// Remplace le contour d'une pièce en gardant les coutures dont les bords n'ont pas bougé.
/// `hint` (facultatif) : nouvel index de chaque ancien point qui a bougé (-1 = inconnu).
void replaceOutline(Garment& g, int panel, std::vector<glm::vec2> points, const std::vector<int>* hint = nullptr);

/// Contour symétrique reconstruit à partir de la moitié gauche (keepLeft) ou droite de la pièce,
/// par rapport à son axe. Vide si la pièce ne traverse pas l'axe.
std::vector<glm::vec2> symmetrizedOutline(const Panel& panel, bool keepLeft);
/// Applique symmetrizedOutline à une pièce symétrique. Renvoie false si rien n'a été fait.
bool symmetrizePanel(Garment& g, int panel, bool keepLeft);

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
