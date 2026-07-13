#pragma once

// game.saida — le manifeste de boot d'un jeu packagé, écrit par le Build de
// l'éditeur et lu par les players (desktop, web). Texte clé=valeur :
//
//   project=MyGame.saidaproj
//   main_scene=scenes/main.scene
//
// Partagé par tous les runtimes (PLAN_SAIDA_ENGINE_UPDATE §2.1 : un seul
// moteur de jeu) : le player web boote exactement le même fichier que l'exe
// desktop.

#include <istream>
#include <string>

namespace saida {

struct BootManifest {
    int schema = 1;
    std::string project;    // chemin du .saidaproj, relatif à la racine du package
    std::string mainScene;  // chemin de la scène de démarrage, relatif idem
};

struct BootManifestResult {
    bool ok = false;
    BootManifest manifest;
    std::string error;  // message stable, vide si ok
};

BootManifestResult parseBootManifest(std::istream& in);
BootManifestResult loadBootManifest(const std::string& path);

} // namespace saida
