// =============================================================================
//  mcp/McpServer — serveur MCP (Model Context Protocol) de Drapeo.
//
//  Un assistant (Claude Desktop...) pilote Drapeo par des « outils » : créer des
//  pièces, les coudre, arrondir un coin, simuler le drapé, exporter en OBJ.
//  Le serveur travaille sur un fichier projet .drapeo : chaque outil le relit,
//  le modifie et le réenregistre. Drapeo, s'il a ce projet ouvert, le recharge
//  tout seul : on voit le vêtement changer en direct.
//
//  Transport : JSON-RPC 2.0 sur l'entrée / la sortie standard, un message par ligne.
//  Unités échangées : centimètres et degrés.
// =============================================================================
#pragma once

#include <string>

#include "mcp/Json.h"

namespace drapeo::mcp {

class Server {
public:
    explicit Server(std::string projectPath, std::string drapeoExe = {});

    /// Traite un message JSON-RPC. Renvoie false s'il n'y a rien à répondre (notification).
    bool handle(const Json& message, Json& response);
    /// Traite une ligne de texte (message JSON-RPC). Renvoie la réponse, ou "" s'il n'y en a pas.
    std::string handleLine(const std::string& line);

    [[nodiscard]] const std::string& projectPath() const { return projectPath_; }

    /// Liste des outils (pour tools/list).
    static Json toolList();
    /// Appelle un outil. Renvoie false (et `error`) si l'outil échoue.
    bool callTool(const std::string& name, const Json& args, std::string& text, std::string& error);

private:
    std::string projectPath_;
    std::string drapeoExe_;
};

/// Projet par défaut : Documents/Drapeo/mcp.drapeo (le dossier est créé si besoin).
std::string defaultProjectPath();

} // namespace drapeo::mcp
