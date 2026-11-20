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

/// Fendre et écarter : la partie du contour entre les points `fromPoint` et `hingePoint` (dans le sens
/// des index) pivote de `degrees` autour du point charnière, ce qui ouvre la pièce en pointe au point de départ.
/// Le sens d'ouverture est choisi pour agrandir la pièce. Renvoie false si les points ne conviennent pas.
bool slashAndSpread(Garment& g, int panel, int fromPoint, int hingePoint, float degrees);

/// Arrondit le coin `point` : il est remplacé par un arc de cercle tangent aux deux bords, qui
/// commence à `tangentLength` (m) du coin sur chaque bord (limité par la longueur des bords).
/// Renvoie l'index du premier point de l'arc, ou -1.
int roundCorner(Garment& g, int panel, int point, float tangentLength, int segments = 10);
/// Longueur de tangente qui donne l'arrondi le plus doux possible au coin `point` (« convertir en point de courbe »).
float maxCornerTangent(const Garment& g, int panel, int point);

/// Courbe qui contient le bord `edge` : suite de bords qui tournent de moins de `maxTurnDeg` à chaque point.
/// `first` = premier point, `count` = nombre de bords.
void curveAround(const Garment& g, int panel, int edge, int& first, int& count, float maxTurnDeg = 35.0f);
/// Coupe la courbe qui contient `edge` en `parts` morceaux de même longueur : des points sont ajoutés
/// aux endroits de coupe (la forme ne change pas). `cuts` reçoit les endroits de coupe.
/// Renvoie false si aucun point n'a été ajouté.
bool splitCurve(Garment& g, int panel, int edge, int parts, std::vector<glm::vec2>* cuts = nullptr);

/// Pince au point `point` : deux points écartés de `width` le long du contour et une pointe
/// enfoncée de `depth` vers l'intérieur de la pièce. Renvoie l'index de la pointe, ou -1.
int addDart(Garment& g, int panel, int point, float depth, float width);
/// Aligne le point sur son voisin (précédent si `next` est faux) : horizontalement ou verticalement.
bool alignPoint(Garment& g, int panel, int point, bool horizontal, bool next);

/// Déplie une demi-pièce : la pièce est complétée par son miroir autour de la droite du bord `edge`
/// (le pli). Si le pli est vertical, la pièce devient symétrique autour de lui.
bool unfoldPanel(Garment& g, int panel, int edge);

/// Contour d'une pièce repéré par la longueur parcourue depuis le point 0 (m), dans le sens des index.
float contourLength(const Garment& g, int panel);
float contourParam(const Garment& g, int panel, int edge, float t);   // position du point (edge, t)
glm::vec2 contourPoint(const Garment& g, int panel, float s);          // point à la longueur s (modulo le tour)

/// Couture libre : la portion de contour de A qui part de `sA` et parcourt `lenA` (m, négatif = sens
/// inverse des index) est cousue sur la portion de B qui part de `sB` et parcourt `lenB`. Les débuts
/// sont cousus ensemble. Des points sont ajoutés aux extrémités si besoin. Renvoie l'index de la couture, ou -1.
int sewFree(Garment& g, int panelA, float sA, float lenA, int panelB, float sB, float lenB);

/// Allonger (amount > 0) ou raccourcir (amount < 0) une pièce à la ligne horizontale y = `atY` (m) :
/// tout ce qui est au-dessus de la ligne monte ou descend de `amount`. En raccourcissant, les points
/// situés dans la bande retirée sont posés sur la ligne. Les coutures, poches et impressions suivent.
/// Renvoie false si la ligne ne traverse pas la pièce.
bool lengthenPanel(Garment& g, int panel, float atY, float amount);

/// Coupe une pièce en deux le long du segment `p0`-`p1`, deux points de son contour (ajoutés s'il le
/// faut). La pièce garde le morceau qui va de p0 à p1 dans l'ordre du contour ; l'autre morceau devient
/// une nouvelle pièce (mêmes propriétés, même place en 3D). Les coutures sont gardées : une couture qui
/// passe par un point de coupe est partagée entre les deux morceaux, en proportion de sa longueur.
/// Les pièces posées et les impressions vont sur le morceau qui les porte. `sewTogether` : les deux
/// morceaux sont recousus le long de la coupe. Renvoie l'index de la nouvelle pièce, ou -1 (et `error`).
int splitPanel(Garment& g, int panel, glm::vec2 p0, glm::vec2 p1, bool sewTogether, std::string& error);
/// Même chose le long de la droite qui passe par `a` et `b` : la coupe va entre les deux bords de la
/// pièce traversés de part et d'autre du milieu de [a, b].
int splitPanelAlongLine(Garment& g, int panel, glm::vec2 a, glm::vec2 b, bool sewTogether, std::string& error);
/// Points où la droite (a, b) entre et sort de la pièce autour du milieu de [a, b] ; false s'il n'y en a pas.
bool lineCut(const Panel& panel, glm::vec2 a, glm::vec2 b, glm::vec2& p0, glm::vec2& p1);

/// Bande de soufflet (épaisseur d'une poche, d'un sac) : crée une bande de largeur `width` aussi longue
/// que la portion de contour de `panel` qui part de `s0` et parcourt `len` (comme sewFree), et coud son
/// grand bord du bas sur cette portion. En 3D, la bande est posée debout le long de la portion, vers le corps
/// (derrière la pièce, là où sera le fond de la poche).
/// Renvoie l'index de la bande, ou -1 (et `error`).
int addGussetStrip(Garment& g, int panel, float s0, float len, float width, std::string& error);
/// Coud le grand bord du haut d'une bande de soufflet sur une portion de contour d'une autre pièce
/// (son début contre le début de la bande). Renvoie l'index de la couture, ou -1.
int sewGussetTop(Garment& g, int strip, int panel, float s0, float len);

/// Fermeture éclair posée sur `base` le long du segment a-b (coordonnées de la base, gardé à l'intérieur de la
/// pièce) : une bande de largeur `width` à l'aspect « Fermeture éclair » (dents dessinées), cousue tout autour,
/// et un curseur avec sa tirette au bout `b`, cousu par le haut. Renvoie l'index de la bande, ou -1 (et `error`).
int addZipper(Garment& g, int base, glm::vec2 a, glm::vec2 b, float width, std::string& error);

/// Poche tracée directement sur `base` : rectangle [lo, hi] (coordonnées du patron, centre sur la pièce),
/// cousu selon `kind` (poche ouverte en haut, rabat, tout le tour) et dessiné à sa place, par-dessus la base.
/// `withFlap` ajoute un rabat cousu en haut, au-dessus de l'ouverture. Renvoie l'index de la poche
/// (le rabat, s'il y en a un, suit), ou -1 (et `error`).
int addPocket(Garment& g, int base, glm::vec2 lo, glm::vec2 hi, AttachKind kind, bool withFlap, std::string& error);

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
