// =============================================================================
//  io/ProjectFile — projet Drapeo (.drapeo)
//
//  Fichier texte lisible : pièces (nom, couleur, placement, contour en mètres)
//  et coutures (suites de bords). Sert à enregistrer un assemblage et à le
//  rouvrir tel quel.
// =============================================================================
#pragma once

#include <string>

#include "garment/Pattern.h"

namespace drapeo::io {

std::string writeProject(const garment::Garment& g);
bool parseProject(const std::string& text, garment::Garment& out, std::string& error);

bool saveProject(const std::string& path, const garment::Garment& g, std::string& error);
bool loadProject(const std::string& path, garment::Garment& out, std::string& error);

} // namespace drapeo::io
