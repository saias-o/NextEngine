// saida_animation_bench — headless benchmark of the current animation runtime
// (PLAN_ANIMATION.md §14.4 / Étape 0). Builds a synthetic humanoid-sized rig and
// clip, then measures Animator::onUpdate (sampling + blend + global solve) for a
// population of characters. No GPU, no window: the numbers are the CPU cost the
// renderer would pay before palette upload.
//
// Usage: saida_animation_bench [--chars N] [--bones N] [--keys N] [--frames N]

#include "scene/animation/AnimationClip.hpp"
#include "scene/animation/Animator.hpp"
#include "scene/animation/Rig.hpp"

#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace {

int argInt(int argc, char** argv, const char* name, int fallback) {
    for (int i = 1; i + 1 < argc; ++i)
        if (std::strcmp(argv[i], name) == 0) return std::atoi(argv[i + 1]);
    return fallback;
}

// A chain-heavy rig: a root plus branches of ~8 bones, like limbs off a spine.
saida::Rig makeRig(int boneCount) {
    saida::Rig rig;
    for (int i = 0; i < boneCount; ++i) {
        const int parent = i == 0 ? -1 : (i % 8 == 0 ? 0 : i - 1);
        saida::Transform rest;
        rest.position = {0.0f, 0.25f, 0.0f};
        rig.addBone("bone_" + std::to_string(i), parent, glm::mat4(1.0f), rest);
    }
    std::string error;
    if (!rig.finalize(&error)) {
        std::fprintf(stderr, "invalid bench rig: %s\n", error.c_str());
        std::exit(1);
    }
    return rig;
}

// Every bone gets a rotation + translation track with `keyCount` linear keys.
std::unique_ptr<saida::AnimationClip> makeClip(int boneCount, int keyCount, float duration) {
    auto clip = std::make_unique<saida::AnimationClip>("bench", duration);
    for (int b = 0; b < boneCount; ++b) {
        const std::string name = "bone_" + std::to_string(b);

        auto rot = std::make_unique<saida::TypedAnimTrack<glm::quat>>();
        rot->target = saida::TrackTarget::Rotation;
        rot->timestamps.resize(keyCount);
        rot->values.resize(keyCount);
        auto pos = std::make_unique<saida::TypedAnimTrack<glm::vec3>>();
        pos->target = saida::TrackTarget::Translation;
        pos->timestamps.resize(keyCount);
        pos->values.resize(keyCount);

        for (int k = 0; k < keyCount; ++k) {
            const float t = duration * float(k) / float(keyCount - 1);
            const float phase = t * 3.0f + float(b) * 0.1f;
            rot->timestamps[k] = t;
            rot->values[k] = glm::angleAxis(0.5f * std::sin(phase), glm::vec3(0, 0, 1));
            pos->timestamps[k] = t;
            pos->values[k] = {0.0f, 0.25f + 0.02f * std::sin(phase), 0.0f};
        }
        clip->addTrack(name, std::move(rot));
        clip->addTrack(name, std::move(pos));
    }
    return clip;
}

} // namespace

int main(int argc, char** argv) {
    const int chars  = argInt(argc, argv, "--chars", 100);
    const int bones  = argInt(argc, argv, "--bones", 80);
    const int keys   = argInt(argc, argv, "--keys", 60);
    const int frames = argInt(argc, argv, "--frames", 300);
    const float dt = 1.0f / 60.0f;

    saida::Rig rig = makeRig(bones);
    auto clip = makeClip(bones, keys, 2.0f);

    std::vector<std::unique_ptr<saida::Animator>> animators;
    animators.reserve(chars);
    for (int i = 0; i < chars; ++i) {
        auto anim = std::make_unique<saida::Animator>();
        anim->setRig(&rig);
        anim->addClip("bench", clip.get());
        anim->play("bench", true, 0.0f);
        anim->onUpdate(dt * float(i % 17));  // desync the population
        animators.push_back(std::move(anim));
    }

    // Warmup, then measure per-frame cost of the whole population.
    for (int f = 0; f < 10; ++f)
        for (auto& a : animators) a->onUpdate(dt);

    std::vector<double> frameMs(frames);
    for (int f = 0; f < frames; ++f) {
        const auto t0 = std::chrono::steady_clock::now();
        for (auto& a : animators) a->onUpdate(dt);
        const auto t1 = std::chrono::steady_clock::now();
        frameMs[f] = std::chrono::duration<double, std::milli>(t1 - t0).count();
    }

    std::vector<double> sorted = frameMs;
    std::sort(sorted.begin(), sorted.end());
    const double p50 = sorted[sorted.size() / 2];
    const double p95 = sorted[size_t(double(sorted.size()) * 0.95)];
    double total = 0.0;
    for (double ms : frameMs) total += ms;
    const double avg = total / double(frames);

    std::printf("saida_animation_bench: %d chars x %d bones, %d keys/track, %d frames\n",
                chars, bones, keys, frames);
    std::printf("  frame update: avg %.3f ms | p50 %.3f ms | p95 %.3f ms\n", avg, p50, p95);
    std::printf("  per character: %.1f us | bone poses/s: %.2f M\n",
                avg * 1000.0 / double(chars),
                double(chars) * double(bones) * (1000.0 / avg) / 1e6);

    // Sanity: the pose actually moved (guards against the loop being optimized out
    // or the FSM never entering the clip state).
    const auto& pose = animators[0]->globalPose();
    if (pose.globalMatrices.size() != size_t(bones)) {
        std::fprintf(stderr, "unexpected pose size\n");
        return 1;
    }
    return 0;
}
