#include "ui/RmlUiRuntime.hpp"

#include "core/Log.hpp"
#include "core/Paths.hpp"
#include "core/Time.hpp"
#include "ui/RmlUiRenderInterface.hpp"

#include <RmlUi/Core/Core.h>
#include <RmlUi/Core/FileInterface.h>
#include <RmlUi/Core/SystemInterface.h>

#include <cstdio>
#include <algorithm>
#include <filesystem>
#include <memory>

namespace ne {

namespace {

std::vector<std::string>* gDependencyCapture = nullptr;

std::string normalizedPathString(const std::filesystem::path& path) {
    std::error_code ec;
    std::filesystem::path absolute = std::filesystem::absolute(path, ec);
    std::string out = (ec ? path : absolute).generic_string();
    return out;
}

void recordDependencyPath(const std::filesystem::path& path) {
    if (!gDependencyCapture) return;

    std::string normalized = normalizedPathString(path);
    if (std::find(gDependencyCapture->begin(), gDependencyCapture->end(), normalized) == gDependencyCapture->end()) {
        gDependencyCapture->push_back(std::move(normalized));
    }
}

class NextRmlSystemInterface final : public Rml::SystemInterface {
public:
    double GetElapsedTime() override {
        return static_cast<double>(Time::elapsed());
    }

    bool LogMessage(Rml::Log::Type type, const Rml::String& message) override {
        switch (type) {
        case Rml::Log::LT_ERROR:
        case Rml::Log::LT_ASSERT:
            Log::error("[RmlUi] ", message);
            break;
        case Rml::Log::LT_WARNING:
            Log::warn("[RmlUi] ", message);
            break;
        default:
            Log::info("[RmlUi] ", message);
            break;
        }
        return true;
    }
};

class NextRmlFileInterface final : public Rml::FileInterface {
public:
    Rml::FileHandle Open(const Rml::String& path) override {
        std::filesystem::path resolved = resolve(path);
        std::FILE* file = std::fopen(resolved.string().c_str(), "rb");
        if (file) recordDependencyPath(resolved);
        return reinterpret_cast<Rml::FileHandle>(file);
    }

    void Close(Rml::FileHandle file) override {
        if (auto* handle = asFile(file)) {
            std::fclose(handle);
        }
    }

    size_t Read(void* buffer, size_t size, Rml::FileHandle file) override {
        auto* handle = asFile(file);
        return handle ? std::fread(buffer, 1, size, handle) : 0;
    }

    bool Seek(Rml::FileHandle file, long offset, int origin) override {
        auto* handle = asFile(file);
        return handle && std::fseek(handle, offset, origin) == 0;
    }

    size_t Tell(Rml::FileHandle file) override {
        auto* handle = asFile(file);
        if (!handle) return 0;
        long pos = std::ftell(handle);
        return pos < 0 ? 0 : static_cast<size_t>(pos);
    }

private:
    static std::FILE* asFile(Rml::FileHandle file) {
        return reinterpret_cast<std::FILE*>(file);
    }

    static std::filesystem::path resolve(const Rml::String& path) {
        std::string normalized = pathFromFileUrl(path);
        std::filesystem::path candidate(normalized);
        if (candidate.is_absolute() && std::filesystem::exists(candidate)) {
            return candidate;
        }
        if (std::filesystem::exists(candidate)) {
            return std::filesystem::absolute(candidate);
        }

        std::filesystem::path assetCandidate(assetPath(normalized));
        if (std::filesystem::exists(assetCandidate)) {
            return assetCandidate;
        }

        return candidate;
    }
};

std::unique_ptr<NextRmlSystemInterface> gSystemInterface;
std::unique_ptr<NextRmlFileInterface> gFileInterface;
std::unique_ptr<RmlUiRenderInterface> gRenderInterface;

} // namespace

bool RmlUiRuntime::initialized_ = false;

bool RmlUiRuntime::ensureInitialized() {
    if (initialized_) return true;

    gSystemInterface = std::make_unique<NextRmlSystemInterface>();
    gFileInterface = std::make_unique<NextRmlFileInterface>();
    gRenderInterface = std::make_unique<RmlUiRenderInterface>();

    Rml::SetSystemInterface(gSystemInterface.get());
    Rml::SetFileInterface(gFileInterface.get());
    Rml::SetRenderInterface(gRenderInterface.get());

    initialized_ = Rml::Initialise();
    if (!initialized_) {
        Log::error("[RmlUi] initialization failed");
        gRenderInterface.reset();
        gFileInterface.reset();
        gSystemInterface.reset();
    }
    return initialized_;
}

void RmlUiRuntime::shutdown() {
    if (!initialized_) return;
    Rml::Shutdown();
    initialized_ = false;
    gRenderInterface.reset();
    gFileInterface.reset();
    gSystemInterface.reset();
}

Rml::RenderInterface* RmlUiRuntime::renderInterface() {
    return gRenderInterface.get();
}

RmlUiRenderInterface* RmlUiRuntime::renderer() {
    return gRenderInterface.get();
}

void RmlUiRuntime::beginFileDependencyCapture(std::vector<std::string>& paths) {
    paths.clear();
    gDependencyCapture = &paths;
}

void RmlUiRuntime::endFileDependencyCapture() {
    gDependencyCapture = nullptr;
}

void RmlUiRuntime::recordFileDependency(const std::string& pathOrUrl) {
    if (!gDependencyCapture) return;

    std::string normalized = pathFromFileUrl(pathOrUrl);
    std::filesystem::path candidate(normalized);
    if (candidate.is_absolute() && std::filesystem::exists(candidate)) {
        recordDependencyPath(candidate);
    } else if (std::filesystem::exists(candidate)) {
        recordDependencyPath(candidate);
    } else {
        std::filesystem::path assetCandidate(assetPath(normalized));
        if (std::filesystem::exists(assetCandidate)) recordDependencyPath(assetCandidate);
    }
}

} // namespace ne
