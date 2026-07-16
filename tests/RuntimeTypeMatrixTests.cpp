#include "authoring/EngineManifest.hpp"
#include "authoring/SceneSnapshot.hpp"
#include "core/Reflection.hpp"
#include "physics/CharacterBodyNode.hpp"
#include "physics/CollisionShapeNode.hpp"
#include "physics/RigidBodyNode.hpp"
#include "physics/StaticBodyNode.hpp"
#include "scene/Node.hpp"
#include "scene/NodeRegistry.hpp"
#include "scene/RuntimeTypeMatrix.hpp"
#include "scene/Scene.hpp"
#include "scene/Blackboard.hpp"
#include "scene/StateMachineBehaviour.hpp"
#include "scene/UICanvasNode.hpp"
#include "scene/UITextNode.hpp"
#include "scripting/ScriptBehaviour.hpp"

#include <cassert>
#include <memory>
#include <set>
#include <string>

namespace {

nlohmann::json changedValue(const saida::reflect::PropertyDesc& property,
                            const nlohmann::json& current) {
    if (property.kind == "bool") return !current.get<bool>();
    if (property.kind == "float") {
        const double value = current.get<double>();
        if (property.hasRange)
            return value != property.min ? property.min : property.max;
        return value + 1.25;
    }
    if (property.kind == "int") {
        const int value = current.get<int>();
        if (property.hasRange)
            return value != static_cast<int>(property.min)
                ? static_cast<int>(property.min) : static_cast<int>(property.max);
        return value + 3;
    }
    if (property.kind == "enum") {
        const int value = current.get<int>();
        const int count = static_cast<int>(property.enumLabels.size());
        return count > 1 ? (value + 1) % count : value + 1;
    }
    if (property.kind == "string" || property.kind == "asset")
        return "roundtrip/" + property.name;
    if (property.kind == "vec3")
        return nlohmann::json::array({1.25f, -2.5f, 3.75f});
    if (property.kind == "vec4")
        return nlohmann::json::array({0.125f, 0.375f, 0.625f, 0.875f});
    assert(false && "unhandled reflected property kind");
    return nullptr;
}

void seedReflectedProperties(const std::string& type, void* object) {
    const saida::reflect::TypeDesc* desc =
        saida::reflect::TypeRegistry::instance().find(type);
    if (!desc) return;
    for (const saida::reflect::PropertyDesc& property : desc->properties) {
        nlohmann::json before;
        property.get(object, before);
        property.set(object, changedValue(property, before));
        nlohmann::json after;
        property.get(object, after);
        assert(after != before);
    }
}

void seedHandwrittenBehaviour(saida::Behaviour& behaviour) {
    if (auto* blackboard = dynamic_cast<saida::Blackboard*>(&behaviour)) {
        blackboard->setNumber("score", 42.5);
        blackboard->setBool("doorOpen", true);
        blackboard->setString("chapter", "contract");
    } else if (auto* stateMachine =
                   dynamic_cast<saida::StateMachineBehaviour*>(&behaviour)) {
        stateMachine->load({
            {"initialState", "Idle"},
            {"states", {"Idle", "Active"}},
            {"transitions", {{{"from", "Idle"}, {"to", "Active"},
                               {"trigger", "go"}, {"after", 1.5f}}}},
        });
    } else if (auto* script = dynamic_cast<saida::ScriptBehaviour*>(&behaviour)) {
        script->load({
            {"script", "scripts/contract.js"},
            {"hotReload", false},
            {"properties", {{"speed", 3.5}, {"enabled", true},
                              {"label", "contract"}}},
        });
    }
}

} // namespace

int main() {
    using namespace saida;

    std::set<std::string> identities;
    RuntimeTypeCategory previousCategory = RuntimeTypeCategory::Node;
    std::string previousName;
    for (const RuntimeTypeRow& row : kRuntimeTypeMatrix) {
        const std::string identity =
            std::string(row.category == RuntimeTypeCategory::Node ? "node:" : "behaviour:") +
            row.name;
        assert(identities.insert(identity).second);
        if (row.category == previousCategory) {
            assert(previousName.empty() || previousName < row.name);
        } else {
            assert(previousCategory == RuntimeTypeCategory::Node);
            assert(row.category == RuntimeTypeCategory::Behaviour);
            previousName.clear();
        }
        previousCategory = row.category;
        previousName = row.name;
    }

    const nlohmann::json matrix = buildRuntimeTypeMatrixManifest();
    assert(matrix["schema"] == 1);
    assert(matrix["targets"].size() == 4);
    assert(matrix["types"].size() == kRuntimeTypeMatrix.size());
    assert(authoring::buildEngineManifest()["runtimeTypeMatrix"] == matrix);

    // A fresh headless process must register exactly the factories declared by
    // the canonical table. Serializing an empty scene initializes that catalog.
    Scene scene;
    auto canvas = std::make_unique<UICanvasNode>();
    canvas->setName("HUD");
    canvas->setSize(1280.0f, 720.0f);
    auto text = std::make_unique<UITextNode>();
    text->setName("Status");
    text->setPosition(24.0f, 36.0f);
    text->setSize(320.0f, 48.0f);
    text->setAnchor(0.0f, 0.0f);
    text->setPivot(0.0f, 0.0f);
    text->setText("Matrix ready");
    text->setFontSize(22.0f);
    text->setColor({0.25f, 0.5f, 0.75f, 1.0f});
    canvas->addChild(std::move(text));
    scene.addChild(std::move(canvas));

    auto rigid = std::make_unique<RigidBodyNode>();
    rigid->setName("Crate");
    rigid->friction = 0.7f;
    rigid->restitution = 0.2f;
    rigid->kinematic = true;
    rigid->mass = 12.0f;
    rigid->gravityFactor = 0.8f;
    rigid->linearDamping = 0.15f;
    rigid->angularDamping = 0.25f;
    auto shape = std::make_unique<CollisionShapeNode>();
    shape->shapeType = CollisionShapeType::Capsule;
    shape->halfExtents = {1.0f, 2.0f, 3.0f};
    shape->radius = 0.75f;
    shape->height = 3.5f;
    shape->axis = 2;
    shape->offset = {0.1f, 0.2f, 0.3f};
    rigid->addChild(std::move(shape));
    scene.addChild(std::move(rigid));

    auto character = std::make_unique<CharacterBodyNode>();
    character->setName("HeroBody");
    character->friction = 0.4f;
    character->restitution = 0.05f;
    character->mass = 82.0f;
    character->maxSlopeAngle = 47.0f;
    scene.addChild(std::move(character));

    auto staticBody = std::make_unique<StaticBodyNode>();
    staticBody->setName("FloorBody");
    staticBody->friction = 0.9f;
    staticBody->restitution = 0.1f;
    scene.addChild(std::move(staticBody));

    const std::string snapshot = authoring::serializeSceneSnapshot(scene, nullptr);
    std::string error;
    assert(verifyRegisteredRuntimeTypes(RuntimeTypeTarget::Headless, error));

    Scene reloaded;
    assert(authoring::deserializeSceneSnapshot(snapshot, reloaded, &error));
    assert(reloaded.children().size() == 4);
    auto* reloadedCanvas = dynamic_cast<UICanvasNode*>(reloaded.children()[0].get());
    assert(reloadedCanvas);
    assert(reloadedCanvas->width() == 1280.0f && reloadedCanvas->height() == 720.0f);
    assert(reloadedCanvas->children().size() == 1);
    auto* reloadedText = dynamic_cast<UITextNode*>(reloadedCanvas->children()[0].get());
    assert(reloadedText);
    assert(reloadedText->text() == "Matrix ready");
    assert(reloadedText->fontSize() == 22.0f);
    assert(reloadedText->x() == 24.0f && reloadedText->y() == 36.0f);

    auto* reloadedRigid = dynamic_cast<RigidBodyNode*>(reloaded.children()[1].get());
    assert(reloadedRigid && reloadedRigid->kinematic && reloadedRigid->mass == 12.0f);
    assert(reloadedRigid->gravityFactor == 0.8f && reloadedRigid->friction == 0.7f);
    assert(reloadedRigid->children().size() == 1);
    auto* reloadedShape =
        dynamic_cast<CollisionShapeNode*>(reloadedRigid->children()[0].get());
    assert(reloadedShape && reloadedShape->shapeType == CollisionShapeType::Capsule);
    assert(reloadedShape->radius == 0.75f && reloadedShape->axis == 2);
    auto* reloadedCharacter =
        dynamic_cast<CharacterBodyNode*>(reloaded.children()[2].get());
    assert(reloadedCharacter && reloadedCharacter->mass == 82.0f);
    assert(reloadedCharacter->maxSlopeAngle == 47.0f);
    auto* reloadedStatic = dynamic_cast<StaticBodyNode*>(reloaded.children()[3].get());
    assert(reloadedStatic && reloadedStatic->friction == 0.9f);
    assert(authoring::serializeSceneSnapshot(reloaded, nullptr) == snapshot);

    // Exhaust the headless rows with non-default common/reflected data. This
    // catches a property codec that is registered but silently disappears.
    Scene contractScene;
    std::size_t nodeIndex = 0;
    for (const RuntimeTypeRow& row : kRuntimeTypeMatrix) {
        if (row.category != RuntimeTypeCategory::Node ||
            row.availability[static_cast<std::size_t>(RuntimeTypeTarget::Headless)] !=
                RuntimeTypeAvailability::Required)
            continue;
        std::unique_ptr<Node> node = NodeRegistry::instance().create(row.name);
        assert(node && std::string(node->typeName()) == row.name);
        node->setName("ContractNode" + std::to_string(nodeIndex++));
        node->setEnabled(false);
        node->transform().position = {1.0f + static_cast<float>(nodeIndex), 2.0f, -3.0f};
        node->transform().scale = {1.25f, 0.75f, 1.5f};
        node->addToGroup("roundtrip");
        seedReflectedProperties(row.name, node.get());
        contractScene.addChild(std::move(node));
    }

    auto behaviourHost = std::make_unique<Node>("BehaviourContract");
    for (const RuntimeTypeRow& row : kRuntimeTypeMatrix) {
        if (row.category != RuntimeTypeCategory::Behaviour ||
            row.availability[static_cast<std::size_t>(RuntimeTypeTarget::Headless)] !=
                RuntimeTypeAvailability::Required)
            continue;
        std::unique_ptr<Behaviour> behaviour = BehaviourRegistry::instance().create(row.name);
        assert(behaviour && std::string(behaviour->typeName()) == row.name);
        behaviour->setEnabled(false);
        seedReflectedProperties(row.name, behaviour.get());
        seedHandwrittenBehaviour(*behaviour);
        behaviourHost->addBehaviour(std::move(behaviour));
    }
    contractScene.addChild(std::move(behaviourHost));

    const std::string contractSnapshot =
        authoring::serializeSceneSnapshot(contractScene, nullptr);
    Scene contractReloaded;
    error.clear();
    assert(authoring::deserializeSceneSnapshot(contractSnapshot, contractReloaded, &error));
    assert(authoring::serializeSceneSnapshot(contractReloaded, nullptr) == contractSnapshot);

    // Unknown additions are also contract drift, not only missing factories.
    NodeRegistry::instance().registerType<Node>("UnexpectedTestNode");
    error.clear();
    assert(!verifyRegisteredRuntimeTypes(RuntimeTypeTarget::Headless, error));
    assert(error.find("UnexpectedTestNode") != std::string::npos);
    return 0;
}
