#pragma once

// ClipView — vue non destructive d'une animation source.
// Un fichier .sclip décrit une découpe/boucle/vitesse d'un clip importé sans
// jamais dupliquer ses clés : plusieurs vues (Idle, Walk, RunStart…) partagent
// la même AnimationClip source.
//
// Schéma JSON (schema == kClipViewSchema) :
//   {
//     "schema": 1,
//     "source": "models/mocap.glb#Take1",   // clé de sous-asset (fichier#clip)
//     "name": "RunLoop",
//     "range": { "start": 1.2, "end": 2.05 },  // optionnel : défaut = tout le clip
//     "loop": true,
//     "speed": 1.0,
//     "events": [ { "time": 0.18, "name": "left_foot_down" } ]
//   }

#include <memory>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace saida {

class AnimationClip;
class ClipNode;
class Rig;

// Diagnostic structuré d'un asset d'authoring :
// code stable pour l'outillage, chemin JSON pour pointer le champ fautif,
// message lisible pour l'humain/LLM.
struct AssetDiagnostic {
    enum class Severity { Error, Warning };

    std::string code;      // ex. "clipview.range.reversed"
    Severity severity = Severity::Error;
    std::string jsonPath;  // ex. "/range/end"
    std::string message;

    nlohmann::json toJson() const;
};

bool hasErrors(const std::vector<AssetDiagnostic>& diagnostics);

constexpr int kClipViewSchema = 1;

struct ClipViewEvent {
    float time = 0.0f;  // secondes, dans l'espace temps de la SOURCE
    std::string name;
};

struct ClipViewParseResult;

class ClipView {
public:
    // Parse strict : un JSON invalide ou d'un autre schéma produit des
    // diagnostics et ok=false.
    static ClipViewParseResult parse(const nlohmann::json& j);
    static ClipViewParseResult loadFile(const std::string& path);

    nlohmann::json toJson() const;
    bool saveFile(const std::string& path) const;

    // Validation contre la source résolue (durées, plages, événements).
    // Utilisable sans source (nullptr) : ne vérifie alors que la cohérence interne.
    std::vector<AssetDiagnostic> validate(const AnimationClip* source) const;

    // Construit le nœud de lecture : le ClipNode référence la source partagée
    // (aucune copie de clés) et applique range/speed/loop.
    std::unique_ptr<ClipNode> instantiate(const AnimationClip& source, const Rig& rig) const;

    // Plage effective une fois résolue contre la source (défauts remplis).
    float effectiveStart() const { return hasRange ? rangeStart : 0.0f; }
    float effectiveEnd(const AnimationClip& source) const;

    std::string source;  // clé de sous-asset "fichier#clip"
    std::string name;
    bool hasRange = false;
    float rangeStart = 0.0f;
    float rangeEnd = 0.0f;
    bool loop = true;
    float speed = 1.0f;
    std::vector<ClipViewEvent> events;

};

struct ClipViewParseResult {
    bool ok = false;
    ClipView view;
    std::vector<AssetDiagnostic> diagnostics;
};

} // namespace saida
