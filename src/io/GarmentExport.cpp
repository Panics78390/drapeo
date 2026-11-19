#include "io/GarmentExport.h"

#include <filesystem>
#include <fstream>

namespace drapeo::io {

bool exportObj(const std::string& utf8Path, const ExportMesh& m, std::string& error)
{
#if defined(_WIN32)
    const std::filesystem::path path(std::u8string(utf8Path.begin(), utf8Path.end()));
#else
    const std::filesystem::path path(utf8Path);
#endif
    std::ofstream out(path);
    if (!out) {
        error = "Impossible d'écrire le fichier.";
        return false;
    }
    out << "# Drapeo - vêtement drapé (mètres, Y vers le haut). UV = coordonnées du patron en mètres.\n";
    for (const auto& p : *m.positions) out << "v " << p.x << ' ' << p.y << ' ' << p.z << '\n';
    for (const auto& n : *m.normals) out << "vn " << n.x << ' ' << n.y << ' ' << n.z << '\n';
    for (const auto& t : *m.uvs) out << "vt " << t.x << ' ' << t.y << '\n';
    int currentPanel = -2;
    for (std::size_t t = 0; t + 2 < m.indices->size(); t += 3) {
        const uint32_t a = (*m.indices)[t] + 1, b = (*m.indices)[t + 1] + 1, c = (*m.indices)[t + 2] + 1;
        if (m.panelOfVertex != nullptr) {
            const int p = (*m.panelOfVertex)[a - 1];
            if (p != currentPanel) {
                currentPanel = p;
                const std::string name = p >= 0 && static_cast<std::size_t>(p) < m.panelNames.size()
                                             ? m.panelNames[static_cast<std::size_t>(p)] : "piece";
                std::string safe;
                for (char ch : name) safe += (ch == ' ' ? '_' : ch);
                out << "g " << safe << '\n';
            }
        }
        out << "f " << a << '/' << a << '/' << a << ' ' << b << '/' << b << '/' << b << ' ' << c << '/' << c << '/' << c << '\n';
    }
    return true;
}

} // namespace drapeo::io
