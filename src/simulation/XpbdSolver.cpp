#include "simulation/XpbdSolver.h"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace drapeo::simulation {

namespace {
constexpr float kEpsilon = 1e-9f;

/// Frottement de Coulomb « position-based » (Macklin et al. 2014, Unified
/// Particle Physics) : on annule tout ou partie du déplacement tangentiel du
/// sous-pas en fonction de la profondeur de pénétration corrigée.
void applyFriction(glm::vec3& x, const glm::vec3& prev, const glm::vec3& normal, float penetration, float mu)
{
    const glm::vec3 disp = x - prev;
    const glm::vec3 tangential = disp - normal * glm::dot(disp, normal);
    const float t = glm::length(tangential);
    if (t < kEpsilon) return;
    const float limit = mu * penetration;
    if (t < limit) {
        x -= tangential;                                       // adhérence (frottement statique)
    } else {
        x -= tangential * std::min(limit / t, 1.0f);           // glissement (frottement cinétique)
    }
}
} // namespace

void XpbdSolver::build(const geometry::ClothMesh& mesh, const FabricMaterial& material)
{
    const std::size_t n = mesh.vertexCount();
    x_ = mesh.positions;
    rest_ = mesh.positions;
    prev_ = mesh.positions;
    v_.assign(n, glm::vec3(0.0f));
    pinned_.assign(n, 0);
    contactNormal_.assign(n, glm::vec3(0.0f));
    indices_ = mesh.indices;
    grabbed_ = -1;

    stretch_.clear();
    stretch_.reserve(mesh.edges.size());
    restSpacing_ = 1e9f;
    for (const auto& e : mesh.edges) {
        const float l = glm::distance(x_[e[0]], x_[e[1]]);
        stretch_.push_back({e[0], e[1], l});
        restSpacing_ = std::min(restSpacing_, l);
    }
    bend_.clear();
    bend_.reserve(mesh.bendPairs.size());
    for (const auto& b : mesh.bendPairs) {
        bend_.push_back({b[0], b[1], glm::distance(x_[b[0]], x_[b[1]])});
    }

    // Aire de repos par particule = 1/3 de l'aire des triangles adjacents.
    massPerParticle_.assign(n, 0.0f);
    for (std::size_t t = 0; t + 2 < indices_.size(); t += 3) {
        const uint32_t a = indices_[t], b = indices_[t + 1], c = indices_[t + 2];
        const float area = 0.5f * glm::length(glm::cross(rest_[b] - rest_[a], rest_[c] - rest_[a]));
        massPerParticle_[a] += area / 3.0f;
        massPerParticle_[b] += area / 3.0f;
        massPerParticle_[c] += area / 3.0f;
    }

    seams_.clear();
    seamKeys_.clear();
    seamFactor_ = 0.0f;
    setMaterial(material);

    stats_ = {};
    stats_.particles = n;
    stats_.stretchConstraints = stretch_.size();
    stats_.bendConstraints = bend_.size();
}

void XpbdSolver::setSeams(const std::vector<std::array<uint32_t, 2>>& pairs, float closeSeconds)
{
    seams_.clear();
    seamKeys_.clear();
    for (const auto& p : pairs) {
        seams_.push_back({p[0], p[1], glm::distance(x_[p[0]], x_[p[1]])});
        const uint32_t lo = std::min(p[0], p[1]), hi = std::max(p[0], p[1]);
        seamKeys_.insert((static_cast<uint64_t>(lo) << 32) | hi);
    }
    seamCloseSeconds_ = std::max(closeSeconds, 0.05f);
    seamFactor_ = seams_.empty() ? 0.0f : 1.0f;
}

void XpbdSolver::setMaterial(const FabricMaterial& material)
{
    material_ = material;
    // Distance minimale entre deux particules du tissu : au moins l'épaisseur,
    // et assez grande par rapport à l'espacement pour qu'aucune particule ne
    // passe entre deux autres (le contact reste ici particule–particule).
    selfDistance_ = std::max(material_.thickness, 0.6f * restSpacing_);
    // Cellules de 2·d0 : une requête de rayon d0 ne touche que 2×2×2 cellules.
    hash_ = SpatialHash(2.0f * selfDistance_, x_.size());
    updateInverseMasses();
}

void XpbdSolver::updateInverseMasses()
{
    invMass_.resize(x_.size());
    for (std::size_t i = 0; i < x_.size(); ++i) {
        const float m = massPerParticle_[i] * material_.density;
        const bool kinematic = pinned_[i] != 0 || static_cast<int>(i) == grabbed_;
        invMass_[i] = (kinematic || m <= 0.0f) ? 0.0f : 1.0f / m;
    }
}

void XpbdSolver::setPinned(uint32_t particle, bool pinned)
{
    pinned_[particle] = pinned ? 1 : 0;
    v_[particle] = glm::vec3(0.0f);
    updateInverseMasses();
}

void XpbdSolver::grab(uint32_t particle)
{
    grabbed_ = static_cast<int>(particle);
    v_[particle] = glm::vec3(0.0f);
    updateInverseMasses();
}

void XpbdSolver::moveGrabbed(const glm::vec3& target)
{
    if (grabbed_ >= 0) x_[static_cast<std::size_t>(grabbed_)] = target;
}

void XpbdSolver::release()
{
    grabbed_ = -1;
    updateInverseMasses();
}

void XpbdSolver::step(float frameDt)
{
    const auto t0 = std::chrono::steady_clock::now();
    const int substeps = std::max(1, settings_.substeps);
    const float dt = frameDt / static_cast<float>(substeps);
    const float dampingFactor = std::exp(-material_.damping * dt);
    const float internalBlend = 1.0f - std::exp(-material_.internalDamping * dt);
    const float maxSpeed = settings_.maxStepRatio * restSpacing_ / dt;
    const std::size_t n = x_.size();
    stats_.selfCollisionPairs = 0;

    // Couture en cours : les longueurs de repos des coutures diminuent
    // linéairement jusqu'à 0. Pas de gravité pendant ce temps, comme sur une
    // table de couture virtuelle : les pièces se rejoignent autour du corps.
    const bool sewing = seamFactor_ > 0.0f;
    if (sewing) seamFactor_ = std::max(0.0f, seamFactor_ - frameDt / seamCloseSeconds_);
    const glm::vec3 gravity = sewing ? glm::vec3(0.0f) : settings_.gravity;

    for (int s = 0; s < substeps; ++s) {
        // 1. Prédiction (Euler symplectique)
        for (std::size_t i = 0; i < n; ++i) {
            prev_[i] = x_[i];
            if (invMass_[i] == 0.0f) continue;
            v_[i] += gravity * dt;
            x_[i] += v_[i] * dt;
        }

        // 2. Contraintes internes du tissu
        solveDistance(stretch_, material_.stretchCompliance, dt);
        solveDistance(bend_, material_.bendCompliance, dt);
        if (!seams_.empty()) solveSeams();

        // 3. Contacts
        solveObstacles();
        if (settings_.selfCollision && s % std::max(1, settings_.selfCollisionInterval) == 0) solveSelfCollisions();

        // 4. Mise à jour des vitesses
        for (std::size_t i = 0; i < n; ++i) {
            if (invMass_[i] == 0.0f) {
                v_[i] = glm::vec3(0.0f);
                continue;
            }
            v_[i] = (x_[i] - prev_[i]) / dt * dampingFactor;
            // Contact inélastique : la projection hors de l'obstacle ne doit pas
            // créer de vitesse sortante, sinon le tissu « rebondit » et gagne de
            // l'énergie (conflit entre contraintes rigides et projection).
            const glm::vec3& cn = contactNormal_[i];
            const float vn = glm::dot(v_[i], cn);
            if (vn > 0.0f) v_[i] -= cn * vn;
            // Limite de vitesse : au plus une fraction de l'espacement par sous-pas.
            // Empêche l'effet « coup de fouet » des bords libres (où l'erreur
            // résiduelle de Gauss-Seidel injecte de l'énergie) et le passage à
            // travers les obstacles ou le tissu lui-même.
            const float sp = glm::length(v_[i]);
            if (sp > maxSpeed) v_[i] *= maxSpeed / sp;
        }
        if (internalBlend > 0.0f) smoothVelocities(internalBlend);
    }

    const auto t1 = std::chrono::steady_clock::now();
    stats_.stepMilliseconds = std::chrono::duration<float, std::milli>(t1 - t0).count();
}

void XpbdSolver::solveDistance(const std::vector<DistanceConstraint>& constraints, float compliance, float dt)
{
    // XPBD : Δλ = (-C - α̃λ) / (Σ w + α̃) avec α̃ = α / dt². En « small steps »
    // une seule itération par sous-pas, donc λ = 0 au départ : Δλ = -C / (w + α̃).
    const float alphaTilde = compliance / (dt * dt);
    for (const auto& c : constraints) {
        const float wa = invMass_[c.a], wb = invMass_[c.b];
        const float w = wa + wb;
        if (w == 0.0f) continue;
        const glm::vec3 d = x_[c.b] - x_[c.a];
        const float len = glm::length(d);
        if (len < kEpsilon) continue;
        const glm::vec3 grad = d / len;               // ∇_b C ; ∇_a C = -grad
        const float C = len - c.rest;
        const float dLambda = -C / (w + alphaTilde);
        x_[c.a] -= grad * (dLambda * wa);
        x_[c.b] += grad * (dLambda * wb);
    }
}

void XpbdSolver::solveSeams()
{
    for (const auto& c : seams_) {
        const float wa = invMass_[c.a], wb = invMass_[c.b];
        const float w = wa + wb;
        if (w == 0.0f) continue;
        const glm::vec3 d = x_[c.b] - x_[c.a];
        const float len = glm::length(d);
        const float rest = c.rest * seamFactor_;
        if (len <= rest || len < kEpsilon) continue;   // une couture tire, ne pousse pas
        const glm::vec3 corr = d / len * ((len - rest) / w);
        x_[c.a] += corr * wa;
        x_[c.b] -= corr * wb;
    }
}

void XpbdSolver::solveObstacles()
{
    const float thickness = material_.thickness;
    const float mu = material_.friction;
    // Rayon de recherche sur le corps : l'épaisseur + le déplacement maximal
    // d'un sous-pas (vitesse limitée), donc une particule qui vient de
    // pénétrer est toujours retrouvée.
    const float searchRadius = thickness + settings_.maxStepRatio * restSpacing_ + 0.002f;
    meshContacts_ = 0;
    for (std::size_t i = 0; i < x_.size(); ++i) {
        contactNormal_[i] = glm::vec3(0.0f);
        if (invMass_[i] == 0.0f) continue;
        glm::vec3& p = x_[i];

        for (const auto& sphere : spheres_) {
            const glm::vec3 d = p - sphere.center;
            const float dist = glm::length(d);
            const float r = sphere.radius + thickness;
            if (dist < r && dist > kEpsilon) {
                const glm::vec3 nrm = d / dist;
                const float penetration = r - dist;
                p = sphere.center + nrm * r;           // projection sur la surface décalée
                contactNormal_[i] = nrm;
                applyFriction(p, prev_[i], nrm, penetration, mu);
            }
        }

        if (meshCollider_ != nullptr) {
            geometry::ClosestHit hit;
            if (meshCollider_->closestPoint(p, searchRadius, hit)) {
                // Côté de la surface donné par la normale du triangle le plus proche.
                const float signedDist = glm::dot(p - hit.point, hit.normal);
                if (signedDist < thickness) {
                    const float penetration = thickness - signedDist;
                    p = hit.point + hit.normal * thickness;
                    applyFriction(p, prev_[i], hit.normal, penetration, mu);
                    contactNormal_[i] = hit.normal;
                    ++meshContacts_;
                }
            }
        }

        if (settings_.groundEnabled) {
            const float floorY = settings_.groundHeight + thickness;
            if (p.y < floorY) {
                const float penetration = floorY - p.y;
                p.y = floorY;
                contactNormal_[i] = glm::vec3(0, 1, 0);
                applyFriction(p, prev_[i], glm::vec3(0, 1, 0), penetration, mu);
            }
        }
    }
}

void XpbdSolver::solveSelfCollisions()
{
    // Contact particule–particule : la grille est reconstruite à chaque
    // sous-pas (O(n)) et on n'interroge que les 27 cellules voisines.
    hash_.build(x_);
    const float d0 = selfDistance_;
    const float d0Sq = d0 * d0;
    std::vector<uint32_t> candidates;
    candidates.reserve(64);

    for (uint32_t i = 0; i < x_.size(); ++i) {
        candidates.clear();
        hash_.query(x_[i], d0, candidates);

        for (const uint32_t j : candidates) {
            if (j <= i) continue;                       // chaque paire une seule fois
            const float w = invMass_[i] + invMass_[j];
            if (w == 0.0f) continue;
            const glm::vec3 d = x_[i] - x_[j];
            const float distSq = glm::dot(d, d);
            if (distSq >= d0Sq || distSq < kEpsilon * kEpsilon) continue;
            // Particules cousues ensemble : elles doivent se toucher.
            if (!seamKeys_.empty() && seamKeys_.count((static_cast<uint64_t>(i) << 32) | j)) continue;
            // Voisins proches au repos (même région du tissu) : pas une collision.
            const glm::vec3 r = rest_[i] - rest_[j];
            if (glm::dot(r, r) < d0Sq) continue;

            const float dist = std::sqrt(distSq);
            const glm::vec3 nrm = d / dist;
            const float corr = (d0 - dist) / w;
            x_[i] += nrm * (corr * invMass_[i]);
            x_[j] -= nrm * (corr * invMass_[j]);
            ++stats_.selfCollisionPairs;
        }
    }
}

void XpbdSolver::smoothVelocities(float blend)
{
    // Amortissement interne (frottement entre fibres) : chaque vitesse est
    // rapprochée de la moyenne de ses voisines. Les mouvements d'ensemble
    // (translation) sont conservés, les oscillations de haute fréquence sont
    // atténuées. Jacobi : on accumule d'abord, on applique ensuite.
    const std::size_t n = v_.size();
    velocityDelta_.assign(n, glm::vec3(0.0f));
    neighborCount_.assign(n, 0.0f);
    for (const auto& c : stretch_) {
        const glm::vec3 d = v_[c.b] - v_[c.a];
        velocityDelta_[c.a] += d;
        velocityDelta_[c.b] -= d;
        neighborCount_[c.a] += 1.0f;
        neighborCount_[c.b] += 1.0f;
    }
    for (std::size_t i = 0; i < n; ++i) {
        if (invMass_[i] > 0.0f && neighborCount_[i] > 0.0f) v_[i] += velocityDelta_[i] * (blend / neighborCount_[i]);
    }
}

void XpbdSolver::computeStrain(std::vector<float>& outStrain) const
{
    outStrain.assign(x_.size(), 0.0f);
    for (const auto& c : stretch_) {
        const float s = std::abs(glm::distance(x_[c.a], x_[c.b]) / c.rest - 1.0f);
        outStrain[c.a] = std::max(outStrain[c.a], s);
        outStrain[c.b] = std::max(outStrain[c.b], s);
    }
}

float XpbdSolver::averageStretchError() const
{
    if (stretch_.empty()) return 0.0f;
    double sum = 0.0;
    for (const auto& c : stretch_) sum += std::abs(glm::distance(x_[c.a], x_[c.b]) / c.rest - 1.0f);
    return static_cast<float>(sum / static_cast<double>(stretch_.size()));
}

} // namespace drapeo::simulation
