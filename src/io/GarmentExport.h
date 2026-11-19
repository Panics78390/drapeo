// io/GarmentExport — export du vêtement drapé en OBJ (positions, normales, UV = patron 2D)
#pragma once

#include <string>
#include <vector>

#include <glm/glm.hpp>

namespace drapeo::io {

struct ExportMesh {
    const std::vector<glm::vec3>* positions = nullptr;
    const std::vector<glm::vec3>* normals = nullptr;
    const std::vector<glm::vec2>* uvs = nullptr;
    const std::vector<uint32_t>* indices = nullptr;
    const std::vector<int>* panelOfVertex = nullptr;     // optionnel : un groupe par pièce
    std::vector<std::string> panelNames;
};

bool exportObj(const std::string& utf8Path, const ExportMesh& mesh, std::string& error);

} // namespace drapeo::io
