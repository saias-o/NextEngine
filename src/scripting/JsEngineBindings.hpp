#pragma once

namespace ne {

class Behaviour;
class JsContext;

class JsEngineBindings {
public:
    static void installForBehaviour(JsContext& context, Behaviour& behaviour);
};

} // namespace ne
