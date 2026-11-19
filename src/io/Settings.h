// Drapeo — réglages persistants de l'utilisateur (dernier avatar utilisé).
#pragma once

#include <string>

namespace drapeo::io {

/// Dossier contenant l'exécutable (UTF-8, sans séparateur final). Vide si inconnu.
std::string executableDirectory();

/// Dernier choix d'avatar mémorisé. Renvoie false si rien n'a encore été mémorisé ;
/// sinon `path` vaut le fichier (vide = mannequin intégré, ou fichier disparu).
bool rememberedAvatar(std::string& path);

/// Mémorise l'avatar à recharger au prochain démarrage (vide = mannequin intégré).
void rememberAvatar(const std::string& utf8Path);

/// Premier modèle 3D (.fbx puis .obj) placé à côté de l'exécutable, vide sinon.
std::string avatarNextToExecutable();

} // namespace drapeo::io
