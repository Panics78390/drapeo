// =============================================================================
//  ui/PatternEditor — éditeur de patrons 2D (vue de gauche)
//
//  Outils : Sélection (déplacer pièces et points, double-clic sur un bord pour
//  ajouter un point), Polygone, Rectangle, Couture (cliquer deux bords).
//  Navigation : molette = zoom, clic milieu ou clic droit glissé = déplacer.
//  Clic droit sans glisser : menu des pièces (ouvert par l'application).
//  Maj = aimanter à la grille de 1 cm. Suppr = supprimer la sélection.
// =============================================================================
#pragma once

#include <string>
#include <vector>

#include <imgui.h>
#include <glm/glm.hpp>

#include "garment/Pattern.h"

namespace drapeo::ui {

enum class Tool { Select, Transform, CurvePoint, Curvature, Smooth, AddPoint, Slash, Polygon, Rectangle, Seam };

/// Outils d'édition du patron (menu de la barre d'outils) : nom, raccourci, aide.
struct ToolInfo {
    Tool tool;
    const char* name;
    const char* key;
    const char* help;
};
inline constexpr ToolInfo kEditTools[] = {
    {Tool::Select, "Éditer le patron", "Z", "Choisir et glisser pièces, points et arêtes"},
    {Tool::Transform, "Transformer points / arêtes", "T", "Cadre autour de la sélection : coins = agrandir, rond du haut = pivoter"},
    {Tool::CurvePoint, "Éditer les points de courbe", "V", "Clic sur un bord : nouveau point à glisser ; clic sur un point : le glisser"},
    {Tool::Curvature, "Éditer la courbure", "C", "Glisser un bord pour le courber"},
    {Tool::Smooth, "Lisser la courbe", "L", "Tirer un coin pour l'arrondir ; pinceau ailleurs pour adoucir le contour"},
    {Tool::AddPoint, "Ajouter un point / couper un bord", "A", "Clic sur un bord ou une courbe : la couper au point cliqué ou en parties égales"},
    {Tool::Slash, "Fendre et écarter", "E", "Clic sur le bord de départ, puis sur le point charnière : la pièce s'ouvre en pointe"},
};
[[nodiscard]] inline bool isEditTool(Tool t) { return t != Tool::Polygon && t != Tool::Rectangle && t != Tool::Seam; }

/// Image de patron (photo, scan, page de PDF) affichée sous les pièces, à l'échelle.
struct Backdrop {
    ImTextureID texture = 0;
    glm::vec2 min{0.0f}, max{0.0f};   // coins bas-gauche et haut-droit (m)
    float opacity = 0.65f;
    bool visible = true;
    bool selected = false;          // cliquée dans la vue 2D (glisser pour déplacer, Suppr pour retirer)
    bool moved = false;             // déplacée à la souris depuis la dernière lecture
    bool removeRequested = false;   // Suppr pressée alors qu'elle était sélectionnée
};

class PatternEditor {
public:
    Tool tool = Tool::Select;
    int selectedPanel = -1;
    int selectedPoint = -1;
    int selectedSeam = -1;
    std::vector<int> selectedPoints;   // points (de selectedPanel) choisis ensemble : arête, Maj+clic, Ctrl+rectangle
    int selectedEdge = -1;             // arête choisie (ses deux points sont dans selectedPoints)
    std::vector<int> selection;   // sélection multiple (rectangle, Maj+clic) ; contient selectedPanel
    Backdrop backdrop;
    /// Silhouette de l'avatar vu de face, sous le patron (coordonnées 2D = mètres du monde, pieds à y = 0).
    struct Silhouette {
        ImTextureID texture = 0;
        glm::vec2 min{0.0f}, max{0.0f};
        bool visible = true;
    } silhouette;
    float bodyScale = 1.0f;     // hauteur de l'avatar / 1,75 m
    float torsoCenterX = 0.0f;  // abscisse du centre du buste (avatar de 1,75 m)
    bool magnet = true;           // aimant : points et pièces s'accrochent aux points et bords voisins
    float spreadDeg = 15.0f;      // « Fendre et écarter » : angle d'ouverture
    bool traceRequested = false;  // touche I : l'application détecte les pièces sur l'image de fond

    [[nodiscard]] bool isSelected(int panel) const;

    /// Dessine l'éditeur dans la région disponible de la fenêtre ImGui courante.
    /// Renvoie true quand une modification du patron est terminée (il faut
    /// remailler et relancer la couture).
    bool draw(garment::Garment& g);

    /// Cadre toutes les pièces dans la vue.
    void frameAll(const garment::Garment& g);
    /// Cadre les pièces données (sélection).
    void frameParts(const garment::Garment& g, const std::vector<int>& ids);
    /// Pièces visées par une commande : la sélection multiple, sinon la pièce sélectionnée.
    [[nodiscard]] std::vector<int> targets() const;
    /// Remplace la sélection (la dernière pièce devient la pièce active).
    void select(const std::vector<int>& ids);
    [[nodiscard]] bool hovered() const { return hovered_; }

    // Clic droit sans glisser : l'application ouvre le menu des pièces.
    bool menuRequested = false;
    int menuPanel = -1;            // pièce sous la souris (-1 : clic dans le vide)
    glm::vec2 menuWorld{0.0f};     // point cliqué (m)
    int menuPoint = -1;            // point sous la souris (options du point), -1 sinon
    garment::EdgeRef menuEdge;     // bord sous la souris (déplier, couper), panel -1 sinon
    // Outil Ajouter un point / couper : clic sur un bord -> l'application ouvre la fenêtre « Couper ».
    bool splitRequested = false;
    garment::EdgeRef splitEdge;
    float splitT = 0.5f;                   // position du clic le long du bord (0..1)
    std::vector<glm::vec2> splitPreview;   // points proposés, dessinés en aperçu
    void clearSelection()
    {
        selectedPanel = selectedPoint = selectedSeam = selectedEdge = -1;
        selection.clear();
        selectedPoints.clear();
        pendingSeam_ = {};
        drawing_.clear();
    }

    [[nodiscard]] std::string hint() const;
    [[nodiscard]] static ImU32 seamColor(int index);

private:
    glm::vec2 toWorld(ImVec2 s) const;
    ImVec2 toScreen(glm::vec2 w) const;
    garment::EdgeRef pickEdge(const garment::Garment& g, ImVec2 mouse, float* t = nullptr) const;
    int pickPoint(const garment::Garment& g, int panel, ImVec2 mouse) const;
    int pickPanel(const garment::Garment& g, glm::vec2 w) const;
    glm::vec2 snap(glm::vec2 w) const;
    /// Petit décalage qui accroche un point des pièces `ids` (déplacées de `delta`) au point le plus proche d'une autre pièce.
    glm::vec2 magnetOffset(const garment::Garment& g, const std::vector<int>& ids, glm::vec2 delta) const;
    /// Guides d'alignement : décale les points `moving` pour que l'un d'eux s'aligne (même x ou même y)
    /// sur un point de `others`, et mémorise les guides à dessiner. Renvoie la correction.
    glm::vec2 alignTo(const std::vector<glm::vec2>& moving, const std::vector<glm::vec2>& others) const;
    /// Points de référence des pièces (sommets + centre), hors pièces `skip` et hors point en cours de glisser.
    std::vector<glm::vec2> alignTargets(const garment::Garment& g, const std::vector<int>& skip) const;
    struct Guide {
        glm::vec2 a, b;        // segment du guide (m)
        bool measure = false;  // affiche la longueur
    };
    mutable std::vector<Guide> guides_;
    const garment::Garment* g_ = nullptr;   // vêtement en cours d'édition (pour l'aimant)
    mutable bool snapped_ = false;          // l'aimant a accroché le dernier point
    mutable glm::vec2 snapPos_{0.0f};
    int snapExcludePanel_ = -1;             // pièce ignorée par l'aimant (celle dont on tire un point)
    int symPartner_ = -1;                   // point miroir du point tiré (pièce symétrique)
    bool symOnAxis_ = false;                // le point tiré est sur l'axe de symétrie
    glm::vec2 dragApplied_{0.0f};           // déplacement déjà appliqué aux pièces glissées
    /// Déplace des pièces dans le patron ; les poches cousues suivent leur base.
    void movePanels(garment::Garment& g, const std::vector<int>& ids, glm::vec2 d);
    /// Pièce symétrique : reconstruit l'autre moitié à partir du côté du point modifié.
    void applySymmetry(garment::Garment& g, int panel, int point);
    bool attachMoved_ = false;   // une pose a bougé pendant le glisser : il faudra recoudre

    ImVec2 canvasPos_{0, 0}, canvasSize_{1, 1};
    glm::vec2 center_{0.3f, 0.35f};   // point du monde au centre de la vue (m)
    float zoom_ = 520.0f;             // pixels par mètre
    bool needsFrame_ = true;

    // États d'interaction
    enum class Drag { None, Point, Points, Panel, Pan, Rect, Backdrop, Marquee, Scale, Rotate, Move, Curve, Smooth, Round } drag_ = Drag::None;
    glm::vec2 dragStart_{0.0f};
    glm::vec2 dragLast_{0.0f};
    std::vector<glm::vec2> drawing_;  // polygone en cours
    garment::EdgeRef pendingSeam_;    // premier bord choisi (outil couture)
    garment::EdgeRef hoverEdge_;
    bool shift_ = false;
    bool hovered_ = false;
    bool pointMarquee_ = false;   // Ctrl + rectangle : sélection de points
    // Outil Transformer : cadre de la sélection et positions au début du glisser
    struct TPoint {
        int panel, index;
        glm::vec2 p;
    };
    std::vector<TPoint> tOrig_;
    std::vector<std::pair<int, glm::vec2>> tAttach_;   // poses dont la base est transformée
    glm::vec2 tLo_{0.0f}, tHi_{0.0f}, tAnchor_{0.0f}, tGrab_{0.0f};
    bool transformBox(const garment::Garment& g, glm::vec2& lo, glm::vec2& hi) const;
    void beginTransform(const garment::Garment& g);
    // Outil Courbure : bord en cours de courbure
    garment::EdgeRef curveEdge_;
    glm::vec2 curveA_{0.0f}, curveB_{0.0f};
    int curveFirst_ = -1, curveCount_ = 0;
    // Outil Lisser : coin en cours d'arrondi (forme de départ gardée pour recalculer à chaque image)
    garment::Garment roundOrig_;
    int roundPanel_ = -1, roundPoint_ = -1;
    float roundT_ = 0.0f;
    // Outil Fendre et écarter : premier point choisi
    int slashPanel_ = -1, slashPoint_ = -1;
    /// Ajoute un point sur le bord survolé (renvoie son index, -1 sinon).
    int insertOnEdge(garment::Garment& g, garment::EdgeRef e, float t);
    [[nodiscard]] bool pointChosen(int panel, int point) const;
    /// Nouvelle pièce tracée sur la silhouette : placée à cet endroit du corps en 3D.
    void placeNewPanel(garment::Panel& panel) const;
    /// Déplace les points choisis de `d` (aimant / guides compris par l'appelant).
    void movePoints(garment::Garment& g, glm::vec2 d);
    bool rightPan_ = false;       // glisser commencé au clic droit
    ImVec2 rightPress_{0, 0};
};

} // namespace drapeo::ui
