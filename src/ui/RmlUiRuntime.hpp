#pragma once

#include <string>
#include <vector>

namespace Rml {
class RenderInterface;
}

namespace saida {

class RmlUiRenderInterface;

class RmlUiRuntime {
public:
    static bool ensureInitialized();
    static void shutdown();
    static Rml::RenderInterface* renderInterface();
    static RmlUiRenderInterface* renderer();
    static void beginFileDependencyCapture(std::vector<std::string>& paths);
    static void endFileDependencyCapture();
    static void recordFileDependency(const std::string& pathOrUrl);

private:
    static bool initialized_;
};

} // namespace saida
