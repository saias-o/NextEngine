#pragma once

#include "editor/EditorUI.hpp"

namespace saida {

class Project;
class ResourceManager;
class Scene;

// Panneau Animation : lecture des clips d'un Animator, édition non destructive
// de ClipViews (.sclip) et application de graphes (.sgraph) avec pilotage live
// des paramètres. Travaille sur l'Animator du nœud sélectionné, sinon le
// premier trouvé (scène de preview comprise).
class Animator;

class AnimationPanel {
public:
    void draw(EditorUI* editor, Scene* scene, ResourceManager* resources, Project* project);

private:
    // Membres (et non fonctions libres) pour bénéficier de l'amitié d'EditorUI.
    static void refreshAssetList(EditorUI* editor, Project* project);
    static void drawClipViewSection(EditorUI* editor, Animator* animator,
                                    ResourceManager* resources, Project* project);
    static void drawAssetsSection(EditorUI* editor, Animator* animator,
                                  ResourceManager* resources, Project* project);
    static void drawGraphSection(EditorUI* editor, Animator* animator,
                                 ResourceManager* resources);
};

} // namespace saida
