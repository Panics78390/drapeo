#include "app/App.h"

#include <algorithm>
#include <numbers>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <unordered_map>
#include <vector>

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include "app/FileDialog.h"
#include "app/FontData.h"
#include "io/ProjectFile.h"
#include "io/Settings.h"
#include "app/LogoData.h"
#include "geometry/ClothMesh.h"
#include "geometry/Mannequin.h"
#include "garment/PatternOps.h"
#include "io/GarmentExport.h"
#include "io/PatternImport.h"
#include "io/PdfImport.h"
#include "io/RasterImage.h"
#include "rendering/GlFunctions.h"
#include "simulation/Materials.h"

namespace drapeo::app {

namespace {
const char* const kAttachKinds[] = {"Poche (ouverte en haut)", "Rabat (cousu en haut)", "Tout le tour (renfort, écusson, bande)"};
}

namespace {
constexpr float kFrameDt = 1.0f / 60.0f;   // pas fixe : simulation déterministe
constexpr float kPi = 3.14159265f;

// --- Palette « atelier » : graphite chaud et laiton, sobre --------------------------
const ImVec4 kBg0(0.078f, 0.073f, 0.066f, 1.0f);     // barres
const ImVec4 kBgToolbar(0.098f, 0.091f, 0.082f, 1.0f);
const ImVec4 kBg1(0.122f, 0.114f, 0.104f, 1.0f);     // panneaux
const ImVec4 kBg2(0.172f, 0.160f, 0.144f, 1.0f);     // champs
const ImVec4 kBg3(0.230f, 0.210f, 0.184f, 1.0f);     // survol
const ImVec4 kLine(0.40f, 0.34f, 0.26f, 0.45f);
const ImVec4 kAccent(0.86f, 0.68f, 0.38f, 1.0f);     // laiton
const ImVec4 kAccentHi(0.94f, 0.80f, 0.52f, 1.0f);
const ImVec4 kAccentDim(0.58f, 0.44f, 0.24f, 1.0f);
const ImVec4 kText(0.93f, 0.91f, 0.86f, 1.0f);
const ImVec4 kTextDim(0.64f, 0.60f, 0.54f, 1.0f);
const ImVec4 kInkOnAccent(0.16f, 0.12f, 0.07f, 1.0f);
const ImU32 kAccentU32 = IM_COL32(220, 174, 96, 255);
const ImVec4 kOk(0.62f, 0.78f, 0.56f, 1.0f);
const ImVec4 kBad(0.93f, 0.48f, 0.40f, 1.0f);

void applyStyle(float scale)
{
    ImGuiStyle& s = ImGui::GetStyle();
    ImGui::StyleColorsDark(&s);
    s.WindowRounding = 0.0f;
    s.ChildRounding = 6.0f;
    s.FrameRounding = 5.0f;
    s.PopupRounding = 8.0f;
    s.GrabRounding = 4.0f;
    s.TabRounding = 5.0f;
    s.ScrollbarRounding = 8.0f;
    s.WindowBorderSize = 0.0f;
    s.PopupBorderSize = 1.0f;
    s.FrameBorderSize = 0.0f;
    s.WindowPadding = ImVec2(14, 12);
    s.FramePadding = ImVec2(10, 5);
    s.ItemSpacing = ImVec2(8, 7);
    s.ItemInnerSpacing = ImVec2(6, 4);
    s.IndentSpacing = 16.0f;
    s.ScrollbarSize = 10.0f;
    s.GrabMinSize = 10.0f;
    s.SeparatorTextBorderSize = 1.0f;
    s.SeparatorTextPadding = ImVec2(8, 6);
    s.WindowMinSize = ImVec2(2, 2);   // la poignée de redimensionnement est très fine
    s.ScaleAllSizes(scale);

    ImVec4* c = s.Colors;
    c[ImGuiCol_Text] = kText;
    c[ImGuiCol_TextDisabled] = kTextDim;
    c[ImGuiCol_WindowBg] = kBg1;
    c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_PopupBg] = ImVec4(0.102f, 0.094f, 0.084f, 0.98f);
    c[ImGuiCol_Border] = kLine;
    c[ImGuiCol_FrameBg] = kBg2;
    c[ImGuiCol_FrameBgHovered] = kBg3;
    c[ImGuiCol_FrameBgActive] = ImVec4(0.26f, 0.23f, 0.19f, 1.0f);
    c[ImGuiCol_TitleBg] = kBg0;
    c[ImGuiCol_TitleBgActive] = kBg0;
    c[ImGuiCol_MenuBarBg] = kBg0;
    c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab] = ImVec4(0.32f, 0.28f, 0.22f, 0.85f);
    c[ImGuiCol_ScrollbarGrabHovered] = kAccentDim;
    c[ImGuiCol_ScrollbarGrabActive] = kAccent;
    c[ImGuiCol_CheckMark] = kAccent;
    c[ImGuiCol_SliderGrab] = kAccentDim;
    c[ImGuiCol_SliderGrabActive] = kAccent;
    c[ImGuiCol_Button] = kBg2;
    c[ImGuiCol_ButtonHovered] = kBg3;
    c[ImGuiCol_ButtonActive] = kAccentDim;
    c[ImGuiCol_Header] = ImVec4(0.20f, 0.18f, 0.15f, 1.0f);
    c[ImGuiCol_HeaderHovered] = ImVec4(0.28f, 0.24f, 0.18f, 1.0f);
    c[ImGuiCol_HeaderActive] = ImVec4(0.34f, 0.27f, 0.16f, 1.0f);
    c[ImGuiCol_Separator] = ImVec4(0.86f, 0.74f, 0.50f, 0.16f);
    c[ImGuiCol_SeparatorHovered] = kAccentDim;
    c[ImGuiCol_SeparatorActive] = kAccent;
    c[ImGuiCol_ResizeGrip] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_Tab] = kBg1;
    c[ImGuiCol_TabHovered] = kBg3;
    c[ImGuiCol_TabSelected] = kBg2;
    c[ImGuiCol_TabSelectedOverline] = kAccent;
    c[ImGuiCol_PlotHistogram] = kAccent;
    c[ImGuiCol_PlotHistogramHovered] = kAccentHi;
    c[ImGuiCol_TextSelectedBg] = ImVec4(0.86f, 0.68f, 0.38f, 0.35f);
    c[ImGuiCol_NavHighlight] = kAccent;
    c[ImGuiCol_ModalWindowDimBg] = ImVec4(0.04f, 0.03f, 0.025f, 0.62f);
}

/// Curseur logarithmique pour les compliances (plusieurs ordres de grandeur).
bool sliderCompliance(const char* label, float& value, float minExp, float maxExp)
{
    float e = value > 0.0f ? std::log10(value) : minExp;
    char text[32];
    std::snprintf(text, sizeof(text), value > 0.0f ? "%.1e" : "0 (rigide)", static_cast<double>(value));
    const bool changed = ImGui::SliderFloat(label, &e, minExp, maxExp, text);
    if (changed) value = (e <= minExp + 1e-3f) ? 0.0f : std::pow(10.0f, e);
    return changed;
}

/// Ligne « libellé à gauche, contrôle à droite » de l'inspecteur.
void label(const char* text)
{
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(text);
    ImGui::SameLine(ImGui::GetContentRegionAvail().x * 0.0f + ImGui::GetFontSize() * 7.2f);
    ImGui::SetNextItemWidth(-1);
}

// --- Icônes dessinées (aucune ressource externe) ---------------------------
enum class Icon { Select, Polygon, Rectangle, Pocket, Seam, Magnet, Symmetry, Play, Pause, Step, Reset, Frame };
enum class Btn { Quiet, On, Primary };   // Quiet : repos, On : outil actif, Primary : action principale

void drawIcon(ImDrawList* dl, Icon icon, ImVec2 c, float r, ImU32 col)
{
    auto P = [&](float x, float y) { return ImVec2(c.x + x * r, c.y + y * r); };
    const float t = std::max(1.5f, r * 0.14f);
    switch (icon) {
    case Icon::Select: {
        const ImVec2 pts[] = {P(-0.45f, -0.85f), P(-0.45f, 0.55f), P(-0.12f, 0.22f), P(0.14f, 0.8f),
                              P(0.36f, 0.7f),    P(0.10f, 0.14f),  P(0.52f, 0.14f)};
        dl->AddConcavePolyFilled(pts, 7, col);
        break;
    }
    case Icon::Polygon: {
        ImVec2 pts[5];
        for (int i = 0; i < 5; ++i) {
            const float a = -kPi / 2 + i * 2 * kPi / 5;
            pts[i] = P(0.8f * std::cos(a), 0.8f * std::sin(a) + 0.05f);
        }
        dl->AddPolyline(pts, 5, col, ImDrawFlags_Closed, t);
        for (const auto& p : pts) dl->AddRectFilled({p.x - t * 1.2f, p.y - t * 1.2f}, {p.x + t * 1.2f, p.y + t * 1.2f}, col);
        break;
    }
    case Icon::Rectangle: {
        dl->AddRect(P(-0.75f, -0.6f), P(0.75f, 0.6f), col, 0.0f, 0, t);
        for (const ImVec2 p : {P(-0.75f, -0.6f), P(0.75f, -0.6f), P(0.75f, 0.6f), P(-0.75f, 0.6f)})
            dl->AddRectFilled({p.x - t * 1.2f, p.y - t * 1.2f}, {p.x + t * 1.2f, p.y + t * 1.2f}, col);
        break;
    }
    case Icon::Pocket: {   // poche plaquée : fond arrondi, surpiqûre en tirets, rabat en pointe
        dl->PathLineTo(P(-0.7f, -0.35f));
        dl->PathLineTo(P(-0.7f, 0.55f));
        dl->PathLineTo(P(-0.45f, 0.8f));
        dl->PathLineTo(P(0.45f, 0.8f));
        dl->PathLineTo(P(0.7f, 0.55f));
        dl->PathLineTo(P(0.7f, -0.35f));
        dl->PathStroke(col, 0, t);
        dl->PathLineTo(P(-0.8f, -0.8f));
        dl->PathLineTo(P(0.8f, -0.8f));
        dl->PathLineTo(P(0.8f, -0.35f));
        dl->PathLineTo(P(0.0f, -0.05f));
        dl->PathLineTo(P(-0.8f, -0.35f));
        dl->PathStroke(col, ImDrawFlags_Closed, t);
        for (int i = 0; i < 3; ++i) dl->AddLine(P(-0.42f + 0.32f * i, 0.5f), P(-0.26f + 0.32f * i, 0.5f), col, t * 0.8f);
        break;
    }
    case Icon::Seam: {
        dl->AddLine(P(-0.7f, -0.8f), P(-0.45f, 0.8f), col, t);
        dl->AddLine(P(0.7f, -0.8f), P(0.45f, 0.8f), col, t);
        for (int i = 0; i < 4; ++i) {
            const float y = -0.6f + i * 0.4f;
            dl->AddLine(P(-0.42f, y), P(0.42f, y), col, t * 0.9f);
        }
        break;
    }
    case Icon::Magnet: {   // aimant en U
        dl->PathArcTo(P(0.0f, 0.05f), r * 0.5f, 0.0f, kPi, 20);
        dl->PathStroke(col, 0, t * 2.2f);
        dl->AddLine(P(-0.5f, 0.05f), P(-0.5f, -0.7f), col, t * 2.2f);
        dl->AddLine(P(0.5f, 0.05f), P(0.5f, -0.7f), col, t * 2.2f);
        dl->AddLine(P(-0.72f, -0.55f), P(-0.28f, -0.55f), col, t);
        dl->AddLine(P(0.28f, -0.55f), P(0.72f, -0.55f), col, t);
        break;
    }
    case Icon::Symmetry: {   // deux demi-formes en miroir autour d'un axe en pointillés
        for (int i = 0; i < 4; ++i) {
            const float y0 = -0.85f + i * 0.45f;
            dl->AddLine(P(0.0f, y0), P(0.0f, y0 + 0.25f), col, t);
        }
        dl->AddTriangle(P(-0.18f, -0.6f), P(-0.18f, 0.6f), P(-0.8f, 0.6f), col, t);
        dl->AddTriangleFilled(P(0.18f, -0.6f), P(0.18f, 0.6f), P(0.8f, 0.6f), col);
        break;
    }
    case Icon::Play:
        dl->AddTriangleFilled(P(-0.5f, -0.7f), P(-0.5f, 0.7f), P(0.7f, 0.0f), col);
        break;
    case Icon::Pause:
        dl->AddRectFilled(P(-0.55f, -0.65f), P(-0.15f, 0.65f), col, 1.0f);
        dl->AddRectFilled(P(0.15f, -0.65f), P(0.55f, 0.65f), col, 1.0f);
        break;
    case Icon::Step:
        dl->AddTriangleFilled(P(-0.6f, -0.65f), P(-0.6f, 0.65f), P(0.35f, 0.0f), col);
        dl->AddRectFilled(P(0.4f, -0.65f), P(0.65f, 0.65f), col, 1.0f);
        break;
    case Icon::Reset: {
        dl->PathArcTo(c, r * 0.62f, -kPi * 0.35f, kPi * 1.35f, 20);
        dl->PathStroke(col, 0, t);
        const ImVec2 tip(c.x + r * 0.62f * std::cos(-kPi * 0.35f), c.y + r * 0.62f * std::sin(-kPi * 0.35f));
        dl->AddTriangleFilled({tip.x - r * 0.38f, tip.y - r * 0.12f}, {tip.x + r * 0.14f, tip.y - r * 0.42f},
                              {tip.x + r * 0.16f, tip.y + r * 0.2f}, col);
        break;
    }
    case Icon::Frame: {
        const float a = 0.75f, b = 0.35f;
        for (int sx = -1; sx <= 1; sx += 2)
            for (int sy = -1; sy <= 1; sy += 2) {
                dl->AddLine(P(sx * a, sy * a), P(sx * b, sy * a), col, t);
                dl->AddLine(P(sx * a, sy * a), P(sx * a, sy * b), col, t);
            }
        dl->AddCircleFilled(c, r * 0.16f, col);
        break;
    }
    }
}

bool iconButton(const char* id, Icon icon, const char* tooltip, Btn tone, float size)
{
    ImGui::PushID(id);
    ImU32 iconCol = ImGui::GetColorU32(ImGuiCol_Text);
    if (tone == Btn::Primary) {
        ImGui::PushStyleColor(ImGuiCol_Button, kAccent);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kAccentHi);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, kAccentDim);
        iconCol = ImGui::GetColorU32(kInkOnAccent);
    } else if (tone == Btn::On) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.20f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.34f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, kAccentDim);
        iconCol = kAccentU32;
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kBg3);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.30f));
    }
    const bool pressed = ImGui::Button("##icon", ImVec2(size, size));
    ImGui::PopStyleColor(3);
    const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (tone == Btn::On) dl->AddLine({a.x + 6, b.y - 2}, {b.x - 6, b.y - 2}, kAccentU32, 2.0f);
    if (tone == Btn::Quiet && ImGui::IsItemHovered()) iconCol = kAccentU32;
    drawIcon(dl, icon, {(a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f}, size * 0.30f, iconCol);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("%s", tooltip);
    ImGui::PopID();
    return pressed;
}

void toolbarSeparator(float height)
{
    ImGui::SameLine(0, 10);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddLine({p.x, p.y + height * 0.15f}, {p.x, p.y + height * 0.85f}, ImGui::GetColorU32(ImGuiCol_Separator));
    ImGui::Dummy(ImVec2(1, height));
    ImGui::SameLine(0, 10);
}

/// Titre de section d'un panneau : petites capitales grisées + filet.
void paneTitle(const char* text, ImFont* bold)
{
    if (bold) ImGui::PushFont(bold);
    const float h = ImGui::GetTextLineHeight();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddRectFilled({p.x, p.y + 2.0f}, {p.x + 2.0f, p.y + h - 1.0f}, kAccentU32, 1.0f);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 8.0f);
    ImGui::PushStyleColor(ImGuiCol_Text, kTextDim);
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
    if (bold) ImGui::PopFont();
}

/// Bouton plein laiton : l'action qui valide une fenêtre.
bool primaryButton(const char* label, const ImVec2& size)
{
    ImGui::PushStyleColor(ImGuiCol_Button, kAccent);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kAccentHi);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, kAccentDim);
    ImGui::PushStyleColor(ImGuiCol_Text, kInkOnAccent);
    const bool pressed = ImGui::Button(label, size);
    ImGui::PopStyleColor(4);
    return pressed;
}

bool fileExists(const char* path)
{
    std::ifstream f(path, std::ios::binary);
    return f.good();
}

std::string lowerExtension(const std::string& path)
{
    const auto dot = path.find_last_of('.');
    if (dot == std::string::npos) return {};
    std::string ext = path.substr(dot + 1);
    for (auto& ch : ext) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return ext;
}

/// Texture RGBA pour l'interface. Les très grandes images sont réduites à 4096 px.
unsigned int uploadTexture(int w, int h, const unsigned char* px)
{
    std::vector<unsigned char> small;
    const int maxSide = 4096;
    if (w > maxSide || h > maxSide) {
        const float k = static_cast<float>(maxSide) / static_cast<float>(std::max(w, h));
        const int nw = std::max(1, static_cast<int>(w * k)), nh = std::max(1, static_cast<int>(h * k));
        small.resize(static_cast<std::size_t>(nw) * nh * 4);
        for (int y = 0; y < nh; ++y)
            for (int x = 0; x < nw; ++x) {
                const int sx = std::min(w - 1, static_cast<int>(x / k)), sy = std::min(h - 1, static_cast<int>(y / k));
                std::copy_n(px + (static_cast<std::size_t>(sy) * w + sx) * 4, 4, &small[(static_cast<std::size_t>(y) * nw + x) * 4]);
            }
        w = nw;
        h = nh;
        px = small.data();
    }
    gl::GLuint t = 0;
    gl::GenTextures(1, &t);
    gl::BindTexture(gl::TEXTURE_2D, t);
    gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MIN_FILTER, static_cast<gl::GLint>(gl::LINEAR));
    gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MAG_FILTER, static_cast<gl::GLint>(gl::LINEAR));
    gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_WRAP_S, static_cast<gl::GLint>(gl::CLAMP_TO_EDGE));
    gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_WRAP_T, static_cast<gl::GLint>(gl::CLAMP_TO_EDGE));
    gl::PixelStorei(gl::UNPACK_ALIGNMENT, 1);
    gl::TexImage2D(gl::TEXTURE_2D, 0, static_cast<gl::GLint>(gl::RGBA), w, h, 0, gl::RGBA, gl::UNSIGNED_BYTE, px);
    gl::BindTexture(gl::TEXTURE_2D, 0);
    return t;
}

void deleteTexture(unsigned int& t)
{
    if (t != 0) gl::DeleteTextures(1, &t);
    t = 0;
}

bool isImageExtension(const std::string& ext)
{
    return ext == "png" || ext == "jpg" || ext == "jpeg" || ext == "jpe" || ext == "jfif" || ext == "bmp" || ext == "tga" ||
           ext == "gif" || ext == "psd";
}

const char* kSpacingNames[] = {"Grossier (3 cm)", "Normal (2 cm)", "Fin (1,5 cm)", "Très fin (1 cm)"};
const float kSpacingValues[] = {0.03f, 0.02f, 0.015f, 0.01f};
} // namespace

void dropCallback(GLFWwindow* window, int count, const char** paths)
{
    // Glisser-déposer d'un fichier sur la fenêtre : traité à l'image suivante.
    if (count < 1) return;
    if (auto* app = static_cast<App*>(glfwGetWindowUserPointer(window))) app->pendingDrop_ = paths[0];
}

// =============================================================================
//  Boucle principale
// =============================================================================
int App::run(const AppOptions& options)
{
    options_ = options;
    if (!initWindow()) return 1;

    std::string error;
    if (!renderer_.init(error)) {
        std::fprintf(stderr, "Erreur de shader : %s\n", error.c_str());
        shutdown();
        return 1;
    }
    material_ = simulation::fabricPresets()[static_cast<std::size_t>(presetIndex_)];
    appearance_.color = material_.color;
    appearance_.roughness = material_.roughness;
    appearance_.sheen = material_.sheen;

    loadDefaultMannequin();
    if (!options_.avatarPath.empty()) {
        loadAvatar(options_.avatarPath);
    } else {
        // Dernier avatar utilisé ; à défaut, un modèle 3D posé à côté de drapeo.exe.
        std::string startAvatar;
        if (!io::rememberedAvatar(startAvatar)) startAvatar = io::avatarNextToExecutable();
        if (!startAvatar.empty()) loadAvatar(startAvatar);
    }
    setGarment({});   // au lancement : l'avatar seul, aucune pièce
    setStatus("Bienvenue dans Drapeo.", false);
    showAbout_ = options_.showAbout;
    if (!options_.patternPath.empty()) {
        setGarment({});
        importPattern(options_.patternPath);
        if (options_.acceptImport && pendingImage_.width > 0) finishImageImport();
        if (options_.startPaused) playing_ = false;
    } else if (!options_.model.empty()) {
        std::string m = options_.model;
        for (char& c : m) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        const char* label = nullptr;
        garment::Garment g;
        if (m == "debardeur") {
            g = garment::makeTankTop();
            label = "Débardeur";
        } else if (m == "jupe") {
            g = garment::makeSkirt();
            label = "Jupe droite";
        } else if (m == "robe") {
            g = garment::makeDress();
            label = "Robe";
        } else {
            setStatus("Modèle inconnu (« " + options_.model + " »). Utilisez debardeur, jupe ou robe.", true);
        }
        if (label != nullptr) {
            setGarment(std::move(g));
            setStatus(std::string("Modèle « ") + label + " » chargé.", false);
        }
        if (options_.startPaused) playing_ = false;
    }
    if (options_.yawDeg > -999.0f) {
        frameCamera();
        camera_.yaw = glm::radians(options_.yawDeg);
        camera_.pitch = 0.05f;
    }
    camera_.distance /= std::max(options_.zoom, 0.1f);

    while (!glfwWindowShouldClose(window_)) {
        glfwPollEvents();
        checkWatchedProject();
        if (!pendingDrop_.empty()) {
            const std::string path = pendingDrop_;
            pendingDrop_.clear();
            const std::string ext = lowerExtension(path);
            if (ext == "fbx" || ext == "obj") loadAvatar(path);
            else importPattern(path);
        }
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        computeLayout();
        handleShortcuts();
        handleViewportInput();

        drawMenuBar();
        drawToolbar();
        drawPatternPane();
        drawSplitter();
        drawInspector();
        drawStatusBar();
        drawViewportOverlay();
        drawAboutWindow();
        drawImageImportPopup();

        if (clothValid_ && (playing_ || stepOnce_)) {
            solver_.step(kFrameDt);
            simTime_ += kFrameDt;
            stepOnce_ = false;
        }
        if (clothValid_) {
            geometry::computeVertexNormals(solver_.positions(), solver_.indices(), normals_);
            solver_.computeStrain(strain_);
            // Pièces posées (poches, bandes, écussons) : légèrement décollées à l'affichage
            // pour qu'elles ne se mélangent pas avec le tissu sur lequel elles sont cousues.
            // Épaisseur d'affichage d'une pièce : le tissu est dessiné décalé vers son endroit.
            const bool anyRenderThick = std::any_of(garment_.panels.begin(), garment_.panels.end(),
                                                    [](const garment::Panel& p) { return p.thickRender > 0.0f; });
            if (!garment_.attachments.empty() || anyRenderThick) {
                std::vector<float> lift(garment_.panels.size(), 0.0f);
                for (std::size_t p = 0; p < garment_.panels.size(); ++p) lift[p] = garment_.panels[p].thickRender;
                for (const auto& a : garment_.attachments)
                    if (a.patch >= 0 && a.patch < static_cast<int>(lift.size())) lift[static_cast<std::size_t>(a.patch)] += 0.004f;
                const auto& pos = solver_.positions();
                displayPositions_.assign(pos.begin(), pos.end());
                for (std::size_t i = 0; i < displayPositions_.size() && i < garmentMesh_.panelOfVertex.size() && i < normals_.size(); ++i) {
                    const int pv = garmentMesh_.panelOfVertex[i];
                    if (pv >= 0 && pv < static_cast<int>(lift.size())) displayPositions_[i] += normals_[i] * lift[static_cast<std::size_t>(pv)];
                }
                renderer_.updateCloth(displayPositions_, normals_, strain_);
            } else {
                renderer_.updateCloth(solver_.positions(), normals_, strain_);
            }
        }

        // Fond de toute la fenêtre, puis la vue 3D dans sa zone.
        int fbw = 0, fbh = 0;
        glfwGetFramebufferSize(window_, &fbw, &fbh);
        const ImVec2 display = ImGui::GetIO().DisplaySize;
        const float sx = display.x > 0 ? static_cast<float>(fbw) / display.x : 1.0f;
        const float sy = display.y > 0 ? static_cast<float>(fbh) / display.y : 1.0f;
        gl::Disable(gl::SCISSOR_TEST);
        gl::Viewport(0, 0, fbw, fbh);
        gl::ClearColor(kBg1.x, kBg1.y, kBg1.z, 1.0f);
        gl::Clear(gl::COLOR_BUFFER_BIT | gl::DEPTH_BUFFER_BIT);

        rendering::SceneView view;
        view.camera = &camera_;
        view.x = static_cast<int>(viewPos_.x * sx);
        view.width = std::max(1, static_cast<int>(viewSize_.x * sx));
        view.height = std::max(1, static_cast<int>(viewSize_.y * sy));
        view.y = fbh - static_cast<int>((viewPos_.y + viewSize_.y) * sy);
        view.spheres = &solver_.spheres();
        view.groundEnabled = solver_.settings().groundEnabled;
        view.groundHeight = solver_.settings().groundHeight;
        renderer_.render(view, appearance_);
        gl::Disable(gl::SCISSOR_TEST);
        gl::Viewport(0, 0, fbw, fbh);

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        ++frameCount_;
        if (options_.frames > 0 && frameCount_ >= options_.frames) {
            if (!options_.screenshotPath.empty()) saveScreenshot(options_.screenshotPath);
            glfwSetWindowShouldClose(window_, GLFW_TRUE);
        }
        glfwSwapBuffers(window_);
    }

    shutdown();
    return 0;
}

bool App::initWindow()
{
    glfwSetErrorCallback([](int code, const char* text) { std::fprintf(stderr, "GLFW %d : %s\n", code, text); });
    if (!glfwInit()) return false;

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#if defined(__APPLE__)
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif
    glfwWindowHint(GLFW_SAMPLES, 4);
    if (options_.frames <= 0) glfwWindowHint(GLFW_MAXIMIZED, GLFW_TRUE);

    window_ = glfwCreateWindow(options_.width, options_.height, "Drapeo - atelier de vêtements 3D", nullptr, nullptr);
    if (window_ == nullptr) {
        glfwTerminate();
        return false;
    }
    glfwMakeContextCurrent(window_);
    glfwSwapInterval(options_.frames > 0 ? 0 : 1);   // vsync sauf en mode capture

    const char* missing = nullptr;
    if (!gl::load(glfwGetProcAddress, &missing)) {
        std::fprintf(stderr, "Fonction OpenGL introuvable : %s\n", missing ? missing : "?");
        return false;
    }
    std::printf("OpenGL : %s - %s\n", reinterpret_cast<const char*>(gl::GetString(gl::VERSION)),
                reinterpret_cast<const char*>(gl::GetString(gl::RENDERER)));
    gl::Enable(gl::MULTISAMPLE);

    // Mise à l'échelle de l'interface selon le réglage d'affichage du système (125 %, 150 %...).
    float csx = 1.0f, csy = 1.0f;
    glfwGetWindowContentScale(window_, &csx, &csy);
#if defined(__APPLE__)
    uiScale_ = 1.0f;   // macOS : les coordonnées sont déjà en points
#else
    uiScale_ = std::clamp(csx, 1.0f, 3.0f);
#endif

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;   // pas de fichier imgui.ini
    applyStyle(uiScale_);
    loadFonts(uiScale_);
    ImGui_ImplGlfw_InitForOpenGL(window_, true);
    ImGui_ImplOpenGL3_Init("#version 330");
    loadLogo();

    glfwSetWindowUserPointer(window_, this);
    glfwSetDropCallback(window_, dropCallback);
    return true;
}

void App::loadFonts(float scale)
{
    // Inter est embarquée : l'atelier reste identique hors ligne, sans fichier à côté de l'exe.
    // Plage par défaut d'ImGui : latin + latin-1, ce qui couvre le français.
    ImGuiIO& io = ImGui::GetIO();
    ImFontConfig cfg;
    cfg.FontDataOwnedByAtlas = false;
    cfg.OversampleH = 2;
    cfg.OversampleV = 2;
    cfg.PixelSnapH = false;
    const float size = 16.5f * scale;
    ImFont* base = io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(font::kRegular), font::kRegularSize, size, &cfg);
    ImFontConfig boldCfg = cfg;
    boldCfg.FontDataOwnedByAtlas = false;
    fontBold_ = io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(font::kSemiBold), font::kSemiBoldSize, size * 0.94f, &boldCfg);
    if (base != nullptr) return;

    const char* regular[] = {"C:\\Windows\\Fonts\\segoeui.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
                             "/System/Library/Fonts/Supplemental/Arial.ttf"};
    const char* bold[] = {"C:\\Windows\\Fonts\\segoeuib.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
                          "/System/Library/Fonts/Supplemental/Arial Bold.ttf"};
    ImFontConfig fileCfg;
    fileCfg.OversampleH = 2;
    for (const char* path : regular)
        if (fileExists(path) && io.Fonts->AddFontFromFileTTF(path, size, &fileCfg) != nullptr) break;
    if (io.Fonts->Fonts.empty()) {
        ImFontConfig def;
        def.SizePixels = 13.0f * scale;
        io.Fonts->AddFontDefault(&def);
    }
    for (const char* path : bold)
        if (fileExists(path) && (fontBold_ = io.Fonts->AddFontFromFileTTF(path, size * 0.94f, &fileCfg)) != nullptr) break;
}

void App::loadLogo()
{
    // Icône de la fenêtre et de la barre des tâches (plusieurs tailles).
    std::vector<std::vector<unsigned char>> copies;
    std::vector<GLFWimage> images;
    auto add = [&](int w, int h, const unsigned char* px) {
        copies.emplace_back(px, px + static_cast<std::size_t>(w * h * 4));
        images.push_back({w, h, copies.back().data()});
    };
    copies.reserve(4);
    add(logo::kIcon16Width, logo::kIcon16Height, logo::kIcon16Pixels);
    add(logo::kIcon32Width, logo::kIcon32Height, logo::kIcon32Pixels);
    add(logo::kIcon48Width, logo::kIcon48Height, logo::kIcon48Pixels);
    add(logo::kIcon64Width, logo::kIcon64Height, logo::kIcon64Pixels);
    glfwSetWindowIcon(window_, static_cast<int>(images.size()), images.data());

    // Textures pour l'interface : petit logo (barre de menus) et logo complet.
    logoIcon_ = uploadTexture(logo::kIcon64Width, logo::kIcon64Height, logo::kIcon64Pixels);
    logoFull_ = uploadTexture(logo::kFullWidth, logo::kFullHeight, logo::kFullPixels);
}

void App::shutdown()
{
    if (window_ == nullptr) return;
    deleteTexture(logoIcon_);
    deleteTexture(logoFull_);
    deleteTexture(backdropTex_);
    deleteTexture(pendingTex_);
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window_);
    glfwTerminate();
    window_ = nullptr;
}

// =============================================================================
//  Vêtement
// =============================================================================
garment::MeshingOptions App::meshingOptions() const
{
    garment::MeshingOptions mo;
    mo.spacing = spacing_;
    mo.bodyScale = avatarHeight_ / 1.75f;
    mo.axes = bodyAxes_;
    return mo;
}

void App::dragPanelTo(const glm::vec3& o, const glm::vec3& d)
{
    if (dragPanel_ < 0 || dragPanel_ >= static_cast<int>(garment_.panels.size())) return;
    auto& panel = garment_.panels[static_cast<std::size_t>(dragPanel_)];
    const garment::MeshingOptions mo = meshingOptions();
    const garment::ZoneFrame f = garment::zoneFrame(panel.placement.zone, mo);
    const float R = std::max(panel.placement.radius * mo.bodyScale, 0.03f);
    // Point du cylindre de la pièce sous la souris (le plus proche du point précédent).
    const auto perp = [&](const glm::vec3& v) { return v - f.up * glm::dot(v, f.up); };
    const glm::vec3 q = perp(o - f.origin), dp = perp(d);
    const float a = glm::dot(dp, dp), b = 2.0f * glm::dot(q, dp), c = glm::dot(q, q) - R * R;
    const float disc = b * b - 4 * a * c;
    glm::vec3 hit;
    if (a < 1e-8f) return;
    if (disc >= 0.0f) {
        const glm::vec3 h0 = o + d * ((-b - std::sqrt(disc)) / (2 * a)), h1 = o + d * ((-b + std::sqrt(disc)) / (2 * a));
        hit = glm::distance(h0, dragLastHit_) <= glm::distance(h1, dragLastHit_) ? h0 : h1;
    } else {
        hit = o + d * (-b / (2 * a));   // la souris sort du cylindre : point le plus proche de l'axe
    }
    const auto angleOf = [&](const glm::vec3& p) {
        const glm::vec3 r = p - f.origin;
        return std::atan2(glm::dot(r, f.side), glm::dot(r, f.front));
    };
    float da = angleOf(hit) - angleOf(dragLastHit_);
    while (da > std::numbers::pi_v<float>) da -= (2.0f * std::numbers::pi_v<float>);
    while (da < -std::numbers::pi_v<float>) da += (2.0f * std::numbers::pi_v<float>);
    const float axial = glm::dot(hit - dragLastHit_, f.up);
    dragLastHit_ = hit;
    float angle = panel.placement.angleDeg + glm::degrees(da);
    while (angle > 180.0f) angle -= 360.0f;
    while (angle < -180.0f) angle += 360.0f;
    panel.placement.angleDeg = angle;
    panel.placement.height = std::clamp(panel.placement.height + axial * f.up.y / mo.bodyScale, -0.5f, 2.2f);
    // On replace tout de suite les particules de la pièce (sans remailler).
    refreshPanelParticles(dragPanel_);
    dragMoved_ = true;
}

App::ClothSnapshot App::clothSnapshot() const
{
    ClothSnapshot s;
    if (!clothValid_) return s;
    s.uvs = garmentMesh_.mesh.uvs;
    s.positions = solver_.positions();
    s.panelOfVertex = garmentMesh_.panelOfVertex;
    return s;
}

void App::applyDrape(const ClothSnapshot& prev, const garment::Garment* prevGarment)
{
    const std::size_t np = garment_.panels.size();
    if (prev.positions.size() != prev.uvs.size() || prev.panelOfVertex.size() != prev.uvs.size()) return;
    std::vector<std::vector<uint32_t>> oldOf(np), newOf(np);
    for (std::size_t i = 0; i < prev.panelOfVertex.size(); ++i)
        if (const int p = prev.panelOfVertex[i]; p >= 0 && static_cast<std::size_t>(p) < np) oldOf[static_cast<std::size_t>(p)].push_back(static_cast<uint32_t>(i));
    for (std::size_t i = 0; i < garmentMesh_.panelOfVertex.size(); ++i)
        if (const int p = garmentMesh_.panelOfVertex[i]; p >= 0 && static_cast<std::size_t>(p) < np) newOf[static_cast<std::size_t>(p)].push_back(static_cast<uint32_t>(i));
    auto samePlacement = [&](int p) {
        if (prevGarment == nullptr) return true;
        if (p < 0 || p >= static_cast<int>(np) || p >= static_cast<int>(prevGarment->panels.size())) return false;
        const auto& a = garment_.panels[static_cast<std::size_t>(p)].placement;
        const auto& b = prevGarment->panels[static_cast<std::size_t>(p)].placement;
        return a.zone == b.zone && a.angleDeg == b.angleDeg && a.height == b.height && a.radius == b.radius && a.offset == b.offset &&
               a.rotation == b.rotation && a.flat == b.flat;
    };
    const auto& uvs = garmentMesh_.mesh.uvs;
    for (std::size_t p = 0; p < np; ++p) {
        const auto& o = oldOf[p];
        const auto& n = newOf[p];
        if (o.empty() || o.size() != n.size()) continue;   // pièce remaillée : elle repart de son placement
        bool keep = samePlacement(static_cast<int>(p));
        if (const int ai = garment_.attachmentOf(static_cast<int>(p)); keep && ai >= 0) keep = samePlacement(garment_.attachments[static_cast<std::size_t>(ai)].base);
        if (!keep) continue;
        const glm::vec2 shift = uvs[n[0]] - prev.uvs[o[0]];   // pièce simplement déplacée dans le patron
        bool same = true;
        for (std::size_t k = 0; k < o.size() && same; ++k) same = glm::length(uvs[n[k]] - prev.uvs[o[k]] - shift) < 1e-5f;
        if (!same) continue;
        for (std::size_t k = 0; k < o.size(); ++k) solver_.setParticlePosition(n[k], prev.positions[o[k]]);
    }
}

void App::commitLight()
{
    pushUndo();
    redoStack_.clear();
    redoCloth_.clear();
    committed_ = garment_;
}

void App::pushUndo(const ClothSnapshot* cloth)
{
    undoStack_.push_back(committed_);
    undoCloth_.push_back(cloth != nullptr ? *cloth : clothSnapshot());
    if (undoStack_.size() > 100) {
        undoStack_.erase(undoStack_.begin());
        undoCloth_.erase(undoCloth_.begin());
    }
}

void App::resetCloth(bool keepDrape, const ClothSnapshot* restore)
{
    // Drapé courant, pour le reprendre si le maillage ne change pas (même patron, autre placement...).
    ClothSnapshot previous;
    garment::Garment previousGarment;
    if (keepDrape && restore == nullptr && clothValid_) {
        previous = clothSnapshot();
        previousGarment = meshGarment_;
    }
    clothValid_ = false;
    simTime_ = 0.0f;
    solver_.release();
    solver_.spheres().clear();   // pas de sphère de démonstration : l'avatar seul
    if (garment_.empty()) {
        garmentMesh_ = {};
        renderer_.setClothTopology({}, 0);
        updateClothPattern();
        return;
    }
    const garment::MeshingOptions mo = meshingOptions();
    garment::GarmentMesh gm;
    std::string error;
    if (!garment::buildGarmentMesh(garment_, mo, gm, error)) {
        setStatus("Maillage impossible : " + error, true);
        garmentMesh_ = {};
        renderer_.setClothTopology({}, 0);
        return;
    }
    garmentMesh_ = std::move(gm);
    solver_.build(garmentMesh_.mesh, material_);
    simulation::applyShrinkage(solver_, garment_, garmentMesh_);   // retrait trame / chaîne des pièces
    simulation::applyFolds(solver_, garmentMesh_);   // lignes de pli
    solver_.spheres().clear();
    solver_.setMeshCollider(&avatarBvh_);
    solver_.setSeams(garmentMesh_.seamPairs, sewingSeconds_);
    renderer_.setClothTopology(garmentMesh_.mesh.indices, garmentMesh_.mesh.vertexCount());
    clothValid_ = true;
    meshGarment_ = garment_;
    if (restore != nullptr) applyDrape(*restore, nullptr);   // Annuler / Rétablir : le drapé de cet état revient
    else if (keepDrape) applyDrape(previous, &previousGarment);
    updateClothPattern();
    highlighted_ = {-2};   // recalcul de la mise en évidence au prochain affichage
    selectionEdges_.clear();
    updateClothColors();
    apply3DState();
}

void App::setGarment(garment::Garment g)
{
    if (!garment_.empty()) {
        pushUndo();
        redoStack_.clear();
        redoCloth_.clear();
    }
    garment_ = std::move(g);
    committed_ = garment_;
    editor_.clearSelection();
    editor_.frameAll(garment_);
    resetCloth();
    playing_ = true;
}

void App::commitGarment()
{
    // Clones liés : la nouvelle forme d'une pièce est reportée sur ses clones.
    if (const int linked = garment::propagateLinkedEdits(committed_, garment_); linked > 0)
        setStatus("Modification reportée sur " + std::to_string(linked) + " clone(s) lié(s).", false);
    pushUndo();
    redoStack_.clear();
    redoCloth_.clear();
    committed_ = garment_;
    resetCloth(true);   // la pause est respectée ; les pièces inchangées gardent leur drapé
}

void App::undo()
{
    if (undoStack_.empty()) {
        setStatus("Rien à annuler.", false);
        return;
    }
    redoStack_.push_back(garment_);
    redoCloth_.push_back(clothSnapshot());
    garment_ = undoStack_.back();
    undoStack_.pop_back();
    const ClothSnapshot snap = undoCloth_.empty() ? ClothSnapshot{} : undoCloth_.back();
    if (!undoCloth_.empty()) undoCloth_.pop_back();
    committed_ = garment_;
    editor_.clearSelection();
    resetCloth(true, &snap);   // seul le dernier changement est annulé : le drapé d'avant revient
    setStatus("Annulé (" + std::to_string(undoStack_.size()) + " étape(s) encore annulable(s), Ctrl+Y pour rétablir).", false);
}

void App::redo()
{
    if (redoStack_.empty()) {
        setStatus("Rien à rétablir.", false);
        return;
    }
    undoStack_.push_back(garment_);
    undoCloth_.push_back(clothSnapshot());
    garment_ = redoStack_.back();
    redoStack_.pop_back();
    const ClothSnapshot snap = redoCloth_.empty() ? ClothSnapshot{} : redoCloth_.back();
    if (!redoCloth_.empty()) redoCloth_.pop_back();
    committed_ = garment_;
    editor_.clearSelection();
    resetCloth(true, &snap);
}

void App::updateClothColors()
{
    appearance_.color = material_.color;
    const bool anySelected = std::any_of(highlighted_.begin(), highlighted_.end(), [](int p) { return p >= 0; });
    if (!clothValid_ || (!panelColors_ && !anySelected)) {
        renderer_.setClothColors({});
        return;
    }
    std::vector<glm::vec3> colors(garmentMesh_.panelOfVertex.size());
    const glm::vec3 highlight(0.96f, 0.86f, 0.34f);   // pièce sélectionnée dans la vue patron
    for (std::size_t i = 0; i < colors.size(); ++i) {
        const int p = garmentMesh_.panelOfVertex[i];
        const bool valid = p >= 0 && p < static_cast<int>(garment_.panels.size());
        colors[i] = valid && panelColors_ ? garment_.panels[static_cast<std::size_t>(p)].color : material_.color;
        if (valid && std::find(highlighted_.begin(), highlighted_.end(), p) != highlighted_.end())
            colors[i] = glm::mix(colors[i], highlight, 0.55f);
    }
    renderer_.setClothColors(colors);
}

std::string App::projectDirectory() const
{
    const auto cut = watchedProject_.find_last_of("/\\");
    return cut == std::string::npos ? std::string() : watchedProject_.substr(0, cut + 1);
}

void App::updateClothPattern()
{
    // Aspect du tissu (uni, ripstop, sergé, réfléchissant) et coordonnées du patron de chaque sommet.
    std::vector<float> finish(garmentMesh_.panelOfVertex.size(), 0.0f);
    for (std::size_t i = 0; i < finish.size(); ++i) {
        const int p = garmentMesh_.panelOfVertex[i];
        if (p >= 0 && p < static_cast<int>(garment_.panels.size()))
            finish[i] = static_cast<float>(garment_.panels[static_cast<std::size_t>(p)].finish);
    }
    if (clothValid_) renderer_.setClothPattern(garmentMesh_.mesh.uvs, finish);

    // Texture des impressions : reconstruite seulement si une impression ou une pièce imprimée a changé.
    std::string sig = projectDirectory() + (showStitches_ ? "+s" : "-s");
    char buf[96];
    for (std::size_t k = 0; k < garment_.panels.size(); ++k) {
        const auto& p = garment_.panels[k];
        if (p.prints.empty() && !showStitches_ && p.finish != garment::FabricFinish::Zipper) continue;
        std::snprintf(buf, sizeof(buf), "#%zu:%.3f,%.3f,%.3f,%d,%d,%d:", k, p.color.r, p.color.g, p.color.b, garment_.attachmentOf(static_cast<int>(k)),
                      static_cast<int>(p.finish), garment_.panelVisible(static_cast<int>(k)) ? 1 : 0);
        sig += buf;
        for (const auto& q : p.points) {
            std::snprintf(buf, sizeof(buf), "%.5f,%.5f;", q.x, q.y);
            sig += buf;
        }
        for (const auto& pr : p.prints) {
            std::snprintf(buf, sizeof(buf), "|%.5f %.5f %.5f %.5f %.3f ", pr.offset.x, pr.offset.y, pr.width, pr.height, pr.angleDeg);
            sig += buf + pr.image;
        }
    }
    {   // le cadre de la texture dépend de toutes les pièces
        glm::vec2 lo(1e9f), hi(-1e9f);
        for (const auto& p : garment_.panels)
            for (const auto& q : p.points) {
                lo = glm::min(lo, q);
                hi = glm::max(hi, q);
            }
        std::snprintf(buf, sizeof(buf), "@%.4f %.4f %.4f %.4f", lo.x, lo.y, hi.x, hi.y);
        sig += buf;
    }
    if (sig == printSignature_) return;
    printSignature_ = sig;
    const std::string dir = projectDirectory();
    auto load = [&](const std::string& path) -> const io::RasterImage* {
        if (auto it = printImages_.find(path); it != printImages_.end()) return &it->second;
        if (printMissing_.count(path) != 0) return nullptr;
        std::string bytes, error;
        const bool absolute = !path.empty() && (path[0] == '/' || path[0] == '\\' || (path.size() > 1 && path[1] == ':'));
        bool ok = !absolute && !dir.empty() && io::readFileBytes(dir + path, bytes, error);
        if (!ok) ok = io::readFileBytes(path, bytes, error);
        io::RasterImage img;
        if (!ok || !io::decodeRasterImage(bytes, img, error)) {
            printMissing_[path] = true;
            setStatus("Image d'impression introuvable : " + path, true);
            return nullptr;
        }
        return &(printImages_[path] = std::move(img));
    };
    garment::PatternTextureOptions opt;
    opt.metersPerPixel = 0.001f;
    opt.maxSize = 4096;
    opt.stitches = showStitches_;
    if (!garment::buildPatternTexture(garment_, load, opt, printAtlas_)) {
        printAtlas_ = {};
        renderer_.setPrintTexture(0, 0, nullptr, {}, {});
        editor_.prints = {};
        return;
    }
    renderer_.setPrintTexture(printAtlas_.width, printAtlas_.height, printAtlas_.rgba.data(), printAtlas_.origin, printAtlas_.size());
    editor_.prints.texture = static_cast<ImTextureID>(renderer_.printTexture());
    editor_.prints.min = printAtlas_.origin;
    editor_.prints.max = printAtlas_.origin + printAtlas_.size();
}

void App::importPattern(const std::string& path)
{
    if (path.empty()) return;
    std::string ext = lowerExtension(path);
    std::string error;
    const auto slash = path.find_last_of("/\\");
    const std::string name = slash == std::string::npos ? path : path.substr(slash + 1);

    // Extension inconnue (.jfif, sans extension...) : on reconnaît le format au contenu.
    std::string bytes;
    if (ext == "drapeo") {
        garment::Garment g;
        io::ProjectAvatar av;
        if (!io::loadProject(path, g, error, &av)) {
            setStatus("Ouverture du projet impossible : " + error, true);
            return;
        }
        std::string avatarNote;
        if (av.present) {
            if (av.path.empty()) {
                if (!builtinAvatar_) loadDefaultMannequin();
            } else if (av.path != avatarPath_ || builtinAvatar_) {
                // Chemin enregistré, sinon même nom de fichier à côté du projet.
                std::string candidate = av.path;
                std::string tmp;
                std::string err2;
                if (!io::readFileBytes(candidate, tmp, err2)) {
                    const auto cut = av.path.find_last_of("/\\");
                    const auto dir = path.find_last_of("/\\");
                    candidate = (dir == std::string::npos ? std::string() : path.substr(0, dir + 1)) +
                                (cut == std::string::npos ? av.path : av.path.substr(cut + 1));
                }
                if (!loadAvatar(candidate)) avatarNote = " Avatar introuvable : " + av.path + " (mannequin conservé).";
            }
            if (!av.path.empty() || builtinAvatar_) {
                avatarHeight_ = std::clamp(av.height, 0.5f, 2.5f);
                avatarRotation_ = av.rotationDeg;
                applyAvatarTransform();
                frameCamera();
            }
        }
        watchProject(path);   // avant le maillage : les images d'impression sont cherchées à côté du projet
        setGarment(std::move(g));
        char text[200];
        std::snprintf(text, sizeof(text), "Projet %s ouvert : %zu pièce(s), %zu couture(s).", name.c_str(), garment_.panels.size(),
                      garment_.seams.size());
        setStatus(text + avatarNote, !avatarNote.empty());
        return;
    }
    if (ext != "svg" && ext != "dxf" && ext != "pdf" && !isImageExtension(ext)) {
        if (!io::readFileBytes(path, bytes, error)) {
            setStatus("Import du patron impossible : " + error, true);
            return;
        }
        if (bytes.compare(0, 5, "%PDF-") == 0) ext = "pdf";
        else if (bytes.find("<svg") != std::string::npos && bytes.find("<svg") < 4096) ext = "svg";
        else {
            io::RasterImage img;
            if (io::decodeRasterImage(bytes, img, error)) {
                beginImageImport(std::move(img), name);
                return;
            }
            const bool webp = bytes.size() > 12 && bytes.compare(8, 4, "WEBP") == 0;
            const bool heic = bytes.size() > 12 && bytes.compare(4, 4, "ftyp") == 0;
            setStatus(webp || heic ? "Format WebP / HEIC non pris en charge : ouvrez l'image (Photos, Paint) et enregistrez-la en PNG ou JPEG."
                                   : "Format non reconnu. Formats acceptés : PDF, PNG, JPEG, BMP, SVG, DXF.",
                      true);
            return;
        }
    }

    if (isImageExtension(ext)) {
        io::RasterImage img;
        if (!io::loadRasterImage(path, img, error)) {
            setStatus("Import de l'image impossible : " + error, true);
            return;
        }
        beginImageImport(std::move(img), name);
        return;
    }
    if (ext == "pdf") {
        io::PdfPatternResult pdf;
        if ((bytes.empty() && !io::readFileBytes(path, bytes, error)) || !io::parsePdf(bytes, pdf, error)) {
            setStatus("Import du PDF impossible : " + error, true);
            return;
        }
        if (!pdf.outlines.empty()) {
            addImportedPanels(pdf.outlines, true, pdf.names);
            return;
        }
        // PDF scanné : la page est une image, on l'importe comme calque.
        io::RasterImage img = std::move(pdf.image);
        if (img.width == 0 && !io::decodeRasterImage(pdf.jpeg, img, error)) {
            setStatus("Image du PDF illisible : " + error, true);
            return;
        }
        if (pdf.pageSize.x > 0.0f) img.dpi = static_cast<float>(img.width) / (pdf.pageSize.x / 0.0254f);
        beginImageImport(std::move(img), name);
        return;
    }
    std::vector<std::vector<glm::vec2>> outlines;
    const bool ok = bytes.empty() ? io::importPatternFile(path, outlines, error)
                                  : (ext == "svg" ? io::parseSvg(bytes, outlines, error) : io::parseDxf(bytes, outlines, error));
    if (!ok) {
        setStatus("Import du patron impossible : " + error, true);
        return;
    }
    addImportedPanels(outlines, true);
}

void App::addImportedPanels(const std::vector<std::vector<glm::vec2>>& outlines, bool arrange,
                            const std::vector<std::string>& names)
{
    garment::Garment g = garment_;
    // Les pièces d'un fichier vectoriel sont rangées à droite des pièces existantes ;
    // celles détectées sur une image restent à leur place sur l'image.
    float offsetX = 0.0f;
    for (const auto& p : g.panels)
        for (const auto& q : p.points) offsetX = std::max(offsetX, q.x + 0.08f);
    glm::vec2 lo(1e9f);
    for (const auto& o : outlines)
        for (const auto& q : o) lo = glm::min(lo, q);
    // Numérotation de gauche à droite, puis de haut en bas.
    std::vector<std::size_t> order(outlines.size());
    for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
        const glm::vec2 ca = garment::centroid(outlines[a]), cb = garment::centroid(outlines[b]);
        if (std::abs(ca.x - cb.x) > 0.05f) return ca.x < cb.x;
        return ca.y > cb.y;
    });
    int added = 0;
    for (const std::size_t idx : order) {
        const auto& o = outlines[idx];
        garment::Panel panel;
        panel.points = io::simplifyOutline(o, 0.002f);
        if (panel.points.size() < 3) continue;
        if (arrange)
            for (auto& q : panel.points) q += glm::vec2(offsetX, 0.0f) - lo;
        panel.name = idx < names.size() && !names[idx].empty() ? names[idx] : "Pièce " + std::to_string(g.panels.size() + 1);
        panel.placement.angleDeg = (added % 2 == 0) ? 0.0f : 180.0f;
        g.panels.push_back(std::move(panel));
        ++added;
    }
    if (added == 0) {
        setStatus("Aucune pièce exploitable dans ce fichier.", true);
        return;
    }
    pushUndo();
    redoStack_.clear();
    redoCloth_.clear();
    garment_ = std::move(g);
    committed_ = garment_;
    editor_.clearSelection();
    editor_.frameAll(garment_);
    resetCloth();
    char text[200];
    std::snprintf(text, sizeof(text),
                  "%d pièce(s) importée(s). Vérifiez le placement (Devant / Dos) et reliez les bords avec l'outil Couture (S).", added);
    setStatus(text, false);
}

void App::beginImageImport(io::RasterImage image, const std::string& name)
{
    deleteTexture(pendingTex_);
    pendingImage_ = std::move(image);
    pendingName_ = name;
    pendingTex_ = uploadTexture(pendingImage_.width, pendingImage_.height, pendingImage_.rgba.data());
    // Taille réelle proposée : d'après la résolution du fichier, sinon 150 ppp (scan courant).
    const float dpi = pendingImage_.dpi > 1.0f ? pendingImage_.dpi : 150.0f;
    importWidthCm_ = static_cast<float>(pendingImage_.width) / dpi * 2.54f;
    showImagePopup_ = true;
}

void App::finishImageImport()
{
    showImagePopup_ = false;
    deleteTexture(backdropTex_);
    backdropImage_ = std::move(pendingImage_);
    pendingImage_ = {};
    backdropTex_ = pendingTex_;
    pendingTex_ = 0;
    backdropWidthCm_ = std::max(1.0f, importWidthCm_);
    // L'image se place à droite des pièces existantes.
    float offsetX = 0.0f;
    for (const auto& p : garment_.panels)
        for (const auto& q : p.points) offsetX = std::max(offsetX, q.x + 0.10f);
    backdropOrigin_ = {offsetX, 0.0f};
    editor_.backdrop.visible = true;
    updateBackdropRect();
    if (importDetect_) detectBackdropPieces();
    else setStatus("Image importée : tracez les pièces par-dessus avec les outils Polygone (P) ou Rectangle (B).", false);
    editor_.frameAll(garment_);
}

void App::updateBackdropRect()
{
    auto& b = editor_.backdrop;
    b.texture = static_cast<ImTextureID>(backdropTex_);
    if (backdropImage_.width <= 0) return;
    const float w = backdropWidthCm_ / 100.0f;
    const float h = w * static_cast<float>(backdropImage_.height) / static_cast<float>(backdropImage_.width);
    b.min = backdropOrigin_;
    b.max = backdropOrigin_ + glm::vec2(w, h);
}

void App::detectBackdropPieces()
{
    if (backdropImage_.width <= 0) return;
    io::TraceOptions opt;
    opt.metersPerPixel = backdropWidthCm_ / 100.0f / static_cast<float>(backdropImage_.width);
    opt.origin = backdropOrigin_;
    opt.threshold = autoThreshold_ ? -1 : threshold_;
    const auto outlines = io::traceOutlines(backdropImage_, opt);
    if (outlines.empty()) {
        setStatus("Aucune pièce détectée. Réglez le seuil dans le panneau Image de fond, ou tracez les pièces à la main "
                  "(outil Polygone) sur l'image.", true);
        return;
    }
    addImportedPanels(outlines, false);
}

void App::removeBackdrop()
{
    deleteTexture(backdropTex_);
    backdropImage_ = {};
    editor_.backdrop = {};
}

void App::newGarment()
{
    setGarment({});
    removeBackdrop();
    setStatus("Nouveau vêtement : patron vide.", false);
}

void App::saveProjectFile()
{
    if (garment_.empty()) {
        setStatus("Rien à enregistrer : le patron est vide.", true);
        return;
    }
    std::string path;
    if (hasNativeFileDialog()) {
        path = saveFileDialog("Enregistrer le projet", "Projet Drapeo|*.drapeo", "drapeo");
        if (path.empty()) return;
    } else if (pathPopupKind_ == 3 && pathInput_[0] != '\0') {
        path = pathInput_;
    } else {
        pathPopupKind_ = 3;
        std::snprintf(pathInput_, sizeof(pathInput_), "vetement.drapeo");
        showPathPopup_ = true;
        return;
    }
    std::string error;
    io::ProjectAvatar av;
    av.present = true;
    av.path = builtinAvatar_ ? std::string() : avatarPath_;
    av.height = avatarHeight_;
    av.rotationDeg = avatarRotation_;
    if (!io::saveProject(path, garment_, error, &av)) {
        setStatus("Enregistrement impossible : " + error, true);
        return;
    }
    setStatus("Projet enregistré : " + path, false);
    watchProject(path);
}

namespace {
/// Date de modification + taille d'un fichier (0 s'il n'existe pas).
long long fileStamp(const std::string& utf8Path)
{
    std::error_code ec;
    const std::filesystem::path p(std::u8string(utf8Path.begin(), utf8Path.end()));
    const auto t = std::filesystem::last_write_time(p, ec);
    if (ec) return 0;
    const auto size = std::filesystem::file_size(p, ec);
    if (ec) return 0;
    return static_cast<long long>(t.time_since_epoch().count()) ^ (static_cast<long long>(size) << 1);
}
} // namespace

void App::watchProject(const std::string& path)
{
    if (path != watchedProject_) {
        printMissing_.clear();
        printSignature_.clear();
    }
    watchedProject_ = path;
    watchedStamp_ = fileStamp(path);
}

void App::checkWatchedProject()
{
    if (watchedProject_.empty()) return;
    const double now = glfwGetTime();
    if (now - watchCheckTime_ < 0.5) return;
    watchCheckTime_ = now;
    const long long stamp = fileStamp(watchedProject_);
    if (stamp == 0 || stamp == watchedStamp_) return;
    garment::Garment g;
    std::string error;
    if (!io::loadProject(watchedProject_, g, error)) return;   // fichier en cours d'écriture : on réessaiera
    watchedStamp_ = stamp;
    printImages_.clear();   // les images d'impression ont pu être ajoutées ou remplacées
    printMissing_.clear();
    printSignature_.clear();
    // Modifié par un autre programme (assistant via le serveur MCP) : on recharge le vêtement,
    // l'avatar et l'état de la simulation (lecture / pause) ne changent pas.
    const bool countChanged = g.panels.size() != garment_.panels.size();
    pushUndo();
    redoStack_.clear();
    redoCloth_.clear();
    garment_ = std::move(g);
    committed_ = garment_;
    editor_.clearSelection();
    if (countChanged) editor_.frameAll(garment_);
    resetCloth(true);
    char text[200];
    std::snprintf(text, sizeof(text), "Projet modifié par un autre programme, rechargé : %zu pièce(s), %zu couture(s). (Ctrl+Z pour annuler)",
                  garment_.panels.size(), garment_.seams.size());
    setStatus(text, false);
}

void App::exportGarment()
{
    if (!clothValid_) {
        setStatus("Rien à exporter : le patron est vide.", true);
        return;
    }
    std::string path;
    if (hasNativeFileDialog()) {
        path = saveFileDialog("Exporter le vêtement", "Wavefront OBJ|*.obj", "obj");
        if (path.empty()) return;
    } else if (pathPopupKind_ == 2 && pathInput_[0] != '\0') {
        path = pathInput_;
    } else {
        pathPopupKind_ = 2;
        std::snprintf(pathInput_, sizeof(pathInput_), "vetement.obj");
        showPathPopup_ = true;
        return;
    }
    geometry::computeVertexNormals(solver_.positions(), solver_.indices(), normals_);
    io::ExportMesh m;
    m.positions = &solver_.positions();
    m.normals = &normals_;
    m.uvs = &garmentMesh_.mesh.uvs;
    m.indices = &solver_.indices();
    m.panelOfVertex = &garmentMesh_.panelOfVertex;
    for (const auto& p : garment_.panels) m.panelNames.push_back(p.name);
    // Export des pièces sélectionnées seulement : sommets et triangles de ces pièces, renumérotés.
    std::vector<glm::vec3> subPos, subNrm;
    std::vector<glm::vec2> subUv;
    std::vector<uint32_t> subIdx;
    std::vector<int> subPanel;
    if (!exportOnly_.empty()) {
        const auto& pov = garmentMesh_.panelOfVertex;
        const auto& pos = solver_.positions();
        std::vector<int> remap(pos.size(), -1);
        auto keep = [&](uint32_t v) { return v < pov.size() && std::find(exportOnly_.begin(), exportOnly_.end(), pov[v]) != exportOnly_.end(); };
        const auto& idx = solver_.indices();
        for (std::size_t t = 0; t + 2 < idx.size(); t += 3) {
            if (!keep(idx[t]) || !keep(idx[t + 1]) || !keep(idx[t + 2])) continue;
            for (int k = 0; k < 3; ++k) {
                const uint32_t v = idx[t + static_cast<std::size_t>(k)];
                if (remap[v] < 0) {
                    remap[v] = static_cast<int>(subPos.size());
                    subPos.push_back(pos[v]);
                    subNrm.push_back(v < normals_.size() ? normals_[v] : glm::vec3(0, 1, 0));
                    subUv.push_back(v < garmentMesh_.mesh.uvs.size() ? garmentMesh_.mesh.uvs[v] : glm::vec2(0.0f));
                    subPanel.push_back(pov[v]);
                }
                subIdx.push_back(static_cast<uint32_t>(remap[v]));
            }
        }
        if (subIdx.empty()) {
            setStatus("Les pièces choisies ne sont pas dans la vue 3D (désactivées ?).", true);
            exportOnly_.clear();
            return;
        }
        m.positions = &subPos;
        m.normals = &subNrm;
        m.uvs = &subUv;
        m.indices = &subIdx;
        m.panelOfVertex = &subPanel;
    }
    std::string error;
    const bool partial = !exportOnly_.empty();
    exportOnly_.clear();
    if (io::exportObj(path, m, error)) setStatus(std::string(partial ? "Pièces exportées : " : "Vêtement exporté : ") + path, false);
    else setStatus("Export impossible : " + error, true);
}

// =============================================================================
//  Avatar
// =============================================================================
void App::loadDefaultMannequin()
{
    avatarSource_ = {};
    geometry::makeMannequin(avatarSource_.positions, avatarSource_.indices);
    avatarSource_.name = "Mannequin Drapeo";
    avatarSource_.sourceObjects = 1;
    avatarSource_.sourceHeight = 1.75f;
    builtinAvatar_ = true;
    avatarPath_.clear();
    avatarHeight_ = 1.75f;
    avatarRotation_ = 0.0f;
    applyAvatarTransform();
    frameCamera();
}

bool App::loadAvatar(const std::string& path)
{
    if (path.empty()) return false;
    io::ImportedMesh mesh;
    std::string error;
    if (!io::importMesh(path, mesh, error)) {
        setStatus("Import impossible : " + error, true);
        return false;
    }
    avatarSource_ = std::move(mesh);
    builtinAvatar_ = false;
    avatarPath_ = path;
    avatarRotation_ = 0.0f;
    if (options_.frames <= 0) io::rememberAvatar(path);   // rechargé au prochain démarrage
    // Hauteur proposée : celle du fichier si elle est plausible, sinon 1,75 m.
    const float h = avatarSource_.sourceHeight;
    avatarHeight_ = (h > 0.3f && h < 3.0f) ? h : 1.75f;
    applyAvatarTransform();
    frameCamera();

    char text[256];
    if (h > 0.3f && h < 3.0f) {
        std::snprintf(text, sizeof(text), "Avatar chargé : %zu triangles, %.2f m de haut.", avatarSource_.indices.size() / 3,
                      static_cast<double>(h));
    } else {
        std::snprintf(text, sizeof(text), "Avatar chargé : %zu triangles. Unité inconnue, taille réglée à 1,75 m.",
                      avatarSource_.indices.size() / 3);
    }
    setStatus(text, false);
    return true;
}

void App::buildSilhouette()
{
    // Projection des triangles de l'avatar dans une image : l'ombre du corps sous le patron,
    // vue de face (x, y), de profil (z, y : devant à droite) ou du dessus (x, z : devant en haut).
    if (silhouetteTex_ != 0) {
        const gl::GLuint t = silhouetteTex_;
        gl::DeleteTextures(1, &t);
        silhouetteTex_ = 0;
    }
    editor_.silhouette.texture = 0;
    const auto& pos = avatarPositions_;
    const auto& idx = avatarSource_.indices;
    if (pos.empty() || idx.size() < 3) return;
    const int view = editor_.silhouette.view;
    auto project = [view](const glm::vec3& p) {
        if (view == 1) return glm::vec2(p.z, p.y);
        if (view == 2) return glm::vec2(p.x, p.z);
        return glm::vec2(p.x, p.y);
    };
    glm::vec2 lo(1e9f), hi(-1e9f);
    for (const auto& p : pos) {
        lo = glm::min(lo, project(p));
        hi = glm::max(hi, project(p));
    }
    const float ppm = 400.0f;   // pixels par mètre
    const int w = std::clamp(static_cast<int>((hi.x - lo.x) * ppm) + 4, 8, 2048);
    const int h = std::clamp(static_cast<int>((hi.y - lo.y) * ppm) + 4, 8, 2048);
    const float sx = static_cast<float>(w - 4) / std::max(hi.x - lo.x, 1e-3f), sy = static_cast<float>(h - 4) / std::max(hi.y - lo.y, 1e-3f);
    std::vector<unsigned char> mask(static_cast<std::size_t>(w) * static_cast<std::size_t>(h), 0);
    auto toPx = [&](const glm::vec3& p3) {
        const glm::vec2 p = project(p3);
        return glm::vec2((p.x - lo.x) * sx + 2.0f, (hi.y - p.y) * sy + 2.0f);
    };
    for (std::size_t t = 0; t + 2 < idx.size(); t += 3) {
        if (idx[t] >= pos.size() || idx[t + 1] >= pos.size() || idx[t + 2] >= pos.size()) continue;
        const glm::vec2 a = toPx(pos[idx[t]]), b = toPx(pos[idx[t + 1]]), c = toPx(pos[idx[t + 2]]);
        const float area = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
        if (std::abs(area) < 1e-6f) continue;
        const int x0 = std::max(0, static_cast<int>(std::floor(std::min({a.x, b.x, c.x})))), x1 = std::min(w - 1, static_cast<int>(std::ceil(std::max({a.x, b.x, c.x}))));
        const int y0 = std::max(0, static_cast<int>(std::floor(std::min({a.y, b.y, c.y})))), y1 = std::min(h - 1, static_cast<int>(std::ceil(std::max({a.y, b.y, c.y}))));
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x) {
                const glm::vec2 p(static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f);
                const float w0 = ((b.x - p.x) * (c.y - p.y) - (b.y - p.y) * (c.x - p.x)) / area;
                const float w1 = ((c.x - p.x) * (a.y - p.y) - (c.y - p.y) * (a.x - p.x)) / area;
                if (w0 >= -0.02f && w1 >= -0.02f && w0 + w1 <= 1.02f) mask[static_cast<std::size_t>(y) * w + x] = 1;
            }
    }
    std::vector<unsigned char> rgba(mask.size() * 4);
    for (std::size_t i = 0; i < mask.size(); ++i) {
        rgba[i * 4 + 0] = 205;
        rgba[i * 4 + 1] = 210;
        rgba[i * 4 + 2] = 222;
        rgba[i * 4 + 3] = mask[i] ? 44 : 0;
    }
    silhouetteTex_ = uploadTexture(w, h, rgba.data());
    editor_.silhouette.texture = static_cast<ImTextureID>(silhouetteTex_);
    editor_.silhouette.min = lo - glm::vec2(2.0f / sx, 2.0f / sy);
    editor_.silhouette.max = hi + glm::vec2(2.0f / sx, 2.0f / sy);
}

void App::applyAvatarTransform()
{
    avatarPositions_ = avatarSource_.positions;
    io::FitOptions fit;
    fit.targetHeight = avatarHeight_;
    fit.rotationYDegrees = avatarRotation_;
    io::fitToGround(avatarPositions_, fit);
    bodyAxes_ = builtinAvatar_ ? garment::BodyAxes{} : garment::estimateBodyAxes(avatarPositions_);
    geometry::computeVertexNormals(avatarPositions_, avatarSource_.indices, avatarNormals_);
    avatarBvh_.build(avatarPositions_, avatarSource_.indices);
    if (showAvatar_) renderer_.setAvatarMesh(avatarPositions_, avatarNormals_, avatarSource_.indices);
    else renderer_.clearAvatar();
    buildSilhouette();
    solver_.setMeshCollider(&avatarBvh_);
    resetCloth();   // le placement des pièces suit la taille de l'avatar
}

void App::frameCamera()
{
    camera_.target = glm::vec3(0.0f, avatarHeight_ * 0.56f, 0.0f);
    camera_.distance = avatarHeight_ * 1.75f;
    camera_.yaw = 0.45f;
    camera_.pitch = 0.12f;
}

// =============================================================================
//  Entrées
// =============================================================================
void App::computeLayout()
{
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    menuHeight_ = ImGui::GetFrameHeight();
    toolbarHeight_ = std::round(50.0f * uiScale_);
    statusHeight_ = std::round(ImGui::GetFrameHeight() + 8.0f * uiScale_);
    inspectorWidth_ = std::round(360.0f * uiScale_);
    const float splitter = 5.0f * uiScale_;
    const float free = display.x - inspectorWidth_;
    if (leftWidth_ < 0.0f) leftWidth_ = std::round(free * 0.42f);
    leftWidth_ = std::clamp(leftWidth_, 240.0f * uiScale_, std::max(240.0f * uiScale_, free - 260.0f * uiScale_));
    const float top = menuHeight_ + toolbarHeight_;
    viewPos_ = {leftWidth_ + splitter, top};
    viewSize_ = {std::max(1.0f, free - viewPos_.x), std::max(1.0f, display.y - top - statusHeight_)};
}

void App::handleShortcuts()
{
    const ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput) return;
    const bool ctrl = io.KeyCtrl;
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Z, false)) io.KeyShift ? redo() : undo();
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Y, false)) redo();
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_O, false)) openAvatarDialog();
    const bool shift = io.KeyShift;
    if (ctrl && !shift && ImGui::IsKeyPressed(ImGuiKey_I, false)) openPatternDialog();
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_E, false)) {
        exportOnly_.clear();
        exportGarment();
    }
    // Menu des pièces (vue patron)
    if (ctrl && shift && ImGui::IsKeyPressed(ImGuiKey_I, false)) pieceAction(PieceAction::InvertSelection);
    if (ctrl && !shift && ImGui::IsKeyPressed(ImGuiKey_C, false)) pieceAction(PieceAction::Copy);
    if (ctrl && !shift && ImGui::IsKeyPressed(ImGuiKey_V, false)) pasteClipboard(false, false);
    if (ctrl && !shift && ImGui::IsKeyPressed(ImGuiKey_R, false)) pasteClipboard(true, false);
    if (ctrl && !shift && ImGui::IsKeyPressed(ImGuiKey_D, false)) pieceAction(PieceAction::CloneMirrorSewn);
    if (ctrl && shift && ImGui::IsKeyPressed(ImGuiKey_V, false)) pieceAction(PieceAction::CloneOver);
    if (ctrl && shift && ImGui::IsKeyPressed(ImGuiKey_B, false)) pieceAction(PieceAction::CloneUnder);
    if (ctrl && !shift && ImGui::IsKeyPressed(ImGuiKey_J, false)) pieceAction(PieceAction::Deactivate);
    const auto all = [&](bool (*test)(const garment::Panel&)) {
        const auto t = editor_.targets();
        return !t.empty() && std::all_of(t.begin(), t.end(), [&](int p) {
            return p >= 0 && p < static_cast<int>(garment_.panels.size()) && test(garment_.panels[static_cast<std::size_t>(p)]);
        });
    };
    if (ctrl && !shift && ImGui::IsKeyPressed(ImGuiKey_K, false))
        pieceAction(all([](const garment::Panel& p) { return p.frozen; }) ? PieceAction::Unfreeze : PieceAction::Freeze);
    if (ctrl && !shift && ImGui::IsKeyPressed(ImGuiKey_H, false))
        pieceAction(all([](const garment::Panel& p) { return p.stiffness > 1.0f; }) ? PieceAction::Soften : PieceAction::Strengthen);
    if (!ctrl && shift && ImGui::IsKeyPressed(ImGuiKey_Q, false))
        pieceAction(all([](const garment::Panel& p) { return p.hidden3D; }) ? PieceAction::Show : PieceAction::Hide);
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_S, false)) saveProjectFile();
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_N, false)) newGarment();
    if (!ctrl && ImGui::IsKeyPressed(ImGuiKey_Space, false)) playing_ = !playing_;
    if (!ctrl && ImGui::IsKeyPressed(ImGuiKey_R, false)) resetCloth();
}

void App::handleViewportInput()
{
    // Sélection de la vue patron : la pièce est mise en évidence dans la vue 3D.
    if (auto current = editor_.targets(); current != highlighted_) {
        highlighted_ = std::move(current);
        selectionEdges_.clear();
        updateClothColors();
    }
    const ImGuiIO& io = ImGui::GetIO();
    const ImVec2 m = io.MousePos;
    const bool inside = m.x >= viewPos_.x && m.y >= viewPos_.y && m.x < viewPos_.x + viewSize_.x &&
                        m.y < viewPos_.y + viewSize_.y && !io.WantCaptureMouse;
    const float aspect = viewSize_.x / viewSize_.y;
    const float ndcX = 2.0f * (m.x - viewPos_.x) / viewSize_.x - 1.0f;
    const float ndcY = 1.0f - 2.0f * (m.y - viewPos_.y) / viewSize_.y;

    // Particule la plus proche du rayon de la souris (-1 si aucune).
    const auto pickParticle = [&](float& depth) {
        glm::vec3 o, d;
        camera_.ray(ndcX, ndcY, aspect, o, d);
        const auto& pos = solver_.positions();
        int best = -1;
        depth = 1e9f;
        const float pickRadius = std::max(0.03f, solver_.restSpacing() * 1.5f);
        for (std::size_t i = 0; i < pos.size(); ++i) {
            const float t = glm::dot(pos[i] - o, d);
            if (t <= 0.0f || t >= depth) continue;
            if (glm::length(pos[i] - (o + d * t)) < pickRadius) {
                best = static_cast<int>(i);
                depth = t;
            }
        }
        return best;
    };

    // Survol : poignées du gizmo, puis lignes de couture.
    updateGizmoFrame();
    const glm::vec2 mouse(m.x, m.y);
    const bool busy = gizmoActive_ != Gizmo::None || dragPanel_ >= 0 || orbiting_ || panning_ || solver_.grabbedParticle() >= 0;
    if (!busy) {
        gizmoHover_ = inside ? gizmoHitTest(mouse) : Gizmo::None;
        hoverSeam3D_ = -1;
        if (inside && gizmoHover_ == Gizmo::None) {
            // Une couture n'est cliquable que si elle passe devant le tissu sous la souris
            // (ou avec Alt, qui donne la priorité aux coutures).
            float seamDepth = 0.0f;
            const int s = seamHitTest3D(mouse, &seamDepth);
            if (s >= 0) {
                float clothDepth = 1e9f;
                const int p = clothValid_ && !io.KeyAlt ? pickParticle(clothDepth) : -1;
                if (p < 0 || seamDepth < clothDepth - 0.015f) hoverSeam3D_ = s;
            }
        }
    }

    if (inside) {
        bool pickedPanel = false;
        // 1. Poignée du gizmo
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && gizmoVisible() && gizmoHover_ != Gizmo::None) {
            gizmoActive_ = gizmoHover_;
            gizmoMoved_ = false;
            dragStartCloth_ = clothSnapshot();
            gizmoLastHit_ = glm::vec3(0.0f);
            const glm::vec2 c = projectToScreen(gizmoCenter_);
            gizmoLastAngle_ = std::atan2(mouse.y - c.y, mouse.x - c.x);
            if (gizmoActive_ == Gizmo::Slide) {
                dragPanel_ = editor_.selectedPanel;
                dragMoved_ = false;
                dragLastHit_ = gizmoCenter_;
            }
            pickedPanel = true;
        }
        // 2. Ligne de couture : activer / désactiver
        if (!pickedPanel && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && hoverSeam3D_ >= 0 && !io.KeyShift) {
            toggleSeam(hoverSeam3D_);
            pickedPanel = true;
        }
        // 3. Clic gauche sur une pièce = la sélectionner (aussi dans la vue patron) ; glisser = la faire
        //    tourner autour du corps (la simulation se met en pause dès que la pièce bouge).
        if (!pickedPanel && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && clothValid_ && !io.KeyShift) {
            float depth = 0.0f;
            const int p = pickParticle(depth);
            if (p >= 0 && static_cast<std::size_t>(p) < garmentMesh_.panelOfVertex.size()) {
                dragPanel_ = garmentMesh_.panelOfVertex[static_cast<std::size_t>(p)];
                // Une pièce posée (poche...) suit sa base : on déplace la base.
                if (const int ai = garment_.attachmentOf(dragPanel_); ai >= 0) dragPanel_ = garment_.attachments[static_cast<std::size_t>(ai)].base;
                dragMoved_ = false;
                dragStartCloth_ = clothSnapshot();
                dragLastHit_ = solver_.positions()[static_cast<std::size_t>(p)];
                editor_.select({dragPanel_});
                pickedPanel = true;
            }
        }
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !pickedPanel) (io.KeyShift ? panning_ : orbiting_) = true;
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) panning_ = true;
        if (io.MouseWheel != 0.0f) camera_.zoom(std::pow(0.9f, io.MouseWheel));
        if (!io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_F, false)) frameCamera();

        // Clic droit : saisir la particule la plus proche du rayon de la souris.
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right) && clothValid_) {
            float bestT = 0.0f;
            const int best = pickParticle(bestT);
            if (best >= 0) {
                grabStartCloth_ = clothSnapshot();   // tissu tiré à la souris : Ctrl+Z le remet comme avant
                solver_.grab(static_cast<uint32_t>(best));
                grabDepth_ = bestT;
            }
        }
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) orbiting_ = false;
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) && !ImGui::IsMouseDown(ImGuiMouseButton_Middle)) panning_ = false;

    if (dragPanel_ >= 0) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left) && clothValid_) {
            if (io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f) {
                if (playing_) {   // pièce saisie pendant la simulation : pause pour la mise en place
                    playing_ = false;
                    refreshPanelParticles(dragPanel_);
                }
                glm::vec3 o, d;
                camera_.ray(ndcX, ndcY, aspect, o, d);
                dragPanelTo(o, d);
            }
        } else {
            // Fin du déplacement : nouvel état du patron (annulable), maillage refait, toujours en pause.
            if (dragMoved_) finishPanelEdit(dragPanel_);
            dragPanel_ = -1;
            dragMoved_ = false;
            if (gizmoActive_ == Gizmo::Slide) gizmoActive_ = Gizmo::None;
        }
    }
    if (gizmoActive_ != Gizmo::None && gizmoActive_ != Gizmo::Slide) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left) && clothValid_) {
            if (io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f) dragGizmo(mouse, {io.MouseDelta.x, io.MouseDelta.y});
        } else {
            if (gizmoMoved_) finishPanelEdit(editor_.selectedPanel);
            gizmoActive_ = Gizmo::None;
            gizmoMoved_ = false;
        }
    }

    const float dx = io.MouseDelta.x, dy = io.MouseDelta.y;
    if (orbiting_) camera_.orbit(-dx * 0.006f, dy * 0.006f);
    if (panning_) camera_.pan(dx * 0.0012f, dy * 0.0012f);

    if (clothValid_ && solver_.grabbedParticle() >= 0) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
            glm::vec3 o, d;
            camera_.ray(ndcX, ndcY, aspect, o, d);
            solver_.moveGrabbed(o + d * grabDepth_);
        } else {
            solver_.release();
            if (!grabStartCloth_.positions.empty()) {
                pushUndo(&grabStartCloth_);
                redoStack_.clear();
                redoCloth_.clear();
                grabStartCloth_ = {};
            }
        }
    }
}

// =============================================================================
//  Gizmo 3D et coutures dans la vue 3D
// =============================================================================
namespace {
const glm::vec3 kAxes[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
const ImU32 kAxisColors[3] = {IM_COL32(232, 76, 76, 255), IM_COL32(126, 200, 60, 255), IM_COL32(70, 132, 236, 255)};
ImU32 brighten(ImU32 c) { return (c & 0xFF000000) | ((c & 0x00FEFEFE) >> 1) | 0x00808080; }
} // namespace

glm::vec2 App::projectToScreen(const glm::vec3& p, bool* inFront) const
{
    const float aspect = viewSize_.x / viewSize_.y;
    const glm::vec4 c = camera_.projection(aspect) * camera_.view() * glm::vec4(p, 1.0f);
    if (inFront) *inFront = c.w > 1e-4f;
    const float w = std::abs(c.w) > 1e-6f ? c.w : 1e-6f;
    return {viewPos_.x + (c.x / w * 0.5f + 0.5f) * viewSize_.x, viewPos_.y + (0.5f - c.y / w * 0.5f) * viewSize_.y};
}

bool App::gizmoVisible() const
{
    return !playing_ && clothValid_ && editor_.selectedPanel >= 0 && editor_.selectedPanel < static_cast<int>(garment_.panels.size()) &&
           garment_.attachmentOf(editor_.selectedPanel) < 0;   // une pièce posée suit sa base
}

void App::updateGizmoFrame()
{
    if (!gizmoVisible()) return;
    // Centre : barycentre actuel des particules de la pièce.
    glm::vec3 c(0.0f);
    int n = 0;
    const auto& pos = solver_.positions();
    for (std::size_t i = 0; i < garmentMesh_.panelOfVertex.size() && i < pos.size(); ++i)
        if (garmentMesh_.panelOfVertex[i] == editor_.selectedPanel) {
            c += pos[i];
            ++n;
        }
    if (n == 0) return;
    gizmoCenter_ = c / static_cast<float>(n);
    // Taille constante à l'écran (~85 px).
    const float dist = std::max(glm::distance(camera_.position(), gizmoCenter_), 0.05f);
    const float worldPerPixel = 2.0f * dist * std::tan(glm::radians(camera_.fovY) * 0.5f) / std::max(viewSize_.y, 1.0f);
    gizmoLength_ = 85.0f * uiScale_ * worldPerPixel;
}

App::Gizmo App::gizmoHitTest(glm::vec2 m) const
{
    if (!gizmoVisible()) return Gizmo::None;
    const glm::vec2 c = projectToScreen(gizmoCenter_);
    const float L = gizmoLength_;
    const auto segDist = [](glm::vec2 p, glm::vec2 a, glm::vec2 b) {
        const glm::vec2 ab = b - a;
        const float t = glm::clamp(glm::dot(p - a, ab) / std::max(glm::dot(ab, ab), 1e-6f), 0.0f, 1.0f);
        return glm::distance(p, a + ab * t);
    };
    const float tol = 7.0f * uiScale_;
    // 1. Disque central : glisser autour du corps.
    if (glm::distance(m, c) < 13.0f * uiScale_) return Gizmo::Slide;
    // 2. Petits carrés : déplacement dans un plan.
    for (int k = 0; k < 3; ++k) {
        const glm::vec3 a = kAxes[(k + 1) % 3], b = kAxes[(k + 2) % 3];
        const glm::vec3 q0 = gizmoCenter_ + (a + b) * (0.30f * L), q1 = q0 + a * (0.16f * L), q2 = q1 + b * (0.16f * L), q3 = q0 + b * (0.16f * L);
        const glm::vec2 s[4] = {projectToScreen(q0), projectToScreen(q1), projectToScreen(q2), projectToScreen(q3)};
        const std::vector<glm::vec2> poly(s, s + 4);
        if (garment::pointInPolygon(poly, m)) return static_cast<Gizmo>(static_cast<int>(Gizmo::PlaneYZ) + k);
    }
    // 3. Flèches : déplacement sur un axe.
    for (int k = 0; k < 3; ++k) {
        const glm::vec2 tip = projectToScreen(gizmoCenter_ + kAxes[k] * L);
        if (glm::distance(m, c) > 15.0f * uiScale_ && segDist(m, c, tip) < tol) return static_cast<Gizmo>(static_cast<int>(Gizmo::MoveX) + k);
    }
    // 4. Anneaux : rotation autour d'un axe.
    for (int k = 0; k < 3; ++k) {
        const glm::vec3 a = kAxes[(k + 1) % 3], b = kAxes[(k + 2) % 3];
        glm::vec2 prev = projectToScreen(gizmoCenter_ + a * (1.25f * L));
        for (int i = 1; i <= 48; ++i) {
            const float t = static_cast<float>(i) / 48.0f * 2.0f * kPi;
            const glm::vec2 cur = projectToScreen(gizmoCenter_ + (a * std::cos(t) + b * std::sin(t)) * (1.25f * L));
            if (segDist(m, prev, cur) < tol * 0.8f) return static_cast<Gizmo>(static_cast<int>(Gizmo::RotX) + k);
            prev = cur;
        }
    }
    return Gizmo::None;
}

void App::refreshPanelParticles(int panel)
{
    if (panel < 0 || panel >= static_cast<int>(garment_.panels.size())) return;
    const garment::MeshingOptions mo = meshingOptions();
    for (std::size_t i = 0; i < garmentMesh_.panelOfVertex.size() && i < solver_.positions().size(); ++i) {
        const int pv = garmentMesh_.panelOfVertex[i];
        const int ai = pv >= 0 ? garment_.attachmentOf(pv) : -1;
        // la pièce, et les pièces posées dessus (poches, rabats...)
        if (pv == panel || (ai >= 0 && garment_.attachments[static_cast<std::size_t>(ai)].base == panel))
            solver_.setParticlePosition(static_cast<uint32_t>(i), garment::placeGarmentPoint(garment_, pv, garmentMesh_.mesh.uvs[i], mo));
    }
}

void App::finishPanelEdit(int panel)
{
    if (panel < 0 || panel >= static_cast<int>(garment_.panels.size())) return;
    pushUndo(dragStartCloth_.positions.empty() ? nullptr : &dragStartCloth_);
    dragStartCloth_ = {};
    redoStack_.clear();
    redoCloth_.clear();
    committed_ = garment_;
    resetCloth(true);   // les autres pièces gardent leur drapé
    playing_ = false;   // on reste en pause pour continuer la mise en place
    const auto& pl = garment_.panels[static_cast<std::size_t>(panel)].placement;
    char text[220];
    const float turn = glm::degrees(2.0f * std::acos(std::clamp(std::abs(pl.rotation.w), 0.0f, 1.0f)));
    std::snprintf(text, sizeof(text), "%s : angle %.0f°, hauteur %.0f cm, décalage %.0f / %.0f / %.0f cm, rotation %.0f°. Espace pour simuler.",
                  garment_.panels[static_cast<std::size_t>(panel)].name.c_str(), static_cast<double>(pl.angleDeg),
                  static_cast<double>(pl.height * 100.0f), static_cast<double>(pl.offset.x * 100.0f),
                  static_cast<double>(pl.offset.y * 100.0f), static_cast<double>(pl.offset.z * 100.0f), static_cast<double>(turn));
    setStatus(text, false);
}

void App::dragGizmo(glm::vec2 m, glm::vec2 delta)
{
    const int idx = editor_.selectedPanel;
    if (idx < 0 || idx >= static_cast<int>(garment_.panels.size())) return;
    auto& pl = garment_.panels[static_cast<std::size_t>(idx)].placement;
    const float L = gizmoLength_;
    const glm::vec2 c = projectToScreen(gizmoCenter_);
    const int g = static_cast<int>(gizmoActive_);
    if (gizmoActive_ >= Gizmo::MoveX && gizmoActive_ <= Gizmo::MoveZ) {
        const glm::vec3 axis = kAxes[g - static_cast<int>(Gizmo::MoveX)];
        const glm::vec2 s = projectToScreen(gizmoCenter_ + axis * L) - c;   // pixels pour L mètres
        const float len2 = glm::dot(s, s);
        if (len2 < 4.0f) return;
        const float d = glm::dot(delta, s) / len2 * L;
        pl.offset += axis * d;
        gizmoCenter_ += axis * d;
    } else if (gizmoActive_ >= Gizmo::PlaneYZ && gizmoActive_ <= Gizmo::PlaneXY) {
        const glm::vec3 n = kAxes[g - static_cast<int>(Gizmo::PlaneYZ)];
        const float aspect = viewSize_.x / viewSize_.y;
        glm::vec3 o, d;
        camera_.ray(2.0f * (m.x - viewPos_.x) / viewSize_.x - 1.0f, 1.0f - 2.0f * (m.y - viewPos_.y) / viewSize_.y, aspect, o, d);
        const float den = glm::dot(d, n);
        if (std::abs(den) < 1e-3f) return;
        const glm::vec3 hit = o + d * (glm::dot(gizmoCenter_ - o, n) / den);
        if (gizmoMoved_ || glm::length(gizmoLastHit_) > 0.0f) {
            const glm::vec3 step = hit - gizmoLastHit_;
            pl.offset += step;
            gizmoCenter_ += step;
        }
        gizmoLastHit_ = hit;
    } else if (gizmoActive_ >= Gizmo::RotX && gizmoActive_ <= Gizmo::RotZ) {
        const glm::vec3 axis = kAxes[g - static_cast<int>(Gizmo::RotX)];
        const float a = std::atan2(m.y - c.y, m.x - c.x);
        float da = a - gizmoLastAngle_;
        while (da > kPi) da -= 2 * kPi;
        while (da < -kPi) da += 2 * kPi;
        gizmoLastAngle_ = a;
        // Écran : y vers le bas -> un angle croissant tourne dans le sens horaire.
        const float facing = glm::dot(axis, camera_.position() - gizmoCenter_) >= 0.0f ? 1.0f : -1.0f;
        pl.rotation = glm::normalize(glm::angleAxis(-da * facing, axis) * pl.rotation);
    }
    refreshPanelParticles(idx);
    gizmoMoved_ = true;
}

void App::drawGizmo()
{
    if (!gizmoVisible()) return;
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    dl->PushClipRect({viewPos_.x, viewPos_.y}, {viewPos_.x + viewSize_.x, viewPos_.y + viewSize_.y}, true);
    const glm::vec2 c = projectToScreen(gizmoCenter_);
    const float L = gizmoLength_;
    const auto hl = [&](Gizmo g) { return gizmoHover_ == g || gizmoActive_ == g; };
    const auto iv = [](glm::vec2 v) { return ImVec2(v.x, v.y); };
    // Anneaux de rotation
    for (int k = 0; k < 3; ++k) {
        const Gizmo g = static_cast<Gizmo>(static_cast<int>(Gizmo::RotX) + k);
        const glm::vec3 a = kAxes[(k + 1) % 3], b = kAxes[(k + 2) % 3];
        std::vector<ImVec2> ring;
        for (int i = 0; i < 64; ++i) {
            const float t = static_cast<float>(i) / 64.0f * 2.0f * kPi;
            ring.push_back(iv(projectToScreen(gizmoCenter_ + (a * std::cos(t) + b * std::sin(t)) * (1.25f * L))));
        }
        const ImU32 col = hl(g) ? brighten(kAxisColors[k]) : ((kAxisColors[k] & 0x00FFFFFF) | 0xB0000000);
        dl->AddPolyline(ring.data(), static_cast<int>(ring.size()), col, ImDrawFlags_Closed, hl(g) ? 3.5f : 2.0f);
    }
    // Carrés de plan
    for (int k = 0; k < 3; ++k) {
        const Gizmo g = static_cast<Gizmo>(static_cast<int>(Gizmo::PlaneYZ) + k);
        const glm::vec3 a = kAxes[(k + 1) % 3], b = kAxes[(k + 2) % 3];
        const glm::vec3 q0 = gizmoCenter_ + (a + b) * (0.30f * L), q1 = q0 + a * (0.16f * L), q2 = q1 + b * (0.16f * L), q3 = q0 + b * (0.16f * L);
        const ImU32 col = hl(g) ? brighten(kAxisColors[k]) : ((kAxisColors[k] & 0x00FFFFFF) | 0xC0000000);
        dl->AddQuadFilled(iv(projectToScreen(q0)), iv(projectToScreen(q1)), iv(projectToScreen(q2)), iv(projectToScreen(q3)), col);
    }
    // Flèches
    for (int k = 0; k < 3; ++k) {
        const Gizmo g = static_cast<Gizmo>(static_cast<int>(Gizmo::MoveX) + k);
        const glm::vec2 tip = projectToScreen(gizmoCenter_ + kAxes[k] * L);
        const ImU32 col = hl(g) ? brighten(kAxisColors[k]) : kAxisColors[k];
        const glm::vec2 dir = tip - c;
        const float len = glm::length(dir);
        if (len < 4.0f) continue;
        const glm::vec2 u = dir / len, nrm(-u.y, u.x);
        const float head = 13.0f * uiScale_;
        dl->AddLine(iv(c + u * (14.0f * uiScale_)), iv(tip - u * head), col, hl(g) ? 4.0f : 2.6f);
        dl->AddTriangleFilled(iv(tip), iv(tip - u * head + nrm * (head * 0.42f)), iv(tip - u * head - nrm * (head * 0.42f)), col);
    }
    // Disque central : glisser la pièce autour du corps
    const float r = 11.0f * uiScale_;
    dl->AddCircle(iv(c), r, hl(Gizmo::Slide) ? IM_COL32(255, 255, 255, 255) : IM_COL32(235, 235, 235, 210), 32, hl(Gizmo::Slide) ? 3.0f : 2.0f);
    dl->AddCircleFilled(iv(c), 3.0f * uiScale_, kAccentU32);
    // Bulle d'aide
    const char* tip = nullptr;
    switch (gizmoHover_ == Gizmo::None ? gizmoActive_ : gizmoHover_) {
    case Gizmo::Slide: tip = "Glisser autour du corps"; break;
    case Gizmo::MoveX: case Gizmo::MoveY: case Gizmo::MoveZ: tip = "Déplacer sur l'axe"; break;
    case Gizmo::PlaneYZ: case Gizmo::PlaneXZ: case Gizmo::PlaneXY: tip = "Déplacer dans le plan"; break;
    case Gizmo::RotX: case Gizmo::RotY: case Gizmo::RotZ: tip = "Tourner la pièce"; break;
    default: break;
    }
    if (tip) {
        const ImVec2 mp = ImGui::GetIO().MousePos;
        const ImVec2 ts = ImGui::CalcTextSize(tip);
        dl->AddRectFilled({mp.x + 14, mp.y + 12}, {mp.x + 22 + ts.x, mp.y + 16 + ts.y + 4}, IM_COL32(15, 16, 18, 225), 4.0f);
        dl->AddText({mp.x + 18, mp.y + 14}, IM_COL32(235, 235, 235, 255), tip);
    }
    dl->PopClipRect();
}

int App::seamHitTest3D(glm::vec2 m, float* depth) const
{
    if (!showSeams3D_ || !clothValid_) return -1;
    const auto& pos = solver_.positions();
    float best = 4.5f * uiScale_;
    int hit = -1;
    for (std::size_t k = 0; k < garmentMesh_.allSeamPairs.size(); ++k) {
        const auto& pr = garmentMesh_.allSeamPairs[k];
        if (pr[0] >= pos.size() || pr[1] >= pos.size()) continue;
        if (glm::distance(pos[pr[0]], pos[pr[1]]) < 0.008f) continue;   // déjà cousue
        bool f0 = false, f1 = false;
        const glm::vec2 a = projectToScreen(pos[pr[0]], &f0), b = projectToScreen(pos[pr[1]], &f1);
        if (!f0 || !f1) continue;
        const glm::vec2 ab = b - a;
        const float t = glm::clamp(glm::dot(m - a, ab) / std::max(glm::dot(ab, ab), 1e-6f), 0.0f, 1.0f);
        const float d = glm::distance(m, a + ab * t);
        if (d < best) {
            best = d;
            hit = garmentMesh_.seamOfPair[k];
            if (depth) *depth = glm::distance(camera_.position(), glm::mix(pos[pr[0]], pos[pr[1]], t));
        }
    }
    return hit;
}

void App::drawSelection3D()
{
    // Contour de la pièce sélectionnée, tracé sur le tissu en 3D.
    if (!clothValid_ || highlighted_.empty() || highlighted_.front() < 0) return;
    const auto& pov = garmentMesh_.panelOfVertex;
    auto chosen = [&](uint32_t v) {
        if (v >= pov.size()) return false;
        const int p = pov[v];
        return std::find(highlighted_.begin(), highlighted_.end(), p) != highlighted_.end() && p < static_cast<int>(garment_.panels.size()) &&
               !garment_.panels[static_cast<std::size_t>(p)].hidden3D;
    };
    if (selectionEdges_.empty()) {   // bords de la pièce : arêtes d'un seul triangle
        std::unordered_map<uint64_t, int> count;
        const auto& idx = garmentMesh_.mesh.indices;
        for (std::size_t t = 0; t + 2 < idx.size(); t += 3) {
            if (!chosen(idx[t]) || !chosen(idx[t + 1]) || !chosen(idx[t + 2])) continue;
            for (int k = 0; k < 3; ++k) {
                uint32_t a = idx[t + static_cast<std::size_t>(k)], b = idx[t + static_cast<std::size_t>((k + 1) % 3)];
                if (a > b) std::swap(a, b);
                ++count[(static_cast<uint64_t>(a) << 32) | b];
            }
        }
        for (const auto& [key, c] : count)
            if (c == 1) selectionEdges_.push_back({static_cast<uint32_t>(key >> 32), static_cast<uint32_t>(key & 0xffffffffu)});
        if (selectionEdges_.empty()) selectionEdges_.push_back({0u, 0u});   // rien à tracer (évite de recalculer)
    }
    const auto& pos = displayPositions_.size() == solver_.positions().size() ? displayPositions_ : solver_.positions();
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    dl->PushClipRect({viewPos_.x, viewPos_.y}, {viewPos_.x + viewSize_.x, viewPos_.y + viewSize_.y}, true);
    for (const auto& e : selectionEdges_) {
        if (e[0] == e[1] || e[0] >= pos.size() || e[1] >= pos.size()) continue;
        bool f0 = false, f1 = false;
        const glm::vec2 a = projectToScreen(pos[e[0]], &f0), b = projectToScreen(pos[e[1]], &f1);
        if (f0 && f1) dl->AddLine({a.x, a.y}, {b.x, b.y}, IM_COL32(255, 222, 70, 235), 2.0f);
    }
    // Flèche « endroit » : du centre de chaque pièce choisie, vers l'extérieur du vêtement.
    if (normals_.size() == pos.size())
        for (const int panel : highlighted_) {
            if (panel < 0 || panel >= static_cast<int>(garment_.panels.size()) || garment_.panels[static_cast<std::size_t>(panel)].hidden3D) continue;
            glm::vec3 c(0.0f), nrm(0.0f);
            int count = 0;
            for (std::size_t i = 0; i < pos.size() && i < pov.size(); ++i)
                if (pov[i] == panel) {
                    c += pos[i];
                    nrm += normals_[i];
                    ++count;
                }
            if (count == 0 || glm::length(nrm) < 1e-6f) continue;
            c /= static_cast<float>(count);
            nrm = glm::normalize(nrm);
            // Point de la pièce le plus proche du centre (le centre moyen peut être dans le vide si la pièce est courbée).
            float best = 1e9f;
            glm::vec3 nearest = c;
            for (std::size_t i = 0; i < pos.size() && i < pov.size(); ++i)
                if (pov[i] == panel && glm::distance(pos[i], c) < best) {
                    best = glm::distance(pos[i], c);
                    nearest = pos[i];
                }
            c = nearest;
            bool f0 = false, f1 = false;
            const glm::vec2 a = projectToScreen(c, &f0), b = projectToScreen(c + nrm * 0.15f, &f1);
            if (!f0 || !f1) continue;
            const ImU32 col = IM_COL32(255, 150, 40, 245);
            dl->AddLine({a.x, a.y}, {b.x, b.y}, col, 3.0f);
            dl->AddCircleFilled({a.x, a.y}, 4.0f, col);
            const glm::vec2 d = b - a;
            const float len = glm::length(d);
            if (len > 4.0f) {
                const glm::vec2 u = d / len, v(-u.y, u.x);
                const glm::vec2 l = b - u * 11.0f + v * 6.0f, r = b - u * 11.0f - v * 6.0f;
                dl->AddTriangleFilled({b.x, b.y}, {l.x, l.y}, {r.x, r.y}, col);
            }
            {   // étiquette au-delà de la pointe, du côté où va la flèche
                const glm::vec2 u = len > 1e-3f ? d / len : glm::vec2(1, 0);
                const ImVec2 ts = ImGui::CalcTextSize("endroit");
                const glm::vec2 at = b + u * 10.0f;
                dl->AddText({u.x < 0.0f ? at.x - ts.x : at.x, at.y - ts.y * 0.5f}, col, "endroit");
            }
        }
    dl->PopClipRect();
}

void App::drawGrain3D()
{
    // Droit fil de la pièce sélectionnée, reporté sur le tissu en 3D.
    const int sel = editor_.selectedPanel;
    if (!clothValid_ || sel < 0 || sel >= static_cast<int>(garment_.panels.size())) return;
    if (garment_.panels[static_cast<std::size_t>(sel)].hidden3D || !garment_.panels[static_cast<std::size_t>(sel)].active3D) return;
    const auto& pos = solver_.positions();
    const auto& uvs = garmentMesh_.mesh.uvs;
    glm::vec2 wa, wb;
    garment::grainLine(garment_.panels[static_cast<std::size_t>(sel)], wa, wb);
    constexpr int kSteps = 12;
    std::vector<glm::vec2> pts;
    const auto& idx = solver_.indices();
    const auto& pov = garmentMesh_.panelOfVertex;
    auto inPanel = [&](uint32_t v) { return v < pos.size() && v < uvs.size() && v < pov.size() && pov[v] == sel; };
    for (int k = 0; k <= kSteps; ++k) {
        const glm::vec2 w = wa + (wb - wa) * (static_cast<float>(k) / kSteps);
        // Triangle du patron qui contient le point : interpolation barycentrique de sa position 3D.
        glm::vec3 p3(0.0f), n3(0.0f);
        bool found = false;
        for (std::size_t t = 0; t + 2 < idx.size() && !found; t += 3) {
            const uint32_t i0 = idx[t], i1 = idx[t + 1], i2 = idx[t + 2];
            if (!inPanel(i0) || !inPanel(i1) || !inPanel(i2)) continue;
            const glm::vec2 a = uvs[i0], b = uvs[i1], c = uvs[i2];
            const float den = (b.y - c.y) * (a.x - c.x) + (c.x - b.x) * (a.y - c.y);
            if (std::abs(den) < 1e-12f) continue;
            const float l0 = ((b.y - c.y) * (w.x - c.x) + (c.x - b.x) * (w.y - c.y)) / den;
            const float l1 = ((c.y - a.y) * (w.x - c.x) + (a.x - c.x) * (w.y - c.y)) / den;
            const float l2 = 1.0f - l0 - l1;
            if (l0 < -1e-4f || l1 < -1e-4f || l2 < -1e-4f) continue;
            p3 = pos[i0] * l0 + pos[i1] * l1 + pos[i2] * l2;
            if (i0 < normals_.size() && i1 < normals_.size() && i2 < normals_.size())
                n3 = normals_[i0] * l0 + normals_[i1] * l1 + normals_[i2] * l2;
            found = true;
        }
        if (!found) continue;
        bool front = false;
        const glm::vec2 sp = projectToScreen(p3 + n3 * 0.004f, &front);
        if (!front) return;
        pts.push_back(sp);
    }
    if (pts.size() < 2) return;
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    dl->PushClipRect({viewPos_.x, viewPos_.y}, {viewPos_.x + viewSize_.x, viewPos_.y + viewSize_.y}, true);
    const ImU32 col = IM_COL32(240, 190, 110, 235);
    for (std::size_t k = 1; k < pts.size(); ++k) dl->AddLine({pts[k - 1].x, pts[k - 1].y}, {pts[k].x, pts[k].y}, col, 2.0f);
    auto head = [&](glm::vec2 tip, glm::vec2 from) {
        const glm::vec2 u = glm::normalize(tip - from + glm::vec2(1e-6f)), v(-u.y, u.x);
        const glm::vec2 p1 = tip - u * 10.0f + v * 4.5f, p2 = tip - u * 10.0f - v * 4.5f;
        dl->AddTriangleFilled({tip.x, tip.y}, {p1.x, p1.y}, {p2.x, p2.y}, col);
    };
    head(pts.back(), pts[pts.size() - 2]);
    head(pts.front(), pts[1]);
    dl->PopClipRect();
}

void App::drawSeams3D()
{
    if (!showSeams3D_ || !clothValid_) return;
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    dl->PushClipRect({viewPos_.x, viewPos_.y}, {viewPos_.x + viewSize_.x, viewPos_.y + viewSize_.y}, true);
    const auto& pos = solver_.positions();
    for (std::size_t k = 0; k < garmentMesh_.allSeamPairs.size(); ++k) {
        const auto& pr = garmentMesh_.allSeamPairs[k];
        const int s = garmentMesh_.seamOfPair[k];
        if (pr[0] >= pos.size() || pr[1] >= pos.size() || s < 0 || s >= static_cast<int>(garment_.seams.size())) continue;
        const bool enabled = garment_.seams[static_cast<std::size_t>(s)].enabled;
        if (glm::distance(pos[pr[0]], pos[pr[1]]) < 0.008f) continue;   // déjà cousue : rien à montrer
        const auto& sm = garment_.seams[static_cast<std::size_t>(s)];
        if (garment_.panels[static_cast<std::size_t>(sm.a.panel)].hidden3D || garment_.panels[static_cast<std::size_t>(sm.b.panel)].hidden3D)
            continue;   // pièce masquée
        bool f0 = false, f1 = false;
        const glm::vec2 a = projectToScreen(pos[pr[0]], &f0), b = projectToScreen(pos[pr[1]], &f1);
        if (!f0 || !f1) continue;
        const bool hover = s == hoverSeam3D_;
        ImU32 col = enabled ? ui::PatternEditor::seamColor(s) : IM_COL32(150, 150, 150, 255);
        col = (col & 0x00FFFFFF) | (hover ? 0xFF000000 : (enabled ? 0xB0000000 : 0x70000000));
        if (enabled) {
            dl->AddLine({a.x, a.y}, {b.x, b.y}, col, hover ? 2.6f : 1.3f);
        } else {   // désactivée : pointillés gris
            const int dashes = std::max(2, static_cast<int>(glm::distance(a, b) / 6.0f));
            for (int i = 0; i < dashes; i += 2) {
                const glm::vec2 p0 = a + (b - a) * (static_cast<float>(i) / dashes), p1 = a + (b - a) * (static_cast<float>(i + 1) / dashes);
                dl->AddLine({p0.x, p0.y}, {p1.x, p1.y}, col, hover ? 2.6f : 1.3f);
            }
        }
    }
    if (hoverSeam3D_ >= 0 && hoverSeam3D_ < static_cast<int>(garment_.seams.size()) && gizmoHover_ == Gizmo::None && gizmoActive_ == Gizmo::None) {
        const auto& sm = garment_.seams[static_cast<std::size_t>(hoverSeam3D_)];
        char tip[200];
        std::snprintf(tip, sizeof(tip), "Couture %d : %s + %s\nClic : %s", hoverSeam3D_ + 1, garment_.panels[static_cast<std::size_t>(sm.a.panel)].name.c_str(),
                      garment_.panels[static_cast<std::size_t>(sm.b.panel)].name.c_str(), sm.enabled ? "désactiver" : "activer");
        const ImVec2 mp = ImGui::GetIO().MousePos;
        const ImVec2 ts = ImGui::CalcTextSize(tip);
        dl->AddRectFilled({mp.x + 14, mp.y + 12}, {mp.x + 22 + ts.x, mp.y + 16 + ts.y + 4}, IM_COL32(15, 16, 18, 225), 4.0f);
        dl->AddText({mp.x + 18, mp.y + 14}, IM_COL32(235, 235, 235, 255), tip);
    }
    dl->PopClipRect();
}

void App::toggleSeam(int seam)
{
    if (seam < 0 || seam >= static_cast<int>(garment_.seams.size())) return;
    pushUndo();
    redoStack_.clear();
    redoCloth_.clear();
    auto& sm = garment_.seams[static_cast<std::size_t>(seam)];
    sm.enabled = !sm.enabled;
    committed_ = garment_;
    // Pas besoin de remailler : on change seulement les paires cousues par le solveur.
    garmentMesh_.seamPairs = garment::enabledSeamPairs(garment_, garmentMesh_);
    solver_.setSeams(garmentMesh_.seamPairs, sewingSeconds_);
    char text[160];
    std::snprintf(text, sizeof(text), "Couture %d %s.", seam + 1, sm.enabled ? "activée" : "désactivée (elle ne sera pas cousue)");
    setStatus(text, false);
}

void App::openAvatarDialog()
{
    if (hasNativeFileDialog()) {
        loadAvatar(openFileDialog("Importer un avatar", "Modèles 3D (FBX, OBJ)|*.fbx;*.obj"));
    } else {
        pathPopupKind_ = 0;
        pathInput_[0] = '\0';
        showPathPopup_ = true;
    }
}

void App::openPatternDialog()
{
    if (hasNativeFileDialog()) {
        importPattern(openFileDialog("Ouvrir un projet ou importer un patron",
                                     "Projets et patrons (DRAPEO, PDF, PNG, JPEG, SVG, DXF)|*.drapeo;*.pdf;*.png;*.jpg;*.jpeg;*.jfif;*.jpe;*.bmp;*.gif;*.tga;*.svg;*.dxf"));
    } else {
        pathPopupKind_ = 1;
        pathInput_[0] = '\0';
        showPathPopup_ = true;
    }
}

void App::setStatus(const std::string& text, bool error)
{
    status_ = text;
    statusIsError_ = error;
    statusTime_ = ImGui::GetCurrentContext() ? ImGui::GetTime() : 0.0;
    std::fprintf(error ? stderr : stdout, "%s\n", text.c_str());
}

// =============================================================================
//  Interface
// =============================================================================
namespace {
constexpr ImGuiWindowFlags kPaneFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus;
}

void App::drawMenuBar()
{
    if (!ImGui::BeginMainMenuBar()) return;
    // Marque : le logo Drapeo, puis le nom.
    {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float h = ImGui::GetFrameHeight(), s = h - 4.0f * uiScale_;
        ImGui::GetWindowDrawList()->AddImage(static_cast<ImTextureID>(logoIcon_), {p.x, p.y + (h - s) * 0.5f},
                                             {p.x + s, p.y + (h + s) * 0.5f});
        ImGui::Dummy(ImVec2(s + 2, h * 0.5f));
        ImGui::SameLine();
        if (fontBold_) ImGui::PushFont(fontBold_);
        ImGui::TextUnformatted("Drapeo");
        if (fontBold_) ImGui::PopFont();
        ImGui::SameLine(0, 18);
    }
    if (ImGui::BeginMenu("Fichier")) {
        if (ImGui::MenuItem("Nouveau vêtement", "Ctrl+N")) newGarment();
        ImGui::Separator();
        if (ImGui::MenuItem("Importer un avatar (FBX, OBJ)...", "Ctrl+O")) openAvatarDialog();
        if (ImGui::MenuItem("Ouvrir un projet / importer un patron...", "Ctrl+I")) openPatternDialog();
        if (ImGui::MenuItem("Enregistrer le projet...", "Ctrl+S", false, !garment_.empty())) saveProjectFile();
        ImGui::Separator();
        if (ImGui::MenuItem("Exporter le vêtement (OBJ)...", "Ctrl+E", false, clothValid_)) {
            exportOnly_.clear();
            exportGarment();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Quitter", "Alt+F4")) glfwSetWindowShouldClose(window_, GLFW_TRUE);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Édition")) {
        if (ImGui::MenuItem("Annuler", "Ctrl+Z", false, !undoStack_.empty())) undo();
        if (ImGui::MenuItem("Rétablir", "Ctrl+Y", false, !redoStack_.empty())) redo();
        ImGui::Separator();
        const bool hasPanel = editor_.selectedPanel >= 0 && editor_.selectedPanel < static_cast<int>(garment_.panels.size());
        if (ImGui::MenuItem("Copie miroir de la pièce", nullptr, false, hasPanel)) {
            editor_.selectedPanel = garment_.addMirrorCopy(editor_.selectedPanel, 0.05f);
            commitGarment();
        }
        if (ImGui::MenuItem("Supprimer la pièce", "Suppr", false, hasPanel)) {
            garment_.removePanel(editor_.selectedPanel);
            editor_.clearSelection();
            commitGarment();
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Outils")) {
        using ui::Tool;
        for (const auto& t : ui::kEditTools)
            if (ImGui::MenuItem(t.name, t.key, editor_.tool == t.tool)) editor_.tool = t.tool;
        if (ImGui::MenuItem("Tracer depuis l'image", "I")) editor_.traceRequested = true;
        ImGui::Separator();
        if (ImGui::MenuItem("Polygone", "P", editor_.tool == Tool::Polygon)) editor_.tool = Tool::Polygon;
        if (ImGui::MenuItem("Rectangle", "B", editor_.tool == Tool::Rectangle)) editor_.tool = Tool::Rectangle;
        if (ImGui::MenuItem("Poche", "O", editor_.tool == Tool::Pocket)) editor_.tool = Tool::Pocket;
        for (const auto& t : ui::kSeamTools)
            if (ImGui::MenuItem(t.name, t.key, editor_.tool == t.tool)) editor_.tool = t.tool;
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Modèles")) {
        if (ImGui::MenuItem("Débardeur")) setGarment(garment::makeTankTop());
        if (ImGui::MenuItem("Jupe droite")) setGarment(garment::makeSkirt());
        if (ImGui::MenuItem("Robe")) setGarment(garment::makeDress());
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Simulation")) {
        if (ImGui::MenuItem(playing_ ? "Pause" : "Simuler", "Espace")) playing_ = !playing_;
        if (ImGui::MenuItem("Image suivante")) stepOnce_ = true;
        if (ImGui::MenuItem("Recoudre depuis le patron", "R")) resetCloth();
        ImGui::Separator();
        ImGui::MenuItem("Auto-collisions", nullptr, &solver_.settings().selfCollision);
        ImGui::MenuItem("Sol", nullptr, &solver_.settings().groundEnabled);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Affichage")) {
        if (ImGui::MenuItem("Couleurs des pièces", nullptr, &panelColors_)) updateClothColors();
        if (ImGui::MenuItem("Coutures et surpiqûres", nullptr, &showStitches_)) {
            printSignature_.clear();
            updateClothPattern();
        }
        ImGui::MenuItem("Fil de fer", nullptr, &appearance_.wireframe);
        ImGui::MenuItem("Carte de tension", nullptr, &appearance_.showStrain);
        ImGui::MenuItem("Envers du tissu en bleu", nullptr, &appearance_.showBackSide);
        ImGui::MenuItem("Ombre de l'avatar dans le patron", nullptr, &editor_.silhouette.visible);
        if (ImGui::BeginMenu("Ombre vue de", editor_.silhouette.visible)) {
            static const char* const kViews[] = {"Face", "Profil", "Dessus"};
            for (int v = 0; v < 3; ++v)
                if (ImGui::MenuItem(kViews[v], nullptr, editor_.silhouette.view == v) && editor_.silhouette.view != v) {
                    editor_.silhouette.view = v;
                    buildSilhouette();
                }
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem("Avatar visible", nullptr, &showAvatar_)) applyAvatarTransform();
        ImGui::Separator();
        if (ImGui::MenuItem("Cadrer l'avatar", "F")) frameCamera();
        if (ImGui::MenuItem("Vue de face")) { frameCamera(); camera_.yaw = 0.0f; camera_.pitch = 0.05f; }
        if (ImGui::MenuItem("Vue de dos")) { frameCamera(); camera_.yaw = kPi; camera_.pitch = 0.05f; }
        if (ImGui::MenuItem("Vue de profil")) { frameCamera(); camera_.yaw = kPi * 0.5f; camera_.pitch = 0.05f; }
        if (ImGui::MenuItem("Cadrer le patron")) editor_.frameAll(garment_);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Aide")) {
        if (ImGui::MenuItem("Raccourcis clavier")) showShortcuts_ = true;
        if (ImGui::MenuItem("À propos de Drapeo")) showAbout_ = true;
        ImGui::EndMenu();
    }
    ImGui::EndMainMenuBar();
    ImGui::GetForegroundDrawList()->AddRectFilled({0, menuHeight_ - 1.0f}, {ImGui::GetIO().DisplaySize.x, menuHeight_},
                                                  IM_COL32(220, 174, 96, 70));
}

void App::drawToolbar()
{
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos({0, menuHeight_});
    ImGui::SetNextWindowSize({display.x, toolbarHeight_});
    ImGui::PushStyleColor(ImGuiCol_WindowBg, kBgToolbar);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10 * uiScale_, 0));
    ImGui::Begin("##toolbar", nullptr, kPaneFlags);
    ImGui::PopStyleVar();
    const float b = std::round(32.0f * uiScale_);
    const float y = (toolbarHeight_ - b) * 0.5f;
    ImGui::SetCursorPosY(y);

    using ui::Tool;
    {
        // Bouton de l'outil d'édition courant + liste déroulante des outils d'édition
        const ui::ToolInfo* cur = &ui::kEditTools[0];
        for (const auto& t : ui::kEditTools)
            if (t.tool == editor_.tool) cur = &t;
        char tip[256];
        std::snprintf(tip, sizeof(tip), "%s (%s)\n%s", cur->name, cur->key, cur->help);
        if (iconButton("sel", Icon::Select, tip, ui::isEditTool(editor_.tool) ? Btn::On : Btn::Quiet, b)) editor_.tool = cur->tool;
        ImGui::SameLine(0, 0);
        ImGui::SetCursorPosY(y);
        const float aw = std::round(14.0f * uiScale_);
        if (ImGui::InvisibleButton("##edittools", ImVec2(aw, b))) ImGui::OpenPopup("##edittoolsmenu");
        {
            const ImVec2 r0 = ImGui::GetItemRectMin(), r1 = ImGui::GetItemRectMax();
            auto* dl = ImGui::GetWindowDrawList();
            if (ImGui::IsItemHovered()) dl->AddRectFilled(r0, r1, IM_COL32(255, 255, 255, 24), 3.0f);
            const float cx = (r0.x + r1.x) * 0.5f, cy = (r0.y + r1.y) * 0.5f, h = 3.5f * uiScale_;
            dl->AddTriangleFilled({cx - h, cy - h * 0.5f}, {cx + h, cy - h * 0.5f}, {cx, cy + h * 0.7f}, IM_COL32(220, 222, 228, 230));
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Outils d'édition du patron");
        if (ImGui::BeginPopup("##edittoolsmenu")) {
            for (const auto& t : ui::kEditTools) {
                if (ImGui::MenuItem(t.name, t.key, editor_.tool == t.tool)) editor_.tool = t.tool;
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", t.help);
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Tracer depuis l'image", "I")) editor_.traceRequested = true;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Détecte les contours des pièces sur l'image de fond importée");
            ImGui::EndPopup();
        }
        if (editor_.tool == Tool::Slash) {
            ImGui::SameLine(0, 6);
            ImGui::SetCursorPosY(y + (b - ImGui::GetFrameHeight()) * 0.5f);
            ImGui::SetNextItemWidth(std::round(110.0f * uiScale_));
            ImGui::DragFloat("##spread", &editor_.spreadDeg, 0.5f, -90.0f, 90.0f, "Écart %.1f°");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Angle d'ouverture de « Fendre et écarter »");
        }
        if (editor_.tool == Tool::Pocket) {
            ImGui::SameLine(0, 6);
            ImGui::SetCursorPosY(y + (b - ImGui::GetFrameHeight()) * 0.5f);
            ImGui::SetNextItemWidth(std::round(170.0f * uiScale_));
            static const char* const kPocketKinds[] = {"Poche (ouverte en haut)", "Rabat (cousu en haut)", "Tout le tour (renfort)"};
            ImGui::Combo("##pochetype", &editor_.pocketKind, kPocketKinds, IM_ARRAYSIZE(kPocketKinds));
            if (editor_.pocketKind == 0) {
                ImGui::SameLine(0, 6);
                ImGui::Checkbox("Avec rabat", &editor_.pocketFlap);
            }
        }
        if (editor_.tool == Tool::Zipper) {
            ImGui::SameLine(0, 6);
            ImGui::SetCursorPosY(y + (b - ImGui::GetFrameHeight()) * 0.5f);
            ImGui::SetNextItemWidth(std::round(120.0f * uiScale_));
            float cm = editor_.zipperWidth * 100.0f;
            if (ImGui::DragFloat("##zipw", &cm, 0.1f, 0.8f, 8.0f, "Largeur %.1f cm")) editor_.zipperWidth = std::clamp(cm, 0.8f, 8.0f) / 100.0f;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Largeur de la bande de fermeture (ruban + dents)");
        }
        if (editor_.tool == Tool::Fold) {
            const bool hasFold = editor_.foldPanel >= 0 && editor_.foldPanel < static_cast<int>(garment_.panels.size()) &&
                                 editor_.foldIndex >= 0 &&
                                 editor_.foldIndex < static_cast<int>(garment_.panels[static_cast<std::size_t>(editor_.foldPanel)].folds.size());
            ImGui::SameLine(0, 6);
            ImGui::SetCursorPosY(y + (b - ImGui::GetFrameHeight()) * 0.5f);
            ImGui::SetNextItemWidth(std::round(110.0f * uiScale_));
            bool edited = ImGui::DragFloat("##foldangle", &editor_.foldAngle, 1.0f, -180.0f, 180.0f, "Angle %.0f°");
            bool done = ImGui::IsItemDeactivatedAfterEdit();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Angle du pli : 0 = à plat, 180 = replié complètement.\n> 0 : les côtés se replient vers l'endroit, < 0 : vers l'envers.");
            ImGui::SameLine(0, 4);
            ImGui::SetNextItemWidth(std::round(100.0f * uiScale_));
            float pct = editor_.foldStrength * 100.0f;
            if (ImGui::DragFloat("##foldforce", &pct, 0.5f, 0.0f, 100.0f, "Force %.0f %%")) {
                editor_.foldStrength = std::clamp(pct, 0.0f, 100.0f) / 100.0f;
                edited = true;
            }
            done = done || ImGui::IsItemDeactivatedAfterEdit();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Force du pli : 0 = souple, 100 = pli bien marqué.");
            editor_.foldAngle = std::clamp(editor_.foldAngle, -180.0f, 180.0f);
            if (hasFold) {
                auto& f = garment_.panels[static_cast<std::size_t>(editor_.foldPanel)].folds[static_cast<std::size_t>(editor_.foldIndex)];
                if (edited) {
                    f.angleDeg = editor_.foldAngle;
                    f.strength = editor_.foldStrength;
                }
                if (done) commitGarment();
            }
        }
        if (editor_.tool == Tool::CutPanel) {
            ImGui::SameLine(0, 6);
            ImGui::SetCursorPosY(y + (b - ImGui::GetFrameHeight()) * 0.5f);
            ImGui::Checkbox("Recoudre", &editor_.cutSew);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Coché : les deux morceaux restent cousus le long de la coupe.\n"
                                  "Décoché : deux pièces séparées (par exemple pour faire une poche).");
        }
    }
    ImGui::SameLine(0, 2);
    if (iconButton("poly", Icon::Polygon, "Polygone (P)\nTracer une pièce point par point", editor_.tool == Tool::Polygon ? Btn::On : Btn::Quiet, b))
        editor_.tool = Tool::Polygon;
    ImGui::SameLine(0, 2);
    if (iconButton("rect", Icon::Rectangle, "Rectangle (B)\nTracer une pièce rectangulaire", editor_.tool == Tool::Rectangle ? Btn::On : Btn::Quiet, b))
        editor_.tool = Tool::Rectangle;
    ImGui::SameLine(0, 2);
    if (iconButton("poche", Icon::Pocket, "Poche (O)\nTracer un rectangle sur une pièce : la poche y est cousue directement", editor_.tool == Tool::Pocket ? Btn::On : Btn::Quiet, b))
        editor_.tool = Tool::Pocket;
    ImGui::SameLine(0, 2);
    {
        // Bouton de l'outil de couture courant + liste déroulante (bord à bord, libre)
        const ui::ToolInfo* cur = &ui::kSeamTools[0];
        for (const auto& t : ui::kSeamTools)
            if (t.tool == editor_.tool) cur = &t;
        char tip[256];
        std::snprintf(tip, sizeof(tip), "%s (%s)\n%s", cur->name, cur->key, cur->help);
        const bool seamTool = editor_.tool == Tool::Seam || editor_.tool == Tool::FreeSeam || editor_.tool == Tool::Gusset;
        if (iconButton("seam", Icon::Seam, tip, seamTool ? Btn::On : Btn::Quiet, b)) editor_.tool = cur->tool;
        ImGui::SameLine(0, 0);
        ImGui::SetCursorPosY(y);
        const float aw = std::round(14.0f * uiScale_);
        if (ImGui::InvisibleButton("##seamtools", ImVec2(aw, b))) ImGui::OpenPopup("##seamtoolsmenu");
        {
            const ImVec2 r0 = ImGui::GetItemRectMin(), r1 = ImGui::GetItemRectMax();
            auto* dl = ImGui::GetWindowDrawList();
            if (ImGui::IsItemHovered()) dl->AddRectFilled(r0, r1, IM_COL32(255, 255, 255, 24), 3.0f);
            const float cx = (r0.x + r1.x) * 0.5f, cy = (r0.y + r1.y) * 0.5f, h = 3.5f * uiScale_;
            dl->AddTriangleFilled({cx - h, cy - h * 0.5f}, {cx + h, cy - h * 0.5f}, {cx, cy + h * 0.7f}, IM_COL32(220, 222, 228, 230));
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Outils de couture");
        if (ImGui::BeginPopup("##seamtoolsmenu")) {
            for (const auto& t : ui::kSeamTools) {
                if (ImGui::MenuItem(t.name, t.key, editor_.tool == t.tool)) editor_.tool = t.tool;
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", t.help);
            }
            ImGui::EndPopup();
        }
        if (editor_.tool == Tool::Gusset) {
            ImGui::SameLine(0, 6);
            ImGui::SetCursorPosY(y + (b - ImGui::GetFrameHeight()) * 0.5f);
            ImGui::SetNextItemWidth(std::round(120.0f * uiScale_));
            float cm = editor_.gussetWidth * 100.0f;
            if (ImGui::DragFloat("##gussetw", &cm, 0.1f, 0.5f, 40.0f, "Largeur %.1f cm")) editor_.gussetWidth = std::clamp(cm, 0.5f, 40.0f) / 100.0f;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Largeur de la bande de soufflet (épaisseur de la poche)");
        }
    }
    ImGui::SameLine(0, 2);
    if (iconButton("magnet", Icon::Magnet, "Aimant (M)\nLes points et les pièces s'accrochent aux points et bords voisins", editor_.magnet ? Btn::On : Btn::Quiet, b))
        editor_.magnet = !editor_.magnet;
    ImGui::SameLine(0, 2);
    {
        const int sel = editor_.selectedPanel;
        const bool has = sel >= 0 && sel < static_cast<int>(garment_.panels.size());
        const bool on = has && garment_.panels[static_cast<std::size_t>(sel)].symmetric;
        if (iconButton("sym", Icon::Symmetry,
                       has ? "Symétrie de la pièce sélectionnée\nUn point déplacé, ajouté ou supprimé l'est aussi de l'autre côté de l'axe"
                           : "Symétrie\nSélectionnez d'abord une pièce",
                       on ? Btn::On : Btn::Quiet, b) && has) {
            std::vector<int> targets = editor_.selection;
            if (targets.empty()) targets.push_back(sel);
            for (const int p : targets) setPanelSymmetric(p, !on);
            setStatus(on ? "Symétrie désactivée." : "Symétrie activée : l'axe est au milieu de la pièce (pointillés bleus).", false);
        }
    }

    toolbarSeparator(b);
    const Btn playTone = !clothValid_ ? Btn::Quiet : (playing_ ? Btn::On : Btn::Primary);
    if (iconButton("play", playing_ ? Icon::Pause : Icon::Play, playing_ ? "Pause (Espace)" : "Simuler (Espace)", playTone, b))
        playing_ = !playing_;
    ImGui::SameLine(0, 2);
    if (iconButton("step", Icon::Step, "Image suivante", Btn::Quiet, b)) stepOnce_ = true;
    ImGui::SameLine(0, 2);
    if (iconButton("reset", Icon::Reset, "Recoudre depuis le patron (R)", Btn::Quiet, b)) resetCloth();
    ImGui::SameLine(0, 2);
    if (iconButton("frame", Icon::Frame, "Cadrer l'avatar (F)", Btn::Quiet, b)) frameCamera();

    toolbarSeparator(b);
    // Listes déroulantes : modèle, tissu, finesse du maillage.
    const float comboY = (toolbarHeight_ - ImGui::GetFrameHeight()) * 0.5f;
    ImGui::SetCursorPosY(comboY);
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("Tissu");
    ImGui::SameLine();
    ImGui::SetCursorPosY(comboY);
    ImGui::SetNextItemWidth(130 * uiScale_);
    const auto presets = simulation::fabricPresets();
    if (ImGui::BeginCombo("##tissu", presets[static_cast<std::size_t>(presetIndex_)].name.c_str())) {
        for (std::size_t i = 0; i < presets.size(); ++i) {
            if (ImGui::Selectable(presets[i].name.c_str(), static_cast<int>(i) == presetIndex_)) {
                presetIndex_ = static_cast<int>(i);
                material_ = presets[i];
                appearance_.roughness = material_.roughness;
                appearance_.sheen = material_.sheen;
                solver_.setMaterial(material_);
                updateClothColors();
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine(0, 14);
    ImGui::SetCursorPosY(comboY);
    ImGui::TextDisabled("Maillage");
    ImGui::SameLine();
    ImGui::SetCursorPosY(comboY);
    ImGui::SetNextItemWidth(140 * uiScale_);
    int sp = 1;
    for (int i = 0; i < 4; ++i)
        if (std::abs(kSpacingValues[i] - spacing_) < 1e-4f) sp = i;
    if (ImGui::Combo("##maillage", &sp, kSpacingNames, 4)) {
        spacing_ = kSpacingValues[sp];
        resetCloth();
    }
    ImGui::SameLine(0, 14);
    ImGui::SetCursorPosY(comboY);
    ImGui::TextDisabled("Modèle");
    ImGui::SameLine();
    ImGui::SetCursorPosY(comboY);
    ImGui::SetNextItemWidth(112 * uiScale_);
    if (ImGui::BeginCombo("##modele", "Choisir")) {
        auto loadModel = [&](const char* label, garment::Garment (*make)()) {
            if (!ImGui::Selectable(label)) return;
            setGarment(make());
            setStatus(std::string("Modèle « ") + label + " » chargé.", false);
        };
        loadModel("Débardeur", garment::makeTankTop);
        loadModel("Jupe droite", garment::makeSkirt);
        loadModel("Robe", garment::makeDress);
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
        ImGui::SetTooltip("Charge un modèle prêt à coudre.\nLe patron actuel est remplacé (Ctrl+Z pour annuler).");

    // À droite : avancement de la couture.
    if (clothValid_) {
        const float prog = solver_.seamCount() > 0 ? solver_.seamProgress() : 1.0f;
        const bool live = playing_ && prog >= 1.0f;
        const float dot = live || prog < 1.0f ? 16.0f * uiScale_ : 0.0f;
        const float w = 190 * uiScale_;
        ImGui::SameLine(ImGui::GetWindowWidth() - w - dot - 14 * uiScale_);
        ImGui::SetCursorPosY(comboY);
        if (dot > 0.0f) {
            const float pulse = prog < 1.0f ? 1.0f : (0.45f + 0.55f * (0.5f + 0.5f * std::sin(static_cast<float>(ImGui::GetTime()) * 2.8f)));
            const ImVec2 p = ImGui::GetCursorScreenPos();
            const float cy = p.y + ImGui::GetFrameHeight() * 0.5f;
            const ImU32 col = prog < 1.0f ? kAccentU32 : IM_COL32(130, 186, 112, static_cast<int>(255.0f * pulse));
            ImGui::GetWindowDrawList()->AddCircleFilled({p.x + 5.0f * uiScale_, cy}, 3.2f * uiScale_, col);
            ImGui::Dummy(ImVec2(dot, ImGui::GetFrameHeight()));
            ImGui::SameLine(0, 0);
            ImGui::SetCursorPosY(comboY);
        }
        char text[64];
        if (prog < 1.0f) std::snprintf(text, sizeof(text), "Couture  %.0f %%", static_cast<double>(prog * 100.0f));
        else std::snprintf(text, sizeof(text), playing_ ? "Drapé en cours" : "En pause");
        ImGui::PushStyleColor(ImGuiCol_FrameBg, kBg0);
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, prog < 1.0f ? kAccent : ImVec4(0.45f, 0.58f, 0.40f, 1.0f));
        ImGui::ProgressBar(prog, ImVec2(w, ImGui::GetFrameHeight()), text);
        ImGui::PopStyleColor(2);
    }
    ImGui::End();
    ImGui::PopStyleColor();
}

void App::drawPatternPane()
{
    const float top = menuHeight_ + toolbarHeight_;
    ImGui::SetNextWindowPos({0, top});
    ImGui::SetNextWindowSize({leftWidth_, viewSize_.y});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("##patron", nullptr, kPaneFlags | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();
    // En-tête de la vue
    {
        const float h = ImGui::GetFrameHeight() + 6 * uiScale_;
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddRectFilled(p, {p.x + leftWidth_, p.y + h}, ImGui::GetColorU32(kBg1));
        ImGui::GetWindowDrawList()->AddLine({p.x, p.y + h - 1.0f}, {p.x + leftWidth_, p.y + h - 1.0f}, IM_COL32(220, 174, 96, 36));
        ImGui::SetCursorPos({12 * uiScale_, (h - ImGui::GetTextLineHeight()) * 0.5f});
        paneTitle("PATRON 2D", fontBold_);
        ImGui::SameLine();
        ImGui::TextDisabled("  %zu pièce(s), %zu couture(s)", garment_.panels.size(), garment_.seams.size());
        // Ombre de l'avatar : face / profil / dessus (à droite de l'en-tête)
        if (editor_.silhouette.texture != 0 || !editor_.silhouette.visible) {
            static const char* const kViews[] = {"Face", "Profil", "Dessus"};
            float bw = 0.0f;
            for (const char* v : kViews) bw += ImGui::CalcTextSize(v).x + 12 * uiScale_;
            const float eye = ImGui::CalcTextSize("Ombre").x + 14 * uiScale_;
            const float x0 = leftWidth_ - bw - eye - 10 * uiScale_;
            if (x0 > ImGui::GetCursorPosX() + 20 * uiScale_) {
                ImGui::SameLine();
                const float by = (h - ImGui::GetFrameHeight()) * 0.5f;
                ImGui::SetCursorPos({x0, by});
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6 * uiScale_, ImGui::GetStyle().FramePadding.y));
                ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
                ImGui::PushStyleColor(ImGuiCol_Button, editor_.silhouette.visible ? kBg2 : kBg1);
                if (ImGui::Button("Ombre")) editor_.silhouette.visible = !editor_.silhouette.visible;
                ImGui::PopStyleColor();
                if (ImGui::IsItemHovered()) ImGui::SetTooltip(editor_.silhouette.visible ? "Masquer l'ombre de l'avatar" : "Afficher l'ombre de l'avatar");
                ImGui::SameLine(0, 4 * uiScale_);
                for (int v = 0; v < 3; ++v) {
                    ImGui::SetCursorPosY(by);
                    const bool on = editor_.silhouette.view == v;
                    ImGui::PushStyleColor(ImGuiCol_Button, on ? kAccentDim : kBg2);
                    ImGui::BeginDisabled(!editor_.silhouette.visible);
                    if (ImGui::Button(kViews[v]) && !on) {
                        editor_.silhouette.view = v;
                        buildSilhouette();
                    }
                    ImGui::EndDisabled();
                    ImGui::PopStyleColor();
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip(v == 0 ? "Ombre de l'avatar vu de face (une pièce tracée dessus est posée à cet endroit du corps)"
                                                 : v == 1 ? "Ombre de l'avatar vu de profil (devant à droite)"
                                                          : "Ombre de l'avatar vu du dessus (devant en haut)");
                    if (v < 2) ImGui::SameLine(0, 0);
                }
                ImGui::PopStyleVar(2);
            }
        }
        ImGui::SetCursorPos({0, h});
    }
    editor_.bodyScale = avatarHeight_ / 1.75f;
    editor_.torsoCenterX = bodyAxes_.torsoCenter.x;
    if (editor_.draw(garment_)) commitGarment();
    if (!editor_.notice.empty()) {
        setStatus(editor_.notice, editor_.noticeError);
        editor_.notice.clear();
    }
    if (editor_.traceRequested) {
        editor_.traceRequested = false;
        if (backdropImage_.width > 0)
            detectBackdropPieces();
        else
            setStatus("Tracer depuis l'image : importez d'abord une image de patron (Fichier > Ouvrir un projet / importer un patron, Ctrl+I).", true);
    }
    if (editor_.menuRequested) {
        editor_.menuRequested = false;
        ImGui::OpenPopup("##menuPieces");
    }
    drawPieceMenu();
    if (editor_.splitRequested) {
        editor_.splitRequested = false;
        splitEdge_ = editor_.splitEdge;
        splitT_ = editor_.splitT;
        splitOpen_ = true;
        ImGui::SetNextWindowPos(ImVec2(ImGui::GetMousePos().x + 24 * uiScale_, ImGui::GetMousePos().y - 20 * uiScale_), ImGuiCond_Always);
    }
    drawSplitWindow();
    if (editor_.backdrop.moved) {
        editor_.backdrop.moved = false;
        backdropOrigin_ = editor_.backdrop.min;
    }
    if (editor_.backdrop.removeRequested) {
        removeBackdrop();
        setStatus("Image de fond retirée.", false);
    }
    ImGui::End();
}

// =============================================================================
//  Menu clic droit des pièces (vue patron)
// =============================================================================

void App::commitState()
{
    pushUndo();
    redoStack_.clear();
    redoCloth_.clear();
    committed_ = garment_;
    apply3DState();
}

void App::apply3DState()
{
    if (!clothValid_) return;
    const auto& pov = garmentMesh_.panelOfVertex;
    const int np = static_cast<int>(garment_.panels.size());
    auto panelOf = [&](uint32_t v) -> const garment::Panel* {
        const int p = v < pov.size() ? pov[v] : -1;
        return p >= 0 && p < np ? &garment_.panels[static_cast<std::size_t>(p)] : nullptr;
    };
    simulation::applyPanelProperties(solver_, garment_, garmentMesh_);   // figées, renforcées, épaisseurs, couches, pression
    // Pièces masquées : simulées mais pas dessinées.
    std::vector<uint32_t> shown;
    const auto& idx = garmentMesh_.mesh.indices;
    shown.reserve(idx.size());
    for (std::size_t t = 0; t + 2 < idx.size(); t += 3) {
        bool hidden = false;
        for (int k = 0; k < 3; ++k) {
            const garment::Panel* p = panelOf(idx[t + static_cast<std::size_t>(k)]);
            hidden = hidden || (p != nullptr && p->hidden3D);
        }
        if (!hidden) shown.insert(shown.end(), {idx[t], idx[t + 1], idx[t + 2]});
    }
    renderer_.setClothTopology(shown, garmentMesh_.mesh.vertexCount());
    updateClothPattern();
}

void App::pasteClipboard(bool mirror, bool atMenuPoint)
{
    if (clipboard_.panels.empty()) {
        setStatus("Rien à coller : copiez d'abord une pièce (Ctrl+C).", true);
        return;
    }
    garment::Garment part = clipboard_;
    for (auto& p : part.panels) p.linkGroup = 0;
    glm::vec2 lo(1e9f), hi(-1e9f);
    for (const auto& p : part.panels)
        for (const auto& q : p.points) {
            lo = glm::min(lo, q);
            hi = glm::max(hi, q);
        }
    glm::vec2 offset(0.0f);
    if (mirror) {
        garment::mirrorGarment(part, hi.x + 0.015f);   // à droite de l'original
        for (auto& p : part.panels) p.name += " (miroir)";
        lo.x += hi.x - lo.x + 0.03f;
        hi.x += hi.x - lo.x + 0.03f;
    }
    if (atMenuPoint) {
        offset = editor_.menuWorld - (lo + hi) * 0.5f;
    } else if (!mirror) {
        ++pasteCount_;
        offset = glm::vec2(0.03f, -0.03f) * static_cast<float>(pasteCount_);
    }
    const auto added = garment::appendPanels(garment_, part, offset);
    editor_.select(added);
    commitGarment();
    setStatus(std::to_string(added.size()) + (mirror ? " pièce(s) collée(s) en miroir." : " pièce(s) collée(s)."), false);
}

void App::pieceAction(PieceAction a)
{
    using A = PieceAction;
    const int n = static_cast<int>(garment_.panels.size());
    std::vector<int> t;
    for (const int p : editor_.targets())
        if (p >= 0 && p < n) t.push_back(p);
    const bool needsPieces = !(a == A::SelectAll || a == A::InvertSelection || a == A::Paste || a == A::PasteMirror || a == A::ShowAll);
    if (needsPieces && t.empty()) return;
    auto each = [&](auto&& f) {
        for (const int p : t) f(garment_.panels[static_cast<std::size_t>(p)]);
    };
    auto plural = [&](const char* what) { return std::to_string(t.size()) + " pièce(s) " + what; };
    switch (a) {
    case A::SelectAll: {
        std::vector<int> all;
        for (int p = 0; p < n; ++p)
            if (garment_.panelEditable(p)) all.push_back(p);   // calques masqués ou verrouillés exclus
        editor_.select(all);
        break;
    }
    case A::InvertSelection: {
        std::vector<int> inv;
        for (int p = 0; p < n; ++p)
            if (std::find(t.begin(), t.end(), p) == t.end()) inv.push_back(p);
        editor_.select(inv);
        break;
    }
    case A::SameColor: editor_.select(garment::panelsLike(garment_, t.back(), garment::SameProperty::Color)); break;
    case A::SameZone: editor_.select(garment::panelsLike(garment_, t.back(), garment::SameProperty::Zone)); break;
    case A::SameLink: editor_.select(garment::panelsLike(garment_, t.back(), garment::SameProperty::LinkGroup)); break;
    case A::NewFabric: {
        static const glm::vec3 palette[] = {{0.36f, 0.52f, 0.78f}, {0.80f, 0.42f, 0.38f}, {0.42f, 0.66f, 0.44f}, {0.86f, 0.70f, 0.34f},
                                            {0.62f, 0.48f, 0.78f}, {0.34f, 0.68f, 0.70f}, {0.84f, 0.56f, 0.30f}, {0.56f, 0.56f, 0.60f},
                                            {0.30f, 0.36f, 0.28f}, {0.72f, 0.36f, 0.56f}};
        glm::vec3 pick = palette[0];
        float best = -1.0f;
        for (const auto& c : palette) {   // la couleur la plus éloignée de celles déjà utilisées
            float d = 10.0f;
            for (const auto& p : garment_.panels) d = std::min(d, glm::distance(p.color, c));
            if (d > best) {
                best = d;
                pick = c;
            }
        }
        each([&](garment::Panel& p) { p.color = pick; });
        commitState();
        updateClothColors();
        setStatus(plural("passée(s) dans un nouveau tissu (nouvelle couleur)."), false);
        break;
    }
    case A::Copy:
        clipboard_ = garment::extractPanels(garment_, t);
        pasteCount_ = 0;
        setStatus(plural("copiée(s). Ctrl+V pour coller, Ctrl+R pour coller en miroir."), false);
        break;
    case A::Paste: pasteClipboard(false, menuAtPoint_); break;
    case A::PasteMirror: pasteClipboard(true, menuAtPoint_); break;
    case A::Delete: {
        std::sort(t.rbegin(), t.rend());
        for (const int p : t) garment_.removePanel(p);
        editor_.clearSelection();
        commitGarment();
        setStatus(plural("supprimée(s). Ctrl+Z pour annuler."), false);
        break;
    }
    case A::DeleteSeams: {
        const auto before = garment_.seams.size();
        std::erase_if(garment_.seams, [&](const garment::Seam& s) {
            return std::find(t.begin(), t.end(), s.a.panel) != t.end() || std::find(t.begin(), t.end(), s.b.panel) != t.end();
        });
        const auto beforeAt = garment_.attachments.size();   // pièces cousues dessus (poches...) : décousues aussi
        std::erase_if(garment_.attachments, [&](const garment::Attachment& a) { return std::find(t.begin(), t.end(), a.patch) != t.end(); });
        const auto removed = before - garment_.seams.size() + (beforeAt - garment_.attachments.size());
        editor_.selectedSeam = -1;
        if (removed > 0) {
            commitGarment();
            setStatus(std::to_string(removed) + " couture(s) effacée(s). Ctrl+Z pour annuler.", false);
        } else {
            setStatus("Aucune couture sur la sélection.", false);
        }
        break;
    }
    case A::CloneMirrorSewn:
    case A::CloneMirror:
    case A::CloneInstance:
    case A::CloneOver:
    case A::CloneUnder: {
        garment::CloneOptions o;
        o.mirror = a == A::CloneMirrorSewn || a == A::CloneMirror;
        o.sewing = a == A::CloneMirrorSewn;
        o.link = true;
        o.layer = a == A::CloneOver ? 1 : (a == A::CloneUnder ? -1 : 0);
        const std::size_t seams = garment_.seams.size();
        const auto clones = garment::clonePanels(garment_, t, o);
        editor_.select(clones);
        commitGarment();
        std::string msg = std::to_string(clones.size()) + " clone(s) lié(s) créé(s)";
        if (o.sewing) msg += ", " + std::to_string(garment_.seams.size() - seams) + " couture(s) ajoutée(s)";
        setStatus(msg + ". Les modifications de forme sont reportées entre clones.", false);
        break;
    }
    case A::Link: {
        const int linked = garment::linkPanels(garment_, t);
        if (linked == 0) {
            setStatus("Impossible de lier : les pièces n'ont pas la même forme (au miroir ou au quart de tour près).", true);
            break;
        }
        commitState();
        setStatus(std::to_string(linked + 1) + " pièces liées : modifier l'une modifie les autres.", false);
        break;
    }
    case A::Unlink:
        garment::unlinkPanels(garment_, t);
        commitState();
        setStatus(plural("déliée(s)."), false);
        break;
    case A::Grow:
    case A::Shrink:
    case A::Offset: {
        const float d = (a == A::Shrink ? -1.0f : 1.0f) * (a == A::Offset ? offsetCm_ : 1.0f) * 0.01f;
        int done = 0;
        for (const int p : t) done += garment::offsetPanel(garment_, p, d) ? 1 : 0;
        if (done == 0) {
            setStatus("Décalage impossible : la pièce est trop petite.", true);
            break;
        }
        commitGarment();
        char text[96];
        std::snprintf(text, sizeof(text), "Contour décalé de %+.1f cm sur %d pièce(s).", static_cast<double>(d * 100.0f), done);
        setStatus(text, false);
        break;
    }
    case A::Simplify: {
        int removed = 0;
        for (const int p : t) removed += garment::simplifyPanel(garment_, p, 0.002f);
        if (removed > 0) commitGarment();
        setStatus(removed > 0 ? std::to_string(removed) + " point(s) inutile(s) retiré(s) (les points des coutures sont gardés)."
                              : std::string("Aucun point à retirer."), false);
        break;
    }
    case A::InnerShape: {
        std::vector<int> made;
        for (const int p : t)
            if (const int k = garment::addInnerShape(garment_, p, 0.02f); k >= 0) made.push_back(k);
        if (made.empty()) {
            setStatus("Renfort impossible sur cette pièce (pièce posée, ou trop petite).", true);
            break;
        }
        editor_.select(made);
        commitGarment();
        setStatus(std::to_string(made.size()) + " renfort(s) intérieur(s) cousu(s) tout le tour, 2 cm à l'intérieur du bord.", false);
        break;
    }
    case A::Deactivate:
    case A::DeactivateWithSeams: {
        each([](garment::Panel& p) { p.active3D = false; });
        if (a == A::DeactivateWithSeams)
            for (auto& s : garment_.seams)
                if (std::find(t.begin(), t.end(), s.a.panel) != t.end() || std::find(t.begin(), t.end(), s.b.panel) != t.end())
                    s.enabled = false;
        commitGarment();
        setStatus(plural(a == A::DeactivateWithSeams ? "désactivée(s) en 3D avec leurs coutures." : "désactivée(s) en 3D."), false);
        break;
    }
    case A::Activate:
        each([](garment::Panel& p) { p.active3D = true; });
        commitGarment();
        setStatus(plural("réactivée(s) en 3D."), false);
        break;
    case A::Freeze:
    case A::Unfreeze:
        each([&](garment::Panel& p) { p.frozen = a == A::Freeze; });
        commitState();
        setStatus(plural(a == A::Freeze ? "figée(s) : elles ne bougent plus." : "libérée(s)."), false);
        break;
    case A::Strengthen:
    case A::Soften:
        each([&](garment::Panel& p) { p.stiffness = a == A::Strengthen ? 20.0f : 1.0f; });
        commitState();
        setStatus(plural(a == A::Strengthen ? "renforcée(s) : plus raides, moins extensibles." : "revenue(s) au tissu normal."), false);
        break;
    case A::Hide:
    case A::Show:
        each([&](garment::Panel& p) { p.hidden3D = a == A::Hide; });
        commitState();
        setStatus(plural(a == A::Hide ? "masquée(s) dans la vue 3D (toujours simulées)." : "affichée(s) en 3D."), false);
        break;
    case A::ShowAll:
        for (auto& p : garment_.panels) p.hidden3D = false;
        commitState();
        break;
    case A::Front:
    case A::Forward:
    case A::Backward:
    case A::Back: {
        const int move = a == A::Front ? 2 : a == A::Forward ? 1 : a == A::Backward ? -1 : -2;
        editor_.select(garment::reorderPanels(garment_, t, move));
        commitGarment();
        break;
    }
    case A::RotateCW:
    case A::RotateCCW:
    case A::Rotate180:
    case A::RotateFree: {
        const float deg = a == A::RotateCW ? -90.0f : a == A::RotateCCW ? 90.0f : a == A::Rotate180 ? 180.0f : -rotateDeg_;
        garment::rotatePanels(garment_, t, deg);
        commitGarment();
        break;
    }
    case A::FlipFace: {
        // Retourne la pièce en 3D (demi-tour sur elle-même) : l'endroit passe de l'autre côté.
        const garment::MeshingOptions mo = meshingOptions();
        for (const int p : t) {
            auto& pl = garment_.panels[static_cast<std::size_t>(p)].placement;
            const glm::vec3 up = garment::zoneFrame(pl.zone, mo).up;
            pl.rotation = glm::normalize(glm::angleAxis(glm::pi<float>(), up) * pl.rotation);
        }
        commitGarment();
        playing_ = false;
        setStatus(plural("retournée(s) : l'endroit est maintenant de l'autre côté (flèche orange)."), false);
        break;
    }
    case A::FlipH:
    case A::FlipV:
        garment::flipPanels(garment_, t, a == A::FlipH);
        commitGarment();
        break;
    case A::FrameSelection: editor_.frameParts(garment_, t); break;
    case A::ExportSelection:
        if (!clothValid_) {
            setStatus("Rien à exporter : la vue 3D est vide.", true);
            break;
        }
        exportOnly_ = t;
        exportGarment();
        break;
    }
}

bool App::applySplit(garment::Garment& g) const
{
    const int p = splitEdge_.panel, e = splitEdge_.edge;
    if (p < 0 || p >= static_cast<int>(g.panels.size())) return false;
    const auto& pts = g.panels[static_cast<std::size_t>(p)].points;
    const int n = static_cast<int>(pts.size());
    if (e < 0 || e >= n) return false;
    if (splitMode_ == 0) {
        const glm::vec2 a = pts[static_cast<std::size_t>(e)], b = pts[static_cast<std::size_t>((e + 1) % n)];
        g.insertPoint(p, e, glm::mix(a, b, glm::clamp(splitT_, 0.02f, 0.98f)));
        return true;
    }
    return garment::splitCurve(g, p, e, std::max(1, splitParts_));
}

void App::drawSplitWindow()
{
    editor_.splitPreview.clear();
    if (!splitOpen_) return;
    const int p = splitEdge_.panel;
    if (p < 0 || p >= static_cast<int>(garment_.panels.size()) || splitEdge_.edge < 0 ||
        splitEdge_.edge >= static_cast<int>(garment_.panels[static_cast<std::size_t>(p)].points.size())) {
        splitOpen_ = false;
        return;
    }
    // Aperçu : les endroits de coupe
    {
        garment::Garment preview = garment_;
        if (splitMode_ == 1) {
            garment::splitCurve(preview, p, splitEdge_.edge, std::max(1, splitParts_), &editor_.splitPreview);
        } else {
            const auto& q = garment_.panels[static_cast<std::size_t>(p)].points;
            const int m = static_cast<int>(q.size());
            editor_.splitPreview.push_back(glm::mix(q[static_cast<std::size_t>(splitEdge_.edge)],
                                                    q[static_cast<std::size_t>((splitEdge_.edge + 1) % m)], glm::clamp(splitT_, 0.02f, 0.98f)));
        }
    }
    const auto& pts = garment_.panels[static_cast<std::size_t>(p)].points;
    const int n = static_cast<int>(pts.size());
    int first = 0, count = 1;
    garment::curveAround(garment_, p, splitEdge_.edge, first, count);
    float curveLen = 0.0f;
    for (int k = 0; k < count; ++k)
        curveLen += glm::distance(pts[static_cast<std::size_t>((first + k) % n)], pts[static_cast<std::size_t>((first + k + 1) % n)]);
    const float edgeLen = glm::distance(pts[static_cast<std::size_t>(splitEdge_.edge)], pts[static_cast<std::size_t>((splitEdge_.edge + 1) % n)]);

    bool open = true;
    ImGui::SetNextWindowSize(ImVec2(300 * uiScale_, 0), ImGuiCond_Always);
    if (ImGui::Begin("Couper la ligne", &open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::RadioButton("Au point cliqué", &splitMode_, 0);
        ImGui::BeginDisabled(splitMode_ != 0);
        ImGui::Indent();
        float ratio = splitT_ * 100.0f;
        ImGui::SetNextItemWidth(150 * uiScale_);
        if (ImGui::DragFloat("Ratio", &ratio, 0.2f, 2.0f, 98.0f, "%.1f %%")) splitT_ = ratio / 100.0f;
        ImGui::TextDisabled("%.1f cm  +  %.1f cm", static_cast<double>(edgeLen * splitT_ * 100.0f),
                            static_cast<double>(edgeLen * (1.0f - splitT_) * 100.0f));
        ImGui::Unindent();
        ImGui::EndDisabled();
        ImGui::RadioButton("En parties égales", &splitMode_, 1);
        ImGui::BeginDisabled(splitMode_ != 1);
        ImGui::Indent();
        ImGui::SetNextItemWidth(150 * uiScale_);
        ImGui::InputInt("Morceaux", &splitParts_);
        splitParts_ = std::clamp(splitParts_, 1, 64);
        ImGui::TextDisabled(count > 1 ? "Courbe de %.1f cm : %.1f cm par morceau" : "Bord de %.1f cm : %.1f cm par morceau",
                            static_cast<double>(curveLen * 100.0f), static_cast<double>(curveLen * 100.0f / static_cast<float>(splitParts_)));
        ImGui::Unindent();
        ImGui::EndDisabled();
        ImGui::Spacing();
        const float bw = 120 * uiScale_;
        if (ImGui::Button("OK", ImVec2(bw, 0)) || ImGui::IsKeyPressed(ImGuiKey_Enter)) {
            if (applySplit(garment_)) {
                commitGarment();
                setStatus("Ligne coupée.", false);
            } else {
                setStatus("Les points de coupe existent déjà : rien à ajouter.", false);
            }
            open = false;
        }
        ImGui::SameLine();
        if (ImGui::Button("Annuler", ImVec2(bw, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) open = false;
    }
    ImGui::End();
    if (!open) {
        splitOpen_ = false;
        editor_.splitPreview.clear();
    }
}

bool App::drawPointMenu()
{
    const int p = editor_.menuPanel, k = editor_.menuPoint;
    if (k < 0 || p < 0 || p >= static_cast<int>(garment_.panels.size()) ||
        k >= static_cast<int>(garment_.panels[static_cast<std::size_t>(p)].points.size()))
        return false;
    ImGui::TextDisabled("Options du point");
    ImGui::Separator();
    if (ImGui::MenuItem("Supprimer le point", "Suppr", false, garment_.panels[static_cast<std::size_t>(p)].points.size() > 3)) {
        garment_.removePoint(p, k);
        editor_.selectedPoint = -1;
        editor_.selectedPoints.clear();
        commitGarment();
    }
    if (ImGui::BeginMenu("Ajouter une pince")) {
        ImGui::SetNextItemWidth(140 * uiScale_);
        ImGui::DragFloat("Profondeur", &dartDepthCm_, 0.1f, 0.5f, 40.0f, "%.1f cm");
        ImGui::SetNextItemWidth(140 * uiScale_);
        ImGui::DragFloat("Largeur", &dartWidthCm_, 0.05f, 0.2f, 20.0f, "%.1f cm");
        if (ImGui::Button("Ajouter la pince", ImVec2(-1, 0))) {
            if (garment::addDart(garment_, p, k, dartDepthCm_ / 100.0f, dartWidthCm_ / 100.0f) >= 0) {
                commitGarment();
                setStatus("Pince ajoutée.", false);
            } else {
                setStatus("Pince impossible ici : les bords voisins sont trop courts.", true);
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndMenu();
    }
    if (ImGui::MenuItem("Convertir en point de courbe")) {
        const glm::vec2 c = garment_.panels[static_cast<std::size_t>(p)].points[static_cast<std::size_t>(k)];
        if (garment::roundCorner(garment_, p, k, 0.5f * garment::maxCornerTangent(garment_, p, k), 12) >= 0) {
            auto& pn = garment_.panels[static_cast<std::size_t>(p)];
            if (pn.symmetric) garment::symmetrizePanel(garment_, p, c.x <= pn.symAxisX);
            commitGarment();
        }
    }
    if (ImGui::BeginMenu("Aligner")) {
        bool done = false;
        if (ImGui::MenuItem("Horizontalement sur le point précédent")) done = garment::alignPoint(garment_, p, k, true, false);
        if (ImGui::MenuItem("Horizontalement sur le point suivant")) done = garment::alignPoint(garment_, p, k, true, true);
        if (ImGui::MenuItem("Verticalement sur le point précédent")) done = garment::alignPoint(garment_, p, k, false, false);
        if (ImGui::MenuItem("Verticalement sur le point suivant")) done = garment::alignPoint(garment_, p, k, false, true);
        if (done) commitGarment();
        ImGui::EndMenu();
    }
    return true;
}

void App::drawPieceMenu()
{
    using A = PieceAction;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8 * uiScale_, 8 * uiScale_));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8 * uiScale_, 5 * uiScale_));
    if (!ImGui::BeginPopup("##menuPieces")) {
        ImGui::PopStyleVar(2);
        return;
    }
    if (drawPointMenu()) {
        ImGui::EndPopup();
        ImGui::PopStyleVar(2);
        return;
    }
    const int n = static_cast<int>(garment_.panels.size());
    std::vector<int> t;
    if (editor_.menuPanel >= 0)
        for (const int p : editor_.targets())
            if (p >= 0 && p < n) t.push_back(p);
    const bool has = !t.empty();
    const bool hasClip = !clipboard_.panels.empty();
    auto all = [&](auto&& test) {
        return has && std::all_of(t.begin(), t.end(), [&](int p) { return test(garment_.panels[static_cast<std::size_t>(p)]); });
    };
    auto any = [&](auto&& test) {
        return std::any_of(t.begin(), t.end(), [&](int p) { return test(garment_.panels[static_cast<std::size_t>(p)]); });
    };
    A chosen{};
    bool act = false;
    auto item = [&](const char* label, const char* keys, A action, bool enabled = true) {
        if (ImGui::MenuItem(label, keys, false, enabled)) {
            chosen = action;
            act = true;
        }
    };

    // En-tête : la ou les pièces visées
    if (has) {
        const std::string title = t.size() == 1 ? garment_.panels[static_cast<std::size_t>(t[0])].name : std::to_string(t.size()) + " pièces";
        ImGui::TextDisabled("%s", title.c_str());
    } else {
        ImGui::TextDisabled("Patron");
    }
    ImGui::Separator();
    // Bord sous la souris : déplier la demi-pièce autour de lui, ou le couper
    if (const auto e = editor_.menuEdge; e.panel >= 0 && e.panel < n) {
        if (ImGui::MenuItem("Déplier la pièce autour de ce bord")) {
            if (garment::unfoldPanel(garment_, e.panel, e.edge)) {
                editor_.selectedPoint = -1;
                editor_.selectedPoints.clear();
                commitGarment();
                setStatus("Pièce dépliée : elle est maintenant symétrique autour du pli.", false);
            } else {
                setStatus("Impossible de déplier autour de ce bord.", true);
            }
        }
        if (ImGui::MenuItem("Couper ce bord / cette courbe...")) {
            splitEdge_ = e;
            splitT_ = 0.5f;
            splitOpen_ = true;
            ImGui::SetNextWindowPos(ImGui::GetMousePos(), ImGuiCond_Always);
        }
        ImGui::Separator();
    }
    if (ImGui::BeginMenu("Sélection")) {
        item("Tout sélectionner", "Ctrl+A", A::SelectAll, n > 0);
        item("Inverser la sélection", "Ctrl+Maj+I", A::InvertSelection, n > 0);
        ImGui::Separator();
        item("Pièces de la même couleur", nullptr, A::SameColor, has);
        item("Pièces de la même zone du corps", nullptr, A::SameZone, has);
        item("Clones liés à cette pièce", nullptr, A::SameLink, has && any([](const garment::Panel& p) { return p.linkGroup > 0; }));
        ImGui::EndMenu();
    }
    item("Nouvelle couleur de tissu", nullptr, A::NewFabric, has);
    // Créer un patron : copie libre de la forme de la pièce, posée à côté, à glisser sur la pièce qui la recevra.
    if (t.size() == 1 && ImGui::MenuItem("Créer un patron")) {
        const auto& src = garment_.panels[static_cast<std::size_t>(t[0])];
        garment::Panel copy = src;
        copy.name = src.name + " (patron)";
        copy.linkGroup = 0;
        glm::vec2 lo(1e9f), hi(-1e9f);
        for (const auto& q : copy.points) {
            lo = glm::min(lo, q);
            hi = glm::max(hi, q);
        }
        const glm::vec2 d((hi.x - lo.x) + 0.03f, 0.0f);
        for (auto& q : copy.points) q += d;
        copy.symAxisX += d.x;
        garment_.panels.push_back(copy);
        editor_.select({static_cast<int>(garment_.panels.size()) - 1});
        commitGarment();
        setStatus("Patron créé à côté. Glissez-le sur la pièce qui doit le recevoir, puis clic droit › Fixer / coudre.", false);
    }
    // Poche : coudre la pièce sur celle qui est dessous dans le patron, ou la découdre.
    if (t.size() == 1) {
        const int p = t[0];
        const int at = garment_.attachmentOf(p);
        bool isBase = false;
        for (const auto& a : garment_.attachments) isBase = isBase || a.base == p;
        if (at >= 0) {
            const int base = garment_.attachments[static_cast<std::size_t>(at)].base;
            if (ImGui::MenuItem(("Découdre de « " + garment_.panels[static_cast<std::size_t>(base)].name + " »").c_str())) {
                garment_.attachments.erase(garment_.attachments.begin() + at);
                commitGarment();
                setStatus("Pièce décousue : elle redevient une pièce libre.", false);
            }
        } else if (!isBase) {
            const auto& self = garment_.panels[static_cast<std::size_t>(p)].points;
            glm::vec2 lo(1e9f), hi(-1e9f);
            for (const auto& q : self) {
                lo = glm::min(lo, q);
                hi = glm::max(hi, q);
            }
            const glm::vec2 c = (lo + hi) * 0.5f;
            int under = -1;
            for (int q = n - 1; q >= 0 && under < 0; --q)
                if (q != p && garment_.attachmentOf(q) < 0 && garment_.panelEditable(q) &&
                    garment::pointInPolygon(garment_.panels[static_cast<std::size_t>(q)].points, c))
                    under = q;
            if (under >= 0 && ImGui::MenuItem(("Fixer / coudre sur « " + garment_.panels[static_cast<std::size_t>(under)].name + " »").c_str())) {
                if (garment_.attachPatch(p, under, c, garment::AttachKind::AllRound) >= 0) {
                    auto& col = garment_.panels[static_cast<std::size_t>(p)].color;
                    const glm::vec3 baseCol = garment_.panels[static_cast<std::size_t>(under)].color;
                    if (glm::distance(col, baseCol) < 0.1f) col = baseCol * 0.82f;
                    commitGarment();
                    setStatus("Cousue tout le tour, à sa place. Outil Couture (S) : clic sur un bord de la pièce pour le découdre "
                              "(ouverture de poche), ou pour le recoudre.", false);
                }
            }
            if (under < 0) ImGui::MenuItem("Fixer / coudre (posez-la d'abord sur une pièce)", nullptr, false, false);
        }
    }
    if (ImGui::BeginMenu("Calque", has)) {
        int target = -2;
        for (int k = 0; k < static_cast<int>(garment_.groups.size()); ++k) {
            const bool inIt = all([&](const garment::Panel& p) { return p.group == k; });
            if (ImGui::MenuItem((garment_.groups[static_cast<std::size_t>(k)].name + "##c" + std::to_string(k)).c_str(), nullptr, inIt)) target = k;
        }
        if (!garment_.groups.empty()) ImGui::Separator();
        if (ImGui::MenuItem("Nouveau calque avec la sélection")) {
            garment::PieceGroup gr;
            gr.name = "Calque " + std::to_string(garment_.groups.size() + 1);
            garment_.groups.push_back(gr);
            target = static_cast<int>(garment_.groups.size()) - 1;
            activeGroup_ = target;
        }
        if (ImGui::MenuItem("Sans calque", nullptr, false, any([](const garment::Panel& p) { return p.group >= 0; }))) target = -1;
        ImGui::EndMenu();
        if (target != -2) moveSelectionToGroup(target);
    }
    ImGui::Separator();
    item("Copier", "Ctrl+C", A::Copy, has);
    item("Coller ici", "Ctrl+V", A::Paste, hasClip);
    item("Coller en miroir", "Ctrl+R", A::PasteMirror, hasClip);
    item("Supprimer", "Suppr", A::Delete, has);
    {
        // Coutures touchant la sélection (une sélection de toutes les pièces efface toutes les coutures).
        const bool seamed = has && (std::any_of(garment_.seams.begin(), garment_.seams.end(), [&](const garment::Seam& s) {
            return std::find(t.begin(), t.end(), s.a.panel) != t.end() || std::find(t.begin(), t.end(), s.b.panel) != t.end();
        }) || std::any_of(garment_.attachments.begin(), garment_.attachments.end(), [&](const garment::Attachment& a) {
            return std::find(t.begin(), t.end(), a.patch) != t.end();
        }));
        item("Effacer les coutures", nullptr, A::DeleteSeams, seamed);
    }
    ImGui::Separator();
    if (ImGui::BeginMenu("Cloner (édition liée)", has)) {
        item("Clone symétrique cousu", "Ctrl+D", A::CloneMirrorSewn);
        item("Clone symétrique", nullptr, A::CloneMirror);
        item("Clone identique", nullptr, A::CloneInstance);
        ImGui::Separator();
        item("Clone par-dessus (couche)", "Ctrl+Maj+V", A::CloneOver);
        item("Clone en dessous (couche)", "Ctrl+Maj+B", A::CloneUnder);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Édition liée", has)) {
        item("Lier les pièces sélectionnées", nullptr, A::Link, t.size() >= 2);
        item("Délier", nullptr, A::Unlink, any([](const garment::Panel& p) { return p.linkGroup > 0; }));
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Contour", has)) {
        item("Agrandir de 1 cm", nullptr, A::Grow);
        item("Réduire de 1 cm", nullptr, A::Shrink);
        ImGui::SetNextItemWidth(110 * uiScale_);
        ImGui::DragFloat("##decalage", &offsetCm_, 0.05f, -20.0f, 20.0f, "%+.1f cm");
        ImGui::SameLine();
        if (ImGui::Button("Décaler")) {
            chosen = A::Offset;
            act = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::Separator();
        item("Simplifier les points", nullptr, A::Simplify);
        item("Renfort intérieur (2 cm du bord)", nullptr, A::InnerShape);
        ImGui::EndMenu();
    }
    ImGui::Separator();
    if (ImGui::BeginMenu("Pièce en 3D", has || n > 0)) {
        const bool inactive = all([](const garment::Panel& p) { return !p.active3D; });
        if (inactive) item("Réactiver en 3D", nullptr, A::Activate, has);
        else {
            item("Désactiver (pièce seule)", "Ctrl+J", A::Deactivate, has);
            item("Désactiver (pièce et coutures)", nullptr, A::DeactivateWithSeams, has);
        }
        ImGui::Separator();
        item("Retourner endroit / envers", nullptr, A::FlipFace, has);
        ImGui::Separator();
        if (all([](const garment::Panel& p) { return p.frozen; })) item("Libérer", "Ctrl+K", A::Unfreeze);
        else item("Figer", "Ctrl+K", A::Freeze, has);
        if (all([](const garment::Panel& p) { return p.stiffness > 1.0f; })) item("Tissu normal", "Ctrl+H", A::Soften);
        else item("Renforcer", "Ctrl+H", A::Strengthen, has);
        ImGui::Separator();
        if (all([](const garment::Panel& p) { return p.hidden3D; })) item("Afficher en 3D", "Maj+Q", A::Show);
        else item("Masquer en 3D", "Maj+Q", A::Hide, has);
        item("Tout afficher en 3D", nullptr, A::ShowAll,
             std::any_of(garment_.panels.begin(), garment_.panels.end(), [](const garment::Panel& p) { return p.hidden3D; }));
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Ordre", has)) {
        item("Premier plan", nullptr, A::Front);
        item("Avancer", nullptr, A::Forward);
        item("Reculer", nullptr, A::Backward);
        item("Arrière-plan", nullptr, A::Back);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Pivoter", has)) {
        item("90° sens horaire", nullptr, A::RotateCW);
        item("90° sens inverse", nullptr, A::RotateCCW);
        item("180°", nullptr, A::Rotate180);
        ImGui::SetNextItemWidth(110 * uiScale_);
        ImGui::DragFloat("##angle", &rotateDeg_, 0.5f, -180.0f, 180.0f, "%.0f°");
        ImGui::SameLine();
        if (ImGui::Button("Pivoter")) {
            chosen = A::RotateFree;
            act = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Retourner", has)) {
        item("Gauche - droite", nullptr, A::FlipH);
        item("Haut - bas", nullptr, A::FlipV);
        ImGui::EndMenu();
    }
    ImGui::Separator();
    item("Cadrer la sélection", nullptr, A::FrameSelection, has);
    item("Exporter la sélection en 3D (OBJ)...", nullptr, A::ExportSelection, has && clothValid_);
    ImGui::EndPopup();
    ImGui::PopStyleVar(2);
    if (act) {
        menuAtPoint_ = true;
        pieceAction(chosen);
        menuAtPoint_ = false;
    }
}

void App::drawSplitter()
{
    // Poignée verticale entre le patron et la vue 3D : glisser pour redimensionner.
    const float top = menuHeight_ + toolbarHeight_;
    ImGui::SetNextWindowPos({leftWidth_, top});
    ImGui::SetNextWindowSize({viewPos_.x - leftWidth_, viewSize_.y});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, kBg0);
    ImGui::Begin("##split", nullptr, kPaneFlags | ImGuiWindowFlags_NoScrollbar);
    ImGui::InvisibleButton("##splitter", ImGui::GetContentRegionAvail());
    const bool hot = ImGui::IsItemHovered() || ImGui::IsItemActive();
    if (hot) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    if (ImGui::IsItemActive()) leftWidth_ += ImGui::GetIO().MouseDelta.x;
    if (hot) {
        const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
        ImGui::GetWindowDrawList()->AddRectFilled(a, b, ImGui::GetColorU32(kAccentDim));
    }
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
}

void App::drawViewportOverlay()
{
    // Textes dessinés sur la vue 3D (sous les fenêtres, au-dessus de l'image 3D).
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const ImVec2 o(viewPos_.x, viewPos_.y);
    const ImVec2 br(o.x + viewSize_.x, o.y + viewSize_.y);
    const float pad = 12 * uiScale_;
    // Vignette légère : le regard reste sur le vêtement, sans assombrir le centre.
    {
        const float v = 64.0f * uiScale_;
        const ImU32 edge = IM_COL32(8, 7, 6, 78), mid = IM_COL32(8, 7, 6, 0);
        dl->AddRectFilledMultiColor(o, {br.x, o.y + v}, edge, edge, mid, mid);
        dl->AddRectFilledMultiColor({o.x, br.y - v}, br, mid, mid, edge, edge);
        dl->AddRectFilledMultiColor(o, {o.x + v * 0.7f, br.y}, edge, mid, mid, edge);
        dl->AddRectFilledMultiColor({br.x - v * 0.7f, o.y}, br, mid, edge, edge, mid);
    }
    char text[160];
    std::snprintf(text, sizeof(text), "%s   ·   %.2f m", avatarSource_.name.c_str(), static_cast<double>(avatarHeight_));
    ImFont* titleFont = fontBold_ ? fontBold_ : ImGui::GetFont();
    const float titleSize = titleFont->FontSize;
    const ImVec2 titleTs = titleFont->CalcTextSizeA(titleSize, FLT_MAX, 0.0f, "VUE 3D");
    const ImVec2 subTs = ImGui::CalcTextSize(text);
    const ImVec2 chip(o.x + pad * 0.7f, o.y + pad * 0.45f);
    const ImVec2 chipBr(chip.x + std::max(titleTs.x, subTs.x) + pad * 1.4f, chip.y + titleTs.y + subTs.y + pad * 0.85f);
    dl->AddRectFilled(chip, chipBr, IM_COL32(14, 12, 10, 150), 7.0f);
    dl->AddRectFilled({chip.x, chip.y + 7.0f}, {chip.x + 2.0f, chipBr.y - 7.0f}, kAccentU32, 1.0f);
    dl->AddText(titleFont, titleSize, {chip.x + pad * 0.7f, chip.y + pad * 0.28f}, ImGui::GetColorU32(kTextDim), "VUE 3D");
    dl->AddText({chip.x + pad * 0.7f, chip.y + pad * 0.28f + titleTs.y}, ImGui::GetColorU32(kText), text);

    if (clothValid_ && solver_.seamCount() > 0 && solver_.seamProgress() < 1.0f) {
        std::snprintf(text, sizeof(text), "Couture en cours   %.0f %%", static_cast<double>(solver_.seamProgress() * 100.0f));
        const ImVec2 ts = ImGui::CalcTextSize(text);
        const ImVec2 c(o.x + viewSize_.x * 0.5f, o.y + pad * 4.4f);
        dl->AddRectFilled({c.x - ts.x * 0.5f - 14, c.y - 7}, {c.x + ts.x * 0.5f + 14, c.y + ts.y + 7}, IM_COL32(16, 13, 10, 220), 8.0f);
        dl->AddRect({c.x - ts.x * 0.5f - 14, c.y - 7}, {c.x + ts.x * 0.5f + 14, c.y + ts.y + 7}, IM_COL32(220, 174, 96, 90), 8.0f);
        dl->AddText({c.x - ts.x * 0.5f, c.y}, kAccentU32, text);
    }
    const bool modalSoon = showAbout_ || showShortcuts_ || showPathPopup_ || showImagePopup_;
    if (!clothValid_ && garment_.empty() && !modalSoon) {
        const char* msg = "Aucun vêtement — tracez sur l'ombre à gauche, ou choisissez un modèle en haut.";
        const ImVec2 ts = ImGui::CalcTextSize(msg);
        const float h = ts.y + pad * 1.15f;
        dl->PushClipRect(o, br, true);
        dl->AddRectFilled({o.x, br.y - h}, br, IM_COL32(14, 12, 10, 200));
        dl->AddLine({o.x, br.y - h}, {br.x, br.y - h}, IM_COL32(220, 174, 96, 80));
        const float x = o.x + std::max(pad, (viewSize_.x - ts.x) * 0.5f);
        dl->AddText({x, br.y - h + pad * 0.45f}, ImGui::GetColorU32(kText), msg);
        dl->PopClipRect();
    }
    drawSelection3D();
    drawSeams3D();
    drawGrain3D();
    drawGizmo();
    const char* help = playing_ ? "Clic : choisir   ·   glisser : tourner la pièce   ·   Maj : déplacer   ·   clic droit : saisir le tissu"
                                : "Pause   ·   gizmo : déplacer   ·   Alt + clic : couture   ·   glisser le vide : tourner la vue";
    const ImVec2 hs = ImGui::CalcTextSize(help);
    if (!garment_.empty() && hs.x < viewSize_.x - 2 * pad) {
        dl->AddRectFilled({o.x, br.y - hs.y - pad * 1.15f}, br, IM_COL32(10, 9, 8, 128));
        dl->AddText({o.x + pad, br.y - hs.y - pad * 0.45f}, IM_COL32(214, 206, 190, 210), help);
    }

    // Boutons de vues prédéfinies, en haut à droite.
    const float bw = 54 * uiScale_;
    ImGui::SetNextWindowPos({o.x + viewSize_.x - 5.6f * bw - pad - 28 * uiScale_, o.y + pad * 0.45f});
    ImGui::SetNextWindowBgAlpha(0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4, 4));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f * uiScale_);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
    ImGui::Begin("##vues", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                        ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.09f, 0.08f, 0.07f, 0.78f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.24f, 0.20f, 0.14f, 0.94f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, kAccentDim);
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.90f, 0.78f, 0.52f, 0.16f));
    auto nearYaw = [&](float target, float pitch) {
        float d = std::fmod(std::abs(camera_.yaw - target), 2.0f * kPi);
        if (d > kPi) d = 2.0f * kPi - d;
        return d < 0.09f && std::abs(camera_.pitch - pitch) < 0.09f;
    };
    auto viewButton = [&](const char* name, float yaw) {
        const bool on = nearYaw(yaw, 0.05f);
        if (on) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.55f));
        if (ImGui::Button(name, ImVec2(bw, 0))) {
            frameCamera();
            camera_.yaw = yaw;
            camera_.pitch = 0.05f;
        }
        if (on) ImGui::PopStyleColor();
        ImGui::SameLine(0, 3);
    };
    viewButton("Face", 0.0f);
    viewButton("Dos", kPi);
    viewButton("Profil", kPi * 0.5f);
    {
        const bool on = nearYaw(0.45f, 0.12f);
        if (on) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.55f));
        if (ImGui::Button("3/4", ImVec2(bw, 0))) frameCamera();
        if (on) ImGui::PopStyleColor();
    }
    ImGui::SameLine(0, 10);
    if (showSeams3D_) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.50f));
    if (ImGui::Button("Coutures", ImVec2(bw * 1.5f, 0))) showSeams3D_ = !showSeams3D_;
    if (showSeams3D_) ImGui::PopStyleColor();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
        ImGui::SetTooltip("Afficher les coutures dans la vue 3D.\nCliquer une couture l'active ou la désactive.");
    ImGui::PopStyleColor(4);
    ImGui::End();
    ImGui::PopStyleVar(3);
}

void App::drawInspector()
{
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const float top = menuHeight_ + toolbarHeight_;
    ImGui::SetNextWindowPos({display.x - inspectorWidth_, top});
    ImGui::SetNextWindowSize({inspectorWidth_, viewSize_.y});
    ImGui::Begin("##inspecteur", nullptr, kPaneFlags);
    {   // filet de séparation à gauche
        const ImVec2 p = ImGui::GetWindowPos();
        ImGui::GetWindowDrawList()->AddLine(p, {p.x, p.y + viewSize_.y}, ImGui::GetColorU32(kBg0), 2.0f);
    }
    drawLayers();
    paneTitle("PROPRIÉTÉS", fontBold_);
    ImGui::Spacing();
    ImGui::BeginChild("##proprietes", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8 * uiScale_, 5 * uiScale_));
    inspectorBackdrop();
    inspectorPanel();
    inspectorSeams();
    inspectorFabric();
    inspectorAvatar();
    inspectorSimulation();
    inspectorDisplay();
    ImGui::PopStyleVar();
    ImGui::EndChild();
    ImGui::End();
}

void App::moveSelectionToGroup(int group)
{
    const auto t = editor_.targets();
    if (t.empty()) {
        setStatus("Sélectionne d'abord des pièces dans le patron.", true);
        return;
    }
    int moved = 0;
    for (const int p : t) {
        if (p < 0 || p >= static_cast<int>(garment_.panels.size())) continue;
        // Une pièce posée (poche, rabat...) suit sa pièce de base : on range aussi les pièces posées dessus.
        auto put = [&](int q) {
            if (garment_.panels[static_cast<std::size_t>(q)].group != group) {
                garment_.panels[static_cast<std::size_t>(q)].group = group;
                ++moved;
            }
        };
        put(p);
        for (const auto& a : garment_.attachments)
            if (a.base == p) put(a.patch);
    }
    if (moved == 0) return;
    commitGarment();
    const std::string where = group >= 0 ? "« " + garment_.groups[static_cast<std::size_t>(group)].name + " »" : "« Sans calque »";
    setStatus(std::to_string(moved) + " pièce(s) rangée(s) dans " + where + ".", false);
}

void App::drawLayers()
{
    auto& groups = garment_.groups;
    const int ng = static_cast<int>(groups.size());
    const int np = static_cast<int>(garment_.panels.size());
    if (activeGroup_ >= ng) activeGroup_ = ng - 1;
    if (renameGroup_ >= ng) renameGroup_ = -1;
    groupOpen_.resize(static_cast<std::size_t>(ng), 0);
    if (layersHeight_ < 0.0f) layersHeight_ = std::round(200.0f * uiScale_);
    layersHeight_ = std::clamp(layersHeight_, 90.0f * uiScale_, std::max(90.0f * uiScale_, viewSize_.y * 0.7f));

    paneTitle("CALQUES", fontBold_);
    // Barre d'outils du panneau
    const auto sel = editor_.targets();
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6 * uiScale_, 3 * uiScale_));
    if (ImGui::SmallButton("+ Nouveau")) {
        garment::PieceGroup gr;
        gr.name = "Calque " + std::to_string(ng + 1);
        groups.push_back(gr);
        activeGroup_ = ng;
        groupOpen_.push_back(1);
        if (!sel.empty()) moveSelectionToGroup(activeGroup_);   // la sélection va dans le nouveau calque
        else commitGarment();
        renameGroup_ = activeGroup_;
        std::snprintf(renameBuf_, sizeof(renameBuf_), "%s", gr.name.c_str());
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Nouveau calque (les pièces sélectionnées y sont rangées)");
    ImGui::SameLine();
    ImGui::BeginDisabled(sel.empty() || activeGroup_ < 0);
    if (ImGui::SmallButton("Ranger ici")) moveSelectionToGroup(activeGroup_);
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Range les pièces sélectionnées dans le calque choisi (avec leurs poches et renforts)");
    ImGui::SameLine();
    ImGui::BeginDisabled(activeGroup_ < 0);
    if (ImGui::SmallButton("Supprimer")) {
        garment_.removeGroup(activeGroup_);
        groupOpen_.erase(groupOpen_.begin() + activeGroup_);
        activeGroup_ = -1;
        commitGarment();
        setStatus("Calque supprimé : ses pièces sont gardées, « sans calque ».", false);
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Supprime le calque choisi (les pièces sont gardées)");
    ImGui::PopStyleVar();

    ImGui::PushStyleColor(ImGuiCol_ChildBg, kBg0);
    ImGui::BeginChild("##calques", ImVec2(0, layersHeight_), ImGuiChildFlags_Borders);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float rowH = ImGui::GetFrameHeight();
    const float icon = rowH;
    const ImU32 on = ImGui::GetColorU32(kText), off = ImGui::GetColorU32(kTextDim), accent = kAccentU32;
    bool changedGeometry = false, changedState = false;

    // Œil : visible (simulé et affiché) ou masqué.
    auto eyeButton = [&](const char* id, bool visible) {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const bool clicked = ImGui::InvisibleButton(id, ImVec2(icon, rowH));
        const bool hot = ImGui::IsItemHovered();
        const ImVec2 c(p.x + icon * 0.5f, p.y + rowH * 0.5f);
        const float rx = icon * 0.32f, ry = rowH * 0.2f;
        const ImU32 col = hot ? accent : (visible ? on : off);
        // amande dessinée par deux arcs
        dl->PathArcTo({c.x, c.y + ry * 1.6f}, ry * 2.6f, -kPi * 0.5f - 0.95f, -kPi * 0.5f + 0.95f, 10);
        dl->PathStroke(col, 0, 1.4f);
        dl->PathArcTo({c.x, c.y - ry * 1.6f}, ry * 2.6f, kPi * 0.5f - 0.95f, kPi * 0.5f + 0.95f, 10);
        dl->PathStroke(col, 0, 1.4f);
        if (visible) dl->AddCircleFilled(c, ry * 0.75f, col);
        else dl->AddLine({c.x - rx, c.y + ry * 1.3f}, {c.x + rx, c.y - ry * 1.3f}, col, 1.4f);
        if (hot) ImGui::SetTooltip(visible ? "Masquer le calque (ni affiché ni simulé)" : "Afficher le calque");
        return clicked;
    };
    // Cadenas : verrouillé (affiché mais non modifiable dans le patron).
    auto lockButton = [&](const char* id, bool locked) {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const bool clicked = ImGui::InvisibleButton(id, ImVec2(icon, rowH));
        const bool hot = ImGui::IsItemHovered();
        const ImVec2 c(p.x + icon * 0.5f, p.y + rowH * 0.56f);
        const float w = icon * 0.26f, h = rowH * 0.22f;
        const ImU32 col = hot ? accent : (locked ? on : IM_COL32(110, 112, 118, 120));
        dl->AddRectFilled({c.x - w, c.y - h * 0.3f}, {c.x + w, c.y + h * 1.3f}, col, 1.5f);
        dl->PathArcTo({c.x, c.y - h * 0.3f}, w * 0.68f, kPi, locked ? 2.0f * kPi : 1.75f * kPi, 8);
        dl->PathStroke(col, 0, 1.5f);
        if (hot) ImGui::SetTooltip(locked ? "Déverrouiller" : "Verrouiller (pièces non modifiables dans le patron)");
        return clicked;
    };
    auto pieceRow = [&](int p) {
        const auto& pn = garment_.panels[static_cast<std::size_t>(p)];
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + icon * 2.0f + 14.0f * uiScale_);
        const bool editable = garment_.panelEditable(p);
        const bool selected = editor_.isSelected(p);
        ImGui::PushID(p);
        // pastille de couleur du tissu
        const ImVec2 sp = ImGui::GetCursorScreenPos();
        dl->AddRectFilled({sp.x, sp.y + rowH * 0.3f}, {sp.x + rowH * 0.4f, sp.y + rowH * 0.7f},
                          ImGui::GetColorU32(ImVec4(pn.color.r, pn.color.g, pn.color.b, 1.0f)), 2.0f);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + rowH * 0.55f);
        ImGui::PushStyleColor(ImGuiCol_Text, editable ? kText : kTextDim);
        const std::string label = pn.name + (garment_.attachmentOf(p) >= 0 ? "  (posée)" : "");
        if (ImGui::Selectable(label.c_str(), selected, editable ? 0 : ImGuiSelectableFlags_Disabled, ImVec2(0, rowH))) {
            if (ImGui::GetIO().KeyCtrl) {
                auto t = editor_.targets();
                const auto it = std::find(t.begin(), t.end(), p);
                if (it == t.end()) t.push_back(p);
                else t.erase(it);
                editor_.select(t);
            } else {
                editor_.select({p});
            }
        }
        ImGui::PopStyleColor();
        ImGui::PopID();
    };
    auto countIn = [&](int group) {
        int c = 0;
        for (const auto& pn : garment_.panels) c += pn.group == group ? 1 : 0;
        return c;
    };
    auto selectGroup = [&](int group) {
        std::vector<int> ids;
        for (int p = 0; p < np; ++p)
            if (garment_.panels[static_cast<std::size_t>(p)].group == group && garment_.panelEditable(p)) ids.push_back(p);
        editor_.select(ids);
    };
    // Une pièce sélectionnée qui devient masquée ou verrouillée sort de la sélection.
    auto dropHiddenSelection = [&]() {
        auto t = editor_.targets();
        const auto before = t.size();
        std::erase_if(t, [&](int p) { return p < 0 || p >= np || !garment_.panelEditable(p); });
        if (t.size() != before) editor_.select(t);
    };

    int moveFrom = -1, moveTo = -1, removeAt = -1;
    for (int k = 0; k < ng; ++k) {
        auto& gr = groups[static_cast<std::size_t>(k)];
        ImGui::PushID(k);
        const ImVec2 rowMin = ImGui::GetCursorScreenPos();
        if (k == activeGroup_)
            dl->AddRectFilled(rowMin, {rowMin.x + ImGui::GetContentRegionAvail().x, rowMin.y + rowH}, ImGui::GetColorU32(kBg2), 3.0f);
        if (eyeButton("##oeil", gr.visible)) {
            // Alt+clic : n'afficher que ce calque
            if (ImGui::GetIO().KeyAlt) {
                for (auto& o : groups) o.visible = false;
                gr.visible = true;
            } else {
                gr.visible = !gr.visible;
            }
            changedGeometry = true;
        }
        ImGui::SameLine(0, 0);
        if (lockButton("##verrou", gr.locked)) {
            gr.locked = !gr.locked;
            changedState = true;
        }
        ImGui::SameLine(0, 4.0f * uiScale_);
        char& open = groupOpen_[static_cast<std::size_t>(k)];
        if (ImGui::ArrowButton("##ouvrir", open != 0 ? ImGuiDir_Down : ImGuiDir_Right)) open = open != 0 ? 0 : 1;
        ImGui::SameLine(0, 6.0f * uiScale_);
        if (renameGroup_ == k) {
            ImGui::SetNextItemWidth(-1);
            if (ImGui::IsWindowAppearing() || !ImGui::IsAnyItemActive()) ImGui::SetKeyboardFocusHere();
            if (ImGui::InputText("##nom", renameBuf_, sizeof(renameBuf_), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll) ||
                ImGui::IsItemDeactivated()) {
                if (renameBuf_[0] != '\0' && gr.name != renameBuf_) {
                    gr.name = renameBuf_;
                    changedState = true;
                }
                renameGroup_ = -1;
            }
        } else {
            const int count = countIn(k);
            const std::string label = gr.name + "##g";
            ImGui::PushStyleColor(ImGuiCol_Text, gr.visible ? kText : kTextDim);
            if (ImGui::Selectable(label.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick | ImGuiSelectableFlags_AllowOverlap, ImVec2(0, rowH))) {
                activeGroup_ = k;
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    renameGroup_ = k;
                    std::snprintf(renameBuf_, sizeof(renameBuf_), "%s", gr.name.c_str());
                } else if (gr.visible && !gr.locked) {
                    selectGroup(k);
                }
            }
            ImGui::PopStyleColor();
            // nombre de pièces, aligné à droite
            const std::string n = std::to_string(count);
            const ImVec2 ts = ImGui::CalcTextSize(n.c_str());
            const ImVec2 r = ImGui::GetItemRectMax();
            dl->AddText({r.x - ts.x - 4.0f, rowMin.y + (rowH - ts.y) * 0.5f}, off, n.c_str());
            if (ImGui::BeginPopupContextItem("##menu")) {
                activeGroup_ = k;
                if (ImGui::MenuItem("Renommer")) {
                    renameGroup_ = k;
                    std::snprintf(renameBuf_, sizeof(renameBuf_), "%s", gr.name.c_str());
                }
                if (ImGui::MenuItem("Sélectionner ses pièces", nullptr, false, gr.visible && !gr.locked)) selectGroup(k);
                if (ImGui::MenuItem("Ranger la sélection ici", nullptr, false, !sel.empty())) moveSelectionToGroup(k);
                ImGui::Separator();
                if (ImGui::MenuItem("Afficher seulement ce calque")) {
                    for (auto& o : groups) o.visible = false;
                    gr.visible = true;
                    changedGeometry = true;
                }
                if (ImGui::MenuItem("Tout afficher", nullptr, false,
                                    std::any_of(groups.begin(), groups.end(), [](const garment::PieceGroup& o) { return !o.visible; }))) {
                    for (auto& o : groups) o.visible = true;
                    changedGeometry = true;
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Monter", nullptr, false, k > 0)) {
                    moveFrom = k;
                    moveTo = k - 1;
                }
                if (ImGui::MenuItem("Descendre", nullptr, false, k + 1 < ng)) {
                    moveFrom = k;
                    moveTo = k + 1;
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Supprimer le calque")) removeAt = k;
                ImGui::EndPopup();
            }
        }
        if (open != 0)
            for (int p = 0; p < np; ++p)
                if (garment_.panels[static_cast<std::size_t>(p)].group == k) pieceRow(p);
        ImGui::PopID();
    }
    // Pièces sans calque
    {
        const int loose = countIn(-1);
        if (loose > 0 || ng == 0) {
            if (ng > 0) ImGui::Separator();
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + icon * 2.0f + 4.0f * uiScale_);
            if (ImGui::ArrowButton("##ouvrirLibres", looseOpen_ ? ImGuiDir_Down : ImGuiDir_Right)) looseOpen_ = !looseOpen_;
            ImGui::SameLine(0, 6.0f * uiScale_);
            ImGui::PushStyleColor(ImGuiCol_Text, kTextDim);
            const std::string label = "Sans calque (" + std::to_string(loose) + ")##libres";
            if (ImGui::Selectable(label.c_str(), false, 0, ImVec2(0, rowH))) {
                activeGroup_ = -1;
                selectGroup(-1);
            }
            ImGui::PopStyleColor();
            if (looseOpen_)
                for (int p = 0; p < np; ++p)
                    if (garment_.panels[static_cast<std::size_t>(p)].group < 0) pieceRow(p);
            if (ng == 0) {
                ImGui::Spacing();
                ImGui::PushStyleColor(ImGuiCol_Text, kTextDim);
                ImGui::TextWrapped("Sélectionne des pièces puis « + Nouveau » pour en faire un objet séparé (veste, pantalon...).");
                ImGui::PopStyleColor();
            }
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
    // Poignée : glisser pour agrandir ou réduire le panneau des calques.
    {
        ImGui::InvisibleButton("##tailleCalques", ImVec2(-1, 6.0f * uiScale_));
        const bool hot = ImGui::IsItemHovered() || ImGui::IsItemActive();
        if (hot) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
        if (ImGui::IsItemActive()) layersHeight_ += ImGui::GetIO().MouseDelta.y;
        const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
        const float cy = (a.y + b.y) * 0.5f, cx = (a.x + b.x) * 0.5f;
        ImGui::GetWindowDrawList()->AddLine({cx - 14, cy}, {cx + 14, cy}, ImGui::GetColorU32(hot ? kAccent : kLine), 2.0f);
    }

    if (moveFrom >= 0) {
        garment_.moveGroup(moveFrom, moveTo);
        std::swap(groupOpen_[static_cast<std::size_t>(moveFrom)], groupOpen_[static_cast<std::size_t>(moveTo)]);
        activeGroup_ = moveTo;
        changedState = true;
    }
    if (removeAt >= 0) {
        garment_.removeGroup(removeAt);
        groupOpen_.erase(groupOpen_.begin() + removeAt);
        activeGroup_ = -1;
        changedGeometry = true;
    }
    if (changedGeometry) {   // calque masqué / affiché : la simulation est refaite sans (ou avec) ses pièces
        dropHiddenSelection();
        commitGarment();
    } else if (changedState) {
        dropHiddenSelection();
        commitState();
    }
}

void App::inspectorPrints(int idx)
{
    auto& panel = garment_.panels[static_cast<std::size_t>(idx)];
    ImGui::SeparatorText("Impressions (logos, textes)");
    int removeAt = -1;
    bool changed = false, done = false;
    for (std::size_t k = 0; k < panel.prints.size(); ++k) {
        auto& pr = panel.prints[k];
        ImGui::PushID(static_cast<int>(k) + 1000);
        const auto cut = pr.image.find_last_of("/\\");
        ImGui::TextUnformatted((cut == std::string::npos ? pr.image : pr.image.substr(cut + 1)).c_str());
        if (printMissing_.count(pr.image) != 0) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.95f, 0.55f, 0.35f, 1.0f), "(introuvable)");
        }
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 8.0f * uiScale_);
        if (ImGui::SmallButton("x")) removeAt = static_cast<int>(k);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Retirer cette impression");
        float off[2] = {pr.offset.x * 100.0f, pr.offset.y * 100.0f};
        label("Position");
        ImGui::SetNextItemWidth(-1);
        if (ImGui::DragFloat2("##off", off, 0.1f, -300.0f, 300.0f, "%.1f cm")) {
            pr.offset = glm::vec2(off[0], off[1]) / 100.0f;
            changed = true;
        }
        done = done || ImGui::IsItemDeactivatedAfterEdit();
        float size[2] = {pr.width * 100.0f, pr.height * 100.0f};
        label("Taille");
        ImGui::SetNextItemWidth(-1);
        if (ImGui::DragFloat2("##size", size, 0.1f, 0.0f, 300.0f, "%.1f cm")) {
            pr.width = std::clamp(size[0], 0.2f, 300.0f) / 100.0f;
            pr.height = std::clamp(size[1], 0.0f, 300.0f) / 100.0f;
            changed = true;
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Largeur, hauteur (0 = proportions de l'image). Position : depuis le centre de la pièce.");
        done = done || ImGui::IsItemDeactivatedAfterEdit();
        label("Rotation");
        ImGui::SetNextItemWidth(-1);
        if (ImGui::DragFloat("##angle", &pr.angleDeg, 0.5f, -360.0f, 360.0f, "%.0f°")) changed = true;
        done = done || ImGui::IsItemDeactivatedAfterEdit();
        ImGui::PopID();
    }
    if (removeAt >= 0) {
        pushUndo();
        panel.prints.erase(panel.prints.begin() + removeAt);
        committed_ = garment_;
        updateClothPattern();
    }
    if (changed) updateClothPattern();
    if (done) {
        pushUndo();
        committed_ = garment_;
    }
    if (ImGui::Button("Ajouter une image...", ImVec2(-1, 0))) {
        std::string path = openFileDialog("Image à imprimer sur la pièce", "Images (PNG, JPEG, BMP, TGA)|*.png;*.jpg;*.jpeg;*.bmp;*.tga");
        if (!path.empty()) {
            const std::string dir = projectDirectory();   // chemin relatif si l'image est dans le dossier du projet
            if (!dir.empty() && path.rfind(dir, 0) == 0) path = path.substr(dir.size());
            garment::Print pr;
            pr.image = path;
            pr.width = 0.10f;
            pushUndo();
            garment_.panels[static_cast<std::size_t>(idx)].prints.push_back(pr);
            committed_ = garment_;
            printMissing_.erase(path);
            updateClothPattern();
        }
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Logo, inscription, drapeau... L'image est découpée au contour de la pièce (PNG transparent conseillé).");
}

void App::inspectorPanel()
{
    if (!ImGui::CollapsingHeader("Pièce", ImGuiTreeNodeFlags_DefaultOpen)) return;
    const int idx = editor_.selectedPanel;
    if (idx < 0 || idx >= static_cast<int>(garment_.panels.size())) {
        ImGui::TextDisabled("Aucune pièce sélectionnée.");
        ImGui::TextDisabled("Cliquez une pièce dans le patron 2D.");
        ImGui::Spacing();
        return;
    }
    auto& panel = garment_.panels[static_cast<std::size_t>(idx)];
    ImGui::PushID(idx);
    char name[64];
    std::snprintf(name, sizeof(name), "%s", panel.name.c_str());
    label("Nom");
    if (ImGui::InputText("##nom", name, sizeof(name))) panel.name = name;
    if (ImGui::IsItemDeactivatedAfterEdit()) commitLight();
    label("Couleur");
    if (ImGui::ColorEdit3("##couleur", &panel.color.x, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel)) {
        updateClothColors();
        commitLight();
    }
    label("Aspect");
    {
        int finish = static_cast<int>(panel.finish);
        const char* finishes[garment::kFabricFinishCount];
        for (int k = 0; k < garment::kFabricFinishCount; ++k) finishes[k] = garment::fabricFinishName(static_cast<garment::FabricFinish>(k));
        ImGui::SetNextItemWidth(-1);
        if (ImGui::Combo("##aspect", &finish, finishes, garment::kFabricFinishCount)) {
            pushUndo();
            panel.finish = static_cast<garment::FabricFinish>(finish);
            commitLight();
            updateClothPattern();
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Toile technique quadrillée (ripstop), sergé en diagonale (denim) ou bande réfléchissante.");
    }
    inspectorPrints(idx);

    label("Droit fil");
    const float fw = (ImGui::GetContentRegionAvail().x - 3 * ImGui::GetStyle().ItemSpacing.x) / 4.0f;
    ImGui::SetNextItemWidth(fw * 2.0f + ImGui::GetStyle().ItemSpacing.x);
    ImGui::SliderFloat("##droitfil", &panel.grainDeg, 0.0f, 180.0f, "%.0f°");
    if (ImGui::IsItemDeactivatedAfterEdit()) commitLight();
    for (const auto& [text, deg] : {std::pair{"|", 90.0f}, std::pair{"/", 45.0f}, std::pair{"--", 0.0f}}) {
        ImGui::SameLine();
        if (ImGui::Button(text, ImVec2((fw * 2.0f - 2 * ImGui::GetStyle().ItemSpacing.x) / 3.0f, 0))) {
            panel.grainDeg = deg;
            commitLight();
        }
    }

    {
        bool sym = panel.symmetric;
        if (ImGui::Checkbox("Symétrie (axe vertical au milieu)", &sym)) setPanelSymmetric(idx, sym);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Un point déplacé, ajouté (double-clic) ou supprimé l'est aussi de l'autre côté de l'axe.");
    }
    inspectorTaper();
    inspectorCombine(idx);

    float perimeter = 0.0f;
    for (std::size_t e = 0; e < panel.points.size(); ++e) perimeter += garment_.edgeLength({idx, static_cast<int>(e)});
    ImGui::TextDisabled("%zu points   %.0f cm²   périmètre %.0f cm", panel.points.size(),
                        static_cast<double>(std::abs(garment::signedArea(panel.points)) * 1e4f),
                        static_cast<double>(perimeter * 100.0f));

    ImGui::SeparatorText("Placement sur l'avatar");
    if (const int ai = garment_.attachmentOf(idx); ai >= 0) {
        auto& at = garment_.attachments[static_cast<std::size_t>(ai)];
        ImGui::TextWrapped("Posée sur « %s » (%zu bord(s) cousu(s)) : elle suit cette pièce.",
                           garment_.panels[static_cast<std::size_t>(at.base)].name.c_str(), garment_.attachedEdges(at).size());
        label("Couture");
        int kind = static_cast<int>(garment_.attachKind(at));
        ImGui::SetNextItemWidth(-1);
        if (ImGui::Combo("##posekind", &kind, kAttachKinds, IM_ARRAYSIZE(kAttachKinds))) {
            garment_.setAttachKind(at, static_cast<garment::AttachKind>(kind));
            commitGarment();
        }
        label("Position");
        float pos[2] = {at.at.x * 100.0f, at.at.y * 100.0f};
        if (ImGui::DragFloat2("##poseat", pos, 0.2f, -500.0f, 500.0f, "%.1f cm")) at.at = glm::vec2(pos[0], pos[1]) / 100.0f;
        if (ImGui::IsItemDeactivatedAfterEdit()) commitGarment();
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextWrapped("Astuce : glissez la pièce dans le patron pour la déplacer sur sa base.");
        ImGui::PopStyleColor();
        if (ImGui::Button("Détacher de la pièce", ImVec2(-1, 0))) {
            garment_.attachments.erase(garment_.attachments.begin() + ai);
            commitGarment();
        }
        ImGui::PopID();
        return;
    }
    inspectorSewOn(idx);
    const float bw = (ImGui::GetContentRegionAvail().x - 3 * ImGui::GetStyle().ItemSpacing.x) / 4.0f;
    auto place = [&](const char* text, float angle) {
        const bool on = std::abs(panel.placement.angleDeg - angle) < 0.5f;
        if (on) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.87f, 0.63f, 0.29f, 0.30f));
        if (ImGui::Button(text, ImVec2(bw, 0))) {
            panel.placement.angleDeg = angle;
            commitGarment();
        }
        if (on) ImGui::PopStyleColor();
    };
    place("Devant", 0.0f);
    ImGui::SameLine();
    place("Dos", 180.0f);
    ImGui::SameLine();
    place("Gauche", 90.0f);
    ImGui::SameLine();
    place("Droite", -90.0f);

    bool edited = false;
    label("Zone");
    int zone = static_cast<int>(panel.placement.zone);
    if (ImGui::BeginCombo("##zone", garment::bodyZoneName(panel.placement.zone))) {
        for (int z = 0; z < garment::kBodyZoneCount; ++z)
            if (ImGui::Selectable(garment::bodyZoneName(static_cast<garment::BodyZone>(z)), z == zone)) {
                const bool wasTorso = panel.placement.zone == garment::BodyZone::Torso;
                panel.placement.zone = static_cast<garment::BodyZone>(z);
                // Valeurs de départ raisonnables quand on passe du buste à un membre (et inversement).
                if (wasTorso && z != 0) {
                    const bool arm = z <= 2;
                    panel.placement.radius = arm ? 0.07f : 0.11f;
                    panel.placement.height = arm ? 1.0f : 0.05f;
                } else if (!wasTorso && z == 0) {
                    panel.placement.radius = 0.24f;
                    panel.placement.height = 1.0f;
                }
                edited = true;
            }
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
        ImGui::SetTooltip("Partie du corps autour de laquelle la pièce est enroulée\n(manches sur les bras, jambes de pantalon sur les jambes).");
    label("Forme");
    {
        int flat = panel.placement.flat ? 1 : 0;
        const float hw = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
        if (ImGui::RadioButton("Plate", flat == 1)) flat = 1;
        ImGui::SameLine(0, hw * 0.4f);
        if (ImGui::RadioButton("Enroulée", flat == 0)) flat = 0;
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            ImGui::SetTooltip("Plate : la pièce est posée à plat devant le corps.\nEnroulée : elle est courbée autour du corps (ou du membre).");
        if ((flat == 1) != panel.placement.flat) {
            panel.placement.flat = flat == 1;
            edited = true;
        }
    }
    label("Endroit");
    if (ImGui::Button("Retourner endroit / envers", ImVec2(-1, 0))) {
        ImGui::PopID();
        pieceAction(PieceAction::FlipFace);
        return;
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
        ImGui::SetTooltip("En 3D, la flèche orange montre l'endroit (extérieur) de la pièce ; l'envers est bleuté.");
    label("Angle");
    ImGui::SliderFloat("##angle", &panel.placement.angleDeg, -180.0f, 180.0f, "%.0f°");
    edited |= ImGui::IsItemDeactivatedAfterEdit();
    float hcm = panel.placement.height * 100.0f;
    label("Hauteur");
    if (ImGui::SliderFloat("##hauteur", &hcm, 0.0f, 180.0f, "%.0f cm")) panel.placement.height = hcm / 100.0f;
    edited |= ImGui::IsItemDeactivatedAfterEdit();
    float rcm = panel.placement.radius * 100.0f;
    label("Distance");
    if (ImGui::SliderFloat("##rayon", &rcm, 3.0f, 60.0f, "%.0f cm")) panel.placement.radius = rcm / 100.0f;
    edited |= ImGui::IsItemDeactivatedAfterEdit();
    if (edited) commitGarment();

    if (std::abs(panel.placement.rotation.w) < 0.99999f || glm::length(panel.placement.offset) > 1e-4f) {
        ImGui::TextDisabled("Réglée en 3D : décalage %.0f / %.0f / %.0f cm", static_cast<double>(panel.placement.offset.x * 100.0f),
                            static_cast<double>(panel.placement.offset.y * 100.0f), static_cast<double>(panel.placement.offset.z * 100.0f));
        if (ImGui::Button("Annuler le réglage 3D (décalage et rotation)", ImVec2(-1, 0))) {
            panel.placement.offset = glm::vec3(0.0f);
            panel.placement.rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
            commitGarment();
        }
    }
    ImGui::Spacing();
    const float hw = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    if (ImGui::Button("Copie miroir", ImVec2(hw, 0))) {
        editor_.selectedPanel = garment_.addMirrorCopy(idx, 0.05f);
        editor_.selectedPoint = -1;
        commitGarment();
    }
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.55f, 0.22f, 0.20f, 1.0f));
    if (ImGui::Button("Supprimer", ImVec2(hw, 0))) {
        garment_.removePanel(idx);
        editor_.clearSelection();
        commitGarment();
    }
    ImGui::PopStyleColor();
    ImGui::PopID();
    ImGui::Spacing();
}

void App::inspectorBackdrop()
{
    if (backdropImage_.width <= 0) return;
    if (!ImGui::CollapsingHeader("Image de fond", ImGuiTreeNodeFlags_DefaultOpen)) return;
    auto& b = editor_.backdrop;
    ImGui::TextDisabled("%d x %d pixels", backdropImage_.width, backdropImage_.height);
    ImGui::Checkbox("Visible", &b.visible);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1);
    ImGui::SliderFloat("##opacite", &b.opacity, 0.05f, 1.0f, "opacité %.2f");
    label("Largeur réelle");
    ImGui::InputFloat("##largeur", &backdropWidthCm_, 1.0f, 10.0f, "%.1f cm");
    if (ImGui::IsItemDeactivatedAfterEdit() || ImGui::IsItemClicked()) {
        backdropWidthCm_ = std::clamp(backdropWidthCm_, 1.0f, 2000.0f);
        updateBackdropRect();
    }
    glm::vec2 cm = backdropOrigin_ * 100.0f;
    label("Position");
    if (ImGui::DragFloat2("##position", &cm.x, 0.5f, -1000.0f, 1000.0f, "%.1f cm")) {
        backdropOrigin_ = cm / 100.0f;
        updateBackdropRect();
    }
    ImGui::Checkbox("Seuil auto", &autoThreshold_);
    if (!autoThreshold_) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1);
        ImGui::SliderInt("##seuil", &threshold_, 1, 254, "seuil %d");
    }
    const float hw = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    if (ImGui::Button("Détecter les pièces", ImVec2(hw, 0))) detectBackdropPieces();
    ImGui::SameLine();
    if (ImGui::Button("Retirer l'image", ImVec2(hw, 0))) removeBackdrop();
    ImGui::Spacing();
}

void App::setPanelSymmetric(int idx, bool on)
{
    if (idx < 0 || idx >= static_cast<int>(garment_.panels.size())) return;
    auto& panel = garment_.panels[static_cast<std::size_t>(idx)];
    panel.symmetric = on;
    if (on) {
        float lo = 1e9f, hi = -1e9f;
        for (const auto& q : panel.points) {
            lo = std::min(lo, q.x);
            hi = std::max(hi, q.x);
        }
        panel.symAxisX = (lo + hi) * 0.5f;
    }
    commitLight();
}

void App::inspectorCombine(int idx)
{
    // Ajouter ou découper la forme sélectionnée dans une autre pièce.
    const int n = static_cast<int>(garment_.panels.size());
    if (n < 2 || garment_.attachmentOf(idx) >= 0) return;
    const auto& self = garment_.panels[static_cast<std::size_t>(idx)].points;
    auto overlaps = [&](int p) {
        if (p < 0 || p >= n || p == idx || garment_.attachmentOf(p) >= 0) return false;
        const auto& other = garment_.panels[static_cast<std::size_t>(p)].points;
        for (const auto& q : self)
            if (garment::pointInPolygon(other, q)) return true;
        return false;
    };
    if (combineFor_ != idx || !overlaps(combineTarget_)) {   // pièce proposée : celle que la forme recouvre
        combineFor_ = idx;
        combineTarget_ = -1;
        for (int p = 0; p < n && combineTarget_ < 0; ++p)
            if (overlaps(p)) combineTarget_ = p;
    }
    if (combineTarget_ < 0 || combineTarget_ >= n || combineTarget_ == idx) {
        combineTarget_ = -1;
        return;   // la forme ne recouvre aucune pièce : rien à proposer
    }
    ImGui::SeparatorText("Ajouter ou découper cette forme");
    label("Pièce");
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##combine", garment_.panels[static_cast<std::size_t>(combineTarget_)].name.c_str())) {
        for (int p = 0; p < n; ++p) {
            if (p == idx || garment_.attachmentOf(p) >= 0) continue;
            ImGui::PushID(p);
            if (ImGui::Selectable(garment_.panels[static_cast<std::size_t>(p)].name.c_str(), p == combineTarget_)) combineTarget_ = p;
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    const float hw = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    int action = -1;
    if (ImGui::Button("Ajouter à la pièce", ImVec2(hw, 0))) action = 0;
    ImGui::SameLine();
    if (ImGui::Button("Découper dans la pièce", ImVec2(hw, 0))) action = 1;
    if (action >= 0) {
        const int target = combineTarget_;
        const std::string name = garment_.panels[static_cast<std::size_t>(target)].name;
        const glm::vec2 shapeCentre = garment::centroid(self);
        const auto targetBefore = garment_.panels[static_cast<std::size_t>(target)].points;
        std::string error;
        if (garment::combinePanels(garment_, target, idx, action == 1, error)) {
            editor_.clearSelection();
            editor_.selectedPanel = idx < target ? target - 1 : target;
            // Pièce symétrique : l'ajout ou la découpe est reproduit de l'autre côté.
            auto& tp = garment_.panels[static_cast<std::size_t>(editor_.selectedPanel)];
            if (tp.symmetric) garment::symmetrizePanel(garment_, editor_.selectedPanel, shapeCentre.x <= tp.symAxisX);
            garment::propagateFrom(garment_, editor_.selectedPanel, targetBefore);   // clones liés
            combineFor_ = -1;
            commitGarment();
            setStatus(std::string(action == 1 ? "Forme découpée dans « " : "Forme ajoutée à « ") + name + " ». Ctrl+Z pour annuler.", false);
        } else {
            setStatus(error, true);
        }
    }
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("Tracez une forme (polygone, rectangle) qui chevauche le bord d'une pièce, puis ajoutez-la ou découpez-la.");
    ImGui::PopStyleColor();
}

void App::inspectorTaper()
{
    // Cintrer : resserrer ou élargir les pièces sélectionnées.
    std::vector<int> targets = editor_.selection;
    if (targets.empty() && editor_.selectedPanel >= 0) targets.push_back(editor_.selectedPanel);
    if (targets.empty()) return;
    ImGui::SeparatorText(targets.size() > 1 ? "Cintrer les pièces sélectionnées" : "Cintrer la pièce");
    const float hw = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    for (int k = 0; k < 2; ++k) {
        if (k == 1) ImGui::SameLine();
        const bool on = taperWiden_ == (k == 1);
        if (on) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.87f, 0.63f, 0.29f, 0.30f));
        if (ImGui::Button(k == 0 ? "Resserrer" : "Élargir", ImVec2(hw, 0))) taperWiden_ = k == 1;
        if (on) ImGui::PopStyleColor();
    }
    label("De");
    ImGui::SetNextItemWidth(-1);
    ImGui::SliderFloat("##cintre", &taperPercent_, 1.0f, 40.0f, "%.0f %%");
    static const char* const kModes[] = {"Partout pareil", "Surtout en bas", "Surtout au milieu (taille)", "Surtout en haut"};
    label("Où");
    ImGui::SetNextItemWidth(-1);
    ImGui::Combo("##cintremode", &taperMode_, kModes, IM_ARRAYSIZE(kModes));
    static const char* const kAnchors[] = {"Le centre", "Le bord gauche", "Le bord droit"};
    label("Garder fixe");
    ImGui::SetNextItemWidth(-1);
    ImGui::Combo("##cintreancre", &taperAnchor_, kAnchors, IM_ARRAYSIZE(kAnchors));
    if (ImGui::Button(taperWiden_ ? "Appliquer : élargir" : "Appliquer : cintrer", ImVec2(-1, 0))) {
        const float amount = (taperWiden_ ? 1.0f : -1.0f) * std::clamp(taperPercent_, 1.0f, 40.0f) / 100.0f;
        for (const int p : targets)
            garment::taperPanel(garment_, p, amount, static_cast<garment::TaperMode>(taperMode_),
                                static_cast<garment::TaperAnchor>(taperAnchor_));
        commitGarment();
        char text[160];
        std::snprintf(text, sizeof(text), "%zu pièce(s) %s de %.0f %%. Ctrl+Z pour annuler.", targets.size(),
                      taperWiden_ ? "élargie(s)" : "resserrée(s)", static_cast<double>(taperPercent_));
        setStatus(text, false);
    }
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("Sélectionnez plusieurs pièces (rectangle, Maj+clic) pour les cintrer ensemble. Ctrl+Z annule.");
    ImGui::PopStyleColor();
}

void App::inspectorSewOn(int idx)
{
    // Coudre cette pièce sur une autre : poche plaquée, rabat, renfort, écusson, bande.
    const int n = static_cast<int>(garment_.panels.size());
    bool isBase = false;
    for (const auto& a : garment_.attachments) isBase = isBase || a.base == idx;
    if (isBase || n < 2) return;
    auto usable = [&](int p) { return p != idx && p >= 0 && p < n && garment_.attachmentOf(p) < 0; };
    const auto& self = garment_.panels[static_cast<std::size_t>(idx)].points;
    glm::vec2 lo(1e9f), hi(-1e9f);
    for (const auto& q : self) {
        lo = glm::min(lo, q);
        hi = glm::max(hi, q);
    }
    const glm::vec2 centre = (lo + hi) * 0.5f;
    if (sewFor_ != idx) {   // nouvelle pièce choisie : base proposée = pièce située sous elle dans le patron
        sewFor_ = idx;
        sewBase_ = -1;
        for (int p = 0; p < n && sewBase_ < 0; ++p)
            if (usable(p) && garment::pointInPolygon(garment_.panels[static_cast<std::size_t>(p)].points, centre)) sewBase_ = p;
    }
    if (!usable(sewBase_)) sewBase_ = -1;

    ImGui::SeparatorText("Coudre sur une pièce (poche, renfort...)");
    label("Sur");
    ImGui::SetNextItemWidth(-1);
    const char* preview = sewBase_ >= 0 ? garment_.panels[static_cast<std::size_t>(sewBase_)].name.c_str() : "choisir la pièce...";
    if (ImGui::BeginCombo("##sewbase", preview)) {
        for (int p = 0; p < n; ++p) {
            if (!usable(p)) continue;
            ImGui::PushID(p);
            if (ImGui::Selectable(garment_.panels[static_cast<std::size_t>(p)].name.c_str(), p == sewBase_)) sewBase_ = p;
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    label("Couture");
    ImGui::SetNextItemWidth(-1);
    ImGui::Combo("##sewkind", &sewKind_, kAttachKinds, IM_ARRAYSIZE(kAttachKinds));
    ImGui::BeginDisabled(sewBase_ < 0);
    if (ImGui::Button("Coudre sur la pièce", ImVec2(-1, 0))) {
        const auto& base = garment_.panels[static_cast<std::size_t>(sewBase_)].points;
        glm::vec2 at = centre;
        if (!garment::pointInPolygon(base, at)) {
            // La pièce n'est pas posée sur sa base dans le patron : on la centre dessus.
            at = garment::centroid(base);
            for (auto& q : garment_.panels[static_cast<std::size_t>(idx)].points) q += at - centre;
        }
        const std::string baseName = garment_.panels[static_cast<std::size_t>(sewBase_)].name;
        // Même couleur que la base : on fonce un peu la poche pour la distinguer.
        auto& col = garment_.panels[static_cast<std::size_t>(idx)].color;
        const glm::vec3 baseCol = garment_.panels[static_cast<std::size_t>(sewBase_)].color;
        if (glm::distance(col, baseCol) < 0.1f) col = baseCol * 0.72f;
        if (garment_.attachPatch(idx, sewBase_, at, static_cast<garment::AttachKind>(sewKind_)) >= 0) {
            commitGarment();
            setStatus("« " + garment_.panels[static_cast<std::size_t>(idx)].name + " » cousue sur « " + baseName +
                          " ». Glissez-la dans le patron pour la placer.", false);
        }
    }
    ImGui::EndDisabled();
    if (sewBase_ < 0) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextWrapped("Posez la pièce sur une autre dans le patron, ou choisissez-la ci-dessus.");
        ImGui::PopStyleColor();
    }
}

void App::inspectorSeams()
{
    char header[48];
    std::snprintf(header, sizeof(header), "Coutures (%zu)###coutures", garment_.seams.size() + garment_.attachments.size());
    if (!ImGui::CollapsingHeader(header, ImGuiTreeNodeFlags_DefaultOpen)) return;
    if (garment_.seams.empty() && garment_.attachments.empty()) {
        ImGui::TextDisabled("Aucune couture. Outil Couture (S) :");
        ImGui::TextDisabled("cliquez deux bords dans le patron.");
        ImGui::Spacing();
        return;
    }
    int removeIndex = -1;
    const auto shortName = [&](const garment::EdgeRef& e) {
        if (e.panel < 0 || e.panel >= static_cast<int>(garment_.panels.size())) return std::string("?");
        return garment_.panels[static_cast<std::size_t>(e.panel)].name + " b" + std::to_string(e.edge + 1);
    };
    const auto sideName = [&](const garment::EdgeRef& e, int count) {
        std::string t = shortName(e);
        if (count > 1) t += "-" + std::to_string((e.edge + count - 1) % static_cast<int>(garment_.panels[static_cast<std::size_t>(e.panel)].points.size()) + 1);
        return t;
    };
    for (std::size_t s = 0; s < garment_.seams.size(); ++s) {
        auto& seam = garment_.seams[s];
        ImGui::PushID(static_cast<int>(s));
        // Pastille de couleur numérotée
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float h = ImGui::GetFrameHeight();
        const ImU32 col = ui::PatternEditor::seamColor(static_cast<int>(s));
        ImGui::GetWindowDrawList()->AddCircleFilled({p.x + h * 0.4f, p.y + h * 0.5f}, h * 0.3f, col);
        char num[24];
        std::snprintf(num, sizeof(num), "%zu", s + 1);
        const ImVec2 ns = ImGui::CalcTextSize(num);
        ImGui::GetWindowDrawList()->AddText({p.x + h * 0.4f - ns.x * 0.5f, p.y + (h - ns.y) * 0.5f}, IM_COL32(20, 20, 24, 255), num);
        ImGui::Dummy(ImVec2(h * 0.85f, h));
        ImGui::SameLine();

        const float la = garment_.seamLength(seam, 0), lb = garment_.seamLength(seam, 1);
        char text[160];
        std::snprintf(text, sizeof(text), "%s  +  %s", sideName(seam.a, seam.countA).c_str(), sideName(seam.b, seam.countB).c_str());
        const float buttons = 2 * h + ImGui::GetStyle().ItemSpacing.x;
        ImGui::AlignTextToFramePadding();
        const bool selected = editor_.selectedSeam == static_cast<int>(s);
        if (ImGui::Selectable(text, selected, ImGuiSelectableFlags_AllowOverlap,
                              ImVec2(ImGui::GetContentRegionAvail().x - buttons - 4, h))) {
            editor_.selectedSeam = static_cast<int>(s);
            editor_.selectedPanel = editor_.selectedPoint = -1;
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            ImGui::SetTooltip("Longueurs : %.1f cm et %.1f cm", static_cast<double>(la * 100.0f), static_cast<double>(lb * 100.0f));
        ImGui::SameLine();
        if (ImGui::Button("<>", ImVec2(h, h))) {
            seam.flipped = !seam.flipped;
            commitGarment();
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("Inverser le sens de la couture");
        ImGui::SameLine(0, ImGui::GetStyle().ItemSpacing.x * 0.5f);
        if (ImGui::Button("x", ImVec2(h, h))) removeIndex = static_cast<int>(s);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("Supprimer la couture");
        if (selected) {
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + h);
            bool on = seam.enabled;
            if (ImGui::Checkbox("Couture active (cousue à la simulation)", &on)) toggleSeam(static_cast<int>(s));
            // Une couture peut suivre plusieurs bords (courbe) : + / - ajoute ou retire le bord suivant.
            for (int side = 0; side < 2; ++side) {
                ImGui::PushID(side);
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + h);
                ImGui::AlignTextToFramePadding();
                ImGui::TextDisabled("Côté %c : %d bord(s)", side == 0 ? 'A' : 'B', side == 0 ? seam.countA : seam.countB);
                ImGui::SameLine();
                if (ImGui::SmallButton("-") && garment_.extendSeam(static_cast<int>(s), side, -1)) commitGarment();
                ImGui::SameLine();
                if (ImGui::SmallButton("+") && garment_.extendSeam(static_cast<int>(s), side, +1)) commitGarment();
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                    ImGui::SetTooltip("Ajouter le bord suivant à ce côté\n(ou Maj + clic sur un bord voisin dans le patron)");
                ImGui::PopID();
            }
        }
        if (std::abs(la - lb) > 0.01f) {
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + h);
            ImGui::TextColored(ImVec4(0.92f, 0.70f, 0.35f, 1.0f), "Longueurs différentes : %.1f / %.1f cm (embu)",
                               static_cast<double>(la * 100.0f), static_cast<double>(lb * 100.0f));
        }
        ImGui::PopID();
    }
    if (removeIndex >= 0) {
        garment_.seams.erase(garment_.seams.begin() + removeIndex);
        editor_.selectedSeam = -1;
        commitGarment();
    }
    // Pièces fixées / cousues sur une autre (poches, rabats, renforts) : même liste que les coutures.
    int unsew = -1;
    const int np = static_cast<int>(garment_.panels.size());
    for (std::size_t k = 0; k < garment_.attachments.size(); ++k) {
        const auto& at = garment_.attachments[k];
        if (at.patch < 0 || at.base < 0 || at.patch >= np || at.base >= np) continue;
        ImGui::PushID(100000 + static_cast<int>(k));
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float h = ImGui::GetFrameHeight();
        const ImU32 col = ui::PatternEditor::seamColor(static_cast<int>(garment_.seams.size() + k));
        ImGui::GetWindowDrawList()->AddCircleFilled({p.x + h * 0.4f, p.y + h * 0.5f}, h * 0.36f, col);
        char num[24];
        std::snprintf(num, sizeof(num), "P%zu", k + 1);
        const ImVec2 ns = ImGui::CalcTextSize(num);
        ImGui::GetWindowDrawList()->AddText({p.x + h * 0.4f - ns.x * 0.5f, p.y + (h - ns.y) * 0.5f}, IM_COL32(20, 20, 24, 255), num);
        ImGui::Dummy(ImVec2(h * 0.85f, h));
        ImGui::SameLine();
        const auto edges = garment_.attachedEdges(at);
        const int n = static_cast<int>(garment_.panels[static_cast<std::size_t>(at.patch)].points.size());
        char text[200];
        std::snprintf(text, sizeof(text), "%s  sur  %s  (%s)", garment_.panels[static_cast<std::size_t>(at.patch)].name.c_str(),
                      garment_.panels[static_cast<std::size_t>(at.base)].name.c_str(),
                      static_cast<int>(edges.size()) >= n ? "tout le tour" : (std::to_string(edges.size()) + " bord(s)").c_str());
        ImGui::AlignTextToFramePadding();
        if (ImGui::Selectable(text, editor_.isSelected(at.patch), ImGuiSelectableFlags_AllowOverlap,
                              ImVec2(ImGui::GetContentRegionAvail().x - h - 4, h)) &&
            garment_.panelEditable(at.patch))
            editor_.select({at.patch});
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            ImGui::SetTooltip("Pièce cousue sur une autre.\nOutil Couture (S) : clic sur un de ses bords pour le découdre (ouverture)\nou le recoudre.");
        ImGui::SameLine();
        if (ImGui::Button("x", ImVec2(h, h))) unsew = static_cast<int>(k);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("Découdre (la pièce redevient libre)");
        ImGui::PopID();
    }
    if (unsew >= 0) {
        garment_.attachments.erase(garment_.attachments.begin() + unsew);
        commitGarment();
    }
    ImGui::Spacing();
}

void App::inspectorFabric()
{
    if (!ImGui::CollapsingHeader("Tissu", ImGuiTreeNodeFlags_DefaultOpen)) return;
    const auto presets = simulation::fabricPresets();
    label("Matière");
    if (ImGui::BeginCombo("##matiere", presets[static_cast<std::size_t>(presetIndex_)].name.c_str())) {
        for (std::size_t i = 0; i < presets.size(); ++i) {
            if (ImGui::Selectable(presets[i].name.c_str(), static_cast<int>(i) == presetIndex_)) {
                presetIndex_ = static_cast<int>(i);
                material_ = presets[i];
                appearance_.roughness = material_.roughness;
                appearance_.sheen = material_.sheen;
                solver_.setMaterial(material_);
                updateClothColors();
            }
        }
        ImGui::EndCombo();
    }
    bool changed = false;
    label("Étirement");
    changed |= sliderCompliance("##etirement", material_.stretchCompliance, -9.0f, -3.0f);
    label("Flexion");
    changed |= sliderCompliance("##flexion", material_.bendCompliance, -5.0f, 1.5f);
    float grams = material_.density * 1000.0f;
    label("Grammage");
    if (ImGui::SliderFloat("##grammage", &grams, 30.0f, 1200.0f, "%.0f g/m²")) {
        material_.density = grams / 1000.0f;
        changed = true;
    }
    float mm = material_.thickness * 1000.0f;
    label("Épaisseur");
    if (ImGui::SliderFloat("##epaisseur", &mm, 1.0f, 20.0f, "%.1f mm")) {
        material_.thickness = mm / 1000.0f;
        changed = true;
    }
    label("Frottement");
    changed |= ImGui::SliderFloat("##frottement", &material_.friction, 0.0f, 1.0f, "%.2f");
    label("Amorti air");
    changed |= ImGui::SliderFloat("##air", &material_.damping, 0.0f, 5.0f, "%.2f /s");
    label("Amorti interne");
    changed |= ImGui::SliderFloat("##interne", &material_.internalDamping, 0.0f, 100.0f, "%.0f /s");
    if (changed) solver_.setMaterial(material_);
    label("Teinte");
    if (ImGui::ColorEdit3("##teinte", &material_.color.x, ImGuiColorEditFlags_NoInputs)) updateClothColors();
    ImGui::SameLine();
    if (ImGui::Checkbox("Couleurs des pièces", &panelColors_)) updateClothColors();
    ImGui::Spacing();
}

void App::inspectorAvatar()
{
    if (!ImGui::CollapsingHeader("Avatar", ImGuiTreeNodeFlags_DefaultOpen)) return;
    ImGui::TextUnformatted(avatarSource_.name.c_str());
    ImGui::TextDisabled("%zu triangles   %zu particules en contact", avatarSource_.indices.size() / 3,
                        clothValid_ ? solver_.meshContacts() : std::size_t{0});
    bool changed = false;
    float hcm = avatarHeight_ * 100.0f;
    label("Taille");
    if (ImGui::SliderFloat("##taille", &hcm, 50.0f, 250.0f, "%.0f cm")) avatarHeight_ = hcm / 100.0f;
    changed |= ImGui::IsItemDeactivatedAfterEdit();
    label("Rotation");
    ImGui::SliderFloat("##rotation", &avatarRotation_, -180.0f, 180.0f, "%.0f°");
    changed |= ImGui::IsItemDeactivatedAfterEdit();
    const float w3 = (ImGui::GetContentRegionAvail().x - 2 * ImGui::GetStyle().ItemSpacing.x) / 3.0f;
    if (ImGui::Button("-90°", ImVec2(w3, 0))) { avatarRotation_ -= 90.0f; changed = true; }
    ImGui::SameLine();
    if (ImGui::Button("+90°", ImVec2(w3, 0))) { avatarRotation_ += 90.0f; changed = true; }
    ImGui::SameLine();
    if (ImGui::Button("Visible", ImVec2(w3, 0))) {
        showAvatar_ = !showAvatar_;
        changed = true;
    }
    if (changed) {
        avatarRotation_ = std::fmod(avatarRotation_ + 540.0f, 360.0f) - 180.0f;
        applyAvatarTransform();
    }
    const float hw = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    if (ImGui::Button("Importer FBX / OBJ...", ImVec2(hw, 0))) openAvatarDialog();
    ImGui::SameLine();
    ImGui::BeginDisabled(builtinAvatar_);
    if (ImGui::Button("Mannequin intégré", ImVec2(hw, 0))) {
        loadDefaultMannequin();
        if (options_.frames <= 0) io::rememberAvatar({});
    }
    ImGui::EndDisabled();
    ImGui::TextDisabled("Astuce : glissez un .fbx ou .obj sur la fenêtre.");
    ImGui::Spacing();
}

void App::inspectorSimulation()
{
    if (!ImGui::CollapsingHeader("Simulation")) return;
    auto& set = solver_.settings();
    float cm = spacing_ * 100.0f;
    label("Particules");
    if (ImGui::SliderFloat("##espacement", &cm, 0.8f, 4.0f, "tous les %.1f cm")) spacing_ = cm / 100.0f;
    if (ImGui::IsItemDeactivatedAfterEdit()) resetCloth();
    label("Couture");
    if (ImGui::SliderFloat("##couture", &sewingSeconds_, 0.2f, 4.0f, "%.1f s")) {}
    if (ImGui::IsItemDeactivatedAfterEdit()) resetCloth();
    label("Sous-pas");
    ImGui::SliderInt("##souspas", &set.substeps, 1, 40, "%d / image");
    label("Contacts tissu");
    ImGui::SliderInt("##contacts", &set.selfCollisionInterval, 1, 5, "tous les %d sous-pas");
    ImGui::Checkbox("Auto-collisions", &set.selfCollision);
    ImGui::SameLine();
    ImGui::Checkbox("Sol", &set.groundEnabled);
    const auto& st = solver_.stats();
    ImGui::TextDisabled("%zu étirement   %zu flexion   %zu contacts", st.stretchConstraints, st.bendConstraints,
                        st.selfCollisionPairs);
    ImGui::Spacing();
    inspectorPanelSimulation();
}

void App::inspectorPanelSimulation()
{
    // Propriétés de simulation des pièces sélectionnées (toutes reçoivent la valeur modifiée)
    std::vector<int> t;
    for (const int p : editor_.targets())
        if (p >= 0 && p < static_cast<int>(garment_.panels.size())) t.push_back(p);
    ImGui::SeparatorText(t.size() > 1 ? "Propriétés de simulation des pièces" : "Propriétés de simulation de la pièce");
    if (t.empty()) {
        ImGui::TextDisabled("Sélectionnez une pièce dans le patron.");
        ImGui::Spacing();
        return;
    }
    auto& pn = garment_.panels[static_cast<std::size_t>(t.front())];
    bool remesh = false, state = false;
    auto applyAll = [&](auto&& set) {
        for (const int p : t) set(garment_.panels[static_cast<std::size_t>(p)]);
    };
    auto after = [&](bool needsRemesh) {
        if (ImGui::IsItemDeactivatedAfterEdit()) (needsRemesh ? remesh : state) = true;
    };

    label("Espacement");
    float mm = pn.particleDistance > 0.0f ? pn.particleDistance * 1000.0f : spacing_ * 1000.0f;
    if (ImGui::DragFloat("##pdist", &mm, 0.1f, 5.0f, 100.0f, "%.1f mm")) applyAll([&](garment::Panel& p) { p.particleDistance = mm / 1000.0f; });
    after(true);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Espacement des particules de cette pièce (plus petit = plus fin et plus lent).\nClic droit : revenir à celui du vêtement.");
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
        applyAll([](garment::Panel& p) { p.particleDistance = 0.0f; });
        remesh = true;
    }

    label("Couche");
    int layer = pn.layer;
    if (ImGui::InputInt("##couche", &layer)) {
        layer = std::clamp(layer, -10, 10);
        applyAll([&](garment::Panel& p) { p.layer = layer; });
        remesh = true;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("0 = normal. Une couche plus haute est posée par-dessus et passe au-dessus dans les contacts.");

    label("Retrait trame");
    float weft = pn.shrinkWeft * 100.0f;
    if (ImGui::DragFloat("##trame", &weft, 0.1f, 50.0f, 150.0f, "%.2f %%")) applyAll([&](garment::Panel& p) { p.shrinkWeft = weft / 100.0f; });
    after(true);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Rétrécissement en trame (en travers du droit fil). 100 %% = taille du patron, moins = la pièce rétrécit.");
    label("Retrait chaîne");
    float warp = pn.shrinkWarp * 100.0f;
    if (ImGui::DragFloat("##chaine", &warp, 0.1f, 50.0f, 150.0f, "%.2f %%")) applyAll([&](garment::Panel& p) { p.shrinkWarp = warp / 100.0f; });
    after(true);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Rétrécissement en chaîne (le long du droit fil). 100 %% = taille du patron.");

    label("Ép. contact");
    float tc = pn.thickCollision * 1000.0f;
    if (ImGui::DragFloat("##epcontact", &tc, 0.1f, 0.0f, 50.0f, "%.1f mm")) applyAll([&](garment::Panel& p) { p.thickCollision = tc / 1000.0f; });
    after(false);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Épaisseur ajoutée pour les collisions (corps, sol et autres tissus).");
    label("Ép. affichage");
    float tr = pn.thickRender * 1000.0f;
    if (ImGui::DragFloat("##epaff", &tr, 0.1f, 0.0f, 50.0f, "%.1f mm")) applyAll([&](garment::Panel& p) { p.thickRender = tr / 1000.0f; });
    after(false);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Épaisseur ajoutée seulement à l'affichage : le tissu est dessiné décalé vers son endroit.");

    label("Pression");
    float pr = pn.pressure;
    if (ImGui::DragFloat("##pression", &pr, 0.5f, -200.0f, 200.0f, "%.1f Pa")) applyAll([&](garment::Panel& p) { p.pressure = pr; });
    after(false);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Pousse la pièce vers son endroit, comme de l'air dans un volume fermé (sac, coussin, doudoune).\nNégatif : vers l'envers.");

    if (remesh) commitGarment();
    else if (state) commitState();
    ImGui::Spacing();
}

void App::inspectorDisplay()
{
    if (!ImGui::CollapsingHeader("Affichage")) return;
    ImGui::Checkbox("Fil de fer", &appearance_.wireframe);
    ImGui::SameLine();
    ImGui::Checkbox("Envers du tissu en bleu", &appearance_.showBackSide);
    ImGui::Checkbox("Carte de tension", &appearance_.showStrain);
    if (appearance_.showStrain) {
        float pct = appearance_.strainScale * 100.0f;
        label("Rouge à");
        if (ImGui::SliderFloat("##rouge", &pct, 1.0f, 30.0f, "%.0f %% d'étirement")) appearance_.strainScale = pct / 100.0f;
    }
    label("Rugosité");
    ImGui::SliderFloat("##rugosite", &appearance_.roughness, 0.2f, 1.0f, "%.2f");
    label("Reflet fibre");
    ImGui::SliderFloat("##sheen", &appearance_.sheen, 0.0f, 1.0f, "%.2f");
    ImGui::Spacing();
}

void App::drawStatusBar()
{
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos({0, display.y - statusHeight_});
    ImGui::SetNextWindowSize({display.x, statusHeight_});
    ImGui::PushStyleColor(ImGuiCol_WindowBg, kBg0);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12 * uiScale_, 2 * uiScale_));
    ImGui::Begin("##etat", nullptr, kPaneFlags | ImGuiWindowFlags_NoScrollbar);
    ImGui::PopStyleVar();
    const ImVec2 bar = ImGui::GetWindowPos();
    ImGui::GetWindowDrawList()->AddLine(bar, {bar.x + display.x, bar.y}, IM_COL32(220, 174, 96, 40));
    ImGui::SetCursorPosY((statusHeight_ - ImGui::GetTextLineHeight()) * 0.5f);

    char right[200];
    const auto& st = solver_.stats();
    std::snprintf(right, sizeof(right), "%zu particules   ·   %zu triangles   ·   %.1f ms   ·   t = %.1f s   ·   %.0f img/s",
                  clothValid_ ? st.particles : std::size_t{0}, clothValid_ ? solver_.indices().size() / 3 : std::size_t{0},
                  static_cast<double>(st.stepMilliseconds), static_cast<double>(simTime_),
                  static_cast<double>(ImGui::GetIO().Framerate));
    const float rightW = ImGui::CalcTextSize(right).x;
    // Message récent (8 s, fondu) sinon l'aide de l'outil courant. Le texte long est coupé, pas superposé aux chiffres.
    const float age = static_cast<float>(ImGui::GetTime() - statusTime_);
    const bool recent = !status_.empty() && age < 8.0f;
    const float leftOrigin = (recent ? 28.0f : 12.0f) * uiScale_;
    const float maxLeft = std::max(40.0f, display.x - rightW - 28.0f * uiScale_ - leftOrigin);
    float alpha = 1.0f;
    if (recent) {
        if (age < 0.16f) alpha = age / 0.16f;
        else if (age > 6.8f) alpha = std::clamp((8.0f - age) / 1.2f, 0.0f, 1.0f);
    }
    std::string left = recent ? status_ : editor_.hint();
    auto popChar = [](std::string& s) {
        while (!s.empty() && (static_cast<unsigned char>(s.back()) & 0xC0) == 0x80) s.pop_back();
        if (!s.empty()) s.pop_back();
    };
    bool clipped = false;
    while (ImGui::CalcTextSize(left.c_str()).x > maxLeft && left.size() > 1) {
        popChar(left);
        clipped = true;
    }
    if (clipped) {
        popChar(left);
        left += "…";
    }
    if (recent) {
        ImVec4 col = statusIsError_ ? kBad : kOk;
        col.w = alpha;
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float cy = p.y + ImGui::GetTextLineHeight() * 0.5f;
        ImGui::GetWindowDrawList()->AddCircleFilled({p.x + 3.5f, cy}, 3.2f, ImGui::GetColorU32(col));
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 14.0f * uiScale_);
        ImGui::TextColored(col, "%s", left.c_str());
    } else {
        ImGui::TextDisabled("%s", left.c_str());
    }
    ImGui::SetCursorPosY((statusHeight_ - ImGui::GetTextLineHeight()) * 0.5f);
    ImGui::SetCursorPosX(display.x - rightW - 14.0f * uiScale_);
    ImGui::TextDisabled("%s", right);
    ImGui::End();
    ImGui::PopStyleColor();
}

void App::drawAboutWindow()
{
    const ImVec2 center(ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y * 0.5f);
    if (showAbout_) ImGui::OpenPopup("À propos de Drapeo");
    if (showShortcuts_) ImGui::OpenPopup("Raccourcis clavier");
    if (showPathPopup_) ImGui::OpenPopup("Ouvrir un fichier");
    if (showImagePopup_) ImGui::OpenPopup("Importer une image de patron");
    showImagePopup_ = false;
    showAbout_ = showShortcuts_ = showPathPopup_ = false;

    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("À propos de Drapeo", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        const float side = 300.0f * uiScale_;
        ImGui::Image(static_cast<ImTextureID>(logoFull_), ImVec2(side, side));
        ImGui::TextUnformatted("Atelier de création de vêtements 3D");
        ImGui::Separator();
        ImGui::TextDisabled("Patrons 2D, coutures, drapé physique XPBD sur avatar.");
        ImGui::TextDisabled("C++20, OpenGL 3.3, Dear ImGui, GLFW, glm, ufbx, CDT.");
        ImGui::TextDisabled("Interface : Inter (SIL Open Font License).");
        ImGui::Spacing();
        if (ImGui::Button("Fermer", ImVec2(120 * uiScale_, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Raccourcis clavier", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        const char* rows[][2] = {
            {"Z / T / V / C", "Éditer le patron, Transformer, Points de courbe, Courbure"},
            {"L / A / E / I", "Lisser, Ajouter un point, Fendre et écarter, Tracer depuis l'image"},
            {"P / B / S / G", "Outils Polygone, Rectangle, Couture bord à bord, Couture libre"},
            {"Double-clic sur un bord", "Ajouter un point"},
            {"Suppr", "Supprimer la sélection"},
            {"Maj (en traçant)", "Aimanter à la grille de 1 cm"},
            {"Entrée / Échap", "Fermer / annuler le polygone"},
            {"F", "Cadrer la vue survolée"},
            {"Espace", "Simuler / pause"},
            {"R", "Recoudre depuis le patron"},
            {"Ctrl+Z / Ctrl+Y", "Annuler / rétablir"},
            {"Ctrl+O / Ctrl+I", "Importer un avatar / un patron"},
            {"Ctrl+E", "Exporter le vêtement en OBJ"},
            {"Clic droit (3D)", "Saisir et tirer le tissu"},
            {"Clic droit (patron)", "Menu de la pièce"},
            {"Ctrl+C / V / R", "Copier / coller / coller en miroir"},
            {"Ctrl+D", "Clone symétrique cousu (lié)"},
            {"Ctrl+Maj+V / B", "Clone par-dessus / en dessous"},
            {"Ctrl+Maj+I", "Inverser la sélection"},
            {"Ctrl+J / K / H", "Désactiver / figer / renforcer en 3D"},
            {"Maj+Q", "Masquer en 3D"},
        };
        if (ImGui::BeginTable("##raccourcis", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV)) {
            for (const auto& r : rows) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextColored(kAccent, "%s", r[0]);
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(r[1]);
            }
            ImGui::EndTable();
        }
        if (ImGui::Button("Fermer", ImVec2(120 * uiScale_, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Ouvrir un fichier", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        const char* what[] = {"Avatar (.fbx, .obj)", "Projet .drapeo ou patron (.svg, .dxf, .pdf, .png, .jpg)", "Export OBJ",
                              "Enregistrer le projet (.drapeo)"};
        ImGui::TextUnformatted(what[pathPopupKind_]);
        ImGui::SetNextItemWidth(420 * uiScale_);
        const bool enter = ImGui::InputText("##chemin", pathInput_, sizeof(pathInput_), ImGuiInputTextFlags_EnterReturnsTrue);
        if (primaryButton("Valider", ImVec2(120 * uiScale_, 0)) || enter) {
            const std::string path = pathInput_;
            ImGui::CloseCurrentPopup();
            if (pathPopupKind_ == 0) loadAvatar(path);
            else if (pathPopupKind_ == 1) importPattern(path);
            else if (pathPopupKind_ == 3) saveProjectFile();
            else exportGarment();
            pathInput_[0] = '\0';
        }
        ImGui::SameLine();
        if (ImGui::Button("Annuler", ImVec2(120 * uiScale_, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void App::drawImageImportPopup()
{
    const ImVec2 center(ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y * 0.5f);
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal("Importer une image de patron", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    const float maxW = 420.0f * uiScale_, maxH = 300.0f * uiScale_;
    if (pendingImage_.width > 0) {
        const float k = std::min(maxW / static_cast<float>(pendingImage_.width), maxH / static_cast<float>(pendingImage_.height));
        ImGui::Image(static_cast<ImTextureID>(pendingTex_),
                     ImVec2(static_cast<float>(pendingImage_.width) * k, static_cast<float>(pendingImage_.height) * k));
        ImGui::TextUnformatted(pendingName_.c_str());
        ImGui::TextDisabled("%d x %d pixels", pendingImage_.width, pendingImage_.height);
        if (pendingImage_.dpi > 1.0f) ImGui::TextDisabled("Résolution lue dans le fichier : %.0f ppp", static_cast<double>(pendingImage_.dpi));
        else ImGui::TextDisabled("Résolution inconnue : mesurez une pièce sur le papier pour régler la largeur.");
        ImGui::Spacing();
        label("Largeur réelle");
        ImGui::SetNextItemWidth(200 * uiScale_);
        ImGui::InputFloat("##largeur_import", &importWidthCm_, 1.0f, 10.0f, "%.1f cm");
        importWidthCm_ = std::clamp(importWidthCm_, 1.0f, 2000.0f);
        ImGui::TextDisabled("Hauteur correspondante : %.1f cm",
                            static_cast<double>(importWidthCm_ * static_cast<float>(pendingImage_.height) /
                                                static_cast<float>(pendingImage_.width)));
        ImGui::Checkbox("Détecter les pièces automatiquement", &importDetect_);
        ImGui::TextDisabled("Traits ou pièces sombres sur papier clair. Sinon, tracez\nles pièces sur l'image avec l'outil Polygone (P).");
    }
    ImGui::Spacing();
    if (primaryButton("Importer", ImVec2(140 * uiScale_, 0))) {
        ImGui::CloseCurrentPopup();
        finishImageImport();
    }
    ImGui::SameLine();
    if (ImGui::Button("Annuler", ImVec2(140 * uiScale_, 0))) {
        ImGui::CloseCurrentPopup();
        deleteTexture(pendingTex_);
        pendingImage_ = {};
    }
    ImGui::EndPopup();
}

void App::saveScreenshot(const std::string& path) const
{
    int w = 0, h = 0;
    glfwGetFramebufferSize(window_, &w, &h);
    std::vector<unsigned char> pixels(static_cast<std::size_t>(w * h * 3));
    gl::PixelStorei(gl::PACK_ALIGNMENT, 1);
    gl::ReadPixels(0, 0, w, h, gl::RGB, gl::UNSIGNED_BYTE, pixels.data());
    std::ofstream out(path, std::ios::binary);
    out << "P6\n" << w << ' ' << h << "\n255\n";
    for (int row = h - 1; row >= 0; --row)   // OpenGL lit de bas en haut
        out.write(reinterpret_cast<const char*>(&pixels[static_cast<std::size_t>(row * w * 3)]), w * 3);
    std::printf("Capture enregistrée : %s (%dx%d)\n", path.c_str(), w, h);
}

} // namespace drapeo::app
