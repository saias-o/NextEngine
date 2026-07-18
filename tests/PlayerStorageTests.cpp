#include "project/PlayerStorage.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

using namespace saida;
using json = nlohmann::json;

namespace {

int gChecks = 0;

void require(bool condition, const char* what) {
    ++gChecks;
    if (!condition) {
        std::cerr << "[player-storage] FAIL: " << what << "\n";
        std::abort();
    }
}

std::filesystem::path testRoot() {
    auto root = std::filesystem::temp_directory_path() / "SaidaPlayerStorageTests";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    return root;
}

PlayerStorage makeStore(const std::filesystem::path& root, StorageQuota quota = {}) {
    return PlayerStorage(root / "saves", root / "prefs", quota);
}

std::string readFile(const std::filesystem::path& path) {
    std::ifstream f(path, std::ios::binary);
    std::string s((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return s;
}

// A save round-trips its opaque payload and metadata untouched.
void testRoundTrip(const std::filesystem::path& root) {
    PlayerStorage store = makeStore(root);
    const std::string payload = R"({"relics":3,"pos":[1.5,-2.0]})";

    StorageResult saved = store.save(StorageKind::Progress, "slot0", payload, 7);
    require(bool(saved), "save ok");
    require(saved.meta.schema == kSaveEnvelopeVersion, "save schema");
    require(saved.meta.dataVersion == 7, "save dataVersion");
    require(saved.meta.bytes == static_cast<std::int64_t>(payload.size()), "save bytes");

    StorageResult loaded = store.load(StorageKind::Progress, "slot0");
    require(bool(loaded), "load ok");
    require(loaded.found, "load found");
    require(loaded.payload == payload, "payload identical after round-trip");
    require(loaded.meta.dataVersion == 7, "load dataVersion");
    require(loaded.meta.schema == kSaveEnvelopeVersion, "load schema");
    require(loaded.meta.savedAt > 0, "savedAt set");

    require(store.has(StorageKind::Progress, "slot0"), "has after save");
    StorageResult info = store.info(StorageKind::Progress, "slot0");
    require(info.found && info.payload.empty(), "info reports meta only");
    require(info.meta.bytes == static_cast<std::int64_t>(payload.size()), "info bytes");
}

// Progression and preferences are independent namespaces.
void testNamespaceSeparation(const std::filesystem::path& root) {
    PlayerStorage store = makeStore(root);
    store.save(StorageKind::Progress, "game", "\"progress\"");
    store.save(StorageKind::Preference, "game", "\"prefs\"");

    require(store.load(StorageKind::Progress, "game").payload == "\"progress\"",
            "progress payload");
    require(store.load(StorageKind::Preference, "game").payload == "\"prefs\"",
            "preference payload");

    // Clearing progression leaves preferences intact.
    require(store.remove(StorageKind::Progress, "game").found, "remove progress");
    require(!store.has(StorageKind::Progress, "game"), "progress gone");
    require(store.has(StorageKind::Preference, "game"), "preference survives");

    // Files live in distinct directories.
    require(std::filesystem::exists(root / "prefs" / "game.json"), "pref file placed");
    require(!std::filesystem::exists(root / "saves" / "game.json"), "progress file removed");
}

// Legacy raw-string saves (no envelope) load verbatim, then upgrade on rewrite.
void testLegacyMigration(const std::filesystem::path& root) {
    PlayerStorage store = makeStore(root);
    std::filesystem::create_directories(root / "saves");
    const std::string legacy = R"({"relics":1})";
    { std::ofstream f(root / "saves" / "old.json", std::ios::binary); f << legacy; }

    StorageResult loaded = store.load(StorageKind::Progress, "old");
    require(bool(loaded) && loaded.payload == legacy, "legacy payload verbatim");
    require(loaded.meta.schema == 0, "legacy reports schema 0");

    // Rewriting upgrades the file to the versioned envelope.
    require(bool(store.save(StorageKind::Progress, "old", legacy)), "rewrite legacy");
    json doc = json::parse(readFile(root / "saves" / "old.json"));
    require(doc.contains("__saidaStore") && doc["__saidaStore"].get<bool>(),
            "rewritten file is an envelope");
    require(doc["schema"].get<int>() == kSaveEnvelopeVersion, "envelope schema");
    require(store.load(StorageKind::Progress, "old").payload == legacy,
            "payload preserved through upgrade");
}

// A future or tampered envelope is refused, not silently misread.
void testCorruptRefused(const std::filesystem::path& root) {
    PlayerStorage store = makeStore(root);
    std::filesystem::create_directories(root / "saves");

    // Future schema.
    json future;
    future["schema"] = kSaveEnvelopeVersion + 1;
    future["version"] = kSaveEnvelopeVersion + 1;
    future["__saidaStore"] = true;
    future["kind"] = "progress";
    future["payload"] = "\"x\"";
    { std::ofstream f(root / "saves" / "future.json"); f << future.dump(); }
    StorageResult r = store.load(StorageKind::Progress, "future");
    require(r.status == StorageStatus::Corrupt, "future schema refused");
    require(r.payload.empty(), "no payload on corrupt");

    // schema/version divergence (tampered).
    json tampered;
    tampered["schema"] = 1;
    tampered["version"] = 2;
    tampered["__saidaStore"] = true;
    tampered["kind"] = "progress";
    tampered["payload"] = "\"x\"";
    { std::ofstream f(root / "saves" / "tamper.json"); f << tampered.dump(); }
    require(store.load(StorageKind::Progress, "tamper").status == StorageStatus::Corrupt,
            "schema/version divergence refused");
}

// Byte and slot quotas fail loudly with a typed status.
void testQuotas(const std::filesystem::path& root) {
    StorageQuota quota;
    quota.maxSlotBytes = 32;
    quota.maxNamespaceBytes = 4096;
    quota.maxSlots = 2;
    PlayerStorage store = makeStore(root, quota);

    require(store.save(StorageKind::Progress, "a", "\"ok\"").status == StorageStatus::Ok,
            "small save ok");
    StorageResult tooBig =
        store.save(StorageKind::Progress, "b", std::string(64, 'x'));
    require(tooBig.status == StorageStatus::QuotaExceeded, "oversize payload refused");
    require(!tooBig.error.empty(), "quota error diagnostic");

    require(store.save(StorageKind::Progress, "b", "\"ok\"").status == StorageStatus::Ok,
            "second slot ok");
    require(store.save(StorageKind::Progress, "c", "\"ok\"").status ==
                StorageStatus::QuotaExceeded,
            "slot count quota refused");
    // Overwriting an existing slot within its budget still works at the cap.
    require(store.save(StorageKind::Progress, "a", "\"ok2\"").status == StorageStatus::Ok,
            "overwrite at slot cap ok");
}

// Invalid slot names and missing slots produce typed results, not disk writes.
void testInvalidAndMissing(const std::filesystem::path& root) {
    PlayerStorage store = makeStore(root);
    require(store.save(StorageKind::Progress, "bad/name", "\"x\"").status ==
                StorageStatus::InvalidSlot,
            "invalid slot refused");
    require(store.save(StorageKind::Progress, "", "\"x\"").status ==
                StorageStatus::InvalidSlot,
            "empty slot refused");
    require(store.load(StorageKind::Progress, "ghost").status == StorageStatus::NotFound,
            "missing load NotFound");
    require(store.remove(StorageKind::Progress, "ghost").status == StorageStatus::NotFound,
            "missing remove NotFound");
    require(!store.has(StorageKind::Progress, "ghost"), "missing has false");
}

// list() enumerates slots per namespace only.
void testList(const std::filesystem::path& root) {
    PlayerStorage store = makeStore(root);
    store.save(StorageKind::Progress, "one", "\"1\"");
    store.save(StorageKind::Progress, "two", "\"2\"");
    store.save(StorageKind::Preference, "theme", "\"dark\"");

    std::vector<std::string> progress = store.list(StorageKind::Progress);
    std::sort(progress.begin(), progress.end());
    require(progress.size() == 2 && progress[0] == "one" && progress[1] == "two",
            "progress list");
    std::vector<std::string> prefs = store.list(StorageKind::Preference);
    require(prefs.size() == 1 && prefs[0] == "theme", "preference list");
}

} // namespace

int main() {
    const std::filesystem::path root = testRoot();
    testRoundTrip(root / "round");
    testNamespaceSeparation(root / "ns");
    testLegacyMigration(root / "legacy");
    testCorruptRefused(root / "corrupt");
    testQuotas(root / "quota");
    testInvalidAndMissing(root / "invalid");
    testList(root / "list");
    std::filesystem::remove_all(root);
    std::cout << "[player-storage] PASS (" << gChecks << " checks)\n";
    return 0;
}
