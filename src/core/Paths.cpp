#include "core/Paths.hpp"

namespace saida {

namespace {
// Directory of the shipped game executable, set once at runtime startup.
// Empty in the editor/dev process → baked absolute paths are used instead.
std::string g_runtimeRoot;
} // namespace

void setRuntimeRoot(const std::string& dir) { g_runtimeRoot = dir; }

const std::string& runtimeRoot() { return g_runtimeRoot; }

} // namespace saida
