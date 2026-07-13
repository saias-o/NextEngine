#pragma once

#include "project/AssetRegistry.hpp"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace saida {

enum class AssetLoadState : uint8_t { Queued, Loading, Ready, Failed };
enum class AssetLoadPriority : uint8_t { Low, Normal, High, Critical };

struct AssetLoadStats {
    uint32_t live = 0;
    uint32_t queued = 0;
    uint32_t loading = 0;
    uint32_t ready = 0;
    uint32_t failed = 0;
    uint64_t residentBytes = 0;
    uint64_t budgetBytes = 0;
};

class AssetLoader;

class AssetHandle {
public:
    AssetHandle() = default;

    AssetID id() const;
    AssetLoadState state() const;
    bool ready() const { return state() == AssetLoadState::Ready; }
    bool failed() const { return state() == AssetLoadState::Failed; }
    const std::vector<uint8_t>& bytes() const;
    std::string error() const;
    uint32_t referenceCount() const;
    explicit operator bool() const { return entry_ != nullptr; }
    void reset() { entry_.reset(); }

private:
    struct Entry;
    explicit AssetHandle(std::shared_ptr<Entry> entry) : entry_(std::move(entry)) {}
    std::shared_ptr<Entry> entry_;
    friend class AssetLoader;
};

class AssetLoader {
public:
    explicit AssetLoader(AssetRegistry* registry = nullptr,
                         uint64_t budgetBytes = 256ull * 1024ull * 1024ull);
    ~AssetLoader();
    AssetLoader(const AssetLoader&) = delete;
    AssetLoader& operator=(const AssetLoader&) = delete;

    void setRegistry(AssetRegistry* registry) { registry_ = registry; }
    AssetHandle request(AssetID id,
                        AssetLoadPriority priority = AssetLoadPriority::Normal);
    AssetHandle request(const std::string& path, AssetType type = AssetType::Unknown,
                        AssetLoadPriority priority = AssetLoadPriority::Normal);
    std::vector<AssetHandle> preload(const std::vector<AssetID>& ids,
                                     AssetLoadPriority priority = AssetLoadPriority::High);

    void pump();
    void collectGarbage();
    AssetLoadStats stats() const;
    void setBudget(uint64_t bytes);

    struct Accounting {
        std::atomic<uint64_t> residentBytes{0};
        std::atomic<uint64_t> budgetBytes{0};
    };

private:
    struct Job {
        AssetLoadPriority priority = AssetLoadPriority::Normal;
        uint64_t sequence = 0;
        std::shared_ptr<AssetHandle::Entry> entry;
    };
    struct JobOrder {
        bool operator()(const Job& a, const Job& b) const;
    };

    AssetHandle requestResolved(AssetID id, const std::string& absolutePath,
                                AssetLoadPriority priority);
    bool popJob(Job& job);
    void load(const std::shared_ptr<AssetHandle::Entry>& entry);
    void workerMain();

    AssetRegistry* registry_ = nullptr;
    std::shared_ptr<Accounting> accounting_;
    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::priority_queue<Job, std::vector<Job>, JobOrder> jobs_;
    std::unordered_map<AssetID, std::weak_ptr<AssetHandle::Entry>> entries_;
    uint64_t nextSequence_ = 1;
    bool stopping_ = false;
#ifndef __EMSCRIPTEN__
    std::thread worker_;
#endif
};

const char* assetLoadStateName(AssetLoadState state);

} // namespace saida
