// =============================================================================
//  io/SewingSheet — planche de couture à l'échelle 1:1 (PDF)
//
//  Valeur d'ourlet, crans, droit fil, repères d'assemblage. Soit des pages A4
//  tuilées, soit une seule page à la taille du traceur.
// =============================================================================
#pragma once

#include <string>
#include <vector>

#include "garment/Pattern.h"

namespace drapeo::io {

struct SewingOptions {
    float allowanceMm = 10.0f;   // valeur de couture ajoutée à l'extérieur
    bool a4 = true;              // false : une seule page (traceur)
    bool notches = true;
    bool grain = true;
};

/// PDF (octets). `error` est rempli si le patron est vide.
bool writeSewingPdf(const garment::Garment& g, const SewingOptions& options, std::vector<unsigned char>& pdf, std::string& error);

/// Aperçu de la première page (ou de la page traceur), image PNG.
bool writeSewingPreviewPng(const garment::Garment& g, const SewingOptions& options, std::vector<unsigned char>& png, std::string& error);

} // namespace drapeo::io
