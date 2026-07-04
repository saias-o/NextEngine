#include "authoring/EngineManifest.hpp"
#include "authoring/SaidaOp.hpp"
#include "authoring/SceneSnapshot.hpp"
#include "authoring/SaidaOpApplier.hpp"
#include "core/FormatVersions.hpp"
#include "scene/LightNode.hpp"
#include "scene/ParticleSystemNode.hpp"
#include "scene/Scene.hpp"
#include "scene/WaterNode.hpp"

#include <nlohmann/json.hpp>
#include <glm/glm.hpp>

#include <cmath>
#include <cstdlib>
#include <memory>
#include <string>

using nlohmann::json;

namespace {

void require(bool condition) {
    if (!condition) std::abort();
}

json applyOp(saida::Scene& scene, const json& op) {
    return json::parse(saida::authoring::applyOpJson(scene, nullptr, op.dump()));
}

json setProperty(const std::string& node, const std::string& property, json value) {
    return {
        {"type", "set_property"},
        {"payload", {
            {"nodeId", node},
            {"property", property},
            {"value", std::move(value)},
        }},
    };
}

bool close(float a, float b) {
    return std::abs(a - b) < 0.0001f;
}

// Recherche directe parmi les enfants (les tests referencent par nom).
saida::Node* findChildByName(saida::Node& parent, const std::string& name) {
    for (const auto& c : parent.children())
        if (c->name() == name) return c.get();
    return nullptr;
}

void testReflectedLightProperty() {
    saida::Scene scene;
    auto light = std::make_unique<saida::LightNode>("Sun", saida::LightType::Directional);
    saida::LightNode* sun = light.get();
    scene.addChild(std::move(light));

    json res = applyOp(scene, setProperty("Sun", "intensity", 3.5f));
    require(res["ok"].get<bool>());
    require(close(sun->intensity, 3.5f));
    require(res["diff"]["before"].get<float>() == 1.0f);
    require(res["diff"]["after"].get<float>() == 3.5f);

    const glm::vec3 beforeColor = sun->color;
    res = applyOp(scene, setProperty("Sun", "color", true));
    require(!res["ok"].get<bool>());
    require(sun->color == beforeColor);

    const saida::LightType beforeType = sun->type;
    res = applyOp(scene, setProperty("Sun", "lightType", 99));
    require(!res["ok"].get<bool>());
    require(sun->type == beforeType);
}

void testReflectedWaterAndParticleProperties() {
    saida::Scene scene;
    auto water = std::make_unique<saida::WaterNode>();
    water->setName("Sea");
    saida::WaterNode* sea = water.get();
    scene.addChild(std::move(water));

    auto particles = std::make_unique<saida::ParticleSystemNode>();
    particles->setName("Sparks");
    saida::ParticleSystemNode* sparks = particles.get();
    scene.addChild(std::move(particles));

    json res = applyOp(scene, setProperty("Sea", "amplitude", 0.75f));
    require(res["ok"].get<bool>());
    require(close(sea->amplitude, 0.75f));

    res = applyOp(scene, setProperty("Sparks", "maxParticles", 1024));
    require(res["ok"].get<bool>());
    require(sparks->maxParticles == 1024);

    res = applyOp(scene, setProperty("Sparks", "maxParticles", 12.5f));
    require(!res["ok"].get<bool>());
    require(sparks->maxParticles == 1024);
}

void testInvalidOpsDoNotMutate() {
    saida::Scene scene;
    auto light = std::make_unique<saida::LightNode>("Lamp", saida::LightType::Point);
    saida::LightNode* lamp = light.get();
    scene.addChild(std::move(light));

    json res = applyOp(scene, setProperty("Ghost", "intensity", 4.0f));
    require(!res["ok"].get<bool>());
    require(close(lamp->intensity, 1.0f));

    res = applyOp(scene, setProperty("Lamp", "wobble", 4.0f));
    require(!res["ok"].get<bool>());
    require(close(lamp->intensity, 1.0f));

    res = applyOp(scene, setProperty("Lamp", "name", false));
    require(!res["ok"].get<bool>());
    require(lamp->name() == "Lamp");

    res = applyOp(scene, json{{"type", "rename_node"}, {"payload", {{"nodeId", "Lamp"}, {"name", ""}}}});
    require(!res["ok"].get<bool>());
    require(lamp->name() == "Lamp");

    res = applyOp(scene, json{{"type", "delete_node"}, {"payload", {{"nodeId", scene.name()}}}});
    require(!res["ok"].get<bool>());
    require(!scene.children().empty());
}

void testCreateNodeResourceBoundaries() {
    saida::Scene scene;

    json res = applyOp(scene, json{{"type", "create_node"},
                                   {"payload", {{"nodeType", "LightNode"}, {"name", "AddedLamp"}}}});
    require(res["ok"].get<bool>());
    require(scene.children().size() == 1);
    require(scene.children()[0]->name() == "AddedLamp");

    res = applyOp(scene, json{{"type", "create_node"},
                              {"payload", {{"nodeType", "MeshNode"}, {"name", "NeedsGpu"}}}});
    require(!res["ok"].get<bool>());
    require(scene.children().size() == 1);

    // Reflected node palette via NodeRegistry (no GPU): Water, ParticleSystem.
    res = applyOp(scene, json{{"type", "create_node"},
                              {"payload", {{"nodeType", "Water"}, {"name", "Sea"}}}});
    require(res["ok"].get<bool>());
    saida::Node* sea = findChildByName(scene, "Sea");
    require(sea != nullptr && dynamic_cast<saida::WaterNode*>(sea) != nullptr);

    res = applyOp(scene, json{{"type", "create_node"},
                              {"payload", {{"nodeType", "ParticleSystem"}, {"name", "Sparks"}}}});
    require(res["ok"].get<bool>());
    require(dynamic_cast<saida::ParticleSystemNode*>(findChildByName(scene, "Sparks")) != nullptr);

    // A property set on a freshly-created reflected node applies.
    res = applyOp(scene, setProperty("Sea", "amplitude", 0.5f));
    require(res["ok"].get<bool>());

    // Unknown types are still rejected.
    res = applyOp(scene, json{{"type", "create_node"},
                              {"payload", {{"nodeType", "Wobbulator"}, {"name", "X"}}}});
    require(!res["ok"].get<bool>());
}

// Phase A1 : le contrat SaidaOp est versionne, serialisable, round-trip stable.
void testSaidaOpRoundTrip() {
    const json in = {
        {"type", "set_transform"},
        {"sceneId", "main"},
        {"payload", {{"nodeId", "Palm_A"}, {"position", {4.0f, 0.6f, 12.0f}}}},
    };
    auto parsed = saida::authoring::parseSaidaOp(in);
    require(parsed.ok);
    require(parsed.op.opVersion == saida::authoring::kOpVersion);  // defaut rempli
    require(parsed.op.type == "set_transform");
    require(parsed.op.sceneId == "main");

    // parse(toJson(op)) == op, et toJson est stable (double round-trip).
    const json out = parsed.op.toJson();
    auto reparsed = saida::authoring::parseSaidaOp(out);
    require(reparsed.ok);
    require(reparsed.op.opVersion == parsed.op.opVersion);
    require(reparsed.op.type == parsed.op.type);
    require(reparsed.op.sceneId == parsed.op.sceneId);
    require(reparsed.op.payload == parsed.op.payload);
    require(reparsed.op.toJson() == out);

    // sceneId vide n'est pas emis.
    auto minimal = saida::authoring::parseSaidaOp(json{{"type", "delete_node"}});
    require(minimal.ok);
    require(!minimal.op.toJson().contains("sceneId"));
    require(minimal.op.toJson()["payload"].is_object());
}

// Phase A4 : table des rejets de forme — aucun ne doit passer le parse.
void testSaidaOpParseRejections() {
    using saida::authoring::parseSaidaOp;
    require(!parseSaidaOp(std::string("not json at all")).ok);
    require(!parseSaidaOp(json::array({1, 2, 3})).ok);                  // pas un objet
    require(!parseSaidaOp(json{{"payload", json::object()}}).ok);       // type manquant
    require(!parseSaidaOp(json{{"type", 42}}).ok);                      // type non string
    require(!parseSaidaOp(json{{"type", "explode_scene"}}).ok);         // type inconnu
    require(!parseSaidaOp(json{{"type", "delete_node"}, {"opVersion", 999}}).ok);
    require(!parseSaidaOp(json{{"type", "delete_node"}, {"opVersion", "1"}}).ok);
    require(!parseSaidaOp(json{{"type", "delete_node"}, {"payload", "x"}}).ok);
    require(!parseSaidaOp(json{{"type", "delete_node"}, {"sceneId", 7}}).ok);

    // L'applier refuse les memes formes sans muter la scene.
    saida::Scene scene;
    scene.addChild(std::make_unique<saida::LightNode>("Keep", saida::LightType::Point));
    json res = applyOp(scene, json{{"type", "explode_scene"}, {"payload", json::object()}});
    require(!res["ok"].get<bool>());
    res = applyOp(scene, json{{"type", "delete_node"}, {"opVersion", 999},
                              {"payload", {{"nodeId", "Keep"}}}});
    require(!res["ok"].get<bool>());
    require(scene.children().size() == 1);
}

// Phase A4 : reparent_node — structure d'arbre protegee (racine, self, cycles).
void testReparentNode() {
    saida::Scene scene;
    auto* a = scene.createChild<saida::Node>("A");
    auto* b = scene.createChild<saida::Node>("B");
    auto* c = a->createChild<saida::Node>("C");
    (void)b;

    // Deplacement valide : C passe de A a B.
    json res = applyOp(scene, json{{"type", "reparent_node"},
                                   {"payload", {{"nodeId", "C"}, {"newParent", "B"}}}});
    require(res["ok"].get<bool>());
    require(c->parent() == b);
    require(a->children().empty());
    require(res["diff"]["from"] == "A");
    require(res["diff"]["to"] == "B");

    // newParent absent/vide = racine de scene.
    res = applyOp(scene, json{{"type", "reparent_node"}, {"payload", {{"nodeId", "C"}}}});
    require(res["ok"].get<bool>());
    require(c->parent() == &scene);

    // Meme parent : accepte, marque unchanged, pas de mutation.
    res = applyOp(scene, json{{"type", "reparent_node"},
                              {"payload", {{"nodeId", "C"}, {"newParent", scene.name()}}}});
    require(res["ok"].get<bool>());
    require(res["diff"]["unchanged"].get<bool>());

    // Rejets : racine, self, descendant (cycle), inconnus.
    res = applyOp(scene, json{{"type", "reparent_node"},
                              {"payload", {{"nodeId", scene.name()}, {"newParent", "B"}}}});
    require(!res["ok"].get<bool>());
    res = applyOp(scene, json{{"type", "reparent_node"},
                              {"payload", {{"nodeId", "A"}, {"newParent", "A"}}}});
    require(!res["ok"].get<bool>());
    auto* d = a->createChild<saida::Node>("D");
    res = applyOp(scene, json{{"type", "reparent_node"},
                              {"payload", {{"nodeId", "A"}, {"newParent", "D"}}}});
    require(!res["ok"].get<bool>());
    require(d->parent() == a);
    res = applyOp(scene, json{{"type", "reparent_node"},
                              {"payload", {{"nodeId", "Ghost"}, {"newParent", "B"}}}});
    require(!res["ok"].get<bool>());
    res = applyOp(scene, json{{"type", "reparent_node"},
                              {"payload", {{"nodeId", "D"}, {"newParent", "Ghost"}}}});
    require(!res["ok"].get<bool>());
    require(d->parent() == a);
}

// Phase B2 : validation de forme statique (sans scene) — dry-run.
void testValidateOpShape() {
    using saida::authoring::parseSaidaOp;
    using saida::authoring::validateOpShape;

    auto shapeOf = [](const json& j) -> std::string {
        auto r = parseSaidaOp(j);
        if (!r.ok) return r.error;               // deja rejete au parse
        return validateOpShape(r.op);            // "" si forme valide
    };

    // Formes valides -> chaine vide.
    require(shapeOf(json{{"type", "set_transform"},
                         {"payload", {{"nodeId", "A"}, {"position", {1, 2, 3}}}}}).empty());
    require(shapeOf(json{{"type", "reparent_node"},
                         {"payload", {{"nodeId", "A"}}}}).empty());  // newParent optionnel
    require(shapeOf(json{{"type", "set_property"},
                         {"payload", {{"nodeId", "A"}, {"property", "intensity"}, {"value", 2}}}}).empty());
    require(shapeOf(json{{"type", "create_node"},
                         {"payload", {{"nodeType", "LightNode"}}}}).empty());

    // Formes invalides -> message non vide.
    require(!shapeOf(json{{"type", "set_transform"}, {"payload", {{"nodeId", "A"}}}}).empty()); // aucun champ
    require(!shapeOf(json{{"type", "set_transform"},
                          {"payload", {{"nodeId", "A"}, {"position", {1, 2}}}}}).empty());      // vec3 tronque
    require(!shapeOf(json{{"type", "set_transform"},
                          {"payload", {{"nodeId", ""}, {"position", {1, 2, 3}}}}}).empty());    // nodeId vide
    require(!shapeOf(json{{"type", "rename_node"}, {"payload", {{"nodeId", "A"}}}}).empty());   // name manquant
    require(!shapeOf(json{{"type", "set_property"},
                          {"payload", {{"nodeId", "A"}, {"property", "x"}}}}).empty());          // value manquante
    require(!shapeOf(json{{"type", "create_node"}, {"payload", json::object()}}).empty());       // nodeType manquant
    // Type inconnu : rejete des le parse.
    require(!shapeOf(json{{"type", "explode_scene"}, {"payload", json::object()}}).empty());
}

void testManifestListsRegistryOps() {
    json manifest = saida::authoring::buildEngineManifest();
    const auto& types = saida::authoring::knownOpTypes();
    require(manifest["ops"].size() == types.size());
    for (const auto& t : types) {
        bool found = false;
        for (const auto& op : manifest["ops"])
            if (op == t) found = true;
        require(found);
    }
    require(saida::authoring::isKnownOpType("reparent_node"));
    require(!saida::authoring::isKnownOpType("explode_scene"));
}

// Phase A5 : chaque op de modification renvoie un inverse re-appliquable
// (apply -> invert restaure l'etat). Fondation undo/redo + dry-run IA.
void testInverseOps() {
    saida::Scene scene;
    auto* a = scene.createChild<saida::Node>("A");
    auto* b = scene.createChild<saida::Node>("B");
    auto* c = a->createChild<saida::Node>("C");
    auto light = std::make_unique<saida::LightNode>("Sun", saida::LightType::Directional);
    saida::LightNode* sun = light.get();
    scene.addChild(std::move(light));
    (void)b; (void)c;

    // set_transform : position restauree apres apply + apply(inverse).
    a->transform().position = {1.0f, 2.0f, 3.0f};
    json res = applyOp(scene, json{{"type", "set_transform"},
                                   {"payload", {{"nodeId", "A"}, {"position", {9.0f, 9.0f, 9.0f}}}}});
    require(res["ok"].get<bool>());
    require(res.contains("inverse"));
    require(close(a->transform().position.x, 9.0f));
    json inv = applyOp(scene, res["inverse"]);
    require(inv["ok"].get<bool>());
    require(close(a->transform().position.x, 1.0f));
    require(close(a->transform().position.y, 2.0f));
    require(close(a->transform().position.z, 3.0f));

    // set_property reflechi : intensity restauree.
    res = applyOp(scene, setProperty("Sun", "intensity", 5.0f));
    require(res["ok"].get<bool>());
    require(close(sun->intensity, 5.0f));
    applyOp(scene, res["inverse"]);
    require(close(sun->intensity, 1.0f));

    // set_property name : l'inverse reference le NOUVEAU nom, restaure l'ancien.
    res = applyOp(scene, setProperty("Sun", "name", "Star"));
    require(res["ok"].get<bool>());
    require(sun->name() == "Star");
    require(res["inverse"]["payload"]["nodeId"] == "Star");
    applyOp(scene, res["inverse"]);
    require(sun->name() == "Sun");

    // rename_node : idem, inverse restaure le nom d'origine.
    res = applyOp(scene, json{{"type", "rename_node"},
                              {"payload", {{"nodeId", "A"}, {"name", "Alpha"}}}});
    require(res["ok"].get<bool>());
    require(a->name() == "Alpha");
    applyOp(scene, res["inverse"]);
    require(a->name() == "A");

    // reparent_node : C revient sous A apres apply(inverse).
    res = applyOp(scene, json{{"type", "reparent_node"},
                              {"payload", {{"nodeId", "C"}, {"newParent", "B"}}}});
    require(res["ok"].get<bool>());
    require(c->parent() == b);
    applyOp(scene, res["inverse"]);
    require(c->parent() == a);

    // create_node : inverse = delete_node du node cree.
    res = applyOp(scene, json{{"type", "create_node"},
                              {"payload", {{"nodeType", "LightNode"}, {"name", "Temp"}}}});
    require(res["ok"].get<bool>());
    require(res["inverse"]["type"] == "delete_node");
    require(findChildByName(scene, "Temp") != nullptr);
    applyOp(scene, res["inverse"]);
    require(findChildByName(scene, "Temp") == nullptr);

    // delete_node : honnetement marque non op-inversible (restauration snapshot).
    res = applyOp(scene, json{{"type", "delete_node"}, {"payload", {{"nodeId", "B"}}}});
    require(res["ok"].get<bool>());
    require(!res.contains("inverse"));
    require(res["diff"]["invertible"].get<bool>() == false);
}

void testManifestContainsReflectedProperties() {
    json manifest = saida::authoring::buildEngineManifest();
    json manifestAgain = saida::authoring::buildEngineManifest();
    require(manifest["opVersion"].get<int>() == saida::authoring::kOpVersion);
    require(manifest == manifestAgain);
    require(manifest["properties"]["LightNode"].is_array());
    require(manifest["properties"]["Water"].is_array());
    require(manifest["properties"]["ParticleSystem"].is_array());

    bool hasLightIntensity = false;
    for (const auto& prop : manifest["properties"]["LightNode"]) {
        if (prop["name"] == "intensity" && prop["kind"] == "float") {
            hasLightIntensity = true;
        }
    }
    require(hasLightIntensity);
}

void testManifestContainsBehavioursSignalsAndScenario() {
    json manifest = saida::authoring::buildEngineManifest();
    require(manifest["behaviours"].is_array());
    require(manifest["scenario"]["actions"].is_array());
    require(manifest["scenario"]["conditions"].is_array());

    bool hasRotatorSignal = false;
    bool hasHealthSlot = false;
    bool hasScenarioRunnerSignals = false;
    for (const auto& behaviour : manifest["behaviours"]) {
        if (behaviour["name"] == "Rotator") {
            for (const auto& signal : behaviour.value("signals", json::array()))
                if (signal["name"] == "fullRotation" && signal["arity"] == 0)
                    hasRotatorSignal = true;
        }
        if (behaviour["name"] == "Health") {
            for (const auto& slot : behaviour.value("slots", json::array()))
                if (slot["name"] == "damage")
                    hasHealthSlot = true;
        }
        if (behaviour["name"] == "ScenarioRunner") {
            for (const auto& signal : behaviour.value("signals", json::array()))
                if (signal["name"] == "finished")
                    hasScenarioRunnerSignals = true;
        }
    }
    require(hasRotatorSignal);
    require(hasHealthSlot);
    require(hasScenarioRunnerSignals);

    bool hasSignalEmit = false;
    for (const auto& action : manifest["scenario"]["actions"])
        if (action["name"] == "signal.emit") hasSignalEmit = true;
    require(hasSignalEmit);

    bool hasCompositeAll = false;
    bool hasTimelineFinished = false;
    for (const auto& condition : manifest["scenario"]["conditions"]) {
        if (condition["name"] == "all" && condition.value("composite", false))
            hasCompositeAll = true;
        if (condition["name"] == "timeline.finished")
            hasTimelineFinished = true;
    }
    require(hasCompositeAll);
    require(hasTimelineFinished);
}

void testSnapshotReflectsAppliedOps() {
    saida::Scene scene;
    auto light = std::make_unique<saida::LightNode>("SnapshotSun", saida::LightType::Directional);
    scene.addChild(std::move(light));

    json res = applyOp(scene, setProperty("SnapshotSun", "intensity", 2.25f));
    require(res["ok"].get<bool>());

    json snapshot = json::parse(saida::authoring::serializeSceneSnapshot(scene, nullptr));
    require(snapshot["version"].get<int>() == saida::format::kSceneVersion);
    require(snapshot["snapshotMode"] == "authoring-headless");
    require(snapshot["scene"]["type"] == "Scene");
    require(snapshot["scene"]["children"].is_array());
    require(snapshot["scene"]["children"].size() == 1);

    const json& child = snapshot["scene"]["children"][0];
    require(child["type"] == "LightNode");
    require(child["name"] == "SnapshotSun");
    require(close(child["intensity"].get<float>(), 2.25f));
    require(child["transform"]["position"].is_array());
}

// Phase B3 : le snapshot headless se re-desserialise sans GPU et round-trip
// (serialize -> deserialize -> serialize) est stable au bit pres. C'est le socle
// de l'outil apply-ops et de la representation d'edition/collaboration.
void testHeadlessSnapshotRoundTrip() {
    saida::Scene scene;
    scene.setName("Root");
    scene.assignSerializedId(1);

    auto light = std::make_unique<saida::LightNode>("Sun", saida::LightType::Directional);
    light->assignSerializedId(2);
    light->intensity = 2.25f;
    light->transform().position = {1.0f, 2.0f, 3.0f};
    light->addToGroup("lights");
    scene.addChild(std::move(light));

    auto* group = scene.createChild<saida::Node>("Group");
    group->assignSerializedId(3);
    group->createChild<saida::ParticleSystemNode>()->assignSerializedId(4);

    const std::string s1 = saida::authoring::serializeSceneSnapshot(scene, nullptr);

    saida::Scene reloaded;
    std::string error;
    require(saida::authoring::deserializeSceneSnapshot(s1, reloaded, &error));
    require(error.empty());

    // Structure + ids + reflected props restaures.
    require(reloaded.name() == "Root");
    require(reloaded.id() == 1);
    require(reloaded.children().size() == 2);
    saida::Node* sun = findChildByName(reloaded, "Sun");
    require(sun != nullptr);
    require(sun->id() == 2);
    require(sun->isInGroup("lights"));
    require(close(sun->transform().position.y, 2.0f));
    auto* sunLight = dynamic_cast<saida::LightNode*>(sun);
    require(sunLight != nullptr);
    require(close(sunLight->intensity, 2.25f));
    require(sunLight->type == saida::LightType::Directional);

    // Round-trip stable au bit pres.
    const std::string s2 = saida::authoring::serializeSceneSnapshot(reloaded, nullptr);
    require(s1 == s2);

    // Une op s'applique sur la scene rechargee (chaine load -> apply -> save).
    json res = applyOp(reloaded, setProperty("Sun", "intensity", 7.0f));
    require(res["ok"].get<bool>());
    require(close(sunLight->intensity, 7.0f));

    // Document malforme : echec propre, scene laissee vide.
    saida::Scene bad;
    require(!saida::authoring::deserializeSceneSnapshot(std::string("{\"nope\":1}"), bad, &error));
    require(!error.empty());
    require(bad.children().empty());
    require(!saida::authoring::deserializeSceneSnapshot(std::string("not json"), bad, &error));
}

// set_scene_setting : édite SceneSettings par nom (float/bool/vec3), inversible.
void testSceneSettingOp() {
    saida::Scene scene;
    auto sceneSetting = [&](const std::string& key, json value) {
        return json{{"type", "set_scene_setting"}, {"payload", {{"setting", key}, {"value", std::move(value)}}}};
    };

    // float
    json res = applyOp(scene, sceneSetting("fogDensity", 0.08f));
    require(res["ok"].get<bool>());
    require(close(scene.settings().fogDensity, 0.08f));
    require(res.contains("inverse"));

    // bool
    const bool bloomBefore = scene.settings().bloomEnabled;
    res = applyOp(scene, sceneSetting("bloomEnabled", !bloomBefore));
    require(res["ok"].get<bool>());
    require(scene.settings().bloomEnabled == !bloomBefore);

    // vec3 colour into vec4 field (alpha preserved)
    scene.settings().ambientLight = {0.1f, 0.2f, 0.3f, 1.0f};
    res = applyOp(scene, sceneSetting("ambientLight", json::array({0.5f, 0.6f, 0.7f})));
    require(res["ok"].get<bool>());
    require(close(scene.settings().ambientLight.x, 0.5f));
    require(close(scene.settings().ambientLight.z, 0.7f));
    require(close(scene.settings().ambientLight.w, 1.0f));  // alpha untouched

    // inverse restores the previous colour
    applyOp(scene, res["inverse"]);
    require(close(scene.settings().ambientLight.x, 0.1f));
    require(close(scene.settings().ambientLight.z, 0.3f));

    // rejections: unknown setting, wrong type
    res = applyOp(scene, sceneSetting("doesNotExist", 1.0f));
    require(!res["ok"].get<bool>());
    res = applyOp(scene, sceneSetting("fogDensity", true));
    require(!res["ok"].get<bool>());
    res = applyOp(scene, sceneSetting("bloomEnabled", 3.0f));
    require(!res["ok"].get<bool>());
}

} // namespace

int main() {
    testReflectedLightProperty();
    testReflectedWaterAndParticleProperties();
    testInvalidOpsDoNotMutate();
    testCreateNodeResourceBoundaries();
    testSaidaOpRoundTrip();
    testSaidaOpParseRejections();
    testReparentNode();
    testInverseOps();
    testValidateOpShape();
    testManifestListsRegistryOps();
    testManifestContainsReflectedProperties();
    testManifestContainsBehavioursSignalsAndScenario();
    testSnapshotReflectsAppliedOps();
    testHeadlessSnapshotRoundTrip();
    testSceneSettingOp();
    return 0;
}
