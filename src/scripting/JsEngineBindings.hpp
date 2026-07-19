#pragma once

struct JSContext;  // quickjs

namespace saida {

class Behaviour;
class JsContext;

class JsEngineBindings {
public:
    static void installForBehaviour(JsContext& context, Behaviour& behaviour);

    // Oublie les résolveurs de storage.flush() encore en vol pour ce contexte
    // (teardown/hot-reload) : le callback IDBFS tardif ne touchera jamais un
    // contexte détruit. No-op sur desktop. Appelé par ~JsContext.
    static void dropPendingFlushes(JSContext* context);
};

} // namespace saida
