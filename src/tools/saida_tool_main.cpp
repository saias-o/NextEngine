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
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

using json = nlohmann::json;

constexpr int kExitOk = 0;
constexpr int kExitInvalid = 1;  // input traite mais invalide (validation)
constexpr int kExitUsage = 2;    // mauvaise invocation / I/O

int usage(std::ostream& out) {
    out << "saida_tool — SaidaEngine headless tooling\n"
           "\n"
           "Usage:\n"
           "  saida_tool describe-engine [--pretty]\n"
           "  saida_tool validate-ops <ops.json> [--pretty]\n"
           "  saida_tool help\n"
           "\n"
           "Commands:\n"
           "  describe-engine   Print the EngineManifest as JSON (nodes, ops,\n"
           "                    reflected properties, contract versions).\n"
           "                    --pretty indents the output.\n"
           "  validate-ops      Statically validate a JSON array of SaidaOps\n"
           "                    (schema + per-type shape). Prints a JSON report;\n"
           "                    exit 0 if all valid, 1 if any invalid.\n"
           "\n"
           "Exit codes: 0 ok, 1 invalid input, 2 usage/IO error.\n";
    return kExitUsage;
}

// Lit un fichier entier ; '-' = stdin. Renvoie false + message sur echec.
bool readInput(const std::string& path, std::string& out, std::string& error) {
    if (path == "-") {
        std::ostringstream ss;
        ss << std::cin.rdbuf();
        out = ss.str();
        return true;
    }
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        error = "cannot open '" + path + "'";
        return false;
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
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

int cmdValidateOps(const std::vector<std::string>& args) {
    std::string path;
    bool pretty = false;
    for (const std::string& a : args) {
        if (a == "--pretty") {
            pretty = true;
        } else if (!a.empty() && a[0] == '-' && a != "-") {
            std::cerr << "validate-ops: unknown option '" << a << "'\n";
            return kExitUsage;
        } else if (path.empty()) {
            path = a;
        } else {
            std::cerr << "validate-ops: unexpected extra argument '" << a << "'\n";
            return kExitUsage;
        }
    }
    if (path.empty()) {
        std::cerr << "validate-ops: missing <ops.json> (use '-' for stdin)\n";
        return kExitUsage;
    }

    std::string text, ioError;
    if (!readInput(path, text, ioError)) {
        std::cerr << "validate-ops: " << ioError << "\n";
        return kExitUsage;
    }

    const json parsed = json::parse(text, nullptr, /*allow_exceptions=*/false);
    if (parsed.is_discarded()) {
        std::cerr << "validate-ops: input is not valid JSON\n";
        return kExitUsage;
    }
    // Accepte un tableau d'ops, ou une op unique (enveloppee en tableau).
    json ops = parsed.is_array() ? parsed : json::array({parsed});

    json errors = json::array();
    for (std::size_t i = 0; i < ops.size(); ++i) {
        const auto res = saida::authoring::parseSaidaOp(ops[i]);
        std::string error = res.ok ? saida::authoring::validateOpShape(res.op) : res.error;
        if (!error.empty())
            errors.push_back(json{{"index", i}, {"error", error}});
    }

    const bool allValid = errors.empty();
    json report{{"ok", allValid}, {"count", ops.size()},
                {"invalid", errors.size()}, {"errors", std::move(errors)}};
    std::cout << (pretty ? report.dump(2) : report.dump()) << "\n";
    return allValid ? kExitOk : kExitInvalid;
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
    if (command == "validate-ops") {
        return cmdValidateOps(rest);
    }

    std::cerr << "saida_tool: unknown command '" << command << "'\n\n";
    return usage(std::cerr);
}
