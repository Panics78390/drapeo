#include "ui/PatternEditor.h"
#include "garment/PatternOps.h"

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

std::vector<glm::vec2> PatternEditor::alignTargets(const garment::Garment& g, const std::vector<int>& skip) const
{
    std::vector<glm::vec2> out;
    for (int p = 0; p < static_cast<int>(g.panels.size()); ++p) {
        if (std::find(skip.begin(), skip.end(), p) != skip.end()) continue;
        const auto& pts = g.panels[static_cast<std::size_t>(p)].points;
        glm::vec2 lo(1e9f), hi(-1e9f);
        for (int i = 0; i < static_cast<int>(pts.size()); ++i) {
            lo = glm::min(lo, pts[static_cast<std::size_t>(i)]);
            hi = glm::max(hi, pts[static_cast<std::size_t>(i)]);
            // Point en cours de glisser (et son miroir) : pas une référence.
            if (drag_ == Drag::Point && p == selectedPanel && (i == selectedPoint || i == symPartner_)) continue;
            if (drag_ == Drag::Points && pointChosen(p, i)) continue;
            out.push_back(pts[static_cast<std::size_t>(i)]);
        }
        if (!pts.empty()) out.push_back((lo + hi) * 0.5f);   // centre de la pièce
    }
    for (const auto& q : drawing_) out.push_back(q);         // polygone en cours : ses propres points
    return out;
}

glm::vec2 PatternEditor::alignTo(const std::vector<glm::vec2>& moving, const std::vector<glm::vec2>& others) const
{
    guides_.clear();
    const float radius = kPickRadius / zoom_;
    float bx = radius, by = radius;
    glm::vec2 corr(0.0f);
    bool hx = false, hy = false;
    for (const auto& m : moving)
        for (const auto& o : others) {
            if (const float d = std::abs(o.x - m.x); d < bx) {
                bx = d;
                corr.x = o.x - m.x;
                hx = true;
            }
            if (const float d = std::abs(o.y - m.y); d < by) {
                by = d;
                corr.y = o.y - m.y;
                hy = true;
            }
        }
    if (!hx && !hy) return corr;
    const float eps = 2e-5f;
    for (int axis = 0; axis < 2; ++axis) {
        if ((axis == 0 && !hx) || (axis == 1 && !hy)) continue;
        // Toutes les valeurs alignées (haut, bas, centre...) : un guide pour chacune.
        const int other = 1 - axis;
        std::vector<float> values;
        for (const auto& m : moving) {
            const float v = m[axis] + corr[axis];
            bool shared = false;
            for (const auto& o : others) shared = shared || std::abs(o[axis] - v) < eps;
            bool known = false;
            for (const float k : values) known = known || std::abs(k - v) < eps;
            if (shared && !known) values.push_back(v);
        }
        glm::vec2 bestM(0.0f), bestO(0.0f);
        float bestGap = 1e9f;
        for (const float value : values) {
            float lo = 1e9f, hi = -1e9f;
            for (const auto& m : moving) {
                const glm::vec2 q = m + corr;
                if (std::abs(q[axis] - value) >= eps) continue;
                lo = std::min(lo, q[other]);
                hi = std::max(hi, q[other]);
                for (const auto& o : others) {
                    if (std::abs(o[axis] - value) >= eps) continue;
                    lo = std::min(lo, o[other]);
                    hi = std::max(hi, o[other]);
                    if (const float gap = std::abs(o[other] - q[other]); gap > eps && gap < bestGap) {
                        bestGap = gap;
                        bestM = q;
                        bestO = o;
                    }
                }
            }
            const float ext = 0.03f;   // le guide dépasse un peu des points
            Guide gl;
            gl.a[axis] = gl.b[axis] = value;
            gl.a[other] = lo - ext;
            gl.b[other] = hi + ext;
            guides_.push_back(gl);
        }
        if (bestGap < 1e8f) guides_.push_back({bestM, bestO, true});
    }
    return corr;
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
        if (!snapped_) {   // guides d'alignement avec les points des pièces (et de la pièce elle-même)
            const glm::vec2 corr = alignTo({w}, alignTargets(*g_, {}));
            if (!guides_.empty()) return w + corr;
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
    if (snapped_) {
        guides_.clear();
        return corr;
    }
    // Pas de point à accrocher : guides d'alignement (bords, sommets, centres des autres pièces).
    std::vector<glm::vec2> mv;
    for (const int m : ids) {
        if (m < 0 || m >= static_cast<int>(g.panels.size())) continue;
        glm::vec2 lo(1e9f), hi(-1e9f);
        for (const auto& v0 : g.panels[static_cast<std::size_t>(m)].points) {
            mv.push_back(v0 + delta);
            lo = glm::min(lo, v0 + delta);
            hi = glm::max(hi, v0 + delta);
        }
        mv.push_back((lo + hi) * 0.5f);
    }
    return alignTo(mv, alignTargets(g, ids));
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
    if (lo.x > hi.x && silhouette.texture != 0 && silhouette.visible) {   // patron vide : on cadre la silhouette
        lo = silhouette.min;
        hi = silhouette.max;
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

bool PatternEditor::transformBox(const garment::Garment& g, glm::vec2& lo, glm::vec2& hi) const
{
    lo = glm::vec2(1e9f);
    hi = glm::vec2(-1e9f);
    if (selectedPoints.size() > 1 && selectedPanel >= 0 && selectedPanel < static_cast<int>(g.panels.size())) {
        const auto& pts = g.panels[static_cast<std::size_t>(selectedPanel)].points;
        for (const int i : selectedPoints)
            if (i >= 0 && i < static_cast<int>(pts.size())) {
                lo = glm::min(lo, pts[static_cast<std::size_t>(i)]);
                hi = glm::max(hi, pts[static_cast<std::size_t>(i)]);
            }
    } else {
        for (const int p : targets())
            if (p >= 0 && p < static_cast<int>(g.panels.size()))
                for (const auto& q : g.panels[static_cast<std::size_t>(p)].points) {
                    lo = glm::min(lo, q);
                    hi = glm::max(hi, q);
                }
    }
    if (lo.x > hi.x) return false;
    // Cadre jamais plat (arête horizontale ou verticale) : 1 mm minimum.
    const glm::vec2 c = (lo + hi) * 0.5f;
    lo = glm::min(lo, c - glm::vec2(0.0005f));
    hi = glm::max(hi, c + glm::vec2(0.0005f));
    return true;
}

void PatternEditor::beginTransform(const garment::Garment& g)
{
    tOrig_.clear();
    tAttach_.clear();
    if (selectedPoints.size() > 1) {
        const auto& pts = g.panels[static_cast<std::size_t>(selectedPanel)].points;
        for (const int i : selectedPoints) tOrig_.push_back({selectedPanel, i, pts[static_cast<std::size_t>(i)]});
    } else {
        const auto ids = targets();
        for (const int p : ids) {
            const auto& pts = g.panels[static_cast<std::size_t>(p)].points;
            for (int i = 0; i < static_cast<int>(pts.size()); ++i) tOrig_.push_back({p, i, pts[static_cast<std::size_t>(i)]});
        }
        for (int a = 0; a < static_cast<int>(g.attachments.size()); ++a)
            if (std::find(ids.begin(), ids.end(), g.attachments[static_cast<std::size_t>(a)].base) != ids.end())
                tAttach_.push_back({a, g.attachments[static_cast<std::size_t>(a)].at});
    }
    transformBox(g, tLo_, tHi_);
}

int PatternEditor::insertOnEdge(garment::Garment& g, garment::EdgeRef e, float t)
{
    if (e.panel < 0 || e.panel >= static_cast<int>(g.panels.size())) return -1;
    const glm::vec2 a = g.edgeStart(e), b = g.edgeEnd(e);
    g.insertPoint(e.panel, e.edge, a + (b - a) * std::clamp(t, 0.02f, 0.98f));
    return e.edge + 1;
}

void PatternEditor::placeNewPanel(garment::Panel& panel) const
{
    if (panel.points.empty() || silhouette.texture == 0) return;
    glm::vec2 lo(1e9f), hi(-1e9f);
    for (const auto& q : panel.points) {
        lo = glm::min(lo, q);
        hi = glm::max(hi, q);
    }
    // La silhouette est à l'échelle réelle (pieds à y = 0) : la pièce tracée devant le buste y est posée en 3D.
    const float scale = std::max(bodyScale, 0.1f);
    panel.placement.height = lo.y / scale;
    panel.placement.offset.x = (lo.x + hi.x) * 0.5f - torsoCenterX * scale;
}

bool PatternEditor::pointChosen(int panel, int point) const
{
    return panel == selectedPanel && std::find(selectedPoints.begin(), selectedPoints.end(), point) != selectedPoints.end();
}

void PatternEditor::movePoints(garment::Garment& g, glm::vec2 d)
{
    if (selectedPanel < 0 || selectedPanel >= static_cast<int>(g.panels.size())) return;
    auto& pts = g.panels[static_cast<std::size_t>(selectedPanel)].points;
    for (const int i : selectedPoints)
        if (i >= 0 && i < static_cast<int>(pts.size())) pts[static_cast<std::size_t>(i)] += d;
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
    selectedPoint = selectedSeam = selectedEdge = -1;
    selectedPoints.clear();
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
        if (selectedEdge >= 0)
            return "Arête choisie : glisser pour la déplacer, flèches pour l'ajuster (Maj : 1 cm). Double-clic : ajouter un point.";
        if (selectedPoints.size() > 1)
            return std::to_string(selectedPoints.size()) + " points choisis : glisser l'un d'eux pour les déplacer, flèches (Maj : 1 cm), Suppr pour les supprimer.";
        return "Sélection : glisser une pièce, un point ou une arête. Maj+clic : plusieurs points. Ctrl+rectangle : points. Rectangle dans le vide : pièces. Double-clic sur un bord : ajouter un point.";
    case Tool::Transform:
        return "Transformer : coin du cadre = agrandir (Maj : proportions), rond du haut = pivoter (Maj : 15°). Ctrl+rectangle : points.";
    case Tool::CurvePoint:
        return "Points de courbe : clic sur un bord pour y créer un point et le glisser, clic sur un point pour le déplacer.";
    case Tool::Curvature:
        return "Courbure : glissez un bord pour le courber (il passe par la souris). Lisser (L) adoucit ensuite.";
    case Tool::Smooth:
        return "Lisser : maintenez le clic et passez le pinceau sur le contour pour l'adoucir.";
    case Tool::AddPoint:
        return "Ajouter un point : clic sur un bord pour le couper en deux à cet endroit.";
    case Tool::Slash:
        return slashPanel_ < 0 ? "Fendre et écarter : cliquez le point ou le bord où la pièce s'ouvre."
                               : "Fendre et écarter : cliquez maintenant le point charnière (la pièce pivote autour de lui).";
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
    if (drag_ != Drag::Point && drag_ != Drag::Points && drag_ != Drag::Panel && drag_ != Drag::Rect && tool != Tool::Polygon) guides_.clear();
    // Points choisis périmés (autre pièce, points supprimés) : on les oublie.
    if (!selectedPoints.empty() && (selectedPanel < 0 || selectedPanel >= static_cast<int>(g.panels.size()) ||
                                    std::any_of(selectedPoints.begin(), selectedPoints.end(), [&](int i) {
                                        return i < 0 || i >= static_cast<int>(g.panels[static_cast<std::size_t>(selectedPanel)].points.size());
                                    }))) {
        selectedPoints.clear();
        selectedEdge = -1;
    }
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
    // Silhouette de l'avatar (ombre de face) sous tout le reste
    if (silhouette.texture != 0 && silhouette.visible) {
        dl->AddImage(silhouette.texture, toScreen({silhouette.min.x, silhouette.max.y}), toScreen({silhouette.max.x, silhouette.min.y}),
                     ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, 255));
        const ImVec2 a = toScreen({0.0f, silhouette.min.y}), b = toScreen({0.0f, silhouette.max.y});
        dl->AddLine(a, b, IM_COL32(255, 255, 255, 22), 1.0f);   // axe du corps
    }
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
            // Outil Transformer : poignées du cadre de la sélection
            bool handled = false;
            if (tool == Tool::Transform) {
                glm::vec2 lo, hi;
                if (transformBox(g, lo, hi)) {
                    const ImVec2 corners[4] = {toScreen({lo.x, lo.y}), toScreen({hi.x, lo.y}), toScreen({hi.x, hi.y}), toScreen({lo.x, hi.y})};
                    const glm::vec2 wc[4] = {{lo.x, lo.y}, {hi.x, lo.y}, {hi.x, hi.y}, {lo.x, hi.y}};
                    const ImVec2 rot(toScreen({(lo.x + hi.x) * 0.5f, hi.y}).x, toScreen({0.0f, hi.y}).y - 26.0f);
                    if (std::hypot(mouse.x - rot.x, mouse.y - rot.y) < kPickRadius + 2.0f) {
                        beginTransform(g);
                        drag_ = Drag::Rotate;
                        tAnchor_ = (lo + hi) * 0.5f;
                        tGrab_ = mw;
                        handled = true;
                    }
                    for (int k = 0; k < 4 && !handled; ++k)
                        if (std::hypot(mouse.x - corners[k].x, mouse.y - corners[k].y) < kPickRadius + 2.0f) {
                            beginTransform(g);
                            drag_ = Drag::Scale;
                            tAnchor_ = wc[(k + 2) % 4];   // coin opposé fixe
                            tGrab_ = wc[k];
                            handled = true;
                        }
                    if (!handled && selectedPoints.size() > 1 && mw.x >= lo.x && mw.x <= hi.x && mw.y >= lo.y && mw.y <= hi.y &&
                        pickPoint(g, selectedPanel, mouse) < 0) {
                        beginTransform(g);   // dans le cadre des points choisis : les déplacer
                        drag_ = Drag::Move;
                        tGrab_ = mw;
                        handled = true;
                    }
                }
            }
            if (!handled) switch (tool) {
            case Tool::CurvePoint:
            case Tool::AddPoint: {
                int hitPoint = selectedPanel >= 0 && selectedPanel < static_cast<int>(g.panels.size()) ? pickPoint(g, selectedPanel, mouse) : -1;
                int hitPanel = hitPoint >= 0 ? selectedPanel : -1;
                for (int p = 0; hitPoint < 0 && p < static_cast<int>(g.panels.size()); ++p)
                    if ((hitPoint = pickPoint(g, p, mouse)) >= 0) hitPanel = p;
                if (hitPoint < 0 && hoverEdge_.panel >= 0) {
                    // Nouveau point sur le bord, à l'endroit cliqué
                    hitPanel = hoverEdge_.panel;
                    hitPoint = insertOnEdge(g, hoverEdge_, edgeT);
                    applySymmetry(g, hitPanel, hitPoint);
                    committed = true;
                }
                if (hitPoint >= 0) {
                    selectedPanel = hitPanel;
                    selectedPoint = hitPoint;
                    selectedPoints.clear();
                    selectedEdge = -1;
                    selection.clear();
                    if (tool == Tool::CurvePoint) {   // le point se glisse aussitôt
                        drag_ = Drag::Point;
                        snapExcludePanel_ = hitPanel;
                        const auto& hp = g.panels[static_cast<std::size_t>(hitPanel)];
                        symPartner_ = garment::symmetricPartner(hp, hitPoint);
                        symOnAxis_ = hp.symmetric && std::abs(hp.points[static_cast<std::size_t>(hitPoint)].x - hp.symAxisX) < 0.0015f;
                    }
                }
                dragStart_ = dragLast_ = mw;
                break;
            }
            case Tool::Curvature:
                if (hoverEdge_.panel >= 0) {
                    curveEdge_ = hoverEdge_;
                    curveA_ = g.edgeStart(hoverEdge_);
                    curveB_ = g.edgeEnd(hoverEdge_);
                    curveFirst_ = -1;   // points ajoutés au premier mouvement
                    curveCount_ = 0;
                    selectedPanel = hoverEdge_.panel;
                    selection.clear();
                    selectedPoints.clear();
                    selectedPoint = selectedEdge = -1;
                    drag_ = Drag::Curve;
                    dragStart_ = dragLast_ = mw;
                }
                break;
            case Tool::Smooth:
                drag_ = Drag::Smooth;
                dragStart_ = dragLast_ = mw;
                break;
            case Tool::Slash: {
                // 1er clic : départ de la fente (point ou bord) ; 2e clic : point charnière sur la même pièce.
                int panel = -1, point = -1;
                for (int p = 0; point < 0 && p < static_cast<int>(g.panels.size()); ++p)
                    if ((point = pickPoint(g, p, mouse)) >= 0) panel = p;
                if (point < 0 && hoverEdge_.panel >= 0) {
                    panel = hoverEdge_.panel;
                    point = insertOnEdge(g, hoverEdge_, edgeT);
                    if (panel == slashPanel_ && slashPoint_ > hoverEdge_.edge) ++slashPoint_;   // index décalé par le nouveau point
                    committed = true;
                }
                if (point < 0) break;
                if (slashPanel_ < 0 || panel != slashPanel_) {
                    slashPanel_ = panel;
                    slashPoint_ = point;
                    selectedPanel = panel;
                    selection.clear();
                } else if (point != slashPoint_) {
                    if (garment::slashAndSpread(g, panel, slashPoint_, point, spreadDeg)) committed = true;
                    slashPanel_ = slashPoint_ = -1;
                }
                break;
            }
            case Tool::Transform:
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
                if (hitPoint >= 0 && shift) {
                    // Maj + clic sur un point : l'ajoute ou le retire des points choisis.
                    if (hitPanel != selectedPanel) selectedPoints.clear();
                    if (selectedPoints.empty() && selectedPoint >= 0 && hitPanel == selectedPanel) selectedPoints.push_back(selectedPoint);
                    selectedPanel = hitPanel;
                    selection.clear();
                    selectedEdge = -1;
                    if (const auto it = std::find(selectedPoints.begin(), selectedPoints.end(), hitPoint); it != selectedPoints.end())
                        selectedPoints.erase(it);
                    else
                        selectedPoints.push_back(hitPoint);
                    selectedPoint = selectedPoints.empty() ? -1 : selectedPoints.back();
                    backdrop.selected = false;
                } else if (hitPoint >= 0 && selectedPoints.size() > 1 && pointChosen(hitPanel, hitPoint)) {
                    // Clic sur un des points choisis : on les déplace tous.
                    drag_ = Drag::Points;
                    dragApplied_ = glm::vec2(0.0f);
                    snapExcludePanel_ = hitPanel;
                } else if (hitPoint >= 0) {
                    selectedPanel = hitPanel;
                    selectedPoint = hitPoint;
                    selectedPoints.clear();
                    selectedEdge = -1;
                    selection.clear();
                    drag_ = Drag::Point;
                    snapExcludePanel_ = hitPanel;
                    const auto& hp = g.panels[static_cast<std::size_t>(hitPanel)];
                    symPartner_ = garment::symmetricPartner(hp, hitPoint);
                    symOnAxis_ = hp.symmetric && std::abs(hp.points[static_cast<std::size_t>(hitPoint)].x - hp.symAxisX) < 0.0015f;
                    backdrop.selected = false;
                } else if (hoverEdge_.panel >= 0 && !(shift && selectedPanel != hoverEdge_.panel && selectedPoints.empty() && false)) {
                    // Clic sur une arête : on la choisit (ses deux points) et on peut la glisser.
                    const int ep = hoverEdge_.panel, e = hoverEdge_.edge;
                    const int n = static_cast<int>(g.panels[static_cast<std::size_t>(ep)].points.size());
                    if (!shift || ep != selectedPanel) selectedPoints.clear();
                    selectedPanel = ep;
                    selection.clear();
                    for (const int i : {e, (e + 1) % n})
                        if (!pointChosen(ep, i)) selectedPoints.push_back(i);
                    selectedEdge = shift ? -1 : e;
                    selectedPoint = -1;
                    backdrop.selected = false;
                    drag_ = Drag::Points;
                    dragApplied_ = glm::vec2(0.0f);
                    snapExcludePanel_ = ep;
                } else if (const int p = pickPanel(g, mw); p >= 0) {
                    selectedPoint = -1;
                    selectedPoints.clear();
                    selectedEdge = -1;
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
                    // Clic dans le vide : rectangle de sélection (Ctrl : points de la pièce choisie).
                    pointMarquee_ = io.KeyCtrl;
                    if (!shift && !pointMarquee_) {
                        selectedPanel = selectedPoint = -1;
                        selection.clear();
                    }
                    if (!shift) {
                        selectedPoints.clear();
                        selectedEdge = -1;
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
                        panel.placement.flat = true;   // nouvelle pièce : posée à plat
                        panel.points = drawing_;
                        placeNewPanel(panel);
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
            } else if (pointMarquee_) {
                // Ctrl + rectangle : points de la pièce choisie (ou de la pièce qui en a le plus dans le rectangle).
                const glm::vec2 lo = glm::min(dragStart_, dragLast_), hi = glm::max(dragStart_, dragLast_);
                const auto inside = [&](glm::vec2 q) { return q.x >= lo.x && q.x <= hi.x && q.y >= lo.y && q.y <= hi.y; };
                int panel = selectedPanel, best = 0;
                for (int p = 0; p < static_cast<int>(g.panels.size()); ++p) {
                    int count = 0;
                    for (const auto& q : g.panels[static_cast<std::size_t>(p)].points) count += inside(q) ? 1 : 0;
                    if (count > best && (p == selectedPanel || selectedPanel < 0 || panel < 0 || best == 0)) {
                        best = count;
                        panel = p;
                    }
                }
                if (panel >= 0 && panel < static_cast<int>(g.panels.size())) {
                    if (panel != selectedPanel) selectedPoints.clear();
                    selectedPanel = panel;
                    selection.clear();
                    const auto& pts = g.panels[static_cast<std::size_t>(panel)].points;
                    for (int i = 0; i < static_cast<int>(pts.size()); ++i)
                        if (inside(pts[static_cast<std::size_t>(i)]) && !pointChosen(panel, i)) selectedPoints.push_back(i);
                    selectedPoint = selectedPoints.size() == 1 ? selectedPoints[0] : -1;
                    selectedEdge = -1;
                }
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
    } else if (drag_ == Drag::Scale || drag_ == Drag::Rotate || drag_ == Drag::Move) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            // Transformer : on repart des positions du début, à chaque image.
            glm::vec2 sc(1.0f);
            float ang = 0.0f;
            glm::vec2 move(0.0f);
            if (drag_ == Drag::Scale) {
                const glm::vec2 from = tGrab_ - tAnchor_, to = mw - tAnchor_;
                sc.x = std::abs(from.x) > 1e-4f ? to.x / from.x : 1.0f;
                sc.y = std::abs(from.y) > 1e-4f ? to.y / from.y : 1.0f;
                if (shift_) sc = glm::vec2(std::max(std::abs(sc.x), std::abs(sc.y)) * (sc.x < 0.0f ? -1.0f : 1.0f));   // proportions gardées
                sc = glm::clamp(sc, glm::vec2(-20.0f), glm::vec2(20.0f));
                if (std::abs(sc.x) < 0.02f) sc.x = 0.02f;
                if (std::abs(sc.y) < 0.02f) sc.y = 0.02f;
            } else if (drag_ == Drag::Rotate) {
                ang = std::atan2(mw.y - tAnchor_.y, mw.x - tAnchor_.x) - std::atan2(tGrab_.y - tAnchor_.y, tGrab_.x - tAnchor_.x);
                if (shift_) ang = glm::radians(std::round(glm::degrees(ang) / 15.0f) * 15.0f);   // pas de 15°
            } else {
                move = mw - tGrab_;
                if (shift_) move = glm::round(move * 100.0f) / 100.0f;
            }
            const float c = std::cos(ang), sn = std::sin(ang);
            auto apply = [&](glm::vec2 q) {
                if (drag_ == Drag::Scale) return tAnchor_ + (q - tAnchor_) * sc;
                if (drag_ == Drag::Rotate) {
                    const glm::vec2 d = q - tAnchor_;
                    return tAnchor_ + glm::vec2(c * d.x - sn * d.y, sn * d.x + c * d.y);
                }
                return q + move;
            };
            for (const auto& tp : tOrig_)
                if (tp.panel < static_cast<int>(g.panels.size()) && tp.index < static_cast<int>(g.panels[static_cast<std::size_t>(tp.panel)].points.size()))
                    g.panels[static_cast<std::size_t>(tp.panel)].points[static_cast<std::size_t>(tp.index)] = apply(tp.p);
            for (const auto& [a, at] : tAttach_)
                if (a < static_cast<int>(g.attachments.size())) g.attachments[static_cast<std::size_t>(a)].at = apply(at);
            dragApplied_ = drag_ == Drag::Rotate ? glm::vec2(ang, 0.0f) : glm::vec2(sc.x - 1.0f + move.x, sc.y - 1.0f + move.y);
            dragLast_ = mw;
        } else {
            if (glm::length(dragApplied_) > 1e-6f) {
                committed = true;
                if (selectedPoints.size() > 1) {
                    applySymmetry(g, selectedPanel, selectedPoints.front());
                } else {
                    // Pièces entières : droit fil et axe de symétrie suivent.
                    std::vector<int> ids;
                    for (const auto& tp : tOrig_)
                        if (std::find(ids.begin(), ids.end(), tp.panel) == ids.end()) ids.push_back(tp.panel);
                    for (const int p : ids) {
                        auto& pn = g.panels[static_cast<std::size_t>(p)];
                        if (drag_ == Drag::Rotate) {
                            pn.grainDeg += glm::degrees(dragApplied_.x);
                            pn.linkAngle += glm::degrees(dragApplied_.x);
                            if (pn.symmetric) pn.symmetric = false;   // l'axe n'est plus vertical
                        } else if (pn.symmetric) {
                            pn.symAxisX = garment::centroid(pn.points).x;   // pièce symétrique : axe au milieu
                        }
                    }
                }
            }
            dragApplied_ = glm::vec2(0.0f);
            drag_ = Drag::None;
        }
    } else if (drag_ == Drag::Curve) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            const ImVec2 s0 = toScreen(dragStart_);
            if (curveFirst_ < 0 && std::hypot(mouse.x - s0.x, mouse.y - s0.y) > 3.0f && curveEdge_.panel >= 0) {
                // Le bord est découpé en 8 segments qui suivront la courbe.
                constexpr int kSegs = 8;
                for (int k = 1; k < kSegs; ++k)
                    g.insertPoint(curveEdge_.panel, curveEdge_.edge + k - 1, curveA_ + (curveB_ - curveA_) * (static_cast<float>(k) / kSegs));
                curveFirst_ = curveEdge_.edge + 1;
                curveCount_ = kSegs - 1;
            }
            if (curveFirst_ >= 0) {
                // Courbe de Bézier qui passe par la souris en son milieu.
                const glm::vec2 ctrl = 2.0f * mw - (curveA_ + curveB_) * 0.5f;
                auto& pts = g.panels[static_cast<std::size_t>(curveEdge_.panel)].points;
                for (int k = 0; k < curveCount_; ++k) {
                    const float t = static_cast<float>(k + 1) / static_cast<float>(curveCount_ + 1);
                    const std::size_t i = static_cast<std::size_t>(curveFirst_ + k);
                    if (i < pts.size()) pts[i] = (1 - t) * (1 - t) * curveA_ + 2 * t * (1 - t) * ctrl + t * t * curveB_;
                }
            }
            dragLast_ = mw;
        } else {
            if (curveFirst_ >= 0) {
                committed = true;
                applySymmetry(g, curveEdge_.panel, curveFirst_ + curveCount_ / 2);
            }
            curveFirst_ = -1;
            curveEdge_ = {};
            drag_ = Drag::None;
        }
    } else if (drag_ == Drag::Smooth) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            // Pinceau : chaque point sous le pinceau se rapproche du milieu de ses voisins.
            const float radius = 28.0f / zoom_;
            for (std::size_t p = 0; p < g.panels.size(); ++p) {
                auto& pts = g.panels[p].points;
                const std::size_t n = pts.size();
                if (n < 4) continue;
                const auto old = pts;
                for (std::size_t i = 0; i < n; ++i) {
                    const float d = glm::distance(old[i], mw);
                    if (d > radius) continue;
                    const float k = 0.18f * (1.0f - d / radius);
                    const glm::vec2 mid = (old[(i + n - 1) % n] + old[(i + 1) % n]) * 0.5f;
                    pts[i] += (mid - old[i]) * k;
                    dragApplied_ = glm::vec2(1.0f);
                }
            }
            dragLast_ = mw;
        } else {
            if (dragApplied_ != glm::vec2(0.0f)) {
                committed = true;
                for (std::size_t p = 0; p < g.panels.size(); ++p)
                    if (g.panels[p].symmetric) garment::symmetrizePanel(g, static_cast<int>(p), mw.x <= g.panels[p].symAxisX);
            }
            dragApplied_ = glm::vec2(0.0f);
            drag_ = Drag::None;
        }
    } else if (drag_ == Drag::Points) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            if (selectedPanel >= 0 && selectedPanel < static_cast<int>(g.panels.size())) {
                // Points / arête glissés : guides d'alignement avec les autres points.
                const auto& pts = g.panels[static_cast<std::size_t>(selectedPanel)].points;
                const glm::vec2 want = mw - dragStart_;
                glm::vec2 total = want;
                if (shift_) total = glm::round(want * 100.0f) / 100.0f;   // pas de 1 cm
                else if (magnet) {
                    std::vector<glm::vec2> moving;
                    for (const int i : selectedPoints)
                        if (i >= 0 && i < static_cast<int>(pts.size())) moving.push_back(pts[static_cast<std::size_t>(i)] - dragApplied_ + want);
                    total = want + alignTo(moving, alignTargets(g, {}));
                }
                movePoints(g, total - dragApplied_);
                dragApplied_ = total;
            }
            dragLast_ = mw;
        } else {
            if (glm::length(dragApplied_) > 1e-6f) {
                committed = true;
                if (!selectedPoints.empty()) applySymmetry(g, selectedPanel, selectedPoints.front());
            }
            snapExcludePanel_ = -1;
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
                panel.placement.flat = true;   // nouvelle pièce : posée à plat
                panel.points = {lo, {hi.x, lo.y}, hi, {lo.x, hi.y}};
                placeNewPanel(panel);
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
                selectedPoints.clear();
                selectedEdge = -1;
                selection.clear();
                selectedPanel = selectedPoint = -1;
            }
            drawing_.clear();
            pendingSeam_ = {};
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Enter) && drawing_.size() >= 3) {
            garment::Panel panel;
            panel.name = "Pièce " + std::to_string(g.panels.size() + 1);
            panel.placement.flat = true;   // nouvelle pièce : posée à plat
            panel.points = drawing_;
            placeNewPanel(panel);
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
            } else if (selectedPoints.size() > 1 && selectedPanel >= 0 && selectedSeam < 0 &&
                       g.panels[static_cast<std::size_t>(selectedPanel)].points.size() >= selectedPoints.size() + 3) {
                // Points choisis : supprimés ensemble (il reste au moins 3 points).
                std::vector<int> doomed = selectedPoints;
                std::sort(doomed.rbegin(), doomed.rend());
                for (const int i : doomed) g.removePoint(selectedPanel, i);
                selectedPoints.clear();
                selectedEdge = selectedPoint = -1;
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
        // Flèches : déplacer les points choisis, le point ou les pièces de 1 mm (Maj : 1 cm).
        if (isEditTool(tool) && drag_ == Drag::None) {
            glm::vec2 step(0.0f);
            if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) step.x -= 1.0f;
            if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) step.x += 1.0f;
            if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) step.y += 1.0f;
            if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) step.y -= 1.0f;
            if (step != glm::vec2(0.0f) && selectedPanel >= 0 && selectedPanel < static_cast<int>(g.panels.size())) {
                step *= io.KeyShift ? 0.01f : 0.001f;
                if (!selectedPoints.empty()) {
                    movePoints(g, step);
                    applySymmetry(g, selectedPanel, selectedPoints.front());
                } else if (selectedPoint >= 0 && selectedPoint < static_cast<int>(g.panels[static_cast<std::size_t>(selectedPanel)].points.size())) {
                    g.panels[static_cast<std::size_t>(selectedPanel)].points[static_cast<std::size_t>(selectedPoint)] += step;
                    applySymmetry(g, selectedPanel, selectedPoint);
                } else {
                    movePanels(g, selection.empty() ? std::vector<int>{selectedPanel} : selection, step);
                }
                committed = true;
            }
        }
        if (!io.KeyCtrl) {
            if (ImGui::IsKeyPressed(ImGuiKey_Z)) tool = Tool::Select;
            if (ImGui::IsKeyPressed(ImGuiKey_T)) tool = Tool::Transform;
            if (ImGui::IsKeyPressed(ImGuiKey_V)) tool = Tool::CurvePoint;
            if (ImGui::IsKeyPressed(ImGuiKey_C)) tool = Tool::Curvature;
            if (ImGui::IsKeyPressed(ImGuiKey_L)) tool = Tool::Smooth;
            if (ImGui::IsKeyPressed(ImGuiKey_A)) tool = Tool::AddPoint;
            if (ImGui::IsKeyPressed(ImGuiKey_E)) tool = Tool::Slash;
            if (ImGui::IsKeyPressed(ImGuiKey_I)) traceRequested = true;
            if (ImGui::IsKeyPressed(ImGuiKey_P)) tool = Tool::Polygon;
            if (ImGui::IsKeyPressed(ImGuiKey_B)) tool = Tool::Rectangle;
            if (ImGui::IsKeyPressed(ImGuiKey_S)) tool = Tool::Seam;
            if (ImGui::IsKeyPressed(ImGuiKey_F)) frameAll(g);
            if (ImGui::IsKeyPressed(ImGuiKey_M)) magnet = !magnet;
        }
    }
    if (tool != Tool::Polygon) drawing_.clear();
    if (tool != Tool::Slash) slashPanel_ = slashPoint_ = -1;
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
            const bool ptSel = sel && (static_cast<int>(i) == selectedPoint || pointChosen(static_cast<int>(p), static_cast<int>(i)));
            const float r = ptSel ? 5.0f : 3.5f;
            dl->AddRectFilled({s.x - r, s.y - r}, {s.x + r, s.y + r}, ptSel ? kAccent : (sel ? kInk : kMuted));
        }
    }

    // Arête choisie : trait épais + longueur
    if (selectedEdge >= 0 && selectedPanel >= 0 && selectedPanel < static_cast<int>(g.panels.size()) &&
        selectedEdge < static_cast<int>(g.panels[static_cast<std::size_t>(selectedPanel)].points.size())) {
        const garment::EdgeRef e{selectedPanel, selectedEdge};
        const ImVec2 a = toScreen(g.edgeStart(e)), b = toScreen(g.edgeEnd(e));
        dl->AddLine(a, b, kAccent, 4.0f);
        char text[32];
        std::snprintf(text, sizeof(text), "%.1f cm", static_cast<double>(g.edgeLength(e) * 100.0f));
        dl->AddText({(a.x + b.x) * 0.5f + 8, (a.y + b.y) * 0.5f - 18}, kAccent, text);
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
    // Guides d'alignement (magenta) et distance entre les points alignés
    if (!guides_.empty() && (drag_ == Drag::Point || drag_ == Drag::Points || drag_ == Drag::Panel || drag_ == Drag::Rect || tool == Tool::Polygon)) {
        const ImU32 kGuide = IM_COL32(225, 70, 225, 230);
        for (const auto& gd : guides_) {
            const ImVec2 a = toScreen(gd.a), b = toScreen(gd.b);
            if (!gd.measure) {
                dl->AddLine(a, b, kGuide, 1.2f);
                continue;
            }
            dl->AddCircleFilled(a, 3.0f, kGuide);
            dl->AddCircleFilled(b, 3.0f, kGuide);
            char text[32];
            std::snprintf(text, sizeof(text), "%.1f cm", static_cast<double>(glm::distance(gd.a, gd.b) * 100.0f));
            const ImVec2 ts = ImGui::CalcTextSize(text);
            const ImVec2 mid((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
            const bool horizontal = std::abs(b.x - a.x) >= std::abs(b.y - a.y);
            const ImVec2 at = horizontal ? ImVec2(mid.x - ts.x * 0.5f, mid.y - ts.y - 4.0f) : ImVec2(mid.x + 6.0f, mid.y - ts.y * 0.5f);
            dl->AddRectFilled({at.x - 3, at.y - 1}, {at.x + ts.x + 3, at.y + ts.y + 1}, IM_COL32(20, 20, 24, 210), 3.0f);
            dl->AddText(at, kGuide, text);
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

    // Outil Transformer : cadre, poignées d'échelle et poignée de rotation
    if (tool == Tool::Transform) {
        glm::vec2 lo, hi;
        if (transformBox(g, lo, hi)) {
            const ImU32 col = IM_COL32(120, 200, 255, 230);
            const ImVec2 corners[4] = {toScreen({lo.x, lo.y}), toScreen({hi.x, lo.y}), toScreen({hi.x, hi.y}), toScreen({lo.x, hi.y})};
            for (int k = 0; k < 4; ++k) {   // bord en pointillés
                const ImVec2 a = corners[k], b = corners[(k + 1) % 4];
                const float len = std::hypot(b.x - a.x, b.y - a.y);
                const int n = std::max(1, static_cast<int>(len / 6.0f));
                for (int s = 0; s < n; s += 2) {
                    const float t0 = static_cast<float>(s) / n, t1 = std::min(1.0f, static_cast<float>(s + 1) / n);
                    dl->AddLine({a.x + (b.x - a.x) * t0, a.y + (b.y - a.y) * t0}, {a.x + (b.x - a.x) * t1, a.y + (b.y - a.y) * t1}, col, 1.5f);
                }
            }
            for (const auto& c : corners) {
                dl->AddRectFilled({c.x - 5, c.y - 5}, {c.x + 5, c.y + 5}, IM_COL32(30, 32, 36, 255));
                dl->AddRect({c.x - 5, c.y - 5}, {c.x + 5, c.y + 5}, col, 0.0f, 0, 1.5f);
            }
            const ImVec2 top = toScreen({(lo.x + hi.x) * 0.5f, hi.y});
            const ImVec2 rot(top.x, top.y - 26.0f);
            dl->AddLine(top, rot, col, 1.5f);
            dl->AddCircleFilled(rot, 6.0f, IM_COL32(30, 32, 36, 255), 16);
            dl->AddCircle(rot, 6.0f, col, 16, 1.5f);
        }
    }
    // Outil Lisser : cercle du pinceau
    if (tool == Tool::Smooth && hovered) dl->AddCircle(mouse, 28.0f, IM_COL32(120, 200, 255, 160), 32, 1.5f);
    // Outil Fendre et écarter : premier point choisi
    if (tool == Tool::Slash && slashPanel_ >= 0 && slashPanel_ < static_cast<int>(g.panels.size()) && slashPoint_ >= 0 &&
        slashPoint_ < static_cast<int>(g.panels[static_cast<std::size_t>(slashPanel_)].points.size())) {
        const ImVec2 sp = toScreen(g.panels[static_cast<std::size_t>(slashPanel_)].points[static_cast<std::size_t>(slashPoint_)]);
        dl->AddCircle(sp, 8.0f, IM_COL32(255, 90, 90, 240), 16, 2.0f);
        if (hovered) dl->AddLine(sp, mouse, IM_COL32(255, 90, 90, 160), 1.5f);
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
