// =============================================================================
//  io/ViewCapture — capture d'une vue 3D sans fenêtre ni OpenGL.
//
//  Petit rendu logiciel (tampon de profondeur, éclairage diffus) : sert au serveur
//  MCP pour montrer à l'assistant le vêtement drapé sur l'avatar, de face, de
//  profil ou de dos, sous forme d'image PNG.
// =============================================================================
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>

namespace drapeo::io {

struct CaptureMesh {
    const std::vector<glm::vec3>* positions = nullptr;
    const std::vector<uint32_t>* indices = nullptr;
    const std::vector<glm::vec3>* vertexColors = nullptr;   // facultatif (sinon `color`)
    const std::vector<int>* partOfVertex = nullptr;          // facultatif : pièce de chaque sommet (contours)
    glm::vec3 color{0.7f};
    float depthBias = 0.0f;   // > 0 : passe devant à profondeur égale (tissu posé sur le corps)
    bool frame = true;        // compte dans le cadrage
};

struct CaptureOptions {
    float yawDeg = 0.0f;     // 0 : face, 90 : profil (côté gauche du personnage), 180 : dos
    float pitchDeg = 0.0f;   // > 0 : vue légèrement plongeante
    int width = 600;         // hauteur = 1,4 × largeur
    float margin = 0.06f;    // marge autour du cadrage (fraction)
};

/// Rend les maillages en RGB (3 octets par pixel, lignes de haut en bas).
void renderView(const std::vector<CaptureMesh>& meshes, const CaptureOptions& options, int& width, int& height,
                std::vector<uint8_t>& rgb);

/// Met plusieurs images de même hauteur côte à côte.
void joinSideBySide(const std::vector<std::vector<uint8_t>>& images, const std::vector<int>& widths, int height,
                    std::vector<uint8_t>& rgb, int& width);

/// Encode une image RGB en PNG (en mémoire).
bool encodePng(const std::vector<uint8_t>& rgb, int width, int height, std::vector<uint8_t>& png);

/// Écrit des octets dans un fichier (chemin UTF-8).
bool writeBytes(const std::string& path, const std::vector<uint8_t>& bytes, std::string& error);

/// Base 64 (pour joindre une image à un message).
std::string base64(const std::vector<uint8_t>& bytes);

} // namespace drapeo::io
