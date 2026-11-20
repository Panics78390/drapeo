// =============================================================================
//  garment/PrintAtlas — texture du patron : impressions (logos, inscriptions,
//  drapeaux, bandes) et, pour l'export, couleur et aspect de chaque pièce.
//
//  La texture couvre le patron 2D entier : un point (x, y) du patron (m) a pour
//  coordonnées de texture ((x - origin.x) / largeur, (y - origin.y) / hauteur).
//  Comme les sommets du maillage gardent leurs coordonnées de patron, la même
//  texture sert au rendu 3D, à la vue patron et à l'export OBJ.
// =============================================================================
#pragma once

#include <functional>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "garment/Pattern.h"
#include "io/RasterImage.h"

namespace drapeo::garment {

struct PatternTexture {
    int width = 0, height = 0;
    std::vector<unsigned char> rgba;   // ligne du BAS en premier (comme OpenGL : v = 0 en bas)
    glm::vec2 origin{0.0f};            // coin inférieur gauche (m)
    float metersPerPixel = 0.001f;
    [[nodiscard]] bool empty() const { return width == 0 || height == 0; }
    [[nodiscard]] glm::vec2 size() const { return glm::vec2(static_cast<float>(width), static_cast<float>(height)) * metersPerPixel; }
    /// Coordonnées de texture (0..1) d'un point du patron.
    [[nodiscard]] glm::vec2 uv(glm::vec2 p) const { return (p - origin) / size(); }
};

struct PatternTextureOptions {
    float metersPerPixel = 0.001f;   // résolution voulue (1 mm par pixel)
    int maxSize = 4096;              // la résolution est réduite si la texture dépasse cette taille
    bool fillPanels = false;         // export : chaque pièce est peinte de sa couleur et de son aspect
};

/// Image d'une impression (nullptr si introuvable). `path` est le chemin enregistré dans la pièce.
using PrintImageLoader = std::function<const io::RasterImage*(const std::string& path)>;

/// Construit la texture. Sans impression ni remplissage, renvoie une texture vide (false).
bool buildPatternTexture(const Garment& g, const PrintImageLoader& load, const PatternTextureOptions& options, PatternTexture& out);

/// Taille d'une impression (m) : la hauteur suit les proportions de l'image si elle vaut 0.
glm::vec2 printSize(const Print& print, const io::RasterImage* image);

/// Coins de l'impression dans le patron (bas-gauche, bas-droit, haut-droit, haut-gauche).
void printCorners(const Panel& panel, const Print& print, const io::RasterImage* image, glm::vec2 corners[4]);

/// Couleur (sRGB 0..1) de l'aspect `finish` au point `p` du patron, pour la texture exportée.
glm::vec3 finishColor(glm::vec3 base, FabricFinish finish, glm::vec2 p);

} // namespace drapeo::garment
