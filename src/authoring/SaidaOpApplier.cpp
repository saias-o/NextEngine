#include "authoring/SaidaOpApplier.hpp"

#include "authoring/SaidaOp.hpp"
#include "core/Reflection.hpp"
#include "graphics/Material.hpp"
#include "graphics/ResourceManager.hpp"
#include "scene/LightNode.hpp"
#include "scene/MeshNode.hpp"
#include "scene/Node.hpp"
#include "scene/ParticleSystemNode.hpp"
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
    if (p.contains("position")) {
        n->transform().position = readVec3(p["position"], n->transform().position);
        diff["position"] = p["position"];
    }
    if (p.contains("rotation")) {
        n->transform().rotation = readQuat(p["rotation"], n->transform().rotation);
        diff["rotation"] = p["rotation"];
    }
    if (p.contains("scale")) {
        n->transform().scale = readVec3(p["scale"], n->transform().scale);
        diff["scale"] = p["scale"];
    }
    Node::g_transformVersion++;
    return ok("set_transform", diff);
}

std::string opCreateNode(Scene& scene, ResourceManager* resources, const json& p) {
    const std::string type = p.value("nodeType", std::string("MeshNode"));
    const std::string name = p.value("name", std::string("Node"));
    const std::string parentName = p.value("parent", std::string());

    Node* parent = parentName.empty() ? &scene : findByName(scene, parentName);
    if (!parent) return err("unknown parent '" + parentName + "'");

    if (type == "MeshNode") {
        if (!resources) return err("create_node MeshNode needs ResourceManager");
        Mesh* mesh = resources->getMesh(kAssetBuiltinCube);
        Material* mat = resources->getMaterial(MaterialDesc{});
        parent->addChild(std::make_unique<MeshNode>(name, mesh, mat));
    } else if (type == "LightNode") {
        parent->addChild(std::make_unique<LightNode>(name, LightType::Point));
    } else {
        return err("unsupported nodeType '" + type + "' (spike)");
    }
    Node::g_hierarchyVersion++;
    return ok("create_node", json{{"created", name}, {"nodeType", type}});
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
    return ok("delete_node", json{{"deleted", target}});
}

std::string opRenameNode(Scene& scene, const json& p) {
    const std::string target = p.value("nodeId", std::string());
    const std::string newName = p.value("name", std::string());
    if (newName.empty()) return err("rename_node needs a non-empty name");
    Node* n = findByName(scene, target);
    if (!n) return err("unknown node '" + target + "'");
    n->setName(newName);
    return ok("rename_node", json{{"from", target}, {"to", newName}});
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
    if (oldParent == newParent)
        return ok("reparent_node", json{{"nodeId", target}, {"unchanged", true}});

    // La transform locale est conservee (la position monde peut changer) —
    // c'est le contrat le plus simple et deterministe pour l'op-log.
    std::unique_ptr<Node> owned = oldParent->detachChild(n);
    if (!owned) return err("failed to detach node '" + target + "'");
    newParent->addChild(std::move(owned));
    Node::g_hierarchyVersion++;
    return ok("reparent_node", json{{"nodeId", target},
                                    {"from", oldParent->name()},
                                    {"to", newParent->name()}});
}

std::string opSetProperty(Scene& scene, const json& p) {
    const std::string target = p.value("nodeId", std::string());
    const std::string prop = p.value("property", std::string());
    Node* n = findByName(scene, target);
    if (!n) return err("unknown node '" + target + "'");
    if (!p.contains("value")) return err("set_property needs a 'value'");
    const json& v = p["value"];

    if (prop == "name") {
        if (!v.is_string()) return err("property 'name' expects string");
        const std::string before = n->name();
        n->setName(v.get<std::string>());
        return ok("set_property", json{{"nodeId", target}, {"property", prop},
                                      {"before", before}, {"after", n->name()}});
    }
    if (prop == "enabled") {
        if (!v.is_boolean()) return err("property 'enabled' expects bool");
        const bool before = n->enabled();
        n->setEnabled(v.get<bool>());
        return ok("set_property", json{{"nodeId", target}, {"property", prop},
                                      {"before", before}, {"after", n->enabled()}});
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
    return ok("set_property", json{{"nodeId", target}, {"nodeType", n->typeName()},
                                  {"property", prop}, {"kind", reflected->kind},
                                  {"before", std::move(before)}, {"after", std::move(after)}});
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
    return err("op type '" + type + "' is registered but not implemented");
}

std::string applyOpJson(Scene& scene, ResourceManager& resources,
                        const std::string& opJson) {
    return applyOpJson(scene, &resources, opJson);
}

} // namespace saida::authoring
