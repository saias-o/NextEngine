#pragma once

// Mutation de scene versionnee, partagee par l'editeur et les outils headless.
// Schema JSON :
//   {
//     "opVersion": 2,
//     "type": "set_transform",
//     "sceneId": "main",
//     "payload": { ... }
//   }

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace saida::authoring {

struct SaidaOp {
    int opVersion = 0;
    std::string type;        // ex. "set_transform"
    std::string sceneId;     // vide = scene courante
    nlohmann::json payload;  // toujours un objet apres parse

    // Omet sceneId lorsqu'il designe la scene courante.
    nlohmann::json toJson() const;
};

struct SaidaOpParseResult {
    bool ok = false;
    SaidaOp op;
    std::string error;  // message stable, vide si ok
};

// Valide le schema, pas l'etat de la scene.
SaidaOpParseResult parseSaidaOp(const nlohmann::json& j);
SaidaOpParseResult parseSaidaOp(const std::string& text);

// Source unique pour le manifest et l'applier.
const std::vector<std::string>& knownOpTypes();
bool isKnownOpType(const std::string& type);

// Validation statique du payload ; l'applier valide ensuite l'etat de la scene.
std::string validateOpShape(const SaidaOp& op);

} // namespace saida::authoring
