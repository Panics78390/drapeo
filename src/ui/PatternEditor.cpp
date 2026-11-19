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
    if (!shift_) return w;
    return glm::round(w * 100.0f) / 100.0f;   // grille de 1 cm
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

std::string PatternEditor::hint() const
{
    switch (tool) {
    case Tool::Select:
        if (backdrop.selected)
            return "Image de fond sélectionnée : glisser pour la déplacer, Suppr pour la retirer.";
        return "Sélection : glisser une pièce ou un point. Double-clic sur un bord : ajouter un point. Suppr : supprimer.";
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
                if (hitPoint >= 0) {
                    selectedPanel = hitPanel;
                    selectedPoint = hitPoint;
                    drag_ = Drag::Point;
                } else if (const int p = pickPanel(g, mw); p >= 0) {
                    selectedPanel = p;
                    selectedPoint = -1;
                    drag_ = Drag::Panel;
                } else {
                    selectedPanel = selectedPoint = -1;
                }
                // Clic dans le vide sur l'image de fond : on la sélectionne pour la déplacer.
                backdrop.selected = false;
                if (selectedPanel < 0 && backdrop.texture != 0 && backdrop.visible &&
                    mw.x >= backdrop.min.x && mw.x <= backdrop.max.x && mw.y >= backdrop.min.y && mw.y <= backdrop.max.y) {
                    backdrop.selected = true;
                    drag_ = Drag::Backdrop;
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
            g.insertPoint(hoverEdge_.panel, hoverEdge_.edge, a + (b - a) * edgeT);
            selectedPanel = hoverEdge_.panel;
            selectedPoint = hoverEdge_.edge + 1;
            drag_ = Drag::None;
            committed = true;
        }
    }

    // Glisser
    if (drag_ == Drag::Pan) {
        center_ -= glm::vec2(io.MouseDelta.x, -io.MouseDelta.y) / zoom_;
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Middle) && !ImGui::IsMouseDown(ImGuiMouseButton_Right)) drag_ = Drag::None;
    } else if (drag_ == Drag::Point || drag_ == Drag::Panel) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            if (selectedPanel >= 0 && selectedPanel < static_cast<int>(g.panels.size())) {
                auto& pts = g.panels[static_cast<std::size_t>(selectedPanel)].points;
                if (drag_ == Drag::Point && selectedPoint >= 0 && selectedPoint < static_cast<int>(pts.size())) {
                    pts[static_cast<std::size_t>(selectedPoint)] = snap(mw);
                } else if (drag_ == Drag::Panel) {
                    const glm::vec2 d = mw - dragLast_;
                    for (auto& q : pts) q += d;   // déplacer la pièce ne change pas le vêtement 3D
                }
            }
            dragLast_ = mw;
        } else {
            if (drag_ == Drag::Point && glm::distance(dragStart_, mw) > 1e-4f) committed = true;
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
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
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
            } else if (selectedSeam >= 0 && selectedSeam < static_cast<int>(g.seams.size())) {
                g.seams.erase(g.seams.begin() + selectedSeam);
                selectedSeam = -1;
                committed = true;
            } else if (selectedPanel >= 0 && selectedPoint >= 0 &&
                       g.panels[static_cast<std::size_t>(selectedPanel)].points.size() > 3) {
                g.removePoint(selectedPanel, selectedPoint);
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
        const bool sel = static_cast<int>(p) == selectedPanel;
        // Remplissage : triangulation en éventail impossible pour les pièces
        // concaves, on utilise le remplissage de polygone concave d'ImGui.
        dl->AddConcavePolyFilled(scr.data(), static_cast<int>(scr.size()), rgba(panel.color, sel ? 0.42f : 0.28f));
        dl->AddPolyline(scr.data(), static_cast<int>(scr.size()), sel ? kAccent : rgba(panel.color * 0.6f + 0.4f, 1.0f),
                        ImDrawFlags_Closed, sel ? 2.2f : 1.4f);
        const glm::vec2 c = garment::centroid(panel.points);
        const ImVec2 sc = toScreen(c);
        const ImVec2 ts = ImGui::CalcTextSize(panel.name.c_str());
        dl->AddText({sc.x - ts.x * 0.5f, sc.y - ts.y * 0.5f}, kInk, panel.name.c_str());
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
