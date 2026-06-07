#pragma once

#include "core/Log.hpp"
#include <Ultralight/CAPI.h>
#include <AppCore/CAPI.h>

namespace ne {

class WebEngine {
public:
    static WebEngine& get() {
        static WebEngine instance;
        return instance;
    }

    // Doit être appelé à chaque frame (Engine::run)
    void update();

    // Accesseur au renderer Ultralight (pour créer des vues)
    ULRenderer renderer() const { return renderer_; }
    ULSession defaultSession() const { return session_; }

private:
    WebEngine();
    ~WebEngine();

    ULConfig config_;
    ULRenderer renderer_;
    ULSession session_;
};

} // namespace ne
