// =============================================================================
//  simulation/GarmentSetup — réglages de simulation venant des pièces du patron
//  (rétrécissement, pièces figées / renforcées, épaisseurs, couches, pression).
//  Partagé par l'application et par le serveur MCP.
// =============================================================================
#pragma once

#include "garment/GarmentMesher.h"
#include "garment/Pattern.h"
#include "simulation/XpbdSolver.h"

namespace drapeo::simulation {

/// Retrait trame / chaîne des pièces : longueurs de repos multipliées selon le droit fil.
/// À appeler une seule fois, juste après XpbdSolver::build.
void applyShrinkage(XpbdSolver& solver, const garment::Garment& g, const garment::GarmentMesh& gm);

/// Lignes de pli des pièces (arêtes de pli du maillage). À appeler juste après XpbdSolver::build.
void applyFolds(XpbdSolver& solver, const garment::GarmentMesh& gm);

/// Pièces figées, renforcées, épaisseur de contact, couche et pression (peut être rappelé).
void applyPanelProperties(XpbdSolver& solver, const garment::Garment& g, const garment::GarmentMesh& gm);

} // namespace drapeo::simulation
