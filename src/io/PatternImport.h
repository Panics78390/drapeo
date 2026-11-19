// =============================================================================
//  io/PatternImport — import de patrons 2D depuis SVG ou DXF
//
//  SVG : <polygon>, <polyline>, <rect> et <path> (M L H V C S Q T Z ; les arcs
//        sont remplacés par des segments). Unité déduite de width/viewBox,
//        sinon pixels CSS (96 ppp). Les attributs transform sont ignorés.
//  DXF : LWPOLYLINE, POLYLINE/VERTEX et LINE chaînées en contours (export
//        AAMA/ASTM compris). Unité lue dans $INSUNITS, sinon millimètres.
//  Seuls les contours fermés d'au moins 3 points deviennent des pièces.
// =============================================================================
#pragma once

#include <string>
#include <vector>

#include <glm/glm.hpp>

namespace drapeo::io {

/// Contours en mètres, Y vers le haut.
bool importPatternFile(const std::string& utf8Path, std::vector<std::vector<glm::vec2>>& outline, std::string& error);

bool parseSvg(const std::string& text, std::vector<std::vector<glm::vec2>>& out, std::string& error);
bool parseDxf(const std::string& text, std::vector<std::vector<glm::vec2>>& out, std::string& error);

/// Supprime les points quasi alignés ou trop proches (tolérance en mètres).
std::vector<glm::vec2> simplifyOutline(const std::vector<glm::vec2>& poly, float tolerance);

} // namespace drapeo::io
