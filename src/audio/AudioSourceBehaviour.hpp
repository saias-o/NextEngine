#pragma once

#include "core/Reflection.hpp"
#include "scene/Behaviour.hpp"
#include <string>

namespace ne {

class AudioSourceBehaviour : public Behaviour {
public:
    void onReady() override;

    std::string audioName = "default_sound";

    NE_REFLECT_BEHAVIOUR(AudioSourceBehaviour, "AudioSource")
};

} // namespace ne
