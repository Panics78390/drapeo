#include "ui/PatternEditor.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace drapeo::ui {

namespace {
constexpr float kPickRadius = 8.0f;   // pixels
ImU32 rgba(glm::vec3 c, float a)
{
    return IM_COL32(static_cast<int>(c.r * 255), static_cast<int>(c.g * 255), static_cast<int>(c.b * 255), static_cast<int>(a * 255));
}
const ImU32 kAccent = IM_COL32(222, 160, 72, 255);
const ImU32 kInk = IM_COL32(226, 224, 218, 255);
const ImU32 kMuted = IM_COL32(150, 154, 162, 255);
} // namespace

ImU32 PatternEditor::seamColor(int index)
{
    static const ImU32 palette[] = {IM_COL32(86, 180, 233, 255),  IM_COL32(230, 120, 160, 255),
                                    IM_COL32(120, 200, 120, 255), IM_COL32(240, 200, 80, 255),
                                    IM_COL32(170, 140, 230, 255), IM_COL32(90, 200, 190, 255),
                                    IM_COL32(240, 140, 90, 255),  IM_COL32(200, 200, 200, 255)};
    return palette[static_cast<std::size_t>(index) % (sizeof(palette) / sizeof(palette[0]))];
}

glm::vec2 PatternEditor::toWorld(ImVec2 s) const
{
    const float cx = canvasPos_.x + canvasSize_.x * 0.5f, cy = canvasPos_.y + canvasSize_.y * 0.5f;
    return {center_.x + (s.x - cx) / zoom_, center_.y - (s.y - cy) / zoom_};
}

ImVec2 PatternEditor::toScreen(glm::vec2 w) const
{
    const float cx = canvasPos_.x + canvasSize_.x * 0.5f, cy = canvasPos_.y + canvasSize_.y * 0.5f;
    return {cx + (w.x - center_.x) * zoom_, cy - (w.y - center_.y) * zoom_};
}

glm::vec2 PatternEditor::snap(glm::vec2 w) const
{
    snapped_ = false;
    if (magnet && g_ != nullptr) {
        // Aimant : d'abord les points des autres pièces, puis leurs bords.
        const float radius = kPickRadius * 1.3f / zoom_;
        float best = radius;
        glm::vec2 hit = w;
        for (int p = 0; p < static_cast<int>(g_->panels.size()); ++p) {
            if (p == snapExcludePanel_) continue;
            for (const auto& q : g_->panels[static_cast<std::size_t>(p)].points)
                if (const float d = glm::distance(q, w); d < best) {
                    best = d;
                    hit = q;
                    snapped_ = true;
                }
        }
        if (!snapped_) {
            for (int p = 0; p < static_cast<int>(g_->panels.size()); ++p) {
                if (p == snapExcludePanel_) continue;
                const auto& pts = g_->panels[static_cast<std::size_t>(p)].points;
                for (std::size_t e = 0; e < pts.size(); ++e) {
                    float t = 0.0f;
                    const glm::vec2 a = pts[e], b = pts[(e + 1) % pts.size()];
                    if (const float d = garment::distanceToSegment(w, a, b, &t); d < best) {
                        best = d;
                        hit = a + (b - a) * t;
                        snapped_ = true;
                    }
                }
            }
        }
        if (snapped_) {
            snapPos_ = hit;
            return hit;
        }
    }
    if (!shift_) return w;
    return glm::round(w * 100.0f) / 100.0f;   // grille de 1 cm
}

glm::vec2 PatternEditor::magnetOffset(const garment::Garment& g, const std::vector<int>& ids, glm::vec2 delta) const
{
    snapped_ = false;
    if (!magnet) return glm::vec2(0.0f);
    const float radius = kPickRadius * 1.3f / zoom_;
    float best = radius;
    glm::vec2 corr(0.0f);
    auto moving = [&](int p) { return std::find(ids.begin(), ids.end(), p) != ids.end(); };
    for (const int m : ids) {
        if (m < 0 || m >= static_cast<int>(g.panels.size())) continue;
        for (const auto& v0 : g.panels[static_cast<std::size_t>(m)].points) {
            const glm::vec2 v = v0 + delta;
            for (int p = 0; p < static_cast<int>(g.panels.size()); ++p) {
                if (moving(p)) continue;
                for (const auto& q : g.panels[static_cast<std::size_t>(p)].points)
                    if (const float d = glm::distance(q, v); d < best) {
                        best = d;
                        corr = q - v;
                        snapped_ = true;
                        snapPos_ = q;
                    }
            }
        }
    }
    return corr;
}

void PatternEditor::frameAll(const garment::Garment& g)
{
    glm::vec2 lo(1e9f), hi(-1e9f);
    for (const auto& p : g.panels)
        for (const auto& q : p.points) {
            lo = glm::min(lo, q);
            hi = glm::max(hi, q);
        }
    if (backdrop.texture != 0 && backdrop.visible) {
        lo = glm::min(lo, backdrop.min);
        hi = glm::max(hi, backdrop.max);
    }
    if (lo.x > hi.x) {
        lo = {-0.3f, -0.1f};
        hi = {0.9f, 0.8f};
    }
    center_ = (lo + hi) * 0.5f;
    const glm::vec2 ext = glm::max(hi - lo, glm::vec2(0.1f));
    zoom_ = std::min((canvasSize_.x - 60.0f) / ext.x, (canvasSize_.y - 60.0f) / ext.y);
    zoom_ = std::clamp(zoom_, 40.0f, 4000.0f);
}

void PatternEditor::frameParts(const garment::Garment& g, const std::vector<int>& ids)
{
    glm::vec2 lo(1e9f), hi(-1e9f);
    for (const int p : ids)
        if (p >= 0 && p < static_cast<int>(g.panels.size()))
            for (const auto& q : g.panels[static_cast<std::size_t>(p)].points) {
                lo = glm::min(lo, q);
                hi = glm::max(hi, q);
            }
    if (lo.x > hi.x) return;
    center_ = (lo + hi) * 0.5f;
    const glm::vec2 ext = glm::max(hi - lo, glm::vec2(0.05f));
    zoom_ = std::clamp(std::min((canvasSize_.x - 80.0f) / ext.x, (canvasSize_.y - 80.0f) / ext.y), 40.0f, 6000.0f);
}

std::vector<int> PatternEditor::targets() const
{
    if (!selection.empty()) return selection;
    if (selectedPanel >= 0) return {selectedPanel};
    return {};
}

void PatternEditor::select(const std::vector<int>& ids)
{
    selection.clear();
    selectedPoint = selectedSeam = -1;
    selectedPanel = ids.empty() ? -1 : ids.back();
    if (ids.size() > 1) selection = ids;
    backdrop.selected = false;
}

int PatternEditor::pickPoint(const garment::Garment& g, int panel, ImVec2 mouse) const
{
    const auto& pts = g.panels[static_cast<std::size_t>(panel)].points;
    for (std::size_t i = 0; i < pts.size(); ++i) {
        const ImVec2 s = toScreen(pts[i]);
        if (std::hypot(s.x - mouse.x, s.y - mouse.y) < kPickRadius) return static_cast<int>(i);
    }
    return -1;
}

int PatternEditor::pickPanel(const garment::Garment& g, glm::vec2 w) const
{
    for (int p = static_cast<int>(g.panels.size()) - 1; p >= 0; --p)
        if (garment::pointInPolygon(g.panels[static_cast<std::size_t>(p)].points, w)) return p;
    return -1;
}

garment::EdgeRef PatternEditor::pickEdge(const garment::Garment& g, ImVec2 mouse, float* t) const
{
    garment::EdgeRef best;
    float bestD = kPickRadius;
    for (std::size_t p = 0; p < g.panels.size(); ++p) {
        const auto& pts = g.panels[p].points;
        for (std::size_t e = 0; e < pts.size(); ++e) {
            const ImVec2 a = toScreen(pts[e]), b = toScreen(pts[(e + 1) % pts.size()]);
            float s = 0;
            const float d = garment::distanceToSegment({mouse.x, mouse.y}, {a.x, a.y}, {b.x, b.y}, &s);
            if (d < bestD) {
                bestD = d;
                best = {static_cast<int>(p), static_cast<int>(e)};
                if (t) *t = s;
            }
        }
    }
    return best;
}

void PatternEditor::movePanels(garment::Garment& g, const std::vector<int>& ids, glm::vec2 d)
{
    if (d == glm::vec2(0.0f)) return;
    const int n = static_cast<int>(g.panels.size());
    auto moving = [&](int p) { return std::find(ids.begin(), ids.end(), p) != ids.end(); };
    auto bboxCenter = [&](int p) {
        glm::vec2 lo(1e9f), hi(-1e9f);
        for (const auto& q : g.panels[static_cast<std::size_t>(p)].points) {
            lo = glm::min(lo, q);
            hi = glm::max(hi, q);
        }
        return (lo + hi) * 0.5f;
    };
    std::vector<int> followers;   // poches posées sur une base déplacée, dessinées sur elle
    for (auto& a : g.attachments) {
        if (a.patch < 0 || a.base < 0 || a.patch >= n || a.base >= n) continue;
        const bool baseMoves = moving(a.base), patchMoves = moving(a.patch);
        if (baseMoves) {
            a.at += d;   // la poche reste au même endroit de sa base
            if (!patchMoves && glm::distance(bboxCenter(a.patch), a.at - d) < 0.01f) followers.push_back(a.patch);
        } else if (patchMoves) {
            a.at += d;   // la poche glisse sur sa base
            attachMoved_ = true;
        }
    }
    for (const int p : ids)
        if (p >= 0 && p < n) {
            for (auto& q : g.panels[static_cast<std::size_t>(p)].points) q += d;
            g.panels[static_cast<std::size_t>(p)].symAxisX += d.x;
        }
    for (const int p : followers) {
        for (auto& q : g.panels[static_cast<std::size_t>(p)].points) q += d;
        g.panels[static_cast<std::size_t>(p)].symAxisX += d.x;
    }
}

void PatternEditor::applySymmetry(garment::Garment& g, int panel, int point)
{
    if (panel < 0 || panel >= static_cast<int>(g.panels.size())) return;
    const auto& pn = g.panels[static_cast<std::size_t>(panel)];
    if (!pn.symmetric || point < 0 || point >= static_cast<int>(pn.points.size())) return;
    const glm::vec2 p = pn.points[static_cast<std::size_t>(point)];
    if (!garment::symmetrizePanel(g, panel, p.x <= pn.symAxisX)) return;
    // Le point modifié garde la sélection (son index a pu changer).
    const auto& pts = g.panels[static_cast<std::size_t>(panel)].points;
    int best = -1;
    float bd = 1e9f;
    for (int k = 0; k < static_cast<int>(pts.size()); ++k)
        if (const float d = glm::distance(pts[static_cast<std::size_t>(k)], p); d < bd) {
            bd = d;
            best = k;
        }
    if (panel == selectedPanel) selectedPoint = best;
}

bool PatternEditor::isSelected(int panel) const
{
    return panel >= 0 && (panel == selectedPanel || std::find(selection.begin(), selection.end(), panel) != selection.end());
}

std::string PatternEditor::hint() const
{
    switch (tool) {
    case Tool::Select:
        if (selection.size() > 1)
            return std::to_string(selection.size()) + " pièces sélectionnées : glisser pour les déplacer, Suppr pour les supprimer, Échap pour désélectionner.";
        if (backdrop.selected)
            return "Image de fond sélectionnée : glisser pour la déplacer, Suppr pour la retirer.";
        return "Sélection : glisser une pièce ou un point, ou tracer un rectangle dans le vide (Maj : ajouter). Double-clic sur un bord : ajouter un point. Suppr : supprimer.";
    case Tool::Polygon:
        return drawing_.empty() ? "Polygone : cliquez pour poser le premier point."
                                : "Polygone : cliquez pour ajouter des points, cliquez sur le premier (ou Entrée) pour fermer. Échap : annuler.";
    case Tool::Rectangle:
        return "Rectangle : cliquez-glissez pour tracer une pièce rectangulaire.";
    case Tool::Seam:
        return pendingSeam_.panel < 0 ? "Couture : cliquez sur le premier bord."
                                      : "Couture : cliquez sur le second bord à coudre. Échap : annuler.";
    }
    return {};
}

bool PatternEditor::draw(garment::Garment& g)
{
    bool committed = false;
    ImGuiIO& io = ImGui::GetIO();
    shift_ = io.KeyShift;
    g_ = &g;
    // Sélection multiple périmée (pièce choisie ailleurs, pièces supprimées) : on l'oublie.
    if (!selection.empty()) {
        const bool stale = std::find(selection.begin(), selection.end(), selectedPanel) == selection.end() ||
                           std::any_of(selection.begin(), selection.end(),
                                       [&](int p) { return p < 0 || p >= static_cast<int>(g.panels.size()); });
        if (stale) selection.clear();
    }

    canvasPos_ = ImGui::GetCursorScreenPos();
    canvasSize_ = ImGui::GetContentRegionAvail();
    canvasSize_.x = std::max(canvasSize_.x, 50.0f);
    canvasSize_.y = std::max(canvasSize_.y, 50.0f);
    if (needsFrame_) {
        frameAll(g);
        needsFrame_ = false;
    }

    ImGui::InvisibleButton("##canvas", canvasSize_,
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
    const bool hovered = ImGui::IsItemHovered();
    hovered_ = hovered;
    const ImVec2 mouse = io.MousePos;
    const glm::vec2 mw = toWorld(mouse);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 c0 = canvasPos_, c1(canvasPos_.x + canvasSize_.x, canvasPos_.y + canvasSize_.y);
    dl->PushClipRect(c0, c1, true);
    dl->AddRectFilled(c0, c1, IM_COL32(30, 32, 36, 255));

    // --- Grille : 1 cm (fine, si zoom suffisant), 10 cm, 50 cm ---------------
    const glm::vec2 wLo = toWorld({c0.x, c1.y}), wHi = toWorld({c1.x, c0.y});
    auto gridLines = [&](float step, ImU32 col) {
        for (float x = std::floor(wLo.x / step) * step; x <= wHi.x; x += step) {
            const float sx = toScreen({x, 0}).x;
            dl->AddLine({sx, c0.y}, {sx, c1.y}, col);
        }
        for (float y = std::floor(wLo.y / step) * step; y <= wHi.y; y += step) {
            const float sy = toScreen({0, y}).y;
            dl->AddLine({c0.x, sy}, {c1.x, sy}, col);
        }
    };
    if (zoom_ * 0.01f > 7.0f) gridLines(0.01f, IM_COL32(255, 255, 255, 8));
    gridLines(0.1f, IM_COL32(255, 255, 255, 18));
    gridLines(0.5f, IM_COL32(255, 255, 255, 34));
    // Calque : image du patron sous les pièces
    if (backdrop.texture != 0 && backdrop.visible) {
        const int alpha = static_cast<int>(std::clamp(backdrop.opacity, 0.0f, 1.0f) * 255.0f);
        dl->AddImage(backdrop.texture, toScreen({backdrop.min.x, backdrop.max.y}), toScreen({backdrop.max.x, backdrop.min.y}),
                     ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, alpha));
        if (backdrop.selected) {
            dl->AddRect(toScreen({backdrop.min.x, backdrop.max.y}), toScreen({backdrop.max.x, backdrop.min.y}), IM_COL32(232, 160, 60, 230), 0.0f, 0, 2.0f);
        } else {
            dl->AddRect(toScreen({backdrop.min.x, backdrop.max.y}), toScreen({backdrop.max.x, backdrop.min.y}), IM_COL32(255, 255, 255, 40));
        }
    }
    {   // échelle graphique
        const ImVec2 o(c0.x + 14, c1.y - 16);
        dl->AddLine(o, {o.x + 0.1f * zoom_, o.y}, kMuted, 2.0f);
        dl->AddText({o.x, o.y - 18}, kMuted, "10 cm");
    }

    // --- Survol ---------------------------------------------------------------
    float edgeT = 0.0f;
    hoverEdge_ = hovered ? pickEdge(g, mouse, &edgeT) : garment::EdgeRef{};

    // --- Interactions ---------------------------------------------------------
    if (hovered) {
        // Zoom autour du curseur
        if (io.MouseWheel != 0.0f) {
            const glm::vec2 before = toWorld(mouse);
            zoom_ = std::clamp(zoom_ * std::pow(1.15f, io.MouseWheel), 40.0f, 6000.0f);
            const glm::vec2 after = toWorld(mouse);
            center_ += before - after;
        }
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Middle) || ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            drag_ = Drag::Pan;
            rightPan_ = ImGui::IsMouseClicked(ImGuiMouseButton_Right);
            rightPress_ = mouse;
        }

        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            const glm::vec2 w = snap(mw);
            switch (tool) {
            case Tool::Select: {
                selectedSeam = -1;
                int hitPanel = -1, hitPoint = -1;
                // Points de la pièce sélectionnée d'abord, puis des autres.
                if (selectedPanel >= 0 && selectedPanel < static_cast<int>(g.panels.size())) {
                    hitPoint = pickPoint(g, selectedPanel, mouse);
                    if (hitPoint >= 0) hitPanel = selectedPanel;
                }
                for (int p = 0; hitPoint < 0 && p < static_cast<int>(g.panels.size()); ++p) {
                    hitPoint = pickPoint(g, p, mouse);
                    if (hitPoint >= 0) hitPanel = p;
                }
                const bool shift = io.KeyShift;
                const bool onBackdrop = backdrop.texture != 0 && backdrop.visible && mw.x >= backdrop.min.x &&
                                        mw.x <= backdrop.max.x && mw.y >= backdrop.min.y && mw.y <= backdrop.max.y;
                if (hitPoint >= 0 && !shift) {
                    selectedPanel = hitPanel;
                    selectedPoint = hitPoint;
                    selection.clear();
                    drag_ = Drag::Point;
                    snapExcludePanel_ = hitPanel;
                    const auto& hp = g.panels[static_cast<std::size_t>(hitPanel)];
                    symPartner_ = garment::symmetricPartner(hp, hitPoint);
                    symOnAxis_ = hp.symmetric && std::abs(hp.points[static_cast<std::size_t>(hitPoint)].x - hp.symAxisX) < 0.0015f;
                    backdrop.selected = false;
                } else if (const int p = pickPanel(g, mw); p >= 0) {
                    selectedPoint = -1;
                    backdrop.selected = false;
                    if (shift) {
                        // Maj + clic : ajoute ou retire la pièce de la sélection.
                        if (selection.empty() && selectedPanel >= 0) selection.push_back(selectedPanel);
                        if (const auto it = std::find(selection.begin(), selection.end(), p); it != selection.end()) {
                            selection.erase(it);
                            selectedPanel = selection.empty() ? -1 : selection.back();
                        } else {
                            selection.push_back(p);
                            selectedPanel = p;
                        }
                        if (selection.size() == 1) selection.clear();
                    } else {
                        if (!isSelected(p)) selection.clear();   // clic dans la sélection : on la déplace entière
                        selectedPanel = p;
                        drag_ = Drag::Panel;
                        dragApplied_ = glm::vec2(0.0f);
                    }
                } else if (backdrop.selected && onBackdrop && !shift) {
                    drag_ = Drag::Backdrop;   // image déjà sélectionnée : on la glisse
                } else {
                    // Clic dans le vide : rectangle de sélection.
                    if (!shift) {
                        selectedPanel = selectedPoint = -1;
                        selection.clear();
                    }
                    backdrop.selected = false;
                    drag_ = Drag::Marquee;
                }
                dragStart_ = dragLast_ = mw;
                break;
            }
            case Tool::Polygon: {
                if (drawing_.size() >= 3) {
                    const ImVec2 first = toScreen(drawing_.front());
                    if (std::hypot(first.x - mouse.x, first.y - mouse.y) < kPickRadius * 1.5f) {
                        garment::Panel panel;
                        panel.name = "Pièce " + std::to_string(g.panels.size() + 1);
                        panel.points = drawing_;
                        g.panels.push_back(panel);
                        selectedPanel = static_cast<int>(g.panels.size()) - 1;
                        drawing_.clear();
                        committed = true;
                        break;
                    }
                }
                drawing_.push_back(w);
                break;
            }
            case Tool::Rectangle:
                drag_ = Drag::Rect;
                dragStart_ = dragLast_ = w;
                break;
            case Tool::Seam:
                if (hoverEdge_.panel >= 0) {
                    // Maj + clic sur un bord voisin d'une couture sélectionnée : il rejoint ce côté.
                    if (ImGui::GetIO().KeyShift && pendingSeam_.panel < 0 && selectedSeam >= 0 &&
                        selectedSeam < static_cast<int>(g.seams.size())) {
                        const auto& sm = g.seams[static_cast<std::size_t>(selectedSeam)];
                        for (int side = 0; side < 2; ++side) {
                            const garment::EdgeRef st = side == 0 ? sm.a : sm.b;
                            const int cnt = side == 0 ? sm.countA : sm.countB;
                            if (st.panel != hoverEdge_.panel) continue;
                            const int n = static_cast<int>(g.panels[static_cast<std::size_t>(st.panel)].points.size());
                            if (hoverEdge_.edge == (st.edge + cnt) % n) {
                                committed = g.extendSeam(selectedSeam, side, +1);
                                break;
                            }
                            if (hoverEdge_.edge == (st.edge - 1 + n) % n) {
                                committed = g.extendSeam(selectedSeam, side, +1, true);
                                break;
                            }
                        }
                        break;
                    }
                    if (pendingSeam_.panel < 0) {
                        pendingSeam_ = hoverEdge_;
                    } else if (!(pendingSeam_ == hoverEdge_)) {
                        g.removeSeamsOn(pendingSeam_);
                        g.removeSeamsOn(hoverEdge_);
                        g.seams.push_back({pendingSeam_, hoverEdge_, false});
                        selectedSeam = static_cast<int>(g.seams.size()) - 1;
                        pendingSeam_ = {};
                        committed = true;
                    }
                }
                break;
            }
        }

        // Double-clic sur un bord : insérer un point
        if (tool == Tool::Select && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && hoverEdge_.panel >= 0) {
            const glm::vec2 a = g.edgeStart(hoverEdge_), b = g.edgeEnd(hoverEdge_);
            const glm::vec2 np = a + (b - a) * edgeT;
            g.insertPoint(hoverEdge_.panel, hoverEdge_.edge, np);
            selectedPanel = hoverEdge_.panel;
            selectedPoint = hoverEdge_.edge + 1;
            applySymmetry(g, selectedPanel, selectedPoint);   // pièce symétrique : reproduit de l'autre côté
            drag_ = Drag::None;
            committed = true;
        }
    }

    // Glisser
    if (drag_ == Drag::Pan) {
        center_ -= glm::vec2(io.MouseDelta.x, -io.MouseDelta.y) / zoom_;
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Middle) && !ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
            drag_ = Drag::None;
            // Clic droit sans glisser : menu de la pièce sous la souris
            if (rightPan_ && std::hypot(mouse.x - rightPress_.x, mouse.y - rightPress_.y) < 5.0f && tool == Tool::Select) {
                const int hit = pickPanel(g, toWorld(rightPress_));
                if (hit >= 0 && !isSelected(hit)) select({hit});
                menuPanel = hit;
                menuWorld = toWorld(rightPress_);
                menuRequested = true;
            }
            rightPan_ = false;
        }
    } else if (drag_ == Drag::Panel && selection.size() > 1) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            const glm::vec2 want = mw - dragStart_;
            const glm::vec2 total = want + magnetOffset(g, selection, want - dragApplied_);
            movePanels(g, selection, total - dragApplied_);
            dragApplied_ = total;
            dragLast_ = mw;
        } else {
            if (attachMoved_) committed = true;
            attachMoved_ = false;
            drag_ = Drag::None;
        }
    } else if (drag_ == Drag::Marquee) {
        dragLast_ = mw;
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            const ImVec2 a = toScreen(dragStart_), b = toScreen(dragLast_);
            if (std::abs(a.x - b.x) < 4.0f && std::abs(a.y - b.y) < 4.0f) {
                // Simple clic dans le vide : sélectionne l'image de fond si on est dessus.
                backdrop.selected = backdrop.texture != 0 && backdrop.visible && mw.x >= backdrop.min.x &&
                                    mw.x <= backdrop.max.x && mw.y >= backdrop.min.y && mw.y <= backdrop.max.y &&
                                    selection.empty() && selectedPanel < 0;
            } else {
                const glm::vec2 lo = glm::min(dragStart_, dragLast_), hi = glm::max(dragStart_, dragLast_);
                const auto inside = [&](glm::vec2 q) { return q.x >= lo.x && q.x <= hi.x && q.y >= lo.y && q.y <= hi.y; };
                if (selection.empty() && selectedPanel >= 0) selection.push_back(selectedPanel);
                for (int p = 0; p < static_cast<int>(g.panels.size()); ++p) {
                    const auto& pts = g.panels[static_cast<std::size_t>(p)].points;
                    bool hit = inside(garment::centroid(pts));
                    for (std::size_t i = 0; !hit && i < pts.size(); ++i) hit = inside(pts[i]);
                    if (hit && !isSelected(p)) selection.push_back(p);
                }
                if (!selection.empty()) selectedPanel = selection.back();
                if (selection.size() == 1) selection.clear();
                selectedPoint = -1;
            }
            drag_ = Drag::None;
        }
    } else if (drag_ == Drag::Point || drag_ == Drag::Panel) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            if (selectedPanel >= 0 && selectedPanel < static_cast<int>(g.panels.size())) {
                auto& pts = g.panels[static_cast<std::size_t>(selectedPanel)].points;
                if (drag_ == Drag::Point && selectedPoint >= 0 && selectedPoint < static_cast<int>(pts.size())) {
                    glm::vec2 np = snap(mw);
                    const auto& pn = g.panels[static_cast<std::size_t>(selectedPanel)];
                    if (symOnAxis_) np.x = pn.symAxisX;   // un point de l'axe y reste
                    pts[static_cast<std::size_t>(selectedPoint)] = np;
                    if (symPartner_ >= 0 && symPartner_ < static_cast<int>(pts.size()))
                        pts[static_cast<std::size_t>(symPartner_)] = {2.0f * pn.symAxisX - np.x, np.y};
                } else if (drag_ == Drag::Panel) {
                    // Déplacer une pièce ne change pas le vêtement 3D, sauf si une poche glisse sur sa base.
                    const glm::vec2 want = mw - dragStart_;
                    const glm::vec2 total = want + magnetOffset(g, {selectedPanel}, want - dragApplied_);
                    movePanels(g, {selectedPanel}, total - dragApplied_);
                    dragApplied_ = total;
                }
            }
            dragLast_ = mw;
        } else {
            if (drag_ == Drag::Point && glm::distance(dragStart_, mw) > 1e-4f) {
                committed = true;
                applySymmetry(g, selectedPanel, selectedPoint);
            }
            if (attachMoved_) committed = true;
            snapExcludePanel_ = symPartner_ = -1;
            symOnAxis_ = false;
            snapped_ = false;
            attachMoved_ = false;
            drag_ = Drag::None;
        }
    } else if (drag_ == Drag::Backdrop) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            const glm::vec2 d = mw - dragLast_;
            if (d != glm::vec2(0.0f)) {
                backdrop.min += d;
                backdrop.max += d;
                backdrop.moved = true;
            }
            dragLast_ = mw;
        } else {
            drag_ = Drag::None;
        }
    } else if (drag_ == Drag::Rect) {
        dragLast_ = snap(mw);
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            const glm::vec2 lo = glm::min(dragStart_, dragLast_), hi = glm::max(dragStart_, dragLast_);
            if (hi.x - lo.x > 0.02f && hi.y - lo.y > 0.02f) {
                garment::Panel panel;
                panel.name = "Pièce " + std::to_string(g.panels.size() + 1);
                panel.points = {lo, {hi.x, lo.y}, hi, {lo.x, hi.y}};
                g.panels.push_back(panel);
                selectedPanel = static_cast<int>(g.panels.size()) - 1;
                selectedPoint = -1;
                committed = true;
            }
            drag_ = Drag::None;
        }
    }

    // Clavier (si la vue 2D est survolée ou active)
    if (hovered || ImGui::IsItemActive()) {
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_A, false) && tool == Tool::Select && !g.panels.empty()) {
            selection.clear();
            for (int p = 0; p < static_cast<int>(g.panels.size()); ++p) selection.push_back(p);
            selectedPanel = selection.back();
            selectedPoint = selectedSeam = -1;
            if (selection.size() == 1) selection.clear();
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            if (drawing_.empty() && pendingSeam_.panel < 0) {
                selection.clear();
                selectedPanel = selectedPoint = -1;
            }
            drawing_.clear();
            pendingSeam_ = {};
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Enter) && drawing_.size() >= 3) {
            garment::Panel panel;
            panel.name = "Pièce " + std::to_string(g.panels.size() + 1);
            panel.points = drawing_;
            g.panels.push_back(panel);
            selectedPanel = static_cast<int>(g.panels.size()) - 1;
            drawing_.clear();
            committed = true;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Delete) || ImGui::IsKeyPressed(ImGuiKey_Backspace)) {
            if (backdrop.selected && selectedPanel < 0 && selectedSeam < 0) {
                backdrop.removeRequested = true;
                backdrop.selected = false;
            } else if (selection.size() > 1 && selectedSeam < 0) {
                std::vector<int> doomed = selection;
                std::sort(doomed.rbegin(), doomed.rend());
                for (const int p : doomed)
                    if (p >= 0 && p < static_cast<int>(g.panels.size())) g.removePanel(p);
                selection.clear();
                selectedPanel = selectedPoint = -1;
                committed = true;
            } else if (selectedSeam >= 0 && selectedSeam < static_cast<int>(g.seams.size())) {
                g.seams.erase(g.seams.begin() + selectedSeam);
                selectedSeam = -1;
                committed = true;
            } else if (selectedPanel >= 0 && selectedPoint >= 0 &&
                       g.panels[static_cast<std::size_t>(selectedPanel)].points.size() > 3) {
                const auto& pn = g.panels[static_cast<std::size_t>(selectedPanel)];
                const bool left = pn.points[static_cast<std::size_t>(selectedPoint)].x <= pn.symAxisX;
                g.removePoint(selectedPanel, selectedPoint);
                if (pn.symmetric) garment::symmetrizePanel(g, selectedPanel, left);   // supprimé des deux côtés
                selectedPoint = -1;
                committed = true;
            } else if (selectedPanel >= 0 && selectedPanel < static_cast<int>(g.panels.size())) {
                g.removePanel(selectedPanel);
                selectedPanel = selectedPoint = -1;
                committed = true;
            }
        }
        if (!io.KeyCtrl) {
            if (ImGui::IsKeyPressed(ImGuiKey_V)) tool = Tool::Select;
            if (ImGui::IsKeyPressed(ImGuiKey_P)) tool = Tool::Polygon;
            if (ImGui::IsKeyPressed(ImGuiKey_B)) tool = Tool::Rectangle;
            if (ImGui::IsKeyPressed(ImGuiKey_S)) tool = Tool::Seam;
            if (ImGui::IsKeyPressed(ImGuiKey_F)) frameAll(g);
            if (ImGui::IsKeyPressed(ImGuiKey_M)) magnet = !magnet;
        }
    }
    if (tool != Tool::Polygon) drawing_.clear();
    if (tool != Tool::Seam) pendingSeam_ = {};

    // --- Dessin des pièces -----------------------------------------------------
    std::vector<ImVec2> scr;
    for (std::size_t p = 0; p < g.panels.size(); ++p) {
        const auto& panel = g.panels[p];
        scr.clear();
        for (const auto& q : panel.points) scr.push_back(toScreen(q));
        const bool sel = isSelected(static_cast<int>(p));
        // Remplissage : triangulation en éventail impossible pour les pièces
        // concaves, on utilise le remplissage de polygone concave d'ImGui.
        dl->AddConcavePolyFilled(scr.data(), static_cast<int>(scr.size()), rgba(panel.color, sel ? 0.42f : 0.28f));
        dl->AddPolyline(scr.data(), static_cast<int>(scr.size()), sel ? kAccent : rgba(panel.color * 0.6f + 0.4f, 1.0f),
                        ImDrawFlags_Closed, sel ? 2.2f : 1.4f);
        const glm::vec2 c = garment::centroid(panel.points);
        const ImVec2 sc = toScreen(c);
        const ImVec2 ts = ImGui::CalcTextSize(panel.name.c_str());
        dl->AddText({sc.x - ts.x * 0.5f, sc.y - ts.y * 0.5f}, kInk, panel.name.c_str());
        // État 3D et clones liés : petite étiquette sous le nom
        std::string tags;
        auto tag = [&](const char* t) { tags += tags.empty() ? t : std::string(" · ") + t; };
        if (!panel.active3D) tag("désactivée en 3D");
        if (panel.hidden3D) tag("masquée");
        if (panel.frozen) tag("figée");
        if (panel.stiffness > 1.0f) tag("renforcée");
        if (panel.linkGroup > 0) tag(("liée " + std::to_string(panel.linkGroup)).c_str());
        if (!tags.empty()) {
            const ImVec2 tt = ImGui::CalcTextSize(tags.c_str());
            dl->AddText({sc.x - tt.x * 0.5f, sc.y + ts.y * 0.5f + 2.0f}, IM_COL32(150, 200, 255, 220), tags.c_str());
        }
        if (!panel.active3D) {   // pièce désactivée : contour en tirets gris par-dessus
            for (std::size_t e = 0; e < scr.size(); ++e) {
                const ImVec2 a = scr[e], b = scr[(e + 1) % scr.size()];
                const float len = std::hypot(b.x - a.x, b.y - a.y);
                const int dashes = std::max(1, static_cast<int>(len / 7.0f));
                for (int k = 0; k < dashes; k += 2)
                    dl->AddLine({a.x + (b.x - a.x) * k / dashes, a.y + (b.y - a.y) * k / dashes},
                                {a.x + (b.x - a.x) * (k + 1) / dashes, a.y + (b.y - a.y) * (k + 1) / dashes}, IM_COL32(20, 20, 24, 220), 2.0f);
            }
        }
    }

    // --- Axe de symétrie des pièces symétriques -------------------------------------
    for (std::size_t p = 0; p < g.panels.size(); ++p) {
        const auto& pn = g.panels[p];
        if (!pn.symmetric || pn.points.size() < 3) continue;
        float y0 = 1e9f, y1 = -1e9f;
        for (const auto& q : pn.points) {
            y0 = std::min(y0, q.y);
            y1 = std::max(y1, q.y);
        }
        const ImVec2 a = toScreen({pn.symAxisX, y1 + 0.02f}), b = toScreen({pn.symAxisX, y0 - 0.02f});
        const int dashes = std::max(2, static_cast<int>((b.y - a.y) / 5.0f));
        const ImU32 col = isSelected(static_cast<int>(p)) ? IM_COL32(120, 200, 255, 220) : IM_COL32(120, 200, 255, 110);
        for (int k = 0; k < dashes; k += 2)
            dl->AddLine({a.x, a.y + (b.y - a.y) * k / dashes}, {a.x, a.y + (b.y - a.y) * (k + 1) / dashes}, col, 1.5f);
    }

    // --- Droit fil : flèche à deux pointes sur chaque pièce -----------------------
    for (std::size_t p = 0; p < g.panels.size(); ++p) {
        if (g.panels[p].points.size() < 3) continue;
        glm::vec2 wa, wb;
        garment::grainLine(g.panels[p], wa, wb);
        const ImVec2 a = toScreen(wa), b = toScreen(wb);
        const float len = std::hypot(b.x - a.x, b.y - a.y);
        if (len < 12.0f) continue;
        const bool sel = isSelected(static_cast<int>(p));
        const ImU32 col = sel ? IM_COL32(240, 190, 110, 230) : IM_COL32(235, 235, 235, 120);
        dl->AddLine(a, b, col, 1.4f);
        const glm::vec2 u((b.x - a.x) / len, (b.y - a.y) / len), v(-u.y, u.x);
        const float h = std::min(9.0f, len * 0.2f);
        for (const auto& [tip, dir] : {std::pair{b, u}, std::pair{a, -u}}) {
            dl->AddTriangleFilled(tip, {tip.x - dir.x * h + v.x * h * 0.45f, tip.y - dir.y * h + v.y * h * 0.45f},
                                  {tip.x - dir.x * h - v.x * h * 0.45f, tip.y - dir.y * h - v.y * h * 0.45f}, col);
        }
    }

    // --- Pièces posées (poches, rabats, renforts) : contour en tirets sur leur base
    for (const auto& at : g.attachments) {
        if (at.patch < 0 || at.base < 0 || at.patch >= static_cast<int>(g.panels.size()) || at.base >= static_cast<int>(g.panels.size()))
            continue;
        const auto& pp = g.panels[static_cast<std::size_t>(at.patch)].points;
        glm::vec2 lo(1e9f), hi(-1e9f);
        for (const auto& q : pp) {
            lo = glm::min(lo, q);
            hi = glm::max(hi, q);
        }
        const glm::vec2 shift = at.at - (lo + hi) * 0.5f;
        const auto edges = g.attachedEdges(at);
        for (std::size_t e = 0; e < pp.size(); ++e) {
            const bool sewn = std::find(edges.begin(), edges.end(), static_cast<int>(e)) != edges.end();
            const ImVec2 a = toScreen(pp[e] + shift), b = toScreen(pp[(e + 1) % pp.size()] + shift);
            const float len = std::hypot(b.x - a.x, b.y - a.y);
            const int dashes = std::max(1, static_cast<int>(len / 6.0f));
            for (int k = 0; k < dashes; k += 2) {
                const float t0 = static_cast<float>(k) / dashes, t1 = static_cast<float>(k + 1) / dashes;
                dl->AddLine({a.x + (b.x - a.x) * t0, a.y + (b.y - a.y) * t0}, {a.x + (b.x - a.x) * t1, a.y + (b.y - a.y) * t1},
                            sewn ? IM_COL32(230, 200, 120, 220) : IM_COL32(160, 160, 160, 140), sewn ? 1.6f : 1.0f);
            }
        }
    }

    // --- Coutures : chaque bord cousu prend la couleur de sa couture ----------
    for (std::size_t s = 0; s < g.seams.size(); ++s) {
        const auto& seam = g.seams[s];
        if (seam.a.panel >= static_cast<int>(g.panels.size()) || seam.b.panel >= static_cast<int>(g.panels.size())) continue;
        const ImU32 col = seam.enabled ? seamColor(static_cast<int>(s)) : IM_COL32(125, 125, 130, 200);
        const float w = static_cast<int>(s) == selectedSeam ? 5.0f : 3.5f;
        // Chaque côté peut couvrir plusieurs bords (courbe) : on les trace tous,
        // et la pastille se place au milieu (en longueur) de chaque côté.
        ImVec2 mids[2];
        for (int side = 0; side < 2; ++side) {
            const auto edges = g.seamEdges(seam, side);
            const float total = g.seamLength(seam, side);
            float run = 0.0f;
            bool midSet = false;
            for (const auto& e : edges) {
                const ImVec2 p0 = toScreen(g.edgeStart(e)), p1 = toScreen(g.edgeEnd(e));
                dl->AddLine(p0, p1, col, w);
                const float l = g.edgeLength(e);
                if (!midSet && run + l >= total * 0.5f) {
                    const float t = l > 0.0f ? (total * 0.5f - run) / l : 0.5f;
                    mids[side] = {p0.x + (p1.x - p0.x) * t, p0.y + (p1.y - p0.y) * t};
                    midSet = true;
                }
                run += l;
            }
            if (!midSet && !edges.empty()) mids[side] = toScreen(g.edgeStart(edges.front()));
        }
        // Liaison en pointillés entre les milieux des deux côtés
        const ImVec2 ma = mids[0], mb = mids[1];
        const float len = std::hypot(mb.x - ma.x, mb.y - ma.y);
        const int dashes = std::max(1, static_cast<int>(len / 10.0f));
        for (int k = 0; k < dashes; k += 2) {
            const float t0 = static_cast<float>(k) / dashes, t1 = static_cast<float>(k + 1) / dashes;
            dl->AddLine({ma.x + (mb.x - ma.x) * t0, ma.y + (mb.y - ma.y) * t0},
                        {ma.x + (mb.x - ma.x) * t1, ma.y + (mb.y - ma.y) * t1}, (col & 0x00FFFFFF) | 0x90000000, 1.2f);
        }
        char label[24];
        std::snprintf(label, sizeof(label), "%zu", s + 1);
        dl->AddCircleFilled(ma, 8, col);
        dl->AddText({ma.x - 4, ma.y - 7}, IM_COL32(20, 20, 24, 255), label);
        dl->AddCircleFilled(mb, 8, col);
        dl->AddText({mb.x - 4, mb.y - 7}, IM_COL32(20, 20, 24, 255), label);
    }

    // Points (pièce sélectionnée, ou toutes en zoom suffisant)
    for (std::size_t p = 0; p < g.panels.size(); ++p) {
        const bool sel = static_cast<int>(p) == selectedPanel;
        for (std::size_t i = 0; i < g.panels[p].points.size(); ++i) {
            const ImVec2 s = toScreen(g.panels[p].points[i]);
            const bool ptSel = sel && static_cast<int>(i) == selectedPoint;
            const float r = ptSel ? 5.0f : 3.5f;
            dl->AddRectFilled({s.x - r, s.y - r}, {s.x + r, s.y + r}, ptSel ? kAccent : (sel ? kInk : kMuted));
        }
    }

    // Bord survolé : surbrillance + cote
    if (hoverEdge_.panel >= 0 && drag_ == Drag::None) {
        const ImVec2 a = toScreen(g.edgeStart(hoverEdge_)), b = toScreen(g.edgeEnd(hoverEdge_));
        dl->AddLine(a, b, tool == Tool::Seam ? IM_COL32(255, 255, 255, 230) : IM_COL32(255, 255, 255, 120), 3.0f);
        char text[32];
        std::snprintf(text, sizeof(text), "%.1f cm", static_cast<double>(g.edgeLength(hoverEdge_) * 100.0f));
        dl->AddRectFilled({mouse.x + 12, mouse.y + 10}, {mouse.x + 18 + ImGui::CalcTextSize(text).x, mouse.y + 30},
                          IM_COL32(15, 16, 18, 220), 4.0f);
        dl->AddText({mouse.x + 15, mouse.y + 12}, kInk, text);
    }
    if (pendingSeam_.panel >= 0 && pendingSeam_.panel < static_cast<int>(g.panels.size())) {
        const ImVec2 a = toScreen(g.edgeStart(pendingSeam_)), b = toScreen(g.edgeEnd(pendingSeam_));
        dl->AddLine(a, b, seamColor(static_cast<int>(g.seams.size())), 5.0f);
        dl->AddLine({(a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f}, mouse, IM_COL32(255, 255, 255, 120), 1.0f);
    }

    // Polygone en cours
    if (!drawing_.empty()) {
        scr.clear();
        for (const auto& q : drawing_) scr.push_back(toScreen(q));
        dl->AddPolyline(scr.data(), static_cast<int>(scr.size()), kAccent, ImDrawFlags_None, 2.0f);
        const ImVec2 cur = toScreen(snap(mw));
        dl->AddLine(scr.back(), cur, IM_COL32(222, 160, 72, 160), 1.5f);
        for (const auto& s : scr) dl->AddRectFilled({s.x - 3, s.y - 3}, {s.x + 3, s.y + 3}, kAccent);
        char text[32];
        std::snprintf(text, sizeof(text), "%.1f cm", static_cast<double>(glm::distance(drawing_.back(), snap(mw)) * 100.0f));
        dl->AddText({cur.x + 12, cur.y + 8}, kInk, text);
    }
    // Pièce symétrique en cours d'édition : aperçu du contour reconstruit en miroir
    if (drag_ == Drag::Point && selectedPanel >= 0 && selectedPanel < static_cast<int>(g.panels.size()) &&
        g.panels[static_cast<std::size_t>(selectedPanel)].symmetric && selectedPoint >= 0) {
        const auto& pn = g.panels[static_cast<std::size_t>(selectedPanel)];
        if (selectedPoint < static_cast<int>(pn.points.size())) {
            const auto preview = garment::symmetrizedOutline(pn, pn.points[static_cast<std::size_t>(selectedPoint)].x <= pn.symAxisX);
            std::vector<ImVec2> ps;
            for (const auto& q : preview) ps.push_back(toScreen(q));
            if (ps.size() >= 3) dl->AddPolyline(ps.data(), static_cast<int>(ps.size()), IM_COL32(120, 200, 255, 230), ImDrawFlags_Closed, 2.0f);
        }
    }
    // Aimant accroché : petit repère sur le point visé
    if (snapped_ && (drag_ == Drag::Point || drag_ == Drag::Panel || drag_ == Drag::Rect || tool == Tool::Polygon)) {
        const ImVec2 sp = toScreen(snapPos_);
        dl->AddCircle(sp, 7.0f, IM_COL32(255, 90, 200, 230), 16, 2.0f);
    }
    if (drag_ == Drag::Marquee) {
        const ImVec2 a = toScreen(dragStart_), b = toScreen(dragLast_);
        dl->AddRectFilled(a, b, IM_COL32(120, 170, 230, 36));
        dl->AddRect(a, b, IM_COL32(120, 170, 230, 220), 0.0f, 0, 1.5f);
    }
    if (drag_ == Drag::Rect) {
        const ImVec2 a = toScreen(dragStart_), b = toScreen(dragLast_);
        dl->AddRectFilled(a, b, IM_COL32(222, 160, 72, 40));
        dl->AddRect(a, b, kAccent, 0.0f, 0, 2.0f);
        char text[48];
        std::snprintf(text, sizeof(text), "%.1f x %.1f cm", static_cast<double>(std::abs(dragLast_.x - dragStart_.x) * 100.0f),
                      static_cast<double>(std::abs(dragLast_.y - dragStart_.y) * 100.0f));
        dl->AddText({b.x + 10, b.y + 6}, kInk, text);
    }

    // Coordonnées du curseur
    if (hovered) {
        char text[48];
        std::snprintf(text, sizeof(text), "x %.1f  y %.1f cm", static_cast<double>(mw.x * 100.0f), static_cast<double>(mw.y * 100.0f));
        const ImVec2 ts = ImGui::CalcTextSize(text);
        dl->AddText({c1.x - ts.x - 12, c1.y - ts.y - 8}, kMuted, text);
    }
    dl->PopClipRect();
    return committed;
}

} // namespace drapeo::ui
