#pragma once

#include "scene/Behaviour.hpp"
#include <string>

namespace ne {

class AudioSourceBehaviour : public Behaviour {
public:
    void onReady() override;
    void onDrawInspector() override;

    const char* typeName() const override { return "AudioSource"; }
    void save(nlohmann::json& json) const override;
    void load(const nlohmann::json& json) override;

    std::string audioName = "default_sound";
};

} // namespace ne
