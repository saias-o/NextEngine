#include "audio/AudioSourceBehaviour.hpp"
#include "audio/AudioManager.hpp"
#include "project/Project.hpp"
#include "scene/Node.hpp"

#include <imgui.h>
#include <nlohmann/json.hpp>
#include <cstring>

namespace ne {

void AudioSourceBehaviour::onReady() {
    if (!audioName.empty()) {
        AudioManager::get().play(audioName, AudioManager::get().defaultSettings(), "Master", node());
    }
}

void AudioSourceBehaviour::onDrawInspector() {
    char buf[128];
    std::strncpy(buf, audioName.c_str(), sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    
    if (ImGui::InputText("Audio Name", buf, sizeof(buf))) {
        audioName = buf;
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
