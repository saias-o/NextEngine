#pragma once

#include <memory>
#include <string>
#include <vector>

struct JSRuntime;

namespace ne {

class JsContext;

class JsRuntime {
public:
    static JsRuntime& instance();

    JsRuntime();
    ~JsRuntime();

    JsRuntime(const JsRuntime&) = delete;
    JsRuntime& operator=(const JsRuntime&) = delete;

    std::unique_ptr<JsContext> createContext();
    void executePendingJobs();
    void beginModuleDependencyCapture(std::vector<std::string>& paths);
    void endModuleDependencyCapture();

    JSRuntime* raw() const { return runtime_; }

private:
    JSRuntime* runtime_ = nullptr;
};

} // namespace ne
