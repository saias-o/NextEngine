// saida_tool — CLI headless d'outillage moteur (Phase B du plan d'integration,
// PLAN_INTEGRATION_SAIDA.md). Pas de fenetre, pas de GPU : lie l'authoring-core
// (EngineManifest / SaidaOp) et sert de socle aux workers Temporal (describe,
// validate, apply-ops). Codes de sortie fiables : 0 = ok, non-zero = erreur.
//
// Sous-commandes :
//   describe-engine [--pretty]   Ecrit l'EngineManifest JSON sur stdout (B4).
//   help | --help | -h           Affiche l'aide.
//
// Convention : les diagnostics vont sur stderr, la sortie machine sur stdout.

#include "authoring/EngineManifest.hpp"
#include "authoring/SaidaOp.hpp"

#include <nlohmann/json.hpp>

#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace {

using json = nlohmann::json;

constexpr int kExitOk = 0;
constexpr int kExitUsage = 2;

int usage(std::ostream& out) {
    out << "saida_tool — SaidaEngine headless tooling\n"
           "\n"
           "Usage:\n"
           "  saida_tool describe-engine [--pretty]\n"
           "  saida_tool help\n"
           "\n"
           "Commands:\n"
           "  describe-engine   Print the EngineManifest as JSON (nodes, ops,\n"
           "                    reflected properties, contract versions).\n"
           "                    --pretty indents the output.\n";
    return kExitUsage;
}

int cmdDescribeEngine(const std::vector<std::string>& args) {
    bool pretty = false;
    for (const std::string& a : args) {
        if (a == "--pretty") {
            pretty = true;
        } else {
            std::cerr << "describe-engine: unknown option '" << a << "'\n";
            return kExitUsage;
        }
    }
    const json manifest = saida::authoring::buildEngineManifest();
    std::cout << (pretty ? manifest.dump(2) : manifest.dump()) << "\n";
    return kExitOk;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        return usage(std::cerr);
    }
    const std::string command = argv[1];
    const std::vector<std::string> rest(argv + 2, argv + argc);

    if (command == "help" || command == "--help" || command == "-h") {
        usage(std::cout);
        return kExitOk;
    }
    if (command == "describe-engine") {
        return cmdDescribeEngine(rest);
    }

    std::cerr << "saida_tool: unknown command '" << command << "'\n\n";
    return usage(std::cerr);
}
