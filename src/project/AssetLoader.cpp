#include "project/AssetLoader.hpp"

#include "core/Log.hpp"
#include "core/Profiler.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>

namespace saida {

struct AssetHandle::Entry {
    AssetID id = kAssetInvalid;
    std::string path;
    std::atomic<AssetLoadState> state{AssetLoadState::Queued};
    mutable std::mutex mutex;
    std::vector<uint8_t> data;
    std::shared_ptr<void> payload;      // sortie du decoder (Ready + decoder fournis)
    AssetDecoder decoder;               // vide = requête Raw (bytes bruts)
    AssetPayloadKind kind = AssetPayloadKind::Raw;
    std::string error;
    std::shared_ptr<AssetLoader::Accounting> accounting;
    uint64_t accountedBytes = 0;

    ~Entry() {
        if (accounting && accountedBytes)
            accounting->residentBytes.fetch_sub(accountedBytes, std::memory_order_relaxed);
    }
};

namespace {
const std::vector<uint8_t>& emptyBytes() {
    static const std::vector<uint8_t> empty;
    return empty;
}
} // namespace

AssetID AssetHandle::id() const { return entry_ ? entry_->id : kAssetInvalid; }

AssetLoadState AssetHandle::state() const {
    return entry_ ? entry_->state.load(std::memory_order_acquire) : AssetLoadState::Failed;
}

const std::vector<uint8_t>& AssetHandle::bytes() const {
    return ready() ? entry_->data : emptyBytes();
}

std::shared_ptr<void> AssetHandle::payload() const {
    if (!entry_ || !ready()) return nullptr;
    std::lock_guard<std::mutex> lock(entry_->mutex);
    return entry_->payload;
}

std::string AssetHandle::error() const {
    if (!entry_) return "invalid asset handle";
    std::lock_guard<std::mutex> lock(entry_->mutex);
    return entry_->error;
}

uint32_t AssetHandle::referenceCount() const {
    if (!entry_) return 0;
    const long count = entry_.use_count();
    return count > 0 ? static_cast<uint32_t>(count) : 0;
}

const char* assetLoadStateName(AssetLoadState state) {
    switch (state) {
        case AssetLoadState::Queued: return "queued";
        case AssetLoadState::Loading: return "loading";
        case AssetLoadState::Ready: return "ready";
        case AssetLoadState::Failed: return "failed";
    }
    return "failed";
}

bool AssetLoader::JobOrder::operator()(const Job& a, const Job& b) const {
    if (a.priority != b.priority)
        return static_cast<uint8_t>(a.priority) < static_cast<uint8_t>(b.priority);
    return a.sequence > b.sequence;
}

AssetLoader::AssetLoader(AssetRegistry* registry, uint64_t budgetBytes)
    : registry_(registry), accounting_(std::make_shared<Accounting>()) {
    accounting_->budgetBytes.store(budgetBytes, std::memory_order_relaxed);
#ifndef __EMSCRIPTEN__
    worker_ = std::thread(&AssetLoader::workerMain, this);
#endif
}

AssetLoader::~AssetLoader() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
    }
    wake_.notify_all();
#ifndef __EMSCRIPTEN__
    if (worker_.joinable()) worker_.join();
#endif
}

AssetHandle AssetLoader::request(AssetID id, AssetLoadPriority priority,
                                 AssetPayloadKind kind, AssetDecoder decoder) {
    if (!registry_ || id == kAssetInvalid) return {};
    std::string path = registry_->getAbsolutePath(id);
    if (path.empty()) path = registry_->getPath(id);
    if (path.empty()) return {};
    return requestResolved(id, path, priority, kind, std::move(decoder));
}

AssetHandle AssetLoader::request(const std::string& path, AssetType type,
                                 AssetLoadPriority priority,
                                 AssetPayloadKind kind, AssetDecoder decoder) {
    if (!registry_ || path.empty()) return {};
    const AssetID id = registry_->registerAsset(path, type);
    std::string absolute = registry_->getAbsolutePath(id);
    if (absolute.empty()) absolute = path;
    return requestResolved(id, absolute, priority, kind, std::move(decoder));
}

AssetHandle AssetLoader::requestResolved(AssetID id, const std::string& absolutePath,
                                         AssetLoadPriority priority, AssetPayloadKind kind,
                                         AssetDecoder decoder) {
    std::lock_guard<std::mutex> lock(mutex_);
    const EntryKey key{id, kind};
    if (auto it = entries_.find(key); it != entries_.end()) {
        if (auto existing = it->second.lock()) return AssetHandle(std::move(existing));
    }

    auto entry = std::make_shared<AssetHandle::Entry>();
    entry->id = id;
    entry->path = absolutePath;
    entry->kind = kind;
    entry->decoder = std::move(decoder);
    entry->accounting = accounting_;
    entries_[key] = entry;
    jobs_.push({priority, nextSequence_++, entry});
    wake_.notify_one();
    return AssetHandle(std::move(entry));
}

std::vector<AssetHandle> AssetLoader::preload(const std::vector<AssetID>& ids,
                                              AssetLoadPriority priority) {
    std::vector<AssetHandle> handles;
    handles.reserve(ids.size());
    for (AssetID id : ids) handles.push_back(request(id, priority));
    return handles;
}

bool AssetLoader::popJob(Job& job) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (jobs_.empty()) return false;
    job = jobs_.top();
    jobs_.pop();
    return true;
}

void AssetLoader::load(const std::shared_ptr<AssetHandle::Entry>& entry) {
    entry->state.store(AssetLoadState::Loading, std::memory_order_release);
    std::ifstream file(entry->path, std::ios::binary | std::ios::ate);
    if (!file) {
        std::lock_guard<std::mutex> lock(entry->mutex);
        entry->error = "asset file not found: " + entry->path;
        entry->state.store(AssetLoadState::Failed, std::memory_order_release);
        failedTotal_.fetch_add(1, std::memory_order_relaxed);
        Log::warn(entry->error);
        return;
    }

    const std::streamoff end = file.tellg();
    if (end < 0) {
        std::lock_guard<std::mutex> lock(entry->mutex);
        entry->error = "cannot determine asset size: " + entry->path;
        entry->state.store(AssetLoadState::Failed, std::memory_order_release);
        failedTotal_.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    const uint64_t size = static_cast<uint64_t>(end);
    const uint64_t previous = accounting_->residentBytes.fetch_add(size, std::memory_order_relaxed);
    if (previous + size > accounting_->budgetBytes.load(std::memory_order_relaxed)) {
        accounting_->residentBytes.fetch_sub(size, std::memory_order_relaxed);
        std::lock_guard<std::mutex> lock(entry->mutex);
        entry->error = "asset memory budget exceeded: " + entry->path;
        entry->state.store(AssetLoadState::Failed, std::memory_order_release);
        failedTotal_.fetch_add(1, std::memory_order_relaxed);
        Log::warn(entry->error);
        return;
    }
    entry->accountedBytes = size;
    entry->data.resize(static_cast<size_t>(size));
    file.seekg(0, std::ios::beg);
    if (size && !file.read(reinterpret_cast<char*>(entry->data.data()), end)) {
        entry->data.clear();
        accounting_->residentBytes.fetch_sub(size, std::memory_order_relaxed);
        entry->accountedBytes = 0;
        std::lock_guard<std::mutex> lock(entry->mutex);
        entry->error = "failed to read asset: " + entry->path;
        entry->state.store(AssetLoadState::Failed, std::memory_order_release);
        failedTotal_.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    if (entry->decoder) {
        AssetDecodeResult decoded;
        std::string decodeError;
        const bool ok = entry->decoder(std::move(entry->data), decoded, decodeError);
        entry->data.clear();
        // Les bytes bruts sont consommés : la comptabilité bascule sur la
        // taille décodée (le budget s'applique à ce qui reste réellement
        // résident, pas au fichier transitoire).
        accounting_->residentBytes.fetch_sub(entry->accountedBytes, std::memory_order_relaxed);
        entry->accountedBytes = 0;
        if (!ok) {
            std::lock_guard<std::mutex> lock(entry->mutex);
            entry->error = "failed to decode asset: " + entry->path +
                           (decodeError.empty() ? "" : " (" + decodeError + ")");
            entry->state.store(AssetLoadState::Failed, std::memory_order_release);
        failedTotal_.fetch_add(1, std::memory_order_relaxed);
            Log::warn(entry->error);
            return;
        }
        const uint64_t decodedPrevious =
            accounting_->residentBytes.fetch_add(decoded.bytes, std::memory_order_relaxed);
        if (decodedPrevious + decoded.bytes > accounting_->budgetBytes.load(std::memory_order_relaxed)) {
            accounting_->residentBytes.fetch_sub(decoded.bytes, std::memory_order_relaxed);
            std::lock_guard<std::mutex> lock(entry->mutex);
            entry->error = "asset memory budget exceeded after decode: " + entry->path;
            entry->state.store(AssetLoadState::Failed, std::memory_order_release);
        failedTotal_.fetch_add(1, std::memory_order_relaxed);
            Log::warn(entry->error);
            return;
        }
        entry->accountedBytes = decoded.bytes;
        std::lock_guard<std::mutex> lock(entry->mutex);
        entry->payload = std::move(decoded.payload);
    }
    entry->state.store(AssetLoadState::Ready, std::memory_order_release);
}

void AssetLoader::workerMain() {
    Profiler::instance().setThreadName("asset-loader");
    for (;;) {
        Job job;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            wake_.wait(lock, [&] { return stopping_ || !jobs_.empty(); });
            if (stopping_ && jobs_.empty()) return;
            job = jobs_.top();
            jobs_.pop();
        }
        load(job.entry);
    }
}

void AssetLoader::pump() {
#ifdef __EMSCRIPTEN__
    // Pas de worker : draine toute la file (mêmes garanties de complétion par
    // frame que le worker desktop, l'API reste asynchrone).
    Job job;
    while (popJob(job)) load(job.entry);
#endif
    collectGarbage();
    const AssetLoadStats current = stats();
    SAIDA_PROFILE_COUNTER("Assets/Live", current.live);
    SAIDA_PROFILE_COUNTER("Assets/ResidentBytes", current.residentBytes);
    SAIDA_PROFILE_COUNTER("Assets/Queued", current.queued);
}

void AssetLoader::collectGarbage() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto it = entries_.begin(); it != entries_.end();) {
        if (it->second.expired()) it = entries_.erase(it);
        else ++it;
    }
}

std::atomic<uint64_t> AssetLoader::failedTotal_{0};

AssetLoadStats AssetLoader::stats() const {
    AssetLoadStats result;
    result.failedTotal = failedTotal_.load(std::memory_order_relaxed);
    result.residentBytes = accounting_->residentBytes.load(std::memory_order_relaxed);
    result.budgetBytes = accounting_->budgetBytes.load(std::memory_order_relaxed);
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& [id, weak] : entries_) {
        (void)id;
        auto entry = weak.lock();
        if (!entry) continue;
        ++result.live;
        switch (entry->state.load(std::memory_order_acquire)) {
            case AssetLoadState::Queued: ++result.queued; break;
            case AssetLoadState::Loading: ++result.loading; break;
            case AssetLoadState::Ready: ++result.ready; break;
            case AssetLoadState::Failed: ++result.failed; break;
        }
    }
    return result;
}

void AssetLoader::setBudget(uint64_t bytes) {
    accounting_->budgetBytes.store(bytes, std::memory_order_relaxed);
}

} // namespace saida
