#pragma once

#include "core/Reflection.hpp"
#include "core/Signal.hpp"
#include "scene/Behaviour.hpp"
#include "scene/animation/AnimationSequence.hpp"

#include <string>
#include <vector>

namespace saida {

// Joue une AnimationSequence (.sseq) dans une scène en cours d'exécution. Les
// cibles des pistes sont résolues par nom dans la scène du nœud porteur : une
// piste d'animation pilote l'Animator du nœud cible (ou d'un descendant), une
// piste de propriété "Nom.propriete" pilote une propriété réfléchie du nœud
// cible ou d'un de ses behaviours, la piste d'événements est relayée par le
// signal réfléchi `sequenceEvent`. Fail-closed : une séquence invalide ou une
// cible toujours absente après le délai de résolution désactive la lecture
// avec diagnostics loggés, sans émettre aucun signal.
class SequenceDirectorBehaviour : public Behaviour {
public:
    void onUpdate(float dt) override;

    SAIDA_REFLECT_BEHAVIOUR(SequenceDirectorBehaviour, "SequenceDirector")

    std::string sequence;  // chemin projet-relatif du .sseq
    bool autoplay = true;

    void play();  // (re)démarre du début ; la liaison se fait au prochain update
    void stop();

    Signal<std::string> sequenceEvent;  // relaie la piste d'événements
    Signal<> sequenceFinished;          // fin de lecture (une fois par run)

private:
    enum class BindState { Unbound, Bound, Failed };

    bool tryBind(float dt);
    void failWith(const std::vector<AssetDiagnostic>& diags, const char* stage);

    SequencePlayer player_;
    BindState bindState_ = BindState::Unbound;
    float bindWait_ = 0.0f;
    bool playing_ = false;
    bool playRequested_ = false;
    bool autoplayConsumed_ = false;
    bool finishedEmitted_ = false;
};

} // namespace saida
