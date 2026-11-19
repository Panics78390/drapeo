// =============================================================================
//  ui/PatternEditor — éditeur de patrons 2D (vue de gauche)
//
//  Outils : Sélection (déplacer pièces et points, double-clic sur un bord pour
//  ajouter un point), Polygone, Rectangle, Couture (cliquer deux bords).
//  Navigation : molette = zoom, clic milieu ou clic droit glissé = déplacer.
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
    std::vector<int> selection;   // sélection multiple (rectangle, Maj+clic) ; contient selectedPanel
    Backdrop backdrop;

    [[nodiscard]] bool isSelected(int panel) const;

    /// Dessine l'éditeur dans la région disponible de la fenêtre ImGui courante.
    /// Renvoie true quand une modification du patron est terminée (il faut
    /// remailler et relancer la couture).
    bool draw(garment::Garment& g);

    /// Cadre toutes les pièces dans la vue.
    void frameAll(const garment::Garment& g);
    void clearSelection() { selectedPanel = selectedPoint = selectedSeam = -1; selection.clear(); pendingSeam_ = {}; drawing_.clear(); }

    [[nodiscard]] std::string hint() const;
    [[nodiscard]] static ImU32 seamColor(int index);

private:
    glm::vec2 toWorld(ImVec2 s) const;
    ImVec2 toScreen(glm::vec2 w) const;
    garment::EdgeRef pickEdge(const garment::Garment& g, ImVec2 mouse, float* t = nullptr) const;
    int pickPoint(const garment::Garment& g, int panel, ImVec2 mouse) const;
    int pickPanel(const garment::Garment& g, glm::vec2 w) const;
    glm::vec2 snap(glm::vec2 w) const;
    /// Déplace des pièces dans le patron ; les poches cousues suivent leur base.
    void movePanels(garment::Garment& g, const std::vector<int>& ids, glm::vec2 d);
    bool attachMoved_ = false;   // une pose a bougé pendant le glisser : il faudra recoudre

    ImVec2 canvasPos_{0, 0}, canvasSize_{1, 1};
    glm::vec2 center_{0.3f, 0.35f};   // point du monde au centre de la vue (m)
    float zoom_ = 520.0f;             // pixels par mètre
    bool needsFrame_ = true;

    // États d'interaction
    enum class Drag { None, Point, Panel, Pan, Rect, Backdrop, Marquee } drag_ = Drag::None;
    glm::vec2 dragStart_{0.0f};
    glm::vec2 dragLast_{0.0f};
    std::vector<glm::vec2> drawing_;  // polygone en cours
    garment::EdgeRef pendingSeam_;    // premier bord choisi (outil couture)
    garment::EdgeRef hoverEdge_;
    bool shift_ = false;
};

} // namespace drapeo::ui
