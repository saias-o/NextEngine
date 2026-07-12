#pragma once

// Contrat de capacités runtime (PLAN_SAIDA_ENGINE_UPDATE §2.5 : dégradation
// explicite). Chaque player déclare au boot ce que sa plateforme/son build
// supporte réellement ; un système absent doit se signaler clairement, jamais
// échouer en silence ni retourner une valeur neutre trompeuse.
//
// Le point d'entrée (main desktop, player web…) appelle setCapabilities() une
// fois ; le défaut est « tout » (desktop complet) pour ne rien changer aux
// exécutables existants.

#include <cstdint>
#include <string>

namespace saida::platform {

enum class Capability : uint32_t {
    Rendering      = 1u << 0,
    Physics        = 1u << 1,
    Audio          = 1u << 2,
    ScriptGameplay = 1u << 3,  // behaviours JavaScript (QuickJS)
    GameUI         = 1u << 4,  // WebCanvas / RmlUi
    KeyboardMouse  = 1u << 5,
    GamepadInput   = 1u << 6,
    TouchInput     = 1u << 7,
    UserStorage    = 1u << 8,  // sauvegardes / préférences joueur
    XR             = 1u << 9,
};

constexpr uint32_t kAllCapabilities = 0x3FFu;

void setCapabilities(uint32_t mask);
bool has(Capability cap);
const char* name(Capability cap);

// Rapport une-ligne-par-capacité, loggé au boot des players.
std::string report();

// Point de passage des systèmes optionnels : retourne has(cap) et, si absent,
// logge UNE erreur explicite « <feature> requires <cap>, unavailable on this
// platform » (dédupliquée par capacité pour ne pas inonder la boucle de jeu).
bool require(Capability cap, const char* feature);

} // namespace saida::platform
