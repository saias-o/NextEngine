#pragma once

namespace saida {

class Behaviour;
class JsContext;

class JsEngineBindings {
public:
    static void installForBehaviour(JsContext& context, Behaviour& behaviour);
};

} // namespace saida
