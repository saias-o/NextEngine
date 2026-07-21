// P0.4 : politique de permissions des scripts publics (contrat capability).
//
// Un script Saida n'a AUCUNE autorité ambiante au-delà des globals que le
// moteur installe explicitement : `console` (JsContext) et les capacités
// gameplay `node/time/input/tree/assets/audio/physics/storage`
// (JsEngineBindings). Pas de réseau, pas d'accès OS/processus/env, pas de
// filesystem hors `storage` (quotas) et des imports confinés à la racine
// projet (prouvé par saida_js_safety_tests), budget temps interruptible.
//
// Ce test verrouille la surface : le delta entre un contexte QuickJS nu et un
// contexte moteur doit être EXACTEMENT l'allowlist — toute nouvelle autorité
// ambiante (ou une disparition) casse le test et doit passer par SPEC.md.
#include "scene/Node.hpp"
#include "scene/ReflectedTypes.hpp"
#include "behaviours/RotatorBehaviour.hpp"
#include "scripting/JsContext.hpp"
#include "scripting/JsEngineBindings.hpp"
#include "scripting/JsRuntime.hpp"

#include <quickjs.h>

#include <cassert>
#include <cstdio>
#include <set>
#include <sstream>
#include <string>

using namespace saida;

namespace {

// Own properties of globalThis, newline-joined, via a raw JS_Eval (works on a
// bare context as well as an engine one).
std::set<std::string> globalNames(JSContext* ctx) {
    const char* code = "Object.getOwnPropertyNames(globalThis).sort().join('\\n')";
    JSValue result = JS_Eval(ctx, code, strlen(code), "<policy>", JS_EVAL_TYPE_GLOBAL);
    assert(!JS_IsException(result));
    const char* text = JS_ToCString(ctx, result);
    assert(text);
    std::set<std::string> names;
    std::stringstream stream((std::string(text)));
    std::string line;
    while (std::getline(stream, line))
        if (!line.empty()) names.insert(line);
    JS_FreeCString(ctx, text);
    JS_FreeValue(ctx, result);
    return names;
}

} // namespace

int main() {
    registerReflectedTypes();
    JsRuntime& runtime = JsRuntime::instance();

    // Baseline : contexte QuickJS nu (le langage seul, sans quickjs-libc).
    JSContext* bare = JS_NewContext(runtime.raw());
    assert(bare);
    const std::set<std::string> baseline = globalNames(bare);

    // Le langage nu ne doit déjà fournir aucune autorité système : si une mise
    // à jour de QuickJS ajoutait un de ces noms au langage, la politique doit
    // être re-décidée explicitement.
    for (const char* forbidden : {"std", "os", "process", "require", "fetch",
                                  "XMLHttpRequest", "WebSocket", "setTimeout",
                                  "setInterval", "open", "exec", "read", "write"}) {
        assert(baseline.find(forbidden) == baseline.end());
    }

    // Contexte moteur complet (mêmes bindings que les scripts de gameplay).
    Node node("Hero");
    auto* rotor = node.addBehaviour<RotatorBehaviour>();
    {
        JsContext ctx(runtime);
        JsEngineBindings::installForBehaviour(ctx, *rotor);
        const std::set<std::string> engine = globalNames(ctx.raw());

        // Allowlist exacte : les capacités du moteur, rien d'autre.
        const std::set<std::string> allowed = {
            "console",  // JsContext
            "node", "time", "input", "tree", "assets", "audio", "physics",
            "storage",  // JsEngineBindings
        };

        std::set<std::string> added;
        for (const std::string& name : engine)
            if (baseline.find(name) == baseline.end()) added.insert(name);

        for (const std::string& name : added) {
            if (allowed.find(name) == allowed.end()) {
                std::printf("FAIL: unexpected ambient global '%s' — update the "
                            "policy in SPEC.md before exposing it\n", name.c_str());
                return 1;
            }
        }
        for (const std::string& name : allowed) {
            if (added.find(name) == added.end()) {
                std::printf("FAIL: expected capability '%s' missing from the "
                            "script surface\n", name.c_str());
                return 1;
            }
        }

        // Les échappatoires classiques restent absentes du contexte moteur.
        assert(ctx.eval("if (typeof std !== 'undefined') throw new Error('std');"));
        assert(ctx.eval("if (typeof os !== 'undefined') throw new Error('os');"));
        assert(ctx.eval("if (typeof fetch !== 'undefined') throw new Error('fetch');"));
        assert(ctx.eval("if (typeof require !== 'undefined') throw new Error('require');"));
    }

    JS_FreeContext(bare);
    std::printf("PASS: script permission policy (capability surface locked)\n");
    return 0;
}
