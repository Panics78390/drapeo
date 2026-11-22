// =============================================================================
//  rendering/Renderer — rendu OpenGL 3.3 core du tissu et de la scène
//
//  Éclairage physique simplifié : GGX (Cook-Torrance) + Lambert + reflet
//  « sheen » de tissu, tone mapping et correction gamma. Le tissu est rendu
//  double face (la normale est retournée pour les faces arrière).
// =============================================================================
#pragma once

#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "rendering/Camera.h"
#include "rendering/GlFunctions.h"
#include "rendering/Shader.h"
#include "simulation/XpbdSolver.h"

namespace drapeo::rendering {

struct ClothAppearance {
    glm::vec3 color{0.8f};
    float roughness = 0.8f;
    float sheen = 0.3f;
    bool showStrain = false;
    float strainScale = 0.10f;   // étirement affiché en rouge (10 %)
    bool wireframe = false;
    bool showBackSide = true;    // envers du tissu (intérieur du vêtement) plus sombre et bleuté
    int motif = 0;               // 0 uni, 1 rayures, 2 carreaux (ignoré si la carte de tension est affichée)
    glm::vec3 motifColor{0.16f, 0.14f, 0.12f};
    unsigned int motifTexture = 0;
    float motifRepeat = 0.15f;   // mètres
};

struct SceneView {
    const OrbitCamera* camera = nullptr;
    int x = 0, y = 0;            // coin inférieur gauche de la vue 3D (pixels du framebuffer)
    int width = 1, height = 1;
    const std::vector<simulation::SphereCollider>* spheres = nullptr;
    bool groundEnabled = true;
    float groundHeight = 0.0f;
};

class Renderer {
public:
    Renderer() = default;
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    bool init(std::string& error);

    /// À appeler quand le maillage change (réinitialisation, résolution).
    void setClothTopology(const std::vector<uint32_t>& indices, std::size_t vertexCount);
    /// À appeler à chaque image : positions, normales, étirement par sommet.
    void updateCloth(const std::vector<glm::vec3>& positions, const std::vector<glm::vec3>& normals,
                     const std::vector<float>& strain);
    /// Couleur par sommet du tissu (couleur de chaque pièce) ; vide = couleur unique.
    void setClothColors(const std::vector<glm::vec3>& colors);
    /// Coordonnées du patron (m) et aspect du tissu (FabricFinish) de chaque sommet.
    void setClothPattern(const std::vector<glm::vec2>& uvs, const std::vector<float>& finish);
    /// Texture des impressions, plaquée selon les coordonnées du patron (lignes du bas en premier).
    /// `origin` = coin inférieur gauche (m), `size` = taille couverte (m). Une image vide retire la texture.
    [[nodiscard]] unsigned int printTexture() const { return printTexture_; }
    void setPrintTexture(int width, int height, const unsigned char* rgba, glm::vec2 origin, glm::vec2 size);

    /// Avatar importé (statique). Appeler clearAvatar() pour le retirer.
    void setAvatarMesh(const std::vector<glm::vec3>& positions, const std::vector<glm::vec3>& normals,
                       const std::vector<uint32_t>& indices);
    void clearAvatar() { destroyMesh(avatar_); }

    void render(const SceneView& scene, const ClothAppearance& cloth);

private:
    struct GpuMesh {
        gl::GLuint vao = 0, position = 0, normal = 0, scalar = 0, color = 0, uv = 0, finish = 0, index = 0;
        gl::GLsizei indexCount = 0;
        std::size_t vertexCount = 0;
    };

    static void createMesh(GpuMesh& mesh, const std::vector<glm::vec3>& positions,
                           const std::vector<glm::vec3>& normals, const std::vector<uint32_t>& indices,
                           gl::GLenum usage);
    static void destroyMesh(GpuMesh& mesh);
    static void drawMesh(const GpuMesh& mesh);

    Shader shader_;
    Shader backdrop_;                 // cyclorama de l'atelier (dégradé, sans géométrie)
    gl::GLuint backdropVao_ = 0;
    GpuMesh cloth_, sphere_, ground_, avatar_;
    bool clothHasColors_ = false;
    gl::GLuint printTexture_ = 0;
    glm::vec2 printOrigin_{0.0f}, printSize_{1.0f};
};

} // namespace drapeo::rendering
