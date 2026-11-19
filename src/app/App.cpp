#include "app/App.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <vector>

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include "app/FileDialog.h"
#include "app/LogoData.h"
#include "geometry/ClothMesh.h"
#include "geometry/Mannequin.h"
#include "io/GarmentExport.h"
#include "io/PatternImport.h"
#include "io/PdfImport.h"
#include "io/RasterImage.h"
#include "rendering/GlFunctions.h"
#include "simulation/Materials.h"

namespace drapeo::app {

namespace {
constexpr float kFrameDt = 1.0f / 60.0f;   // pas fixe : simulation déterministe
constexpr float kPi = 3.14159265f;

// --- Palette « graphite + fil d'or » propre à Drapeo --------------------------
const ImVec4 kBg0(0.090f, 0.094f, 0.102f, 1.0f);    // fonds les plus sombres (barres)
const ImVec4 kBg1(0.122f, 0.127f, 0.137f, 1.0f);    // panneaux
const ImVec4 kBg2(0.165f, 0.171f, 0.184f, 1.0f);    // champs
const ImVec4 kBg3(0.215f, 0.222f, 0.238f, 1.0f);    // survol
const ImVec4 kLine(0.215f, 0.222f, 0.238f, 1.0f);
const ImVec4 kAccent(0.87f, 0.63f, 0.29f, 1.0f);
const ImVec4 kAccentDim(0.55f, 0.40f, 0.19f, 1.0f);
const ImVec4 kText(0.88f, 0.87f, 0.84f, 1.0f);
const ImVec4 kTextDim(0.56f, 0.57f, 0.60f, 1.0f);
const ImU32 kAccentU32 = IM_COL32(222, 160, 74, 255);

void applyStyle(float scale)
{
    ImGuiStyle& s = ImGui::GetStyle();
    ImGui::StyleColorsDark(&s);
    s.WindowRounding = 0.0f;
    s.ChildRounding = 4.0f;
    s.FrameRounding = 3.0f;
    s.PopupRounding = 4.0f;
    s.GrabRounding = 3.0f;
    s.TabRounding = 3.0f;
    s.ScrollbarRounding = 6.0f;
    s.WindowBorderSize = 0.0f;
    s.PopupBorderSize = 1.0f;
    s.FrameBorderSize = 0.0f;
    s.WindowPadding = ImVec2(12, 10);
    s.FramePadding = ImVec2(8, 4);
    s.ItemSpacing = ImVec2(8, 6);
    s.ItemInnerSpacing = ImVec2(6, 4);
    s.IndentSpacing = 14.0f;
    s.ScrollbarSize = 11.0f;
    s.GrabMinSize = 9.0f;
    s.SeparatorTextBorderSize = 1.0f;
    s.SeparatorTextPadding = ImVec2(0, 4);
    s.WindowMinSize = ImVec2(2, 2);   // la poignée de redimensionnement est très fine
    s.ScaleAllSizes(scale);

    ImVec4* c = s.Colors;
    c[ImGuiCol_Text] = kText;
    c[ImGuiCol_TextDisabled] = kTextDim;
    c[ImGuiCol_WindowBg] = kBg1;
    c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_PopupBg] = ImVec4(0.110f, 0.114f, 0.124f, 0.98f);
    c[ImGuiCol_Border] = kLine;
    c[ImGuiCol_FrameBg] = kBg2;
    c[ImGuiCol_FrameBgHovered] = kBg3;
    c[ImGuiCol_FrameBgActive] = ImVec4(0.25f, 0.26f, 0.28f, 1.0f);
    c[ImGuiCol_TitleBg] = kBg0;
    c[ImGuiCol_TitleBgActive] = kBg0;
    c[ImGuiCol_MenuBarBg] = kBg0;
    c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab] = kBg3;
    c[ImGuiCol_CheckMark] = kAccent;
    c[ImGuiCol_SliderGrab] = kAccentDim;
    c[ImGuiCol_SliderGrabActive] = kAccent;
    c[ImGuiCol_Button] = kBg2;
    c[ImGuiCol_ButtonHovered] = kBg3;
    c[ImGuiCol_ButtonActive] = kAccentDim;
    c[ImGuiCol_Header] = ImVec4(0.18f, 0.185f, 0.20f, 1.0f);
    c[ImGuiCol_HeaderHovered] = kBg3;
    c[ImGuiCol_HeaderActive] = ImVec4(0.26f, 0.27f, 0.29f, 1.0f);
    c[ImGuiCol_Separator] = kLine;
    c[ImGuiCol_SeparatorHovered] = kAccentDim;
    c[ImGuiCol_SeparatorActive] = kAccent;
    c[ImGuiCol_ResizeGrip] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_Tab] = kBg1;
    c[ImGuiCol_TabHovered] = kBg3;
    c[ImGuiCol_TabSelected] = kBg2;
    c[ImGuiCol_PlotHistogram] = kAccentDim;
    c[ImGuiCol_TextSelectedBg] = ImVec4(0.87f, 0.63f, 0.29f, 0.35f);
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
enum class Icon { Select, Polygon, Rectangle, Seam, Play, Pause, Step, Reset, Frame };

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
    case Icon::Seam: {
        dl->AddLine(P(-0.7f, -0.8f), P(-0.45f, 0.8f), col, t);
        dl->AddLine(P(0.7f, -0.8f), P(0.45f, 0.8f), col, t);
        for (int i = 0; i < 4; ++i) {
            const float y = -0.6f + i * 0.4f;
            dl->AddLine(P(-0.42f, y), P(0.42f, y), col, t * 0.9f);
        }
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

bool iconButton(const char* id, Icon icon, const char* tooltip, bool active, float size)
{
    ImGui::PushID(id);
    if (active) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.87f, 0.63f, 0.29f, 0.22f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.87f, 0.63f, 0.29f, 0.32f));
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kBg3);
    }
    const bool pressed = ImGui::Button("##icon", ImVec2(size, size));
    ImGui::PopStyleColor(2);
    const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (active) dl->AddLine({a.x + 4, b.y - 1}, {b.x - 4, b.y - 1}, kAccentU32, 2.0f);
    const ImU32 col = active ? kAccentU32 : ImGui::GetColorU32(ImGuiCol_Text);
    drawIcon(dl, icon, {(a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f}, size * 0.30f, col);
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
    ImGui::PushStyleColor(ImGuiCol_Text, kTextDim);
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
    if (bold) ImGui::PopFont();
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
    if (!options_.avatarPath.empty()) loadAvatar(options_.avatarPath);
    setGarment(garment::makeTankTop());
    setStatus("Bienvenue dans Drapeo. Modifiez le patron à gauche : le vêtement est recousu en 3D.", false);
    editor_.selectedPanel = 0;
    showAbout_ = options_.showAbout;
    if (!options_.patternPath.empty()) {
        setGarment({});
        importPattern(options_.patternPath);
        if (options_.acceptImport && pendingImage_.width > 0) finishImageImport();
    }

    while (!glfwWindowShouldClose(window_)) {
        glfwPollEvents();
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
            renderer_.updateCloth(solver_.positions(), normals_, strain_);
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
    // Police du système : Segoe UI sous Windows, DejaVu Sans sous Linux.
    // Plage de glyphes par défaut : latin de base + latin-1 (accents français).
    ImGuiIO& io = ImGui::GetIO();
    const char* regular[] = {"C:\\Windows\\Fonts\\segoeui.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
                             "/System/Library/Fonts/Supplemental/Arial.ttf"};
    const char* bold[] = {"C:\\Windows\\Fonts\\segoeuib.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
                          "/System/Library/Fonts/Supplemental/Arial Bold.ttf"};
    const bool segoe = fileExists(regular[0]);
    const float size = (segoe ? 17.0f : 15.0f) * scale;
    ImFontConfig cfg;
    cfg.OversampleH = 2;
    ImFont* base = nullptr;
    for (const char* path : regular)
        if (fileExists(path) && (base = io.Fonts->AddFontFromFileTTF(path, size, &cfg)) != nullptr) break;
    if (base == nullptr) {
        ImFontConfig def;
        def.SizePixels = 13.0f * scale;
        io.Fonts->AddFontDefault(&def);
    }
    for (const char* path : bold)
        if (fileExists(path) && (fontBold_ = io.Fonts->AddFontFromFileTTF(path, size * 0.86f, &cfg)) != nullptr) break;
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
void App::resetCloth()
{
    clothValid_ = false;
    simTime_ = 0.0f;
    solver_.release();
    if (garment_.empty()) {
        garmentMesh_ = {};
        renderer_.setClothTopology({}, 0);
        return;
    }
    garment::MeshingOptions mo;
    mo.spacing = spacing_;
    mo.bodyScale = avatarHeight_ / 1.75f;
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
    solver_.spheres().clear();
    solver_.setMeshCollider(&avatarBvh_);
    solver_.setSeams(garmentMesh_.seamPairs, sewingSeconds_);
    renderer_.setClothTopology(garmentMesh_.mesh.indices, garmentMesh_.mesh.vertexCount());
    clothValid_ = true;
    updateClothColors();
}

void App::setGarment(garment::Garment g)
{
    if (!garment_.empty()) {
        undoStack_.push_back(committed_);
        redoStack_.clear();
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
    undoStack_.push_back(committed_);
    if (undoStack_.size() > 100) undoStack_.erase(undoStack_.begin());
    redoStack_.clear();
    committed_ = garment_;
    resetCloth();
    playing_ = true;
}

void App::undo()
{
    if (undoStack_.empty()) return;
    redoStack_.push_back(garment_);
    garment_ = undoStack_.back();
    undoStack_.pop_back();
    committed_ = garment_;
    editor_.clearSelection();
    resetCloth();
}

void App::redo()
{
    if (redoStack_.empty()) return;
    undoStack_.push_back(garment_);
    garment_ = redoStack_.back();
    redoStack_.pop_back();
    committed_ = garment_;
    editor_.clearSelection();
    resetCloth();
}

void App::updateClothColors()
{
    appearance_.color = material_.color;
    if (!clothValid_ || !panelColors_) {
        renderer_.setClothColors({});
        return;
    }
    std::vector<glm::vec3> colors(garmentMesh_.panelOfVertex.size());
    for (std::size_t i = 0; i < colors.size(); ++i) {
        const int p = garmentMesh_.panelOfVertex[i];
        colors[i] = (p >= 0 && p < static_cast<int>(garment_.panels.size())) ? garment_.panels[static_cast<std::size_t>(p)].color
                                                                           : material_.color;
    }
    renderer_.setClothColors(colors);
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
            addImportedPanels(pdf.outlines, true);
            return;
        }
        // PDF scanné : la page est une image, on l'importe comme calque.
        io::RasterImage img;
        if (!io::decodeRasterImage(pdf.jpeg, img, error)) {
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

void App::addImportedPanels(const std::vector<std::vector<glm::vec2>>& outlines, bool arrange)
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
        panel.name = "Pièce " + std::to_string(g.panels.size() + 1);
        panel.placement.angleDeg = (added % 2 == 0) ? 0.0f : 180.0f;
        g.panels.push_back(std::move(panel));
        ++added;
    }
    if (added == 0) {
        setStatus("Aucune pièce exploitable dans ce fichier.", true);
        return;
    }
    undoStack_.push_back(committed_);
    redoStack_.clear();
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
    std::string error;
    if (io::exportObj(path, m, error)) setStatus("Vêtement exporté : " + path, false);
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
    avatarRotation_ = 0.0f;
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

void App::applyAvatarTransform()
{
    avatarPositions_ = avatarSource_.positions;
    io::FitOptions fit;
    fit.targetHeight = avatarHeight_;
    fit.rotationYDegrees = avatarRotation_;
    io::fitToGround(avatarPositions_, fit);
    geometry::computeVertexNormals(avatarPositions_, avatarSource_.indices, avatarNormals_);
    avatarBvh_.build(avatarPositions_, avatarSource_.indices);
    if (showAvatar_) renderer_.setAvatarMesh(avatarPositions_, avatarNormals_, avatarSource_.indices);
    else renderer_.clearAvatar();
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
    toolbarHeight_ = std::round(44.0f * uiScale_);
    statusHeight_ = std::round(ImGui::GetFrameHeight() + 4.0f * uiScale_);
    inspectorWidth_ = std::round(345.0f * uiScale_);
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
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_I, false)) openPatternDialog();
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_E, false)) exportGarment();
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_N, false)) setGarment({});
    if (!ctrl && ImGui::IsKeyPressed(ImGuiKey_Space, false)) playing_ = !playing_;
    if (!ctrl && ImGui::IsKeyPressed(ImGuiKey_R, false)) resetCloth();
}

void App::handleViewportInput()
{
    const ImGuiIO& io = ImGui::GetIO();
    const ImVec2 m = io.MousePos;
    const bool inside = m.x >= viewPos_.x && m.y >= viewPos_.y && m.x < viewPos_.x + viewSize_.x &&
                        m.y < viewPos_.y + viewSize_.y && !io.WantCaptureMouse;
    const float aspect = viewSize_.x / viewSize_.y;
    const float ndcX = 2.0f * (m.x - viewPos_.x) / viewSize_.x - 1.0f;
    const float ndcY = 1.0f - 2.0f * (m.y - viewPos_.y) / viewSize_.y;

    if (inside) {
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) (io.KeyShift ? panning_ : orbiting_) = true;
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) panning_ = true;
        if (io.MouseWheel != 0.0f) camera_.zoom(std::pow(0.9f, io.MouseWheel));
        if (!io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_F, false)) frameCamera();

        // Clic droit : saisir la particule la plus proche du rayon de la souris.
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right) && clothValid_) {
            glm::vec3 o, d;
            camera_.ray(ndcX, ndcY, aspect, o, d);
            const auto& pos = solver_.positions();
            int best = -1;
            float bestT = 1e9f;
            const float pickRadius = std::max(0.03f, solver_.restSpacing() * 1.5f);
            for (std::size_t i = 0; i < pos.size(); ++i) {
                const float t = glm::dot(pos[i] - o, d);
                if (t <= 0.0f || t >= bestT) continue;
                if (glm::length(pos[i] - (o + d * t)) < pickRadius) {
                    best = static_cast<int>(i);
                    bestT = t;
                }
            }
            if (best >= 0) {
                solver_.grab(static_cast<uint32_t>(best));
                grabDepth_ = bestT;
            }
        }
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) orbiting_ = false;
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) && !ImGui::IsMouseDown(ImGuiMouseButton_Middle)) panning_ = false;

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
        }
    }
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
        importPattern(openFileDialog("Importer un patron",
                                     "Patrons (PDF, PNG, JPEG, SVG, DXF)|*.pdf;*.png;*.jpg;*.jpeg;*.jfif;*.jpe;*.bmp;*.gif;*.tga;*.svg;*.dxf"));
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
        if (ImGui::MenuItem("Nouveau vêtement", "Ctrl+N")) setGarment({});
        ImGui::Separator();
        if (ImGui::MenuItem("Importer un avatar (FBX, OBJ)...", "Ctrl+O")) openAvatarDialog();
        if (ImGui::MenuItem("Importer un patron (SVG, DXF, PDF, image)...", "Ctrl+I")) openPatternDialog();
        ImGui::Separator();
        if (ImGui::MenuItem("Exporter le vêtement (OBJ)...", "Ctrl+E", false, clothValid_)) exportGarment();
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
        if (ImGui::MenuItem("Sélection", "V", editor_.tool == Tool::Select)) editor_.tool = Tool::Select;
        if (ImGui::MenuItem("Polygone", "P", editor_.tool == Tool::Polygon)) editor_.tool = Tool::Polygon;
        if (ImGui::MenuItem("Rectangle", "B", editor_.tool == Tool::Rectangle)) editor_.tool = Tool::Rectangle;
        if (ImGui::MenuItem("Couture", "S", editor_.tool == Tool::Seam)) editor_.tool = Tool::Seam;
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
        ImGui::MenuItem("Fil de fer", nullptr, &appearance_.wireframe);
        ImGui::MenuItem("Carte de tension", nullptr, &appearance_.showStrain);
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
}

void App::drawToolbar()
{
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos({0, menuHeight_});
    ImGui::SetNextWindowSize({display.x, toolbarHeight_});
    ImGui::PushStyleColor(ImGuiCol_WindowBg, kBg0);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8 * uiScale_, 0));
    ImGui::Begin("##toolbar", nullptr, kPaneFlags);
    ImGui::PopStyleVar();
    const float b = std::round(32.0f * uiScale_);
    const float y = (toolbarHeight_ - b) * 0.5f;
    ImGui::SetCursorPosY(y);

    using ui::Tool;
    if (iconButton("sel", Icon::Select, "Sélection (V)\nDéplacer pièces et points, double-clic sur un bord pour ajouter un point",
                   editor_.tool == Tool::Select, b))
        editor_.tool = Tool::Select;
    ImGui::SameLine(0, 2);
    if (iconButton("poly", Icon::Polygon, "Polygone (P)\nTracer une pièce point par point", editor_.tool == Tool::Polygon, b))
        editor_.tool = Tool::Polygon;
    ImGui::SameLine(0, 2);
    if (iconButton("rect", Icon::Rectangle, "Rectangle (B)\nTracer une pièce rectangulaire", editor_.tool == Tool::Rectangle, b))
        editor_.tool = Tool::Rectangle;
    ImGui::SameLine(0, 2);
    if (iconButton("seam", Icon::Seam, "Couture (S)\nCliquer deux bords à coudre ensemble", editor_.tool == Tool::Seam, b))
        editor_.tool = Tool::Seam;

    toolbarSeparator(b);
    if (iconButton("play", playing_ ? Icon::Pause : Icon::Play, playing_ ? "Pause (Espace)" : "Simuler (Espace)", playing_, b))
        playing_ = !playing_;
    ImGui::SameLine(0, 2);
    if (iconButton("step", Icon::Step, "Image suivante", false, b)) stepOnce_ = true;
    ImGui::SameLine(0, 2);
    if (iconButton("reset", Icon::Reset, "Recoudre depuis le patron (R)", false, b)) resetCloth();
    ImGui::SameLine(0, 2);
    if (iconButton("frame", Icon::Frame, "Cadrer l'avatar (F)", false, b)) frameCamera();

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

    // À droite : avancement de la couture.
    if (clothValid_) {
        const float w = 190 * uiScale_;
        ImGui::SameLine(ImGui::GetWindowWidth() - w - 12 * uiScale_);
        ImGui::SetCursorPosY(comboY);
        const float prog = solver_.seamCount() > 0 ? solver_.seamProgress() : 1.0f;
        char text[64];
        if (prog < 1.0f) std::snprintf(text, sizeof(text), "Couture %.0f %%", static_cast<double>(prog * 100.0f));
        else std::snprintf(text, sizeof(text), playing_ ? "Drapé en cours" : "En pause");
        ImGui::PushStyleColor(ImGuiCol_FrameBg, kBg2);
        ImGui::ProgressBar(prog, ImVec2(w, ImGui::GetFrameHeight()), text);
        ImGui::PopStyleColor();
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
        ImGui::SetCursorPos({12 * uiScale_, (h - ImGui::GetTextLineHeight()) * 0.5f});
        paneTitle("PATRON 2D", fontBold_);
        ImGui::SameLine();
        ImGui::TextDisabled("  %zu pièce(s), %zu couture(s)", garment_.panels.size(), garment_.seams.size());
        ImGui::SetCursorPos({0, h});
    }
    if (editor_.draw(garment_)) commitGarment();
    ImGui::End();
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
    const float pad = 12 * uiScale_;
    if (fontBold_) dl->AddText(fontBold_, fontBold_->FontSize, {o.x + pad, o.y + pad * 0.7f}, ImGui::GetColorU32(kTextDim), "VUE 3D");
    else dl->AddText({o.x + pad, o.y + pad * 0.7f}, ImGui::GetColorU32(kTextDim), "VUE 3D");
    char text[160];
    std::snprintf(text, sizeof(text), "%s  -  %.2f m", avatarSource_.name.c_str(), static_cast<double>(avatarHeight_));
    dl->AddText({o.x + pad, o.y + pad * 0.7f + ImGui::GetTextLineHeightWithSpacing()}, ImGui::GetColorU32(kTextDim), text);

    if (clothValid_ && solver_.seamCount() > 0 && solver_.seamProgress() < 1.0f) {
        std::snprintf(text, sizeof(text), "Couture en cours  %.0f %%", static_cast<double>(solver_.seamProgress() * 100.0f));
        const ImVec2 ts = ImGui::CalcTextSize(text);
        const ImVec2 c(o.x + viewSize_.x * 0.5f, o.y + pad * 2.2f);
        dl->AddRectFilled({c.x - ts.x * 0.5f - 12, c.y - 6}, {c.x + ts.x * 0.5f + 12, c.y + ts.y + 6}, IM_COL32(18, 19, 21, 210), 12.0f);
        dl->AddText({c.x - ts.x * 0.5f, c.y}, kAccentU32, text);
    }
    if (!clothValid_ && garment_.empty()) {
        const char* msg = "Patron vide : tracez une pièce à gauche ou choisissez un modèle (menu Modèles).";
        const ImVec2 ts = ImGui::CalcTextSize(msg);
        dl->AddText({o.x + (viewSize_.x - ts.x) * 0.5f, o.y + viewSize_.y * 0.5f}, ImGui::GetColorU32(kTextDim), msg);
    }
    const char* help = "Clic gauche : tourner    Maj / clic milieu : déplacer    Molette : zoom    Clic droit : saisir le tissu";
    const ImVec2 hs = ImGui::CalcTextSize(help);
    if (hs.x < viewSize_.x - 2 * pad)
        dl->AddText({o.x + pad, o.y + viewSize_.y - hs.y - pad * 0.7f}, IM_COL32(150, 154, 162, 170), help);

    // Boutons de vues prédéfinies, en haut à droite.
    const float bw = 54 * uiScale_;
    ImGui::SetNextWindowPos({o.x + viewSize_.x - 4 * bw - pad - 18 * uiScale_, o.y + pad * 0.5f});
    ImGui::SetNextWindowBgAlpha(0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4, 4));
    ImGui::Begin("##vues", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                        ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.10f, 0.105f, 0.115f, 0.80f));
    auto viewButton = [&](const char* name, float yaw) {
        if (ImGui::Button(name, ImVec2(bw, 0))) {
            frameCamera();
            camera_.yaw = yaw;
            camera_.pitch = 0.05f;
        }
        ImGui::SameLine(0, 3);
    };
    viewButton("Face", 0.0f);
    viewButton("Dos", kPi);
    viewButton("Profil", kPi * 0.5f);
    if (ImGui::Button("3/4", ImVec2(bw, 0))) frameCamera();
    ImGui::PopStyleColor();
    ImGui::End();
    ImGui::PopStyleVar();
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
    paneTitle("PROPRIÉTÉS", fontBold_);
    ImGui::Spacing();
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8 * uiScale_, 5 * uiScale_));
    inspectorBackdrop();
    inspectorPanel();
    inspectorSeams();
    inspectorFabric();
    inspectorAvatar();
    inspectorSimulation();
    inspectorDisplay();
    ImGui::PopStyleVar();
    ImGui::End();
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
    if (ImGui::IsItemDeactivatedAfterEdit()) committed_ = garment_;
    label("Couleur");
    if (ImGui::ColorEdit3("##couleur", &panel.color.x, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel)) {
        updateClothColors();
        committed_ = garment_;
    }

    float perimeter = 0.0f;
    for (std::size_t e = 0; e < panel.points.size(); ++e) perimeter += garment_.edgeLength({idx, static_cast<int>(e)});
    ImGui::TextDisabled("%zu points   %.0f cm²   périmètre %.0f cm", panel.points.size(),
                        static_cast<double>(std::abs(garment::signedArea(panel.points)) * 1e4f),
                        static_cast<double>(perimeter * 100.0f));

    ImGui::SeparatorText("Placement sur l'avatar");
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
    label("Angle");
    ImGui::SliderFloat("##angle", &panel.placement.angleDeg, -180.0f, 180.0f, "%.0f°");
    edited |= ImGui::IsItemDeactivatedAfterEdit();
    float hcm = panel.placement.height * 100.0f;
    label("Hauteur");
    if (ImGui::SliderFloat("##hauteur", &hcm, 0.0f, 180.0f, "%.0f cm")) panel.placement.height = hcm / 100.0f;
    edited |= ImGui::IsItemDeactivatedAfterEdit();
    float rcm = panel.placement.radius * 100.0f;
    label("Distance");
    if (ImGui::SliderFloat("##rayon", &rcm, 8.0f, 60.0f, "%.0f cm")) panel.placement.radius = rcm / 100.0f;
    edited |= ImGui::IsItemDeactivatedAfterEdit();
    if (edited) commitGarment();

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

void App::inspectorSeams()
{
    char header[48];
    std::snprintf(header, sizeof(header), "Coutures (%zu)###coutures", garment_.seams.size());
    if (!ImGui::CollapsingHeader(header, ImGuiTreeNodeFlags_DefaultOpen)) return;
    if (garment_.seams.empty()) {
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

        const float la = garment_.edgeLength(seam.a), lb = garment_.edgeLength(seam.b);
        char text[160];
        std::snprintf(text, sizeof(text), "%s  +  %s", shortName(seam.a).c_str(), shortName(seam.b).c_str());
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
    if (ImGui::Button("Mannequin intégré", ImVec2(hw, 0))) loadDefaultMannequin();
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
}

void App::inspectorDisplay()
{
    if (!ImGui::CollapsingHeader("Affichage")) return;
    ImGui::Checkbox("Fil de fer", &appearance_.wireframe);
    ImGui::SameLine();
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
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10 * uiScale_, 2 * uiScale_));
    ImGui::Begin("##etat", nullptr, kPaneFlags | ImGuiWindowFlags_NoScrollbar);
    ImGui::PopStyleVar();
    ImGui::SetCursorPosY((statusHeight_ - ImGui::GetTextLineHeight()) * 0.5f);

    // Message récent (8 s) sinon l'aide de l'outil courant.
    const bool recent = !status_.empty() && ImGui::GetTime() - statusTime_ < 8.0;
    if (recent) {
        ImGui::PushStyleColor(ImGuiCol_Text, statusIsError_ ? ImVec4(0.95f, 0.46f, 0.40f, 1.0f) : ImVec4(0.60f, 0.83f, 0.62f, 1.0f));
        ImGui::TextUnformatted(status_.c_str());
        ImGui::PopStyleColor();
    } else {
        ImGui::TextDisabled("%s", editor_.hint().c_str());
    }

    char right[200];
    const auto& st = solver_.stats();
    std::snprintf(right, sizeof(right), "%zu particules   |   %zu triangles   |   simulation %.1f ms   |   t = %.1f s   |   %.0f img/s",
                  clothValid_ ? st.particles : std::size_t{0}, clothValid_ ? solver_.indices().size() / 3 : std::size_t{0},
                  static_cast<double>(st.stepMilliseconds), static_cast<double>(simTime_),
                  static_cast<double>(ImGui::GetIO().Framerate));
    const float w = ImGui::CalcTextSize(right).x;
    ImGui::SameLine(std::max(ImGui::GetCursorPosX() + 20, display.x - w - 14 * uiScale_));
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
        ImGui::Spacing();
        if (ImGui::Button("Fermer", ImVec2(120 * uiScale_, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Raccourcis clavier", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        const char* rows[][2] = {
            {"V / P / B / S", "Outils Sélection, Polygone, Rectangle, Couture"},
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
        const char* what[] = {"Avatar (.fbx, .obj)", "Patron (.svg, .dxf, .pdf, .png, .jpg)", "Export OBJ"};
        ImGui::TextUnformatted(what[pathPopupKind_]);
        ImGui::SetNextItemWidth(420 * uiScale_);
        const bool enter = ImGui::InputText("##chemin", pathInput_, sizeof(pathInput_), ImGuiInputTextFlags_EnterReturnsTrue);
        if (ImGui::Button("Valider", ImVec2(120 * uiScale_, 0)) || enter) {
            const std::string path = pathInput_;
            ImGui::CloseCurrentPopup();
            if (pathPopupKind_ == 0) loadAvatar(path);
            else if (pathPopupKind_ == 1) importPattern(path);
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
    if (ImGui::Button("Importer", ImVec2(140 * uiScale_, 0))) {
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
