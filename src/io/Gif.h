// =============================================================================
//  io/Gif — petit GIF animé (palette fixe, sans bibliothèque)
//
//  Suffisant pour un tour de mannequin. Ce n'est pas un master vidéo :
//  216 couleurs, images déjà réduites par l'appelant.
// =============================================================================
#pragma once

#include <cstdint>
#include <vector>

namespace drapeo::io {

/// `frames` : images RGB (3 octets par pixel, ligne du haut en premier), toutes de la même taille.
/// `delayCs` : durée d'une image en centièmes de seconde.
bool encodeGif(const std::vector<std::vector<std::uint8_t>>& frames, int width, int height, int delayCs,
               std::vector<std::uint8_t>& gif);

} // namespace drapeo::io
