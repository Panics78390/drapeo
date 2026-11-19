// =============================================================================
//  garment/PatternOps — opérations sur des groupes de pièces (menu clic droit
//  de la vue patron) : copier / coller, clones en miroir ou liés, pivoter,
//  retourner, ordre d'affichage, décaler le contour, simplifier les points,
//  renfort intérieur, sélection par propriété.
//
//  Toutes ces opérations gardent les coutures et les pièces posées cohérentes.
// =============================================================================
#pragma once

#include <vector>

#include "garment/Pattern.h"

namespace drapeo::garment {

/// Copie des pièces `ids` dans un vêtement à part, avec les coutures et poses qui
/// relient ces pièces entre elles (index renumérotés dans l'ordre de `ids`).
Garment extractPanels(const Garment& g, const std::vector<int>& ids);

/// Miroir gauche / droite d'un vêtement autour de la verticale x = axis. Le placement 3D
/// passe de l'autre côté du corps (bras gauche <-> bras droit...), le droit fil suit.
void mirrorGarment(Garment& part, float axis);

/// Ajoute les pièces de `part` à `g`, décalées de `offset`. Renvoie leurs nouveaux index.
std::vector<int> appendPanels(Garment& g, const Garment& part, glm::vec2 offset);

struct CloneOptions {
    bool mirror = false;   // clone symétrique (à droite des pièces), sinon clone identique
    bool link = false;     // les clones restent liés aux originaux (édition liée)
    bool sewing = false;   // clone symétrique : coud aussi le clone sur le côté opposé des pièces voisines
    int layer = 0;         // +1 : clone au-dessus (posé par-dessus en 3D), -1 : en dessous
};
/// Clone les pièces `ids`. Renvoie les index des clones (dans l'ordre de `ids`).
std::vector<int> clonePanels(Garment& g, const std::vector<int>& ids, const CloneOptions& options);

/// Rotation des pièces autour du centre du groupe (degrés, sens trigonométrique).
void rotatePanels(Garment& g, const std::vector<int>& ids, float degrees);
/// Retourne les pièces autour du centre du groupe : horizontalement (gauche <-> droite) ou verticalement.
void flipPanels(Garment& g, const std::vector<int>& ids, bool horizontal);

/// Ordre d'affichage : 2 = premier plan, 1 = avancer, -1 = reculer, -2 = arrière-plan.
/// Renvoie les nouveaux index des pièces `ids`.
std::vector<int> reorderPanels(Garment& g, const std::vector<int>& ids, int move);

/// Contour décalé de `distance` (m) vers l'extérieur (négatif = vers l'intérieur).
std::vector<glm::vec2> offsetOutline(const std::vector<glm::vec2>& points, float distance);
/// Agrandit (distance > 0) ou rétrécit le contour d'une pièce. Les coutures sont gardées.
bool offsetPanel(Garment& g, int panel, float distance);

/// Retire les points presque alignés (écart < tolerance, m) qui ne portent pas de couture.
/// Renvoie le nombre de points retirés.
int simplifyPanel(Garment& g, int panel, float tolerance);

/// Renfort intérieur : copie du contour réduite de `inset`, cousue tout le tour sur la pièce.
/// Renvoie l'index du renfort, ou -1.
int addInnerShape(Garment& g, int panel, float inset);

/// Propriété commune pour « sélectionner les pièces semblables ».
enum class SameProperty { Color, Zone, LinkGroup };
std::vector<int> panelsLike(const Garment& g, int panel, SameProperty property);

/// Lie des pièces de même forme (même nombre de points, éventuellement retournées ou
/// pivotées d'un multiple de 90°) : la première sert de référence. Renvoie le nombre
/// de pièces liées à la référence (0 si aucune ne correspond).
int linkPanels(Garment& g, const std::vector<int>& ids);
void unlinkPanels(Garment& g, const std::vector<int>& ids);

/// Reporte sur les clones liés la nouvelle forme de `panel`, dont le contour était `oldPoints`.
/// Renvoie le nombre de pièces mises à jour.
int propagateFrom(Garment& g, int panel, const std::vector<glm::vec2>& oldPoints);
/// Compare deux états du vêtement (mêmes pièces) et reporte les changements de forme des
/// pièces liées sur leurs clones. Les déplacements, rotations et retournements ne sont pas reportés.
int propagateLinkedEdits(const Garment& before, Garment& after);

/// Transformation (rotation de `angleDeg` après miroir éventuel) d'un vecteur.
glm::vec2 linkTransform(glm::vec2 v, float angleDeg, bool mirror);

} // namespace drapeo::garment
