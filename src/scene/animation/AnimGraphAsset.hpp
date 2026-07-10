#pragma once

// AnimGraphAsset — l'asset persistant de logique de lecture (.sgraph, JSON,
// PLAN_ANIMATION.md §4.1/§7). L'éditeur visuel et les outils LLM manipulent ce
// document ; le runtime n'évalue jamais le JSON — build() le compile en
// AnimStateMachine. Le DSL texte d'AnimGraphParser reste un chemin legacy.
//
// Schéma JSON (schema == kAnimGraphSchema) :
//   {
//     "schema": 1,
//     "name": "Locomotion",
//     "parameters": [ { "name": "speed", "type": "float", "default": 0 } ],
//     "clips": { "idle": "models/hero.glb#Idle" },   // alias → clé de sous-asset
//     "states": [ { "name": "Idle", "play": "idle", "loop": true } ],
//     "initial": "Idle",
//     "transitions": [
//       { "from": "Idle", "to": "Walk", "crossfade": 0.2,
//         "when": [ { "param": "speed", "op": ">", "value": 0.1 } ] }
//     ]
//   }

#include "scene/animation/ClipView.hpp"  // AssetDiagnostic

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace saida {

class AnimationClip;
class AnimStateMachine;
class Rig;

constexpr int kAnimGraphSchema = 1;

// Types de paramètres du schéma 1. "trigger" (plan §7.3) est réservé : le
// runtime ne le supporte pas encore, le parse le refuse explicitement.
enum class AnimParamType { Float, Int, Bool };

struct AnimGraphParam {
    std::string name;
    AnimParamType type = AnimParamType::Float;
    float defaultValue = 0.0f;
};

struct AnimGraphClipRef {
    std::string alias;  // nom local dans le graphe
    std::string key;    // clé de sous-asset "fichier#clip"
};

struct AnimGraphState {
    std::string name;
    std::string play;  // alias de clip
    bool loop = true;
};

struct AnimGraphCondition {
    std::string param;
    std::string op;  // "==", "!=", ">", "<", ">=", "<="
    float value = 0.0f;
};

struct AnimGraphTransition {
    std::string from;
    std::string to;
    float crossfade = 0.0f;
    std::vector<AnimGraphCondition> when;  // conditions ET
};

struct AnimGraphParseResult;

class AnimGraphAsset {
public:
    static AnimGraphParseResult parse(const nlohmann::json& j);
    static AnimGraphParseResult loadFile(const std::string& path);

    nlohmann::json toJson() const;
    bool saveFile(const std::string& path) const;

    // Cohérence interne : états/alias/paramètres référencés existent, initial
    // valide, doublons, états inaccessibles depuis initial (warning).
    std::vector<AssetDiagnostic> validate() const;

    // Compile vers la machine d'états runtime. `resolveClip` traduit une clé de
    // sous-asset en clip chargé (null = introuvable → diagnostic, état ignoré).
    // Retourne null si aucun état n'a pu être construit.
    std::unique_ptr<AnimStateMachine> build(
        const std::function<const AnimationClip*(const std::string& key)>& resolveClip,
        const Rig& rig, std::vector<AssetDiagnostic>* diagnostics = nullptr) const;

    std::string name;
    std::vector<AnimGraphParam> parameters;
    std::vector<AnimGraphClipRef> clips;
    std::vector<AnimGraphState> states;
    std::string initial;
    std::vector<AnimGraphTransition> transitions;

    const AnimGraphClipRef* findClip(const std::string& alias) const;
    const AnimGraphState* findState(const std::string& stateName) const;
};

struct AnimGraphParseResult {
    bool ok = false;
    AnimGraphAsset graph;
    std::vector<AssetDiagnostic> diagnostics;
};

} // namespace saida
