#include "io/MeshImport.h"

#include <algorithm>
#include <cmath>

#include <ufbx.h>

namespace drapeo::io {

bool importMesh(const std::string& utf8Path, ImportedMesh& out, std::string& error)
{
    ufbx_load_opts opts{};
    opts.target_axes = ufbx_axes_right_handed_y_up;   // convention du logiciel : Y vers le haut
    opts.target_unit_meters = 1.0f;                   // tout en mètres
    opts.space_conversion = UFBX_SPACE_CONVERSION_MODIFY_GEOMETRY;
    opts.obj_axes = ufbx_axes_right_handed_y_up;      // OBJ : pas d'unité dans le format
    opts.generate_missing_normals = true;

    ufbx_error err{};
    ufbx_scene* scene = ufbx_load_file(utf8Path.c_str(), &opts, &err);
    if (scene == nullptr) {
        char text[512];
        ufbx_format_error(text, sizeof(text), &err);
        error = text;
        return false;
    }

    out = {};
    const auto slash = utf8Path.find_last_of("/\\");
    out.name = slash == std::string::npos ? utf8Path : utf8Path.substr(slash + 1);

    std::vector<uint32_t> triIndices;
    for (size_t n = 0; n < scene->nodes.count; ++n) {
        const ufbx_node* node = scene->nodes.data[n];
        if (node->mesh == nullptr || !node->visible) continue;
        const ufbx_mesh* mesh = node->mesh;
        ++out.sourceObjects;

        // Sommets topologiques partagés : le maillage reste soudé (utile pour
        // les normales lissées et pour le BVH).
        const auto base = static_cast<uint32_t>(out.positions.size());
        for (size_t v = 0; v < mesh->vertices.count; ++v) {
            const ufbx_vec3 p = ufbx_transform_position(&node->geometry_to_world, mesh->vertices.data[v]);
            out.positions.emplace_back(static_cast<float>(p.x), static_cast<float>(p.y), static_cast<float>(p.z));
        }

        triIndices.resize(mesh->max_face_triangles * 3);
        for (size_t f = 0; f < mesh->faces.count; ++f) {
            const ufbx_face face = mesh->faces.data[f];
            const uint32_t tris = ufbx_triangulate_face(triIndices.data(), triIndices.size(), mesh, face);
            for (uint32_t k = 0; k < tris * 3; ++k) {
                out.indices.push_back(base + mesh->vertex_indices.data[triIndices[k]]);
            }
        }
    }
    ufbx_free_scene(scene);

    if (out.indices.empty()) {
        error = "Le fichier ne contient aucun maillage visible.";
        return false;
    }
    float lo = 1e30f, hi = -1e30f;
    for (const auto& p : out.positions) {
        lo = std::min(lo, p.y);
        hi = std::max(hi, p.y);
    }
    out.sourceHeight = hi - lo;
    return true;
}

float fitToGround(std::vector<glm::vec3>& positions, const FitOptions& options)
{
    if (positions.empty()) return 0.0f;
    glm::vec3 lo(1e30f), hi(-1e30f);
    for (const auto& p : positions) {
        lo = glm::min(lo, p);
        hi = glm::max(hi, p);
    }
    const float height = hi.y - lo.y;
    float target = options.targetHeight;
    if (target <= 0.0f) {
        // Échelle d'origine si elle ressemble à un humain en mètres ; sinon 1,75 m.
        target = (height > 0.3f && height < 3.0f) ? height : 1.75f;
    }
    const float scale = height > 1e-6f ? target / height : 1.0f;
    const glm::vec3 pivot((lo.x + hi.x) * 0.5f, lo.y, (lo.z + hi.z) * 0.5f);
    const float a = glm::radians(options.rotationYDegrees);
    const float c = std::cos(a), s = std::sin(a);
    for (auto& p : positions) {
        const glm::vec3 q = (p - pivot) * scale;
        p = glm::vec3(c * q.x + s * q.z, q.y, -s * q.x + c * q.z);
    }
    return target;
}

} // namespace drapeo::io
