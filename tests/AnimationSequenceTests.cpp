// Tests des séquences .sseq : roundtrip et validation du schéma, lecture
// déterministe (seek), crossfade entre deux clips qui se chevauchent,
// événements émis en lecture seulement, piste de propriété réfléchie.

#include "scene/RotatorBehaviour.hpp"
#include "scene/animation/AnimationClip.hpp"
#include "scene/animation/AnimationSequence.hpp"
#include "scene/animation/Animator.hpp"
#include "scene/animation/Rig.hpp"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>

namespace {

bool near(float a, float b, float tolerance = 1e-4f) {
    return std::abs(a - b) < tolerance;
}

bool hasDiagnostic(const std::vector<saida::AssetDiagnostic>& diags, const char* code) {
    for (const auto& d : diags)
        if (d.code == code) return true;
    return false;
}

saida::Rig makeRig() {
    saida::Rig rig;
    saida::Transform rest;
    rest.position = {0.0f, 1.0f, 0.0f};
    rig.addBone("root", -1, glm::mat4(1.0f), rest);
    std::string error;
    assert(rig.finalize(&error));
    return rig;
}

// Clip 1 s : translation X du root de `from` à `to`.
std::unique_ptr<saida::AnimationClip> makeClip(const char* name, float from, float to) {
    auto clip = std::make_unique<saida::AnimationClip>(name, 1.0f);
    auto track = std::make_unique<saida::TypedAnimTrack<glm::vec3>>();
    track->target = saida::TrackTarget::Translation;
    track->timestamps = {0.0f, 1.0f};
    track->values = {{from, 1.0f, 0.0f}, {to, 1.0f, 0.0f}};
    clip->addTrack("root", std::move(track));
    return clip;
}

saida::AnimationSequence makeSequence() {
    saida::AnimationSequence sequence;
    sequence.name = "Intro";
    sequence.duration = 3.0f;

    saida::SequenceAnimationTrack track;
    track.target = "Hero";
    saida::SequenceClipEntry first;
    first.start = 0.0f;
    first.duration = 1.5f;
    first.clip = "synthetic#walk";
    first.blendOut = 0.5f;
    saida::SequenceClipEntry second;
    second.start = 1.0f;
    second.duration = 2.0f;
    second.clip = "synthetic#pose";
    second.blendIn = 0.5f;
    track.clips = {first, second};
    sequence.animationTracks.push_back(std::move(track));

    sequence.events.push_back({1.25f, "handoff"});

    saida::SequencePropertyTrack property;
    property.target = "rotator.speed";
    property.keys.push_back({0.0f, 0.0f});
    property.keys.push_back({3.0f, 6.0f});
    sequence.propertyTracks.push_back(std::move(property));
    return sequence;
}

void testRoundtripAndValidation() {
    const saida::AnimationSequence sequence = makeSequence();
    assert(sequence.validate().empty());

    auto parsed = saida::AnimationSequence::parse(sequence.toJson());
    assert(parsed.ok);
    assert(parsed.sequence.animationTracks.size() == 1);
    assert(parsed.sequence.animationTracks[0].clips.size() == 2);
    assert(near(parsed.sequence.animationTracks[0].clips[0].blendOut, 0.5f));
    assert(parsed.sequence.events.size() == 1);
    assert(parsed.sequence.propertyTracks.size() == 1);

    // Schéma plus récent refusé ; clip hors timeline signalé.
    nlohmann::json future = sequence.toJson();
    future["schema"] = saida::kAnimationSequenceSchema + 1;
    assert(!saida::AnimationSequence::parse(future).ok);

    saida::AnimationSequence broken = sequence;
    broken.animationTracks[0].clips[1].duration = 5.0f;
    assert(hasDiagnostic(broken.validate(), "sequence.clip.outside_timeline"));
}

void testDeterministicPlayback() {
    saida::Rig rig = makeRig();
    auto walk = makeClip("walk", 0.0f, 2.0f);
    auto pose = makeClip("pose", 5.0f, 5.0f);

    saida::Animator animator;
    animator.setRig(&rig);

    saida::RotatorBehaviour rotator;

    const saida::AnimationSequence sequence = makeSequence();
    saida::SequencePlayer player;
    std::vector<saida::AssetDiagnostic> diags;
    const bool bound = player.bind(
        sequence,
        [&](const std::string& target) { return target == "Hero" ? &animator : nullptr; },
        [&](const std::string& key) -> const saida::AnimationClip* {
            if (key == "synthetic#walk") return walk.get();
            if (key == "synthetic#pose") return pose.get();
            return nullptr;
        },
        [&](const std::string& target, saida::TimelinePropertyTrack& track) {
            if (target != "rotator.speed") return false;
            track.bind(rotator, "speed");
            return true;
        },
        &diags);
    assert(bound && diags.empty());

    // Seek déterministe : t=0.5 → walk seul (x = 1.0).
    std::vector<std::string> fired;
    saida::Connection events =
        player.sequenceEvent.connect([&](const std::string& name) { fired.push_back(name); });

    player.seek(0.5f);
    animator.onUpdate(0.0f);
    assert(near(animator.globalPose().globalMatrices[0][3].x, 1.0f, 1e-3f));
    assert(fired.empty());  // un seek n'émet pas

    // t=1.25 : milieu du chevauchement — walk (x=2.5→clampé 2.0) pèse 0.5,
    // pose (x=5) pèse 0.5 → blend séquentiel : mix(2.0, 5.0, 0.5) = 3.5.
    player.seek(1.25f);
    animator.onUpdate(0.0f);
    assert(near(animator.globalPose().globalMatrices[0][3].x, 3.5f, 1e-3f));

    // Deux seeks au même temps donnent la même pose (déterminisme).
    player.seek(0.5f);
    player.seek(1.25f);
    animator.onUpdate(0.0f);
    assert(near(animator.globalPose().globalMatrices[0][3].x, 3.5f, 1e-3f));

    // Lecture : l'événement à 1.25 s est émis une fois, la propriété suit.
    player.seek(0.0f);
    player.update(1.5f);
    assert(fired.size() == 1 && fired[0] == "handoff");
    assert(near(rotator.speed, 3.0f, 1e-3f));  // rampe 0→6 sur 3 s, à t=1.5

    player.update(10.0f);  // clampe à la fin
    assert(player.finished());
    assert(near(rotator.speed, 6.0f, 1e-3f));
}

} // namespace

int main() {
    testRoundtripAndValidation();
    testDeterministicPlayback();
    std::puts("saida_animation_sequence_tests: OK");
    return 0;
}
