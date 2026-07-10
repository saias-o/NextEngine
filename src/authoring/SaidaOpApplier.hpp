#pragma once

// Applique les operations d'edition sans dependre de l'UI ou du reseau.

#include <string>

namespace saida {

class Scene;
class ResourceManager;

namespace authoring {

// Retourne :
//   {"ok":true,"applied":"set_transform","diff":{...}}
//   {"ok":false,"error":"unknown node 'Foo'"}
// Une operation invalide ne modifie pas la scene.
std::string applyOpJson(Scene& scene, ResourceManager& resources,
                        const std::string& opJson);

// Sans ressources GPU, les operations qui en dependent echouent explicitement.
std::string applyOpJson(Scene& scene, ResourceManager* resources,
                        const std::string& opJson);

} // namespace authoring
} // namespace saida
