// =============================================================================
//  garment/Grading — gradation : une taille de plus, à partir du patron courant
//
//  Les copies ne sont pas simulées (active3D = false) : le drapé en cours ne
//  s'alourdit pas. On les réactive une par une si on veut les voir en 3D.
// =============================================================================
#pragma once

#include "garment/Pattern.h"

namespace drapeo::garment {

/// Ajoute une copie gradée de toutes les pièces (et de leurs coutures), posée à droite du patron.
/// `baseSize` et `targetSize` sont des tailles françaises (34…46). Un cran = +4 % en largeur, +0,8 % en longueur.
/// Renvoie le nombre de pièces ajoutées (0 si la taille est la même ou le patron est vide).
int appendGradedSize(Garment& g, int baseSize, int targetSize);

} // namespace drapeo::garment
