#pragma once

#include "project/AssetRegistry.hpp"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
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

// Distingue les requêtes du même AssetID selon la forme demandée : les bytes
// bruts (JS assets.load) et un payload décodé (texture, mesh) coexistent sans
// se télescoper dans le cache d'entrées.
enum class AssetPayloadKind : uint8_t { Raw = 0, Image = 1, MeshObj = 2 };

// Résultat d'un décodage exécuté sur le worker (desktop) ou dans pump() (web) :
// un payload opaque typé par l'appelant + sa taille réelle pour la comptabilité
// (remplace la taille du fichier une fois les bytes bruts libérés).
struct AssetDecodeResult {
    std::shared_ptr<void> payload;
    uint64_t bytes = 0;
};

// Consomme les bytes bruts du fichier et produit le payload décodé. Retourne
// false + `error` en cas d'échec. Exécuté hors du thread principal sur desktop :
// ne toucher ni au GPU ni à l'état du moteur.
using AssetDecoder =
    std::function<bool(std::vector<uint8_t>&& bytes, AssetDecodeResult& out, std::string& error)>;

struct AssetLoadStats {
    uint32_t live = 0;
    uint32_t queued = 0;
    uint32_t loading = 0;
    uint32_t ready = 0;
    uint32_t failed = 0;
    // Cumul des requêtes passées en échec depuis le boot (jamais décrémenté,
    // même une fois le handle relâché) — critère CI « contenu refusé ».
    uint64_t failedTotal = 0;
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
    // Payload produit par le décodeur de la requête (null pour une requête Raw
    // ou tant que l'asset n'est pas Ready).
    std::shared_ptr<void> payload() const;
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
                        AssetLoadPriority priority = AssetLoadPriority::Normal,
                        AssetPayloadKind kind = AssetPayloadKind::Raw,
                        AssetDecoder decoder = {});
    AssetHandle request(const std::string& path, AssetType type = AssetType::Unknown,
                        AssetLoadPriority priority = AssetLoadPriority::Normal,
                        AssetPayloadKind kind = AssetPayloadKind::Raw,
                        AssetDecoder decoder = {});
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

    // Cumul des échecs de chargement depuis le boot (jamais décrémenté).
    uint64_t failedTotal() const { return failedTotal_.load(std::memory_order_relaxed); }

private:
    friend class AssetHandle;
    static std::atomic<uint64_t> failedTotal_;

    struct Job {
        AssetLoadPriority priority = AssetLoadPriority::Normal;
        uint64_t sequence = 0;
        std::shared_ptr<AssetHandle::Entry> entry;
    };
    struct JobOrder {
        bool operator()(const Job& a, const Job& b) const;
    };

    struct EntryKey {
        AssetID id = kAssetInvalid;
        AssetPayloadKind kind = AssetPayloadKind::Raw;
        bool operator==(const EntryKey& o) const { return id == o.id && kind == o.kind; }
    };
    struct EntryKeyHash {
        size_t operator()(const EntryKey& k) const {
            return std::hash<AssetID>()(k.id) ^ (static_cast<size_t>(k.kind) * 0x9e3779b97f4a7c15ull);
        }
    };

    AssetHandle requestResolved(AssetID id, const std::string& absolutePath,
                                AssetLoadPriority priority, AssetPayloadKind kind,
                                AssetDecoder decoder);
    bool popJob(Job& job);
    void load(const std::shared_ptr<AssetHandle::Entry>& entry);
    void workerMain();

    AssetRegistry* registry_ = nullptr;
    std::shared_ptr<Accounting> accounting_;
    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::priority_queue<Job, std::vector<Job>, JobOrder> jobs_;
    std::unordered_map<EntryKey, std::weak_ptr<AssetHandle::Entry>, EntryKeyHash> entries_;
    uint64_t nextSequence_ = 1;
    bool stopping_ = false;
#ifndef __EMSCRIPTEN__
    std::thread worker_;
#endif
};

const char* assetLoadStateName(AssetLoadState state);

} // namespace saida
