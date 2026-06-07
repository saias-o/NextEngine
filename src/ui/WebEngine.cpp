#include "WebEngine.hpp"
#include "core/Paths.hpp"
#include <string>

namespace ne {

WebEngine::WebEngine() {
    // 1. Initialiser les sous-systèmes de base (AppCore fournit des implémentations par défaut)
    ULString logPath = ulCreateString("ultralight.log");
    ulEnableDefaultLogger(logPath);
    ulDestroyString(logPath);

    ulEnablePlatformFontLoader();

    // Point FileSystem to the build directory where resources/ was copied
    std::string baseDir = std::string(NE_PROJECT_ROOT) + "/build";
    ULString basePathStr = ulCreateString(baseDir.c_str());
    ulEnablePlatformFileSystem(basePathStr);
    ulDestroyString(basePathStr);

    // 2. Configurer Ultralight
    config_ = ulCreateConfig();
    // Par défaut Ultralight utilise le rendu CPU si on ne fournit pas de GPUDriver.
    // On veut générer des pixels qu'on copiera vers Vulkan.
    
    ULString resourcePath = ulCreateString("resources/");
    ulConfigSetResourcePathPrefix(config_, resourcePath);
    ulDestroyString(resourcePath);

    // 3. Créer le Renderer
    renderer_ = ulCreateRenderer(config_);

    // 4. Créer la session (in-memory)
    ULString sessionName = ulCreateString("ne_session");
    session_ = ulCreateSession(renderer_, false, sessionName);
    ulDestroyString(sessionName);

    Log::info("WebEngine (Ultralight) initialisé.");
}

WebEngine::~WebEngine() {
    ulDestroySession(session_);
    ulDestroyRenderer(renderer_);
    ulDestroyConfig(config_);
    Log::info("WebEngine (Ultralight) détruit.");
}

void WebEngine::update() {
    // Met à jour la logique web (timers, events, js)
    ulUpdate(renderer_);
    // Rend les vues qui ont besoin d'être redessinées dans leurs bitmaps CPU respectifs
    ulRender(renderer_);
}

} // namespace ne
