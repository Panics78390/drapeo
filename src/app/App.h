// =============================================================================
//  app/App — fenêtre, boucle principale et interface de l'atelier
//
//  Disposition de l'écran :
//    ┌ barre de menus ─────────────────────────────────────────────────────┐
//    ├ barre d'outils ─────────────────────────────────────────────────────┤
//    │ Patron 2D (éditeur) │ Vue 3D (drapé sur l'avatar) │ Inspecteur      │
//    ├ barre d'état ───────────────────────────────────────────────────────┤
//
//  L'application orchestre les modules sans contenir de logique métier :
//    garment    -> patrons 2D, coutures, maillage du vêtement
//    simulation -> avance l'état physique
//    rendering  -> affiche l'état
//    ui         -> éditeur de patrons 2D
// =============================================================================
#pragma once

#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "garment/GarmentMesher.h"
#include "garment/Pattern.h"
#include "geometry/TriangleBvh.h"
#include "io/MeshImport.h"
#include "io/RasterImage.h"
#include "rendering/Camera.h"
#include "rendering/Renderer.h"
#include "simulation/XpbdSolver.h"
#include "ui/PatternEditor.h"

struct GLFWwindow;
struct ImFont;

namespace drapeo::app {

struct AppOptions {
    int width = 1600;
    int height = 900;
    int frames = -1;              // > 0 : quitte après N images (tests, captures)
    std::string screenshotPath;   // capture PPM de la dernière image
    bool showAbout = false;       // ouvre la fenêtre « À propos » au démarrage
    std::string patternPath;      // patron à importer au démarrage (.svg, .dxf, .pdf, image)
    bool acceptImport = false;    // valide directement la fenêtre d'import d'image
    std::string avatarPath;       // avatar à charger au démarrage (.fbx / .obj)
};

class App {
public:
    int run(const AppOptions& options);

private:
    // --- Cycle de vie ---------------------------------------------------------
    bool initWindow();
    void loadFonts(float scale);
    void loadLogo();
    void shutdown();

    // --- Vêtement -------------------------------------------------------------
    void resetCloth();                     // remaille le patron et relance la couture
    void setGarment(garment::Garment g);   // nouveau patron (modèle, fichier...)
    void commitGarment();                  // une modification est terminée
    void undo();
    void redo();
    void updateClothColors();
    void importPattern(const std::string& path);
    void addImportedPanels(const std::vector<std::vector<glm::vec2>>& outlines, bool arrange,
                           const std::vector<std::string>& names = {});
    void saveProjectFile();
    void beginImageImport(io::RasterImage image, const std::string& name);
    void finishImageImport();
    void updateBackdropRect();
    void detectBackdropPieces();
    void removeBackdrop();
    void exportGarment();

    // --- Avatar ---------------------------------------------------------------
    bool loadAvatar(const std::string& path);
    void loadDefaultMannequin();
    void applyAvatarTransform();
    void frameCamera();

    // --- Interface --------------------------------------------------------------
    void computeLayout();
    void handleViewportInput();
    void handleShortcuts();
    void drawMenuBar();
    void drawToolbar();
    void drawPatternPane();
    void drawSplitter();
    void drawViewportOverlay();
    void drawInspector();
    void drawStatusBar();
    void drawAboutWindow();
    void drawImageImportPopup();
    void inspectorBackdrop();
    void inspectorPanel();
    void inspectorSeams();
    void inspectorFabric();
    void inspectorAvatar();
    void inspectorSimulation();
    void inspectorDisplay();
    void setStatus(const std::string& text, bool error);
    void openAvatarDialog();
    void openPatternDialog();
    void saveScreenshot(const std::string& path) const;

    AppOptions options_;
    GLFWwindow* window_ = nullptr;
    ImFont* fontBold_ = nullptr;
    unsigned int logoIcon_ = 0, logoFull_ = 0;   // textures OpenGL du logo
    float uiScale_ = 1.0f;

    rendering::OrbitCamera camera_;
    rendering::Renderer renderer_;
    rendering::ClothAppearance appearance_;
    simulation::XpbdSolver solver_;
    simulation::FabricMaterial material_;
    int presetIndex_ = 0;

    // Vêtement
    garment::Garment garment_;
    garment::Garment committed_;                 // dernier état validé (pour annuler)
    std::vector<garment::Garment> undoStack_, redoStack_;
    garment::GarmentMesh garmentMesh_;
    bool clothValid_ = false;
    ui::PatternEditor editor_;
    float spacing_ = 0.02f;                      // distance entre particules (m)
    float sewingSeconds_ = 1.0f;
    bool panelColors_ = true;

    bool playing_ = true;
    bool stepOnce_ = false;
    int frameCount_ = 0;
    float simTime_ = 0.0f;

    // Buffers réutilisés à chaque image
    std::vector<glm::vec3> normals_;
    std::vector<float> strain_;

    // Avatar (le mannequin intégré par défaut)
    bool builtinAvatar_ = true;
    bool showAvatar_ = true;
    io::ImportedMesh avatarSource_;
    std::vector<glm::vec3> avatarPositions_;
    std::vector<glm::vec3> avatarNormals_;
    geometry::TriangleBvh avatarBvh_;
    float avatarHeight_ = 1.75f;
    float avatarRotation_ = 0.0f;

    // Image de patron (calque sous l'éditeur 2D)
    io::RasterImage backdropImage_;
    unsigned int backdropTex_ = 0;
    glm::vec2 backdropOrigin_{0.0f};
    float backdropWidthCm_ = 100.0f;
    bool autoThreshold_ = true;
    int threshold_ = 128;
    io::RasterImage pendingImage_;               // image en attente de validation
    unsigned int pendingTex_ = 0;
    std::string pendingName_;
    float importWidthCm_ = 100.0f;
    bool importDetect_ = true;
    bool showImagePopup_ = false;

    // Messages
    std::string status_;
    bool statusIsError_ = false;
    double statusTime_ = 0.0;
    std::string pendingDrop_;
    char pathInput_[1024] = "";
    bool showPathPopup_ = false;
    int pathPopupKind_ = 0;                      // 0 avatar, 1 patron, 2 export, 3 projet
    bool showAbout_ = false;
    bool showShortcuts_ = false;

    // Disposition (coordonnées fenêtre, en points ImGui)
    float menuHeight_ = 0.0f, toolbarHeight_ = 0.0f, statusHeight_ = 0.0f;
    float leftWidth_ = -1.0f, inspectorWidth_ = 340.0f;
    glm::vec2 viewPos_{0.0f}, viewSize_{1.0f};

    // Souris dans la vue 3D
    bool orbiting_ = false, panning_ = false;
    float grabDepth_ = 0.0f;
    // Déplacement d'une pièce dans la vue 3D (simulation en pause)
    int dragPanel_ = -1;
    bool dragMoved_ = false;
    glm::vec3 dragLastHit_{0.0f};
    [[nodiscard]] garment::MeshingOptions meshingOptions() const;
    void dragPanelTo(const glm::vec3& rayOrigin, const glm::vec3& rayDir);

    friend void dropCallback(GLFWwindow*, int, const char**);
};

} // namespace drapeo::app
