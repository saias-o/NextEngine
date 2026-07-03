#pragma once

// SaidaOp — le type d'operation d'edition du contrat moteur (Phase A1 du plan
// d'integration, PLAN_INTEGRATION_SAIDA.md). C'est LA representation versionnee
// et serialisable d'une mutation de scene : l'UI web, l'IA, le MCP desktop et
// les outils headless produisent des SaidaOp ; l'applier les valide puis les
// applique. Code PARTAGE (invariant 0.2) : aucune dependance editeur/reseau.
//
// Schema JSON (opVersion == kOpVersion, intra-version — invariant 0.6) :
//   {
//     "opVersion": 1,            // optionnel a l'entree (defaut courant),
//                                // toujours present a la sortie
//     "type": "set_transform",   // requis, doit etre un type connu
//     "sceneId": "main",         // optionnel (reserve multi-scenes)
//     "payload": { ... }         // optionnel, objet si present
//   }
//
// Le parse est strict et n'applique rien : erreurs stables, aucune mutation.

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace saida::authoring {

struct SaidaOp {
    int opVersion = 0;       // rempli par parse (defaut kOpVersion)
    std::string type;        // ex. "set_transform"
    std::string sceneId;     // vide = scene courante
    nlohmann::json payload;  // toujours un objet apres parse

    // Serialisation canonique (round-trip stable : parse(toJson(op)) == op).
    // "sceneId" n'est emis que s'il est non vide.
    nlohmann::json toJson() const;
};

struct SaidaOpParseResult {
    bool ok = false;
    SaidaOp op;
    std::string error;  // message stable, vide si ok
};

// Parse + validation de forme (schema, version, type connu). Ne valide PAS
// contre l'etat de la scene — c'est le role de l'applier.
SaidaOpParseResult parseSaidaOp(const nlohmann::json& j);
SaidaOpParseResult parseSaidaOp(const std::string& text);

// Registre des types d'ops du contrat. Source unique de verite : le manifest
// ("ops") et l'applier s'appuient dessus.
const std::vector<std::string>& knownOpTypes();
bool isKnownOpType(const std::string& type);

// Validation de FORME (statique, sans scene) : le payload porte-t-il les champs
// requis par le type d'op, avec les bons types JSON ? Renvoie "" si la forme est
// valide, sinon un message d'erreur stable. Ne verifie PAS la semantique liee a
// l'etat (node existant, propriete reflechie du bon type) : ca reste le role de
// l'applier sur une scene vivante. Utilisee par le dry-run (CLI, IA, gateway).
std::string validateOpShape(const SaidaOp& op);

} // namespace saida::authoring
