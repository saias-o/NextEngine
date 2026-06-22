#include "audio/AudioSourceBehaviour.hpp"
#include "audio/AudioManager.hpp"
#include "project/Project.hpp"
#include "scene/Node.hpp"

#include <nlohmann/json.hpp>

namespace ne {

void AudioSourceBehaviour::onReady() {
    if (!audioName.empty()) {
        AudioManager::get().play(audioName, AudioManager::get().defaultSettings(), "Master", node());
    }
}

void AudioSourceBehaviour::save(nlohmann::json& json) const {
    json["audioName"] = audioName;
}

void AudioSourceBehaviour::load(const nlohmann::json& json) {
    if (json.contains("audioName")) {
        audioName = json["audioName"].get<std::string>();
    }
}

} // namespace ne
