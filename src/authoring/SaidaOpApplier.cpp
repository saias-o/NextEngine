#include "authoring/SaidaOpApplier.hpp"

#include "authoring/EngineManifest.hpp"
#include "authoring/SaidaOp.hpp"
#include "core/Reflection.hpp"
#include "graphics/Material.hpp"
#include "graphics/ResourceManager.hpp"
#include "scene/Behaviour.hpp"
#include "scene/BehaviourRegistry.hpp"
#include "scene/LightNode.hpp"
#include "scene/MeshNode.hpp"
#include "scene/Node.hpp"
#include "scene/NodeRegistry.hpp"
#include "scene/ParticleSystemNode.hpp"
#include "scene/ReflectedTypes.hpp"
#include "scene/Scene.hpp"
#include "scene/WaterNode.hpp"

#include <nlohmann/json.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace saida::authoring {
namespace {

using json = nlohmann::json;

// --- helpers ----------------------------------------------------------------

// Recherche par nom (le spike reference les nodes par nom ; le systeme final
// utilisera NodeId). Volontairement local : pas de find-by-name global moteur.
Node* findByName(Node& n, const std::string& name) {
    if (n.name() == name) return &n;
    for (const auto& c : n.children())
        if (Node* r = findByName(*c, name)) return r;
    return nullptr;
}

glm::vec3 readVec3(const json& a, glm::vec3 fallback) {
    if (!a.is_array() || a.size() < 3) return fallback;
    return {a[0].get<float>(), a[1].get<float>(), a[2].get<float>()};
}
// Scene stocke les quaternions en [x, y, z, w] ; glm::quat est (w, x, y, z).
glm::quat readQuat(const json& a, glm::quat fallback) {
    if (!a.is_array() || a.size() < 4) return fallback;
    return {a[3].get<float>(), a[0].get<float>(), a[1].get<float>(), a[2].get<float>()};
}

std::string err(const std::string& msg) {
    return json{{"ok", false}, {"error", msg}}.dump();
}
std::string ok(const std::string& applied, json diff) {
    return json{{"ok", true}, {"applied", applied}, {"diff", std::move(diff)}}.dump();
}
// Phase A5 : les ops reussies portent leur inverse ("inverse" = une SaidaOp
// prete a re-appliquer). C'est la brique undo/redo et dry-run (apply -> invert).
std::string ok(const std::string& applied, json diff, json inverse) {
    return json{{"ok", true}, {"applied", applied}, {"diff", std::move(diff)},
                {"inverse", std::move(inverse)}}.dump();
}
json inverseOp(const std::string& type, json payload) {
    return json{{"opVersion", kOpVersion}, {"type", type}, {"payload", std::move(payload)}};
}
json vec3Json(const glm::vec3& v) { return json::array({v.x, v.y, v.z}); }
// Convention scene : quaternions en [x, y, z, w].
json quatJson(const glm::quat& q) { return json::array({q.x, q.y, q.z, q.w}); }

const reflect::TypeDesc* reflectedNodeDesc(Node& n) {
    const std::string type = n.typeName() ? n.typeName() : "";
    if (type == LightNode::reflectName()) return &reflect::localDesc<LightNode>();
    if (type == WaterNode::reflectName()) return &reflect::localDesc<WaterNode>();
    if (type == ParticleSystemNode::reflectName()) return &reflect::localDesc<ParticleSystemNode>();
    return nullptr;
}

bool valueMatchesKind(const reflect::PropertyDesc& prop, const json& v, std::string& why) {
    const std::string& kind = prop.kind;
    if (kind == "bool" && !v.is_boolean()) {
        why = "expected bool";
        return false;
    }
    if (kind == "float" && !v.is_number()) {
        why = "expected number";
        return false;
    }
    if (kind == "int" && !v.is_number_integer()) {
        why = "expected integer";
        return false;
    }
    if ((kind == "string" || kind == "asset") && !v.is_string()) {
        why = "expected string";
        return false;
    }
    if (kind == "vec3" && (!v.is_array() || v.size() != 3)) {
        why = "expected vec3 array";
        return false;
    }
    if (kind == "vec4" && (!v.is_array() || v.size() != 4)) {
        why = "expected vec4 array";
        return false;
    }
    if (kind == "quat" && (!v.is_array() || v.size() != 4)) {
        why = "expected quat array";
        return false;
    }
    if (kind == "enum") {
        if (!v.is_number_integer()) {
            why = "expected enum integer";
            return false;
        }
        const int index = v.get<int>();
        if (!prop.enumLabels.empty() &&
            (index < 0 || index >= static_cast<int>(prop.enumLabels.size()))) {
            why = "enum value out of range";
            return false;
        }
    }
    return true;
}

// --- ops --------------------------------------------------------------------

std::string opSetTransform(Scene& scene, const json& p) {
    const std::string target = p.value("nodeId", std::string());
    Node* n = findByName(scene, target);
    if (!n) return err("unknown node '" + target + "'");
    json diff;
    json invPayload{{"nodeId", target}};  // ne restaure que les champs touches
    if (p.contains("position")) {
        invPayload["position"] = vec3Json(n->transform().position);
        n->transform().position = readVec3(p["position"], n->transform().position);
        diff["position"] = p["position"];
    }
    if (p.contains("rotation")) {
        invPayload["rotation"] = quatJson(n->transform().rotation);
        n->transform().rotation = readQuat(p["rotation"], n->transform().rotation);
        diff["rotation"] = p["rotation"];
    }
    if (p.contains("scale")) {
        invPayload["scale"] = vec3Json(n->transform().scale);
        n->transform().scale = readVec3(p["scale"], n->transform().scale);
        diff["scale"] = p["scale"];
    }
    Node::g_transformVersion++;
    return ok("set_transform", diff, inverseOp("set_transform", std::move(invPayload)));
}

std::string opCreateNode(Scene& scene, ResourceManager* resources, const json& p) {
    const std::string type = p.value("nodeType", std::string("MeshNode"));
    const std::string name = p.value("name", std::string("Node"));
    const std::string parentName = p.value("parent", std::string());

    Node* parent = parentName.empty() ? &scene : findByName(scene, parentName);
    if (!parent) return err("unknown parent '" + parentName + "'");

    if (type == "MeshNode") {
        // MeshNode references a mesh + material owned by the ResourceManager, so
        // it can only be created where those exist (editor/runtime, not headless).
        if (!resources) return err("create_node MeshNode needs ResourceManager");
        Mesh* mesh = resources->getMesh(kAssetBuiltinCube);
        Material* mat = resources->getMaterial(MaterialDesc{});
        parent->addChild(std::make_unique<MeshNode>(name, mesh, mat));
    } else {
        // Every other reflected node type is built by name from the NodeRegistry
        // (LightNode, Water, ParticleSystem, …) — no GPU resources needed.
        registerReflectedTypes();  // idempotent: ensure the registry is populated
        std::unique_ptr<Node> node = NodeRegistry::instance().create(type);
        if (!node) return err("unsupported nodeType '" + type + "'");
        node->setName(name);
        parent->addChild(std::move(node));
    }
    Node::g_hierarchyVersion++;
    // Inverse : supprimer le node cree (reference par nom au stade spike).
    json inv = inverseOp("delete_node", json{{"nodeId", name}});
    return ok("create_node", json{{"created", name}, {"nodeType", type}}, std::move(inv));
}

std::string opDeleteNode(Scene& scene, const json& p) {
    const std::string target = p.value("nodeId", std::string());
    Node* n = findByName(scene, target);
    if (!n) return err("unknown node '" + target + "'");
    if (n == &scene) return err("cannot delete scene root");
    Node* parent = n->parent();
    if (!parent) return err("node '" + target + "' has no parent");
    parent->removeChild(n);
    Node::g_hierarchyVersion++;
    // delete_node n'est PAS op-inversible (reconstruire le sous-arbre supprime
    // exige sa serialisation ; le pont de restauration est le snapshot,
    // invariant 0.6). L'op reussit mais ne propose pas d'inverse.
    return ok("delete_node", json{{"deleted", target}, {"invertible", false}});
}

std::string opRenameNode(Scene& scene, const json& p) {
    const std::string target = p.value("nodeId", std::string());
    const std::string newName = p.value("name", std::string());
    if (newName.empty()) return err("rename_node needs a non-empty name");
    Node* n = findByName(scene, target);
    if (!n) return err("unknown node '" + target + "'");
    n->setName(newName);
    // Inverse : renommer en sens inverse (nouveau nom -> ancien nom).
    json inv = inverseOp("rename_node", json{{"nodeId", newName}, {"name", target}});
    return ok("rename_node", json{{"from", target}, {"to", newName}}, std::move(inv));
}

std::string opReparentNode(Scene& scene, const json& p) {
    const std::string target = p.value("nodeId", std::string());
    const std::string parentName = p.value("newParent", std::string());

    Node* n = findByName(scene, target);
    if (!n) return err("unknown node '" + target + "'");
    if (n == &scene) return err("cannot reparent scene root");

    Node* newParent = parentName.empty() ? &scene : findByName(scene, parentName);
    if (!newParent) return err("unknown parent '" + parentName + "'");
    if (newParent == n) return err("cannot reparent node under itself");
    // Cycle : le nouveau parent ne doit pas etre un descendant du node deplace.
    for (Node* a = newParent->parent(); a; a = a->parent())
        if (a == n) return err("cannot reparent under a descendant (cycle)");

    Node* oldParent = n->parent();
    if (!oldParent) return err("node '" + target + "' has no parent");
    if (oldParent == newParent) {
        json inv = inverseOp("reparent_node",
                             json{{"nodeId", target}, {"newParent", newParent->name()}});
        return ok("reparent_node", json{{"nodeId", target}, {"unchanged", true}},
                  std::move(inv));
    }

    // Inverse : renvoyer le node sous son ancien parent. Capture avant mutation
    // (oldParent == scene racine -> newParent vide dans l'inverse).
    const std::string oldParentName = (oldParent == &scene) ? std::string() : oldParent->name();

    // La transform locale est conservee (la position monde peut changer) —
    // c'est le contrat le plus simple et deterministe pour l'op-log.
    std::unique_ptr<Node> owned = oldParent->detachChild(n);
    if (!owned) return err("failed to detach node '" + target + "'");
    newParent->addChild(std::move(owned));
    Node::g_hierarchyVersion++;
    json inv = inverseOp("reparent_node",
                         json{{"nodeId", target}, {"newParent", oldParentName}});
    return ok("reparent_node", json{{"nodeId", target},
                                    {"from", oldParent->name()},
                                    {"to", newParent->name()}}, std::move(inv));
}

std::string opSetProperty(Scene& scene, const json& p) {
    const std::string target = p.value("nodeId", std::string());
    const std::string prop = p.value("property", std::string());
    Node* n = findByName(scene, target);
    if (!n) return err("unknown node '" + target + "'");
    if (!p.contains("value")) return err("set_property needs a 'value'");
    const json& v = p["value"];

    // Inverse d'un set_property : re-set la meme propriete a la valeur d'avant.
    // Le nodeId de l'inverse doit referencer le node APRES l'op (cas 'name').
    auto setPropInverse = [&](const std::string& nodeIdAfter, const json& before) {
        return inverseOp("set_property", json{{"nodeId", nodeIdAfter},
                                              {"property", prop}, {"value", before}});
    };

    if (prop == "name") {
        if (!v.is_string()) return err("property 'name' expects string");
        const std::string before = n->name();
        n->setName(v.get<std::string>());
        return ok("set_property", json{{"nodeId", target}, {"property", prop},
                                      {"before", before}, {"after", n->name()}},
                  setPropInverse(n->name(), before));
    }
    if (prop == "enabled") {
        if (!v.is_boolean()) return err("property 'enabled' expects bool");
        const bool before = n->enabled();
        n->setEnabled(v.get<bool>());
        return ok("set_property", json{{"nodeId", target}, {"property", prop},
                                      {"before", before}, {"after", n->enabled()}},
                  setPropInverse(target, before));
    }

    const reflect::TypeDesc* desc = reflectedNodeDesc(*n);
    if (!desc) {
        return err("node type '" + std::string(n->typeName() ? n->typeName() : "<unknown>") +
                   "' has no reflected authoring properties");
    }
    const reflect::PropertyDesc* reflected = desc->findProperty(prop);
    if (!reflected) {
        return err("unknown reflected property '" + prop + "' on " + desc->name);
    }

    std::string why;
    if (!valueMatchesKind(*reflected, v, why)) {
        return err("property '" + prop + "' expects " + reflected->kind + " (" + why + ")");
    }

    json before;
    try {
        reflected->get(n, before);
        reflected->set(n, v);
    } catch (const std::exception& e) {
        return err("failed to set reflected property '" + prop + "': " + std::string(e.what()));
    }
    json after;
    reflected->get(n, after);
    json inv = setPropInverse(target, before);
    return ok("set_property", json{{"nodeId", target}, {"nodeType", n->typeName()},
                                  {"property", prop}, {"kind", reflected->kind},
                                  {"before", std::move(before)}, {"after", std::move(after)}},
              std::move(inv));
}

// set_scene_setting : édite l'ambiance de scène (SceneSettings) par nom. Sous-
// ensemble curé, pertinent pour l'éditeur (fog/bloom/ambient/ibl/gi/skybox).
// Renvoie un inverse re-appliquable (undo). Les couleurs sont stockées en vec4
// mais éditées en vec3 (xyz), w conservé.
std::string opSetSceneSetting(Scene& scene, const json& p) {
    const std::string key = p.value("setting", std::string());
    if (key.empty()) return err("set_scene_setting needs 'setting'");
    if (!p.contains("value")) return err("set_scene_setting needs a 'value'");
    const json& v = p["value"];
    SceneSettings& s = scene.settings();

    auto sceneInverse = [&](const json& before) {
        return inverseOp("set_scene_setting", json{{"setting", key}, {"value", before}});
    };
    auto okScene = [&](const json& before, const json& after) {
        return ok("set_scene_setting",
                  json{{"setting", key}, {"before", before}, {"after", after}},
                  sceneInverse(before));
    };
    auto setFloat = [&](float& field) -> std::string {
        if (!v.is_number()) return err("scene setting '" + key + "' expects a number");
        const float before = field;
        field = v.get<float>();
        return okScene(before, field);
    };
    auto setBool = [&](bool& field) -> std::string {
        if (!v.is_boolean()) return err("scene setting '" + key + "' expects a bool");
        const bool before = field;
        field = v.get<bool>();
        return okScene(before, field);
    };
    // Colour stored as vec4; edited as a vec3, alpha preserved.
    auto setColor = [&](glm::vec4& field) -> std::string {
        if (!v.is_array() || v.size() != 3) return err("scene setting '" + key + "' expects a vec3");
        const json before = json::array({field.x, field.y, field.z});
        field.x = v[0].get<float>();
        field.y = v[1].get<float>();
        field.z = v[2].get<float>();
        return okScene(before, json::array({field.x, field.y, field.z}));
    };

    if (key == "fogEnabled") return setBool(s.fogEnabled);
    if (key == "bloomEnabled") return setBool(s.bloomEnabled);
    if (key == "aoEnabled") return setBool(s.aoEnabled);
    if (key == "iblEnabled") return setBool(s.iblEnabled);
    if (key == "giEnabled") return setBool(s.giEnabled);
    if (key == "enablePostProcessing") return setBool(s.enablePostProcessing);

    if (key == "fogStart") return setFloat(s.fogStart);
    if (key == "fogDensity") return setFloat(s.fogDensity);
    if (key == "bloomThreshold") return setFloat(s.bloomThreshold);
    if (key == "bloomIntensity") return setFloat(s.bloomIntensity);
    if (key == "bloomRadius") return setFloat(s.bloomRadius);
    if (key == "aoRadius") return setFloat(s.aoRadius);
    if (key == "aoIntensity") return setFloat(s.aoIntensity);
    if (key == "aoPower") return setFloat(s.aoPower);
    if (key == "giIntensity") return setFloat(s.giIntensity);
    if (key == "iblDiffuseIntensity") return setFloat(s.iblDiffuseIntensity);
    if (key == "iblSpecularIntensity") return setFloat(s.iblSpecularIntensity);
    if (key == "skyboxExposure") return setFloat(s.skyboxExposure);
    if (key == "skyboxRotation") return setFloat(s.skyboxRotation);

    if (key == "ambientLight") return setColor(s.ambientLight);
    if (key == "clearColor") return setColor(s.clearColor);
    if (key == "fogColor") return setColor(s.fogColor);

    return err("unknown scene setting '" + key + "'");
}

// --- behaviours (gameplay logic) --------------------------------------------

Behaviour* findBehaviourByType(Node& n, const std::string& type) {
    for (const auto& b : n.behaviours())
        if (b->typeName() && type == b->typeName()) return b.get();
    return nullptr;
}

std::string opAddBehaviour(Scene& scene, const json& p) {
    const std::string target = p.value("nodeId", std::string());
    const std::string btype = p.value("behaviourType", std::string());
    Node* n = findByName(scene, target);
    if (!n) return err("unknown node '" + target + "'");
    if (btype.empty()) return err("add_behaviour needs 'behaviourType'");

    registerReflectedTypes();  // idempotent: ensure the factory registry is ready
    // One behaviour of a given type per node keeps add/remove/inverse unambiguous.
    if (findBehaviourByType(*n, btype))
        return err("node '" + target + "' already has behaviour '" + btype + "'");
    std::unique_ptr<Behaviour> b = BehaviourRegistry::instance().create(btype);
    if (!b) return err("unknown behaviour type '" + btype + "'");
    n->addBehaviour(std::move(b));

    json inv = inverseOp("remove_behaviour",
                         json{{"nodeId", target}, {"behaviourType", btype}});
    return ok("add_behaviour", json{{"nodeId", target}, {"behaviourType", btype}},
              std::move(inv));
}

std::string opRemoveBehaviour(Scene& scene, const json& p) {
    const std::string target = p.value("nodeId", std::string());
    const std::string btype = p.value("behaviourType", std::string());
    Node* n = findByName(scene, target);
    if (!n) return err("unknown node '" + target + "'");
    Behaviour* b = findBehaviourByType(*n, btype);
    if (!b) return err("node '" + target + "' has no behaviour '" + btype + "'");
    n->removeBehaviour(b);
    // Not op-invertible: restoring the removed behaviour's properties needs the
    // snapshot (same rule as delete_node, invariant 0.6).
    return ok("remove_behaviour",
              json{{"nodeId", target}, {"behaviourType", btype}, {"invertible", false}});
}

std::string opSetBehaviourProperty(Scene& scene, const json& p) {
    const std::string target = p.value("nodeId", std::string());
    const std::string btype = p.value("behaviourType", std::string());
    const std::string prop = p.value("property", std::string());
    Node* n = findByName(scene, target);
    if (!n) return err("unknown node '" + target + "'");
    if (!p.contains("value")) return err("set_behaviour_property needs a 'value'");
    const json& v = p["value"];

    registerReflectedTypes();
    Behaviour* b = findBehaviourByType(*n, btype);
    if (!b) return err("node '" + target + "' has no behaviour '" + btype + "'");
    const reflect::TypeDesc* desc = reflect::TypeRegistry::instance().find(btype);
    if (!desc) return err("behaviour '" + btype + "' has no reflected contract");
    const reflect::PropertyDesc* reflected = desc->findProperty(prop);
    if (!reflected) return err("unknown behaviour property '" + prop + "' on " + btype);

    std::string why;
    if (!valueMatchesKind(*reflected, v, why))
        return err("property '" + prop + "' expects " + reflected->kind + " (" + why + ")");

    json before;
    try {
        reflected->get(b, before);
        reflected->set(b, v);
    } catch (const std::exception& e) {
        return err("failed to set behaviour property '" + prop + "': " + std::string(e.what()));
    }
    json after;
    reflected->get(b, after);
    json inv = inverseOp("set_behaviour_property",
                         json{{"nodeId", target}, {"behaviourType", btype},
                              {"property", prop}, {"value", before}});
    return ok("set_behaviour_property",
              json{{"nodeId", target}, {"behaviourType", btype}, {"property", prop},
                   {"kind", reflected->kind}, {"before", std::move(before)},
                   {"after", std::move(after)}},
              std::move(inv));
}

// Signal connections are data-driven links stored in the scene's "connections"
// block (SignalConnectionDef {from, signal, to, slot}), round-tripped by the
// snapshot and wired by SignalWiring at Play. Here we only edit the data — node
// refs come in as names (like every other op) and resolve to NodeId at rest.
std::string opAddSignalConnection(Scene& scene, const json& p) {
    const std::string fromName = p.value("from", std::string());
    const std::string signal = p.value("signal", std::string());
    const std::string toName = p.value("to", std::string());
    const std::string slot = p.value("slot", std::string());

    Node* from = findByName(scene, fromName);
    if (!from) return err("unknown node '" + fromName + "'");
    Node* to = findByName(scene, toName);
    if (!to) return err("unknown node '" + toName + "'");

    for (const auto& c : scene.connections()) {
        if (c.from == from->id() && c.to == to->id() && c.signal == signal && c.slot == slot)
            return err("signal connection already exists");
    }

    scene.connections().push_back(
        SignalConnectionDef{from->id(), signal, to->id(), slot});

    json diff{{"from", fromName}, {"signal", signal}, {"to", toName}, {"slot", slot}};
    return ok("add_signal_connection", diff,
              inverseOp("remove_signal_connection", diff));
}

std::string opRemoveSignalConnection(Scene& scene, const json& p) {
    const std::string fromName = p.value("from", std::string());
    const std::string signal = p.value("signal", std::string());
    const std::string toName = p.value("to", std::string());
    const std::string slot = p.value("slot", std::string());

    Node* from = findByName(scene, fromName);
    if (!from) return err("unknown node '" + fromName + "'");
    Node* to = findByName(scene, toName);
    if (!to) return err("unknown node '" + toName + "'");

    auto& conns = scene.connections();
    for (auto it = conns.begin(); it != conns.end(); ++it) {
        if (it->from == from->id() && it->to == to->id() &&
            it->signal == signal && it->slot == slot) {
            conns.erase(it);
            json diff{{"from", fromName}, {"signal", signal}, {"to", toName}, {"slot", slot}};
            return ok("remove_signal_connection", diff,
                      inverseOp("add_signal_connection", diff));
        }
    }
    return err("signal connection not found");
}

} // namespace

std::string applyOpJson(Scene& scene, ResourceManager* resources,
                        const std::string& opJson) {
    // Parse + validation de forme via le contrat SaidaOp (Phase A1) : version,
    // type connu, payload objet. Aucune mutation tant que ce n'est pas valide.
    const SaidaOpParseResult parsed = parseSaidaOp(opJson);
    if (!parsed.ok) return err(parsed.error);

    const std::string& type = parsed.op.type;
    const json& p = parsed.op.payload;

    if (type == "set_transform") return opSetTransform(scene, p);
    if (type == "create_node")   return opCreateNode(scene, resources, p);
    if (type == "delete_node")   return opDeleteNode(scene, p);
    if (type == "rename_node")   return opRenameNode(scene, p);
    if (type == "reparent_node") return opReparentNode(scene, p);
    if (type == "set_property")  return opSetProperty(scene, p);
    if (type == "set_scene_setting") return opSetSceneSetting(scene, p);
    if (type == "add_behaviour") return opAddBehaviour(scene, p);
    if (type == "remove_behaviour") return opRemoveBehaviour(scene, p);
    if (type == "set_behaviour_property") return opSetBehaviourProperty(scene, p);
    if (type == "add_signal_connection") return opAddSignalConnection(scene, p);
    if (type == "remove_signal_connection") return opRemoveSignalConnection(scene, p);
    return err("op type '" + type + "' is registered but not implemented");
}

std::string applyOpJson(Scene& scene, ResourceManager& resources,
                        const std::string& opJson) {
    return applyOpJson(scene, &resources, opJson);
}

} // namespace saida::authoring
