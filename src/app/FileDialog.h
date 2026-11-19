// app/FileDialog — boîtes « Ouvrir » / « Enregistrer sous » natives (Windows).
// Sur les autres systèmes, hasNativeFileDialog() est faux et l'interface
// propose un champ de saisie du chemin.
#pragma once

#include <string>

namespace drapeo::app {

/// filter : "Description|*.ext1;*.ext2" (UTF-8). Renvoie le chemin UTF-8 ou "" si annulé.
std::string openFileDialog(const std::string& title, const std::string& filter);
std::string saveFileDialog(const std::string& title, const std::string& filter, const std::string& defaultExtension);
[[nodiscard]] bool hasNativeFileDialog();

} // namespace drapeo::app
