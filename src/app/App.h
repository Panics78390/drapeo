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

#include <array>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

#include "garment/PrintAtlas.h"
#include "garment/GarmentMesher.h"
#include "garment/Pattern.h"
#include "geometry/TriangleBvh.h"
#include "io/MeshImport.h"
#include "io/RasterImage.h"
#include "rendering/Camera.h"
#include "rendering/Renderer.h"
#include "simulation/GarmentSetup.h"
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
    bool startPaused = false;     // démarre simulation en pause (mise en place, captures)
    std::string avatarPath;       // avatar à charger au démarrage (.fbx / .obj)
    float yawDeg = -1000.0f;      // captures : angle de vue (degrés, 0 = de face), sinon vue par défaut
    float zoom = 1.0f;            // captures : > 1 rapproche la caméra
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
    void updateClothPattern();   // coordonnées du patron, aspect du tissu et texture des impressions
    std::string projectDirectory() const;
    void inspectorPrints(int idx);
    void importPattern(const std::string& path);
    void addImportedPanels(const std::vector<std::vector<glm::vec2>>& outlines, bool arrange,
                           const std::vector<std::string>& names = {});
    void saveProjectFile();
    /// Projet ouvert / enregistré : rechargé tout seul s'il est modifié par un autre programme (serveur MCP).
    void watchProject(const std::string& path);
    void checkWatchedProject();
    void beginImageImport(io::RasterImage image, const std::string& name);
    void finishImageImport();
    void updateBackdropRect();
    void detectBackdropPieces();
    void removeBackdrop();
    void newGarment();
    void exportGarment();

    // --- Menu clic droit des pièces (vue patron) ------------------------------
    enum class PieceAction {
        SelectAll, InvertSelection, SameColor, SameZone, SameLink, NewFabric,
        Copy, Paste, PasteMirror, Delete,
        CloneMirrorSewn, CloneMirror, CloneInstance, CloneOver, CloneUnder, Link, Unlink,
        Grow, Shrink, Offset, Simplify, InnerShape,
        Deactivate, DeactivateWithSeams, Activate, Freeze, Unfreeze, Strengthen, Soften, Hide, Show, ShowAll,
        Front, Forward, Backward, Back, FlipFace, RotateCW, RotateCCW, Rotate180, RotateFree, FlipH, FlipV,
        FrameSelection, ExportSelection
    };
    void drawPieceMenu();
    void inspectorPanelSimulation();
    void pieceAction(PieceAction action);
    void pasteClipboard(bool mirror, bool atMenuPoint);
    void commitState();      // modification sans remaillage (état 3D des pièces)
    void apply3DState();     // figer, renforcer, masquer : appliqué au solveur et à l'affichage

    // --- Avatar ---------------------------------------------------------------
    bool loadAvatar(const std::string& path);
    void loadDefaultMannequin();
    void applyAvatarTransform();
    void buildSilhouette();                // ombre de l'avatar (vue de face) pour la vue patron
    unsigned int silhouetteTex_ = 0;
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
    void inspectorSewOn(int panel);
    void inspectorTaper();
    void inspectorCombine(int panel);
    void setPanelSymmetric(int panel, bool on);
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
    bool showStitches_ = true;                   // coutures et surpiqûres dessinées sur le tissu

    bool playing_ = true;
    bool stepOnce_ = false;
    int frameCount_ = 0;
    float simTime_ = 0.0f;

    int sewFor_ = -1, sewBase_ = -1, sewKind_ = 0;
    garment::Garment clipboard_;                  // pièces copiées (Ctrl+C)
    // Fenêtre « Couper » (outil Ajouter un point / couper) et options du point
    bool splitOpen_ = false;
    int splitMode_ = 1;          // 0 : au point cliqué, 1 : en parties égales
    int splitParts_ = 2;
    garment::EdgeRef splitEdge_;
    float splitT_ = 0.5f;
    float dartDepthCm_ = 6.0f, dartWidthCm_ = 2.0f;
    void drawSplitWindow();
    bool applySplit(garment::Garment& g) const;
    bool drawPointMenu();        // options du point sous la souris (true : menu affiché)
    int pasteCount_ = 0;
    bool menuAtPoint_ = false;                    // collage au point du clic droit
    float offsetCm_ = 1.0f, rotateDeg_ = 15.0f;   // réglages du menu des pièces
    std::vector<int> exportOnly_;                 // export limité à ces pièces (vide = tout)
    float taperPercent_ = 10.0f;   // « Cintrer » de l'inspecteur
    bool taperWiden_ = false;
    int combineFor_ = -1, combineTarget_ = -1;   // « Ajouter ou découper » de l'inspecteur
    int taperMode_ = 1, taperAnchor_ = 0;   // « Coudre sur une pièce » de l'inspecteur
    std::vector<glm::vec3> displayPositions_;   // positions affichées (pièces posées décollées)
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
    std::string avatarPath_;                     // fichier de l'avatar importé (vide = mannequin)
    std::string watchedProject_;                 // projet .drapeo surveillé (rechargement automatique)
    long long watchedStamp_ = 0;                 // date + taille du fichier au dernier chargement
    double watchCheckTime_ = 0.0;
    // Impressions (logos, inscriptions) : images chargées et texture du patron
    std::unordered_map<std::string, io::RasterImage> printImages_;
    std::unordered_map<std::string, bool> printMissing_;
    std::string printSignature_;
    garment::PatternTexture printAtlas_;
    garment::BodyAxes bodyAxes_;                 // axes du buste et des membres de l'avatar
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
    void refreshPanelParticles(int panel);
    void finishPanelEdit(int panel);

    // Gizmo 3D de la pièce sélectionnée (simulation en pause)
    enum class Gizmo { None, Slide, MoveX, MoveY, MoveZ, PlaneYZ, PlaneXZ, PlaneXY, RotX, RotY, RotZ };
    Gizmo gizmoHover_ = Gizmo::None, gizmoActive_ = Gizmo::None;
    glm::vec3 gizmoCenter_{0.0f};
    float gizmoLength_ = 0.1f;
    glm::vec3 gizmoLastHit_{0.0f};
    float gizmoLastAngle_ = 0.0f;
    bool gizmoMoved_ = false;
    [[nodiscard]] bool gizmoVisible() const;
    void updateGizmoFrame();
    [[nodiscard]] glm::vec2 projectToScreen(const glm::vec3& p, bool* inFront = nullptr) const;
    [[nodiscard]] Gizmo gizmoHitTest(glm::vec2 mouse) const;
    void dragGizmo(glm::vec2 mouse, glm::vec2 delta);
    void drawGizmo();

    // Coutures dessinées dans la vue 3D (clic : activer / désactiver)
    bool showSeams3D_ = true;
    int hoverSeam3D_ = -1;
    [[nodiscard]] int seamHitTest3D(glm::vec2 mouse, float* depth = nullptr) const;
    void drawSeams3D();
    void drawGrain3D();
    void drawSelection3D();
    std::vector<int> highlighted_;                          // pièces mises en évidence en 3D
    std::vector<std::array<uint32_t, 2>> selectionEdges_;   // leurs bords dans le maillage
    void toggleSeam(int seam);

    friend void dropCallback(GLFWwindow*, int, const char**);
};

} // namespace drapeo::app
