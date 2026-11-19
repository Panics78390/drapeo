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

/// Avatar du projet : fichier FBX / OBJ (vide = mannequin intégré), taille, rotation.
struct ProjectAvatar {
    bool present = false;   // le fichier contenait une ligne « avatar »
    std::string path;
    float height = 1.75f;
    float rotationDeg = 0.0f;
};

std::string writeProject(const garment::Garment& g, const ProjectAvatar* avatar = nullptr);
bool parseProject(const std::string& text, garment::Garment& out, std::string& error, ProjectAvatar* avatar = nullptr);

bool saveProject(const std::string& path, const garment::Garment& g, std::string& error, const ProjectAvatar* avatar = nullptr);
bool loadProject(const std::string& path, garment::Garment& out, std::string& error, ProjectAvatar* avatar = nullptr);

} // namespace drapeo::io
