#pragma once

// AnimationSequence — montage multipiste déterministe (.sseq, JSON). Une piste
// d'animation cible un personnage (clips posés sur la timeline avec trim,
// vitesse, boucle et blends d'entrée/sortie), une piste d'événements émet des
// signaux, une piste de propriété anime une valeur réfléchie via la Timeline
// existante. Le runtime ne lit jamais le JSON : SequencePlayer compile la
// séquence en nœuds d'animation injectés dans les Animators cibles.
//
// Schéma JSON (schema == kAnimationSequenceSchema) :
//   {
//     "schema": 1,
//     "name": "Intro",
//     "duration": 10.0,
//     "tracks": [
//       { "type": "animation", "target": "Hero", "clips": [
//           { "start": 0.0, "duration": 4.0, "clip": "hero.glb#Walk",
//             "trimStart": 0.5, "speed": 1.0, "loop": true,
//             "blendIn": 0.25, "blendOut": 0.25 } ] },
//       { "type": "event", "events": [ { "time": 2.0, "name": "door_open" } ] },
//       { "type": "property", "target": "sun.intensity", "keys": [
//           { "time": 0.0, "value": 1.0 }, { "time": 3.0, "value": 0.2 } ] }
//     ]
//   }

#include "core/Signal.hpp"
#include "scene/animation/AnimNode.hpp"
#include "scene/animation/ClipView.hpp"  // AssetDiagnostic, ClipViewEvent
#include "scene/animation/Timeline.hpp"

#include <nlohmann/json.hpp>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace saida {

class AnimationClip;
class Animator;
class ClipNode;
class Rig;

constexpr int kAnimationSequenceSchema = 1;

struct SequenceClipEntry {
    float start = 0.0f;      // secondes sur la timeline
    float duration = 0.0f;   // durée occupée sur la timeline
    std::string clip;        // clé de sous-asset "fichier#clip"
    float trimStart = 0.0f;  // décalage dans la source
    float speed = 1.0f;
    bool loop = false;
    float blendIn = 0.0f;
    float blendOut = 0.0f;

    float end() const { return start + duration; }
    // Poids de blend [0,1] à `time` (rampes d'entrée/sortie linéaires).
    float weightAt(float time) const;
};

struct SequenceAnimationTrack {
    std::string target;  // nom logique du personnage (résolu par le player)
    std::vector<SequenceClipEntry> clips;
};

struct SequencePropertyKey {
    float time = 0.0f;
    nlohmann::json value;
};

struct SequencePropertyTrack {
    std::string target;  // chemin de propriété, résolu par le binder du player
    std::vector<SequencePropertyKey> keys;
};

struct AnimationSequenceParseResult;

class AnimationSequence {
public:
    static AnimationSequenceParseResult parse(const nlohmann::json& j);
    static AnimationSequenceParseResult loadFile(const std::string& path);

    nlohmann::json toJson() const;
    bool saveFile(const std::string& path) const;

    // Cohérence interne : durées positives, clips dans la timeline, blends
    // plus courts que leur clip, événements dans la séquence (warning).
    std::vector<AssetDiagnostic> validate() const;

    std::string name;
    float duration = 0.0f;
    std::vector<SequenceAnimationTrack> animationTracks;
    std::vector<ClipViewEvent> events;
    std::vector<SequencePropertyTrack> propertyTracks;
};

struct AnimationSequenceParseResult {
    bool ok = false;
    AnimationSequence sequence;
    std::vector<AssetDiagnostic> diagnostics;
};

// Nœud runtime d'une piste d'animation : les clips résolus sont échantillonnés
// au temps de la séquence (seek déterministe) et mélangés par leurs rampes.
class SequenceTrackNode : public AnimNode {
public:
    SequenceTrackNode(const SequenceAnimationTrack& track,
                      const std::function<const AnimationClip*(const std::string&)>& resolveClip,
                      const Rig& rig, float sequenceDuration,
                      std::vector<AssetDiagnostic>* diagnostics = nullptr);

    void update(float deltaTime) override;
    void evaluate(const LocalPose& bindPose, LocalPose& outPose) const override;

    void setTime(float time);
    float time() const { return time_; }
    bool empty() const { return entries_.empty(); }

private:
    struct Entry {
        SequenceClipEntry placement;
        std::unique_ptr<ClipNode> node;
    };

    float localClipTime(const SequenceClipEntry& placement, float clipDuration) const;

    std::vector<Entry> entries_;
    float duration_ = 0.0f;
    float time_ = 0.0f;

    mutable LocalPose scratchPose_;
};

// Pilote une AnimationSequence : injecte un SequenceTrackNode dans chaque
// Animator cible, relie les pistes de propriétés à la Timeline réflexive et
// émet les événements en lecture. seek() est déterministe et silencieux.
class SequencePlayer {
public:
    using ClipResolver = std::function<const AnimationClip*(const std::string& key)>;
    using AnimatorResolver = std::function<Animator*(const std::string& target)>;
    // Le binder attache une piste de propriété à sa cible réfléchie ;
    // retourner false laisse la piste débranchée (diagnostic).
    using PropertyBinder =
        std::function<bool(const std::string& target, TimelinePropertyTrack& track)>;

    bool bind(const AnimationSequence& sequence, const AnimatorResolver& resolveAnimator,
              const ClipResolver& resolveClip, const PropertyBinder& bindProperty = nullptr,
              std::vector<AssetDiagnostic>* diagnostics = nullptr);

    void seek(float time);       // positionne tout, n'émet aucun événement
    void update(float deltaTime);  // avance, évalue les propriétés, émet
    float time() const { return time_; }
    float duration() const { return duration_; }
    bool finished() const { return time_ >= duration_; }

    Signal<const std::string&> sequenceEvent;

private:
    void applyTime();

    std::vector<SequenceTrackNode*> trackNodes_;  // possédés par les Animators
    Timeline propertyTimeline_;
    std::vector<ClipViewEvent> events_;
    float duration_ = 0.0f;
    float time_ = 0.0f;
};

} // namespace saida
