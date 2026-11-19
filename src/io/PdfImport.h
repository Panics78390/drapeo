// =============================================================================
//  io/PdfImport — patrons au format PDF
//
//  Les patrons vendus en PDF sont presque toujours vectoriels : les contours
//  des pièces sont des tracés (m, l, c, re, h...) dans les flux de contenu.
//  On décompresse les flux (FlateDecode), on rejoue les opérateurs de tracé
//  avec la matrice de transformation (cm, q, Q), on aplatit les courbes de
//  Bézier et on garde les contours fermés, en mètres (1 pt = 1/72 pouce).
//
//  Un PDF scanné ne contient qu'une image : on renvoie alors la première
//  image JPEG trouvée, qui sera importée comme calque.
// =============================================================================
#pragma once

#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "io/RasterImage.h"

namespace drapeo::io {

struct PdfPatternResult {
    std::vector<std::vector<glm::vec2>> outlines;   // contours fermés (m), Y vers le haut
    std::string jpeg;                               // plus grande image, si elle est en JPEG (PDF scanné)
    RasterImage image;                              // plus grande image, si elle est en pixels compressés
    std::size_t paths = 0;                          // tracés lus (fermés ou non)
    glm::vec2 pageSize{0.0f};                       // format de la première page (m), 0 si inconnu
};

bool parsePdf(const std::string& bytes, PdfPatternResult& out, std::string& error);

/// Rejoue un flux de contenu PDF décompressé (exposé pour les tests).
void parsePdfContent(const std::string& content, std::vector<std::vector<glm::vec2>>& closedPaths, std::size_t& pathCount);

} // namespace drapeo::io
