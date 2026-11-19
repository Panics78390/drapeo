// =============================================================================
//  io/RasterImage — images de patrons (PNG, JPEG, BMP...) et détection des pièces
//
//  Une photo ou un scan de patron sert de calque sous l'éditeur 2D. On peut
//  aussi en extraire automatiquement les contours des pièces :
//    1. luminance, seuil automatique (méthode d'Otsu) : traits / pièces sombres
//       sur papier clair (ou l'inverse si le fond est sombre) ;
//    2. légère dilatation pour refermer les petites coupures des traits ;
//    3. remplissage depuis les bords de l'image : tout ce qui n'est pas
//       atteint est l'intérieur d'une pièce (contour fermé ou pièce pleine) ;
//    4. composantes connexes, suivi du bord extérieur, simplification
//       (Douglas-Peucker) et conversion en mètres.
// =============================================================================
#pragma once

#include <string>
#include <vector>

#include <glm/glm.hpp>

namespace drapeo::io {

struct RasterImage {
    int width = 0, height = 0;
    std::vector<unsigned char> rgba;   // 4 octets par pixel, ligne du haut en premier
    float dpi = 0.0f;                  // résolution lue dans le fichier (0 = inconnue)
};

/// Lit un fichier entier (chemin UTF-8, y compris sous Windows).
bool readFileBytes(const std::string& utf8Path, std::string& out, std::string& error);

bool loadRasterImage(const std::string& utf8Path, RasterImage& out, std::string& error);
bool decodeRasterImage(const std::string& bytes, RasterImage& out, std::string& error);

/// Résolution en points par pouce lue dans un PNG (pHYs) ou un JPEG (JFIF), sinon 0.
float detectDpi(const std::string& bytes);

struct TraceOptions {
    float metersPerPixel = 0.0254f / 150.0f;
    glm::vec2 origin{0.0f};          // position (m) du coin inférieur gauche de l'image
    int threshold = -1;              // 0..255, -1 = automatique (Otsu)
    float simplify = 0.006f;         // tolérance de simplification (m)
    float minArea = 0.0025f;         // aire minimale d'une pièce (m²) : 25 cm²
};

/// Contours des pièces détectées, en mètres, Y vers le haut.
std::vector<std::vector<glm::vec2>> traceOutlines(const RasterImage& image, const TraceOptions& options);

/// Simplification de Douglas-Peucker d'un polygone fermé.
std::vector<glm::vec2> simplifyClosed(const std::vector<glm::vec2>& poly, float tolerance);

/// Retire les contours contenus dans un contour plus grand (ligne de couture
/// dans la ligne de coupe, doublons trait + remplissage...).
void keepOutermost(std::vector<std::vector<glm::vec2>>& outlines);

} // namespace drapeo::io
