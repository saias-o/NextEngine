// P0.4 : contrat asynchrone du storage. `storage.flush()` retourne une
// Promise résolue `true` quand les écritures en attente sont durables (jamais
// de rejet). Sur desktop les écritures sont déjà durables au save() : la
// promesse se résout au prochain drain de microtasks. Sur web la résolution
// vient du callback FS.syncfs — même contrat, prouvé par le harnais
// navigateur de WitnessGame (PASS émis après flush, save relue au reload).
#include "scene/Node.hpp"
#include "scene/ReflectedTypes.hpp"
#include "scene/RotatorBehaviour.hpp"
#include "scripting/JsContext.hpp"
#include "scripting/JsEngineBindings.hpp"
#include "scripting/JsRuntime.hpp"

#include <quickjs.h>

#include <cassert>
#include <cstdio>

using namespace saida;

namespace {

int readGlobalInt(JsContext& ctx, const char* name) {
    JSContext* raw = ctx.raw();
    JSValue global = JS_GetGlobalObject(raw);
    JSValue v = JS_GetPropertyStr(raw, global, name);
    int32_t out = -1;
    JS_ToInt32(raw, &out, v);
    JS_FreeValue(raw, v);
    JS_FreeValue(raw, global);
    return out;
}

} // namespace

int main() {
    registerReflectedTypes();
    JsRuntime& runtime = JsRuntime::instance();

    Node node("Hero");
    auto* rotor = node.addBehaviour<RotatorBehaviour>();

    JsContext ctx(runtime);
    JsEngineBindings::installForBehaviour(ctx, *rotor);

    // flush() rend une vraie Promise; la réaction .then court après le drain
    // de microtasks, avec `true` en valeur de résolution.
    assert(ctx.eval(
        "globalThis.state = 0;"
        "const p = storage.flush();"
        "if (!(p instanceof Promise)) throw new Error('flush must return a Promise');"
        "p.then(function(ok) { globalThis.state = ok === true ? 1 : 2; });"));
    // ctx.eval draine déjà les jobs en sortie; un drain explicite reste sûr.
    assert(ctx.executePendingJobs());
    assert(readGlobalInt(ctx, "state") == 1);

    // Deux flushes en vol se résolvent indépendamment.
    assert(ctx.eval(
        "globalThis.count = 0;"
        "storage.flush().then(function() { globalThis.count++; });"
        "storage.flush().then(function() { globalThis.count++; });"));
    assert(ctx.executePendingJobs());
    assert(readGlobalInt(ctx, "count") == 2);

    std::printf("PASS: storage.flush async durability contract\n");
    return 0;
}
