// =============================================================================
//  simulation/XpbdSolver — solveur de tissu XPBD à petits pas (« small steps »)
//
//  Références :
//   - Macklin, Müller, Chentanez. « XPBD: Position-Based Simulation of
//     Compliant Constrained Dynamics », MIG 2016.
//   - Macklin et al. « Small Steps in Physics Simulation », SCA 2019 :
//     beaucoup de sous-pas avec UNE itération chacun converge mieux que peu de
//     pas avec beaucoup d'itérations, et le multiplicateur λ peut être remis à
//     zéro à chaque sous-pas.
//
//  Pipeline d'un sous-pas (dt = dtImage / sousPas) :
//    1. v += g·dt ; x_prev = x ; x += v·dt         (prédiction)
//    2. contraintes de distance (étirement), puis flexion
//    3. coutures (à venir), épingles
//    4. collisions : obstacles (sphère, sol), puis auto-collisions
//    5. v = (x - x_prev) / dt, contact inélastique, vitesse max, amortissements
// =============================================================================
#pragma once

#include <array>
#include <cstdint>
#include <unordered_set>
#include <vector>

#include <glm/glm.hpp>

#include "geometry/ClothMesh.h"
#include "geometry/TriangleBvh.h"
#include "simulation/Materials.h"
#include "simulation/SpatialHash.h"

namespace drapeo::simulation {

struct SphereCollider {
    glm::vec3 center{0.0f, 0.6f, 0.0f};
    float radius = 0.5f;
};

struct SolverSettings {
    int       substeps      = 15;
    glm::vec3 gravity       {0.0f, -9.81f, 0.0f};
    bool      groundEnabled = true;
    float     groundHeight  = 0.0f;
    bool      selfCollision = true;
    int       selfCollisionInterval = 2;   // auto-collisions tous les N sous-pas
    float     maxStepRatio  = 0.5f;        // déplacement max par sous-pas / espacement
};

struct SolverStats {
    float stepMilliseconds = 0.0f;
    std::size_t particles = 0;
    std::size_t stretchConstraints = 0;
    std::size_t bendConstraints = 0;
    std::size_t selfCollisionPairs = 0;
};

class XpbdSolver {
public:
    /// (Ré)initialise le solveur à partir d'un maillage et d'un matériau.
    void build(const geometry::ClothMesh& mesh, const FabricMaterial& material);

    /// Coutures : paires de particules à réunir. Elles se resserrent
    /// progressivement pendant `closeSeconds` (sans gravité), puis restent
    /// fermées (longueur 0) et la gravité s'applique.
    void setSeams(const std::vector<std::array<uint32_t, 2>>& pairs, float closeSeconds);
    /// 0 = coutures en train de se fermer, 1 = fermées.
    [[nodiscard]] float seamProgress() const { return 1.0f - seamFactor_; }
    [[nodiscard]] std::size_t seamCount() const { return seams_.size(); }

    /// Change de matériau sans reconstruire (masses recalculées).
    void setMaterial(const FabricMaterial& material);

    /// Avance la simulation d'une image de durée `frameDt` secondes.
    void step(float frameDt);

    // --- Obstacles et réglages ---------------------------------------------
    std::vector<SphereCollider>& spheres() { return spheres_; }
    /// Corps (avatar) : BVH de ses triangles, ou nullptr. Le BVH doit survivre au solveur.
    void setMeshCollider(const geometry::TriangleBvh* bvh) { meshCollider_ = bvh; }
    [[nodiscard]] std::size_t meshContacts() const { return meshContacts_; }
    SolverSettings& settings() { return settings_; }
    [[nodiscard]] const SolverStats& stats() const { return stats_; }

    // --- Épingles et manipulation ------------------------------------------
    void setPinned(uint32_t particle, bool pinned);
    /// Raideur par particule (1 = matériau normal, > 1 = renforcé). Vide = partout 1.
    void setStiffnessScale(std::vector<float> scale) { stiffness_ = std::move(scale); }
    [[nodiscard]] bool isPinned(uint32_t particle) const { return pinned_[particle] != 0; }
    /// Saisie à la souris : la particule devient cinématique et suit `target`.
    void grab(uint32_t particle);
    void moveGrabbed(const glm::vec3& target);
    void release();
    /// Place une particule (vitesse remise à zéro) : déplacement d'une pièce avant simulation.
    void setParticlePosition(uint32_t particle, const glm::vec3& p);
    [[nodiscard]] int grabbedParticle() const { return grabbed_; }

    // --- Accès aux résultats -----------------------------------------------
    [[nodiscard]] const std::vector<glm::vec3>& positions() const { return x_; }
    [[nodiscard]] const std::vector<uint32_t>& indices() const { return indices_; }
    /// Étirement relatif max par sommet (|l/l0 - 1|) : carte de tension.
    void computeStrain(std::vector<float>& outStrain) const;
    [[nodiscard]] float averageStretchError() const;
    [[nodiscard]] float restSpacing() const { return restSpacing_; }

private:
    struct DistanceConstraint {
        uint32_t a, b;
        float rest;
    };

    void updateInverseMasses();
    void solveDistance(const std::vector<DistanceConstraint>& constraints, float compliance, float dt);
    void solveSeams();
    void solveObstacles();
    void findSelfCollisionCandidates(float frameDt);
    void solveSelfCollisions();
    void smoothVelocities(float blend);

    // État par particule
    std::vector<glm::vec3> x_, prev_, v_, rest_;
    std::vector<float>     invMass_;
    std::vector<uint8_t>   pinned_;
    std::vector<float>     stiffness_;      // raideur par particule (pièces renforcées)
    std::vector<float>     massPerParticle_;
    std::vector<glm::vec3> contactNormal_;
    std::vector<glm::vec3> velocityDelta_;  // tampons de smoothVelocities
    std::vector<float>     neighborCount_;  // normale du dernier contact du sous-pas (0 si aucun)

    std::vector<uint32_t>           indices_;
    std::vector<DistanceConstraint> stretch_, bend_, seams_;
    std::unordered_set<uint64_t>    seamKeys_;      // paires cousues : pas d'auto-collision entre elles
    float seamFactor_ = 0.0f;                       // 1 -> 0 pendant la fermeture
    float seamCloseSeconds_ = 1.0f;

    // Auto-collisions : voisins candidats (CSR) recalculés une fois par image
    std::vector<uint32_t> neighborStart_, neighbors_;
    SpatialHash hash_{0.02f, 1};
    float selfDistance_ = 0.01f;
    float maxVelocity_  = 10.0f;

    std::vector<SphereCollider> spheres_{SphereCollider{}};
    const geometry::TriangleBvh* meshCollider_ = nullptr;
    std::size_t meshContacts_ = 0;
    SolverSettings settings_;
    FabricMaterial material_;
    SolverStats    stats_;
    float restSpacing_ = 0.02f;
    int   grabbed_ = -1;
};

} // namespace drapeo::simulation
