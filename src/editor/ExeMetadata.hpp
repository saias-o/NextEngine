#pragma once

#include <string>

namespace saida {

// Métadonnées d'exécutable écrites dans le <Game>.exe packagé par le Build :
// ressource VERSIONINFO (nom du
// produit, version, éditeur) et icône principale depuis un fichier .ico.
// Windows uniquement — no-op succès ailleurs (l'export web n'a pas d'exe).
struct ExeMetadata {
    std::string productName;   // FileDescription + ProductName (nom du jeu)
    std::string version;       // "1.2.3" ou "1.2.3.4" — chaque champ ≤ 65535
    std::string companyName;   // optionnel
    std::string iconPath;      // .ico absolu, optionnel (vide = pas d'icône)
};

// Patche l'exe en place (UpdateResource). Retourne false + `error` renseignée
// si la version ne se parse pas, si le .ico est invalide ou si l'écriture des
// ressources échoue. L'exe ne doit pas être en cours d'exécution.
bool applyExeMetadata(const std::string& exePath, const ExeMetadata& meta,
                      std::string& error);

// Parse "a.b.c.d" (1 à 4 champs numériques ≤ 65535) vers out[4] (manquants =
// 0). Exposé pour la validation UI et les tests.
bool parseExeVersion(const std::string& version, unsigned short out[4]);

} // namespace saida
