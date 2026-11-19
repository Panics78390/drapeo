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

enum class Tool { Select, Polygon, Rectangle, Seam };

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
    bool magnet = true;           // aimant : points et pièces s'accrochent aux points et bords voisins

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
    enum class Drag { None, Point, Points, Panel, Pan, Rect, Backdrop, Marquee } drag_ = Drag::None;
    glm::vec2 dragStart_{0.0f};
    glm::vec2 dragLast_{0.0f};
    std::vector<glm::vec2> drawing_;  // polygone en cours
    garment::EdgeRef pendingSeam_;    // premier bord choisi (outil couture)
    garment::EdgeRef hoverEdge_;
    bool shift_ = false;
    bool hovered_ = false;
    bool pointMarquee_ = false;   // Ctrl + rectangle : sélection de points
    [[nodiscard]] bool pointChosen(int panel, int point) const;
    /// Déplace les points choisis de `d` (aimant / guides compris par l'appelant).
    void movePoints(garment::Garment& g, glm::vec2 d);
    bool rightPan_ = false;       // glisser commencé au clic droit
    ImVec2 rightPress_{0, 0};
};

} // namespace drapeo::ui
