# NextEngine

> Fichier de contexte pour assistants IA (Claude Code & co). Lis-le en premier.

## But du projet

NextEngine est un **moteur de rendu 3D léger, propre et robuste** écrit en
**C++17 + Vulkan**, développé par étapes. La priorité n'est pas la quantité de
fonctionnalités mais la **qualité de l'architecture** : abstractions RAII
simples, ownership clair, code lisible. On garde le moteur « léger » — pas de
sur-ingénierie (pas d'ECS lourd ni de render-graph AAA tant que ce n'est pas
justifié).

### Objectif final

Permettre de créer :
1. des **jeux vidéo 3D simples** (desktop, rendu mono classique) ;
2. des **jeux XR (VR/AR) via OpenXR** — rendu stéréo, suivi des casques et
   contrôleurs.

Conséquence d'architecture importante : Vulkan est la base commune, mais le
chemin de présentation doit pouvoir être **soit la swapchain GLFW (desktop),
soit la swapchain OpenXR (XR)**. Garder cette dualité en tête en concevant le
`Renderer`/`Swapchain` : ne pas câbler en dur l'hypothèse « une fenêtre, une
vue mono ». Le rendu XR est stéréo (deux vues) — viser le *multiview* Vulkan.

Le projet est parti d'un « Hello Cube » Vulkan monolithique (style
vulkan-tutorial.com) et est progressivement transformé en vrai moteur. L'état
actuel affiche une **scène à hiérarchie de nœuds** (une « planète » avec une
« lune » qui orbite et une « sous-lune » enfant — chaque nœud hérite de la
transform de son parent), cubes texturés à **matériaux distincts** (même texture, teintes différentes via
`Material`), **éclairés en temps réel** (Blinn-Phong : une lumière directionnelle
« soleil » + une lumière ponctuelle qui orbite),
autour de laquelle on **vole librement** (caméra FPS : ZQSD/WASD + souris,
Espace/Ctrl, Maj pour accélérer, Échap pour quitter), avec depth buffer et
**MSAA**. Un **panneau ImGui** (TAB pour libérer le curseur) affiche FPS/caméra
et règle les lumières en direct. Le bugatti reste chargeable via
`Mesh::fromObjFile` — voir `Engine::buildScene()`.

## Stack & dépendances

- **C++17**, **Vulkan**, **GLFW** (fenêtre/surface), **GLM** (maths).
- **VulkanMemoryAllocator (VMA)** v3.1.0 — header unique *vendu* dans
  `third_party/vma/vk_mem_alloc.h` (committé volontairement pour rester
  reproductible sans réseau ; ne pas supprimer).
- Build : **CMake + Ninja**. Shaders compilés en SPIR-V par **glslc** (Vulkan SDK).
- Toolchain de dev : **MSYS2 ucrt64 / GCC**.

## Build & run

```sh
cmake -S . -B build -G Ninja
cmake --build build
```

Les chemins d'assets et de shaders sont **absolus** (bakés par CMake :
`NE_PROJECT_ROOT`, `NE_SHADER_DIR`), donc l'exe se lance **depuis n'importe quel
répertoire** :

```sh
./build/NextEngine.exe
```

Pour lancer **avec les validation layers Vulkan** : `./run.sh` (depuis la racine).
Le script met `ucrt64/bin` en tête du PATH et pointe `VK_LAYER_PATH` sur le
manifeste de la layer (paquet MSYS2 `vulkan-validation-layers`, non enregistré
auprès du loader). En build Debug, le moteur les active automatiquement.

C'est une application GUI à boucle de rendu infinie : elle ouvre une fenêtre et
ne rend pas la main (ne pas l'exécuter dans un contexte automatisé/headless).
Pour valider sans interaction : lancer avec un timeout (~60 s) et vérifier que
les logs de démarrage passent (`GPU: ...`, `loaded '...': N vertices`) — y
arriver signifie que l'init et le chargement ont réussi.

**Localisation des assets** : `core/Paths.hpp` centralise la résolution —
`assetPath(rel)` (sous `NE_PROJECT_ROOT` : `models/`, `assets/`) et
`shaderPath(name)` (sous `NE_SHADER_DIR` = `build/shaders/`). Tout est absolu,
donc indépendant du cwd. (Pour packager un jeu plus tard : remplacer ces racines
par un dossier d'assets relatif à l'exe.)

## Architecture

**Split lib/exe** : le moteur compile en bibliothèque statique `ne_engine`
(tout `src/` sauf `main.cpp` + `game/`) ; l'exécutable `NextEngine` ne contient
que `main.cpp` + le code de jeu (`game/`) et linke la lib. Conséquence : itérer
le jeu ne recompile/relink que l'exe, jamais le moteur. (L'éditeur vit encore
dans la lib — à déplacer dans un target éditeur dédié plus tard.)

Tout est dans le namespace `ne`. Chaque classe possède ses handles Vulkan et
les détruit (RAII), tout emprunte un `VulkanDevice&`, copies interdites.
**L'ordre de déclaration des membres compte** (destruction en ordre inverse :
`device_` doit survivre aux ressources qui le référencent).

```
src/
  main.cpp              Point d'entrée du jeu : crée Engine(buildDemoScene), run.
  game/
    DemoScene.{hpp,cpp} Contenu du *jeu* (cube, matériaux, lumières, behaviours
                        de démo). Vit dans l'exe, pas dans la lib moteur → itérer
                        le jeu ne recompile pas le moteur.
  Engine.{hpp,cpp}      Orchestration légère : possède les sous-systèmes, la
                        boucle (input, update de scène, UI, present délégué au
                        Renderer), la caméra. Reçoit un `SceneSetup` (le jeu
                        peuple la scène) — aucun contenu en dur dans le moteur.
  render/
    Renderer.{hpp,cpp}  Toute la machinerie de frame GPU : pipeline de scène,
                        set 0 (global), UBOs par-frame, command buffers, sync,
                        draw + UI overlay, acquire/submit/present (+ resize).
                        Seul à toucher la présentation → couture pour le XR.
  core/
    Window.{hpp,cpp}    GLFW : fenêtre, surface, resize, et inputs (capture
                        curseur, delta souris, état clavier).
    Camera.{hpp,cpp}    Caméra fly yaw/pitch → matrices view/projection
                        (projection avec flip Y Vulkan intégré).
    Log.hpp             Logger minimal header-only (info/warn/error).
    Paths.hpp           assetPath()/shaderPath() — résolution centralisée
                        (chemins absolus bakés par CMake, cwd-indépendant).
    Time.hpp            Timing global (delta scaled/unscaled, elapsed, scale)
                        façon Unity. L'Engine l'avance ; tout le monde le lit.
    Input.{hpp,cpp}     Input global échantillonné 1×/frame depuis la Window
                        (keyDown/keyPressed, mouse delta/pos). Codes GLFW.
  graphics/
    VulkanDevice.{hpp,cpp}  Instance, debug messenger, surface, device,
                            queues, command pool, VmaAllocator, pipeline cache
                            (sérialisé sur disque), max sample count (MSAA).
                            Helpers (copyBuffer, createImageView, single-time
                            cmds, formats/depth). Objet « GPU » central.
    Swapchain.{hpp,cpp}     Swapchain + depth (VMA) + render pass + framebuffers
                            + sémaphores renderFinished (1 par image). recreate()
                            au resize. La render pass est créée une fois et survit
                            aux recreate (pipelines restent valides).
    Pipeline.{hpp,cpp}      Pipeline graphique + layout (depuis chemins de shaders).
    Buffer.{hpp,cpp}        Wrapper RAII de VkBuffer via VMA. enum MemoryUsage
                            {GpuOnly, HostVisible}. HostVisible = mappé en permanence.
    Texture.{hpp,cpp}       Charge une image (stb_image) → VkImage échantillonnable
                            (VMA) + view + sampler. Staging + transitions de layout.
    ImGuiLayer.{hpp,cpp}    Wrappe Dear ImGui + backends GLFW/Vulkan. beginFrame/
                            endFrame/renderDrawData(cmd) dans la render pass.
    Mesh.{hpp,cpp}          Vertex (pos, normal, color, uv) + VBO/IBO (uint32)
                            via staging + bind/draw. Mesh::fromObjFile() charge un .obj.
    Material.{hpp,cpp}      Texture + params (baseColor) → descriptor set 1
                            (sampler + UBO). Référencé par les MeshNode.
    ResourceManager.{hpp,cpp}  Cache meshes/textures/matériaux par clé ; possède
                            le layout + pool du set matériau. Crée les Material.
    VmaFwd.hpp              Forward-decls VMA (garde le gros header hors des en-têtes).
    VmaUsage.cpp            Seule TU définissant VMA_IMPLEMENTATION.
    TinyObjUsage.cpp        Seule TU définissant TINYOBJLOADER_IMPLEMENTATION.
    StbImageUsage.cpp       Seule TU définissant STB_IMAGE_IMPLEMENTATION.
  scene/
    Node.{hpp,cpp}          Nœud du graphe : Transform (pos/quat/scale),
                            parent/enfants, propagation de la matrice monde
                            (traverse), behaviours attachés (addBehaviour +
                            updateTree). mesh() virtuel (null = non dessinable).
    MeshNode.hpp            Node dessinant un Mesh + un Material (réfs non-possédantes).
    LightNode.hpp           Lumière (Directional/Point) dans le graphe + champ
                            bakeMode (Realtime/Baked/Mixed) pour le futur bake.
    Behaviour.hpp           Logique attachable à un Node (onReady/onUpdate),
                            façon MonoBehaviour/script Godot. Accède à node().
                            Hooks de sérialisation (typeName/save/load).
    BehaviourRegistry.{hpp,cpp}  Nom de type → factory ; reconstruit les
                            behaviours au chargement d'une scène.
    SceneSerializer.{hpp,cpp}    Save/load de scène en JSON (nlohmann, vendu) :
                            nœuds, transforms, lumières, mesh/material par clé,
                            behaviours enregistrés. Aussi nœud↔JSON (copier/coller).
    Scene.{hpp,cpp}         Scene : public Node — la scène EST le nœud racine
                            (crée un nœud Settings avec SceneSettingsBehaviour).
  shaders/
    shader.vert / .frag     GLSL : transforme par UBO, échantillonne la texture.
third_party/vma/             VMA vendu.
third_party/tinyobjloader/   tinyobjloader vendu.
third_party/stb/             stb_image vendu.
third_party/imgui/           Dear ImGui vendu (core + backends/).
models/bugatti/              Modèle de test (bugatti.obj, ~84 Mo, LFS).
assets/textures/             Textures (checker.png généré, LFS).
```

### Conventions de code
- Membres suffixés `_` (ex. `device_`). Namespace `ne`.
- Helpers VMA cachés derrière l'API moteur : le code applicatif parle de
  `MemoryUsage::GpuOnly`/`HostVisible`, **jamais** de types VMA directement
  (sauf dans les `.cpp` qui incluent `vk_mem_alloc.h`).
- Les en-têtes n'incluent pas `vk_mem_alloc.h` (711 Ko) : utiliser `VmaFwd.hpp`.
- **Descriptor sets par fréquence** : *set 0* = données par-frame globales
  (camera + lighting UBOs, possédé par `Engine`) ; *set 1* = par matériau
  (sampler + params UBO, possédé par `Material`). La matrice `model` par objet
  passe en **push constant**. Bind set 0 une fois/frame, set 1 par nœud.

## Avancement (feuille de route)

Le moteur est construit par étapes numérotées :

- [x] **Étape 1 — Découpage modulaire.** Monolithe `CubeApp` → classes RAII
      (Window, VulkanDevice, Swapchain, Pipeline, Buffer, Mesh, Engine).
- [x] **Étape 2 — Gestion mémoire via VMA.** Allocateur unique, fin des
      `vkAllocateMemory` manuels.
- [x] **Étape 3 — Chargement d'assets.**
      - [x] Meshes `.obj` via `tinyobjloader` (`Mesh::fromObjFile`) : triangulation,
            dédup (position,normale), recentrage + scale unitaire, couleur = normale.
      - [x] Textures via `stb_image` (classe `Texture`, sampler + binding 1 image ;
            `Vertex` = pos+color+texCoord). Démo sur cube texturé.
- [x] **Étape 4 — Caméra & inputs.** Caméra fly `Camera`, inputs clavier/souris
      gérés par `Window`, déplacement avec delta time dans `Engine::processInput`.
- [x] **Étape 5 — Multi-objets / scène.** Graphe de scène à nœuds (`Node`,
      `MeshNode`, `Scene`) façon Godot/Unity : transform par nœud, hiérarchie
      parent/enfant, matrice `model` par objet via **push constant** (l'UBO ne
      garde que view/proj). Le rendu traverse la scène (`Engine::recordCommandBuffer`).
      Une `Scene` **est** un `Node` (racine). Des `Behaviour` (onReady/onUpdate)
      s'attachent aux nœuds — l'orbite de la démo est pilotée par un
      `RotatorBehaviour`, pas par du code en dur dans l'Engine.
- [~] **Étape 6 — Éclairage.**
      - [x] Temps réel : normales dans `Vertex`, Blinn-Phong en espace monde,
            lumières **Directional / Point / Spot** (`LightNode`) collectées par
            traversée dans un UBO d'éclairage unifié (`GpuLight[16]`, set 0
            binding 1). La math vit dans `shaders/lighting.glsl` (source unique).
      - [x] **Ombres temps réel** (Directional + Spot) : shadow mapping 2D-array
            (`graphics/ShadowMap`, jusqu'à 4 casters), PCF matériel + biais. Les
            Point lights éclairent sans ombre (cubemap hors scope).
      - [x] **Lightmap baking GPU** (sans GI) : `render/LightBaker` rend chaque
            mesh « Include in light baking » dans une lightmap via `bake.frag`
            (même `lighting.glsl` → résultat **identique** au temps réel), stockant
            l'irradiance diffuse ; le spéculaire reste live. Toggle Realtime/Baked
            + bouton « Generate Bake » (`SceneSettings`). Canal `lightmapUV`
            (unwrap propre du cube ; fallback `.obj`). *Limites v1* : lightmaps non
            sérialisées (re-bake au chargement), pas de dilation de seams, pas
            d'unwrap auto pour `.obj` arbitraires.
      - [x] PBR (metallic-roughness) + normal mapping, tonemapping HDR.
      - [x] **Vulkan 1.3 Dynamic Rendering** : implémenté (suppression des RenderPass et Framebuffers).
- [x] **Étape 7 — Outillage.**
      - [x] Pipeline cache (`VkPipelineCache` sérialisé sur disque, dans
            `VulkanDevice`) et **MSAA** (couleur multisamplée + resolve dans
            `Swapchain`, sample count auto plafonné à 4×).
      - [x] Petit `Log` (`core/Log.hpp`, niveaux info/warn/error).
      - [x] **Dear ImGui** (`graphics/ImGuiLayer`, backends GLFW+Vulkan vendus) :
            panneau debug (FPS, caméra, réglage live des lumières). Toggle de
            capture du curseur (TAB) dans `Window` pour passer fly-cam ↔ UI.
- [~] **Étape 8 — Couche jeu.**
      - [x] **Split moteur (lib `ne_engine`) / jeu (exe)** : le moteur compile
            une fois, le jeu est un petit target. Le contenu de jeu (scène,
            behaviours) vit dans `game/` (exe) ; l'`Engine` reçoit un `SceneSetup`.
      - [x] **Sérialisation de scène** (JSON via nlohmann vendu) :
            `SceneSerializer` save/load + nœud↔JSON. Behaviours sérialisables
            via `BehaviourRegistry`. Round-trip vérifié (save→load→save identique).
      - [x] **Undo/redo** éditeur (`editor/Command*` : command pattern +
            `CommandHistory`) et branchement UI (menus File/Edit, copier/coller/
            dupliquer, raccourcis Ctrl+Z/Y/C/V/D/S, ops scene-tree → commandes).
      - [ ] Boucle de jeu plus complète, point d'entrée utilisateur encore plus
            simple. (L'éditeur ImGui dans `editor/`, à déplacer dans un target
            dédié plus tard.)
- [ ] **Étape 8b — Scripting (Lua).** Voir « Décision scripting » ci-dessous.
      Différé : à faire une fois l'API moteur stable (après `Material`/
      `ResourceManager`), pour exposer une API propre aux scripts.
- [ ] **Étape 9 — XR / OpenXR.** Intégration OpenXR : instance/session/espaces,
      swapchain XR, rendu stéréo (multiview), poses casque + contrôleurs.
      Le rendu desktop et XR partagent le même pipeline de scène ; seul le
      chemin de présentation diffère. Objectif final du moteur.

Quand une étape est finie : cocher ici et compiler pour vérifier.

Note : les étapes 1→7 construisent un renderer desktop solide ; l'étape 9 (XR)
réutilise tout sauf la présentation. Concevoir les étapes intermédiaires en
gardant la dualité desktop/XR à l'esprit (cf. « Objectif final »).

## Décision scripting (prise, différée)

Comment les développeurs écriront la logique de jeu. **Décidé, pas encore
implémenté** (ne pas re-débattre) :

- **Logique de jeu en C++ `Behaviour`** (déjà en place) pour le moteur et le
  perf-critique, **+ scripts Lua** pour la logique itérée. Les deux se branchent
  sur le même point de couture : un futur `ScriptBehaviour : Behaviour` qui
  délègue `onReady`/`onUpdate` à des fonctions Lua.
- **Pourquoi Lua** : contrainte clé = *ne pas recompiler/relier le moteur à
  chaque évolution d'un script*. Lua est **vendu en source C compilée dans le
  moteur** (zéro DLL, zéro lien dynamique) ; les scripts sont du **texte** →
  changement = aucun recompile, aucun link, **hot-reload** sans redémarrage
  (file-watcher). Binding via **sol2** (header-only). L'état du jeu reste
  possédé côté moteur (survit au reload).
- **Pourquoi PAS un hot-reload DLL natif C++** : il impose le **linkage
  dynamique de libstdc++**, ce qui réveille le bug `ld` (exit 116) de cette
  toolchain MSYS2 qu'on contourne avec `-static` (cf. « Pièges connus »). Trop
  fragile ici. À reconsidérer seulement si la toolchain change.
- **Pourquoi PAS C#** : héberger .NET + marshalling + bindings + friction XR =
  effort disproportionné qui dépasserait la taille du moteur ; contredit
  l'objectif « léger ».

Implémentation prévue : vendre Lua + sol2, `ScriptBehaviour`, file-watcher de
hot-reload, et exposer une API de binding (`Node`/`Transform`/`Input`/…). À
caler en **étape 8b**, après que `Material`/`ResourceManager` aient stabilisé
l'API moteur.

## Pièges connus / environnement

- **Linker MSYS2 cassé sur libstdc++ dynamique.** Sur cette machine, lier
  dynamiquement libstdc++ fait **crasher `ld` (exit 116, sans message)** dès
  qu'il y a des symboles `<iostream>` ou d'exceptions. Contournement en place
  dans `CMakeLists.txt` : `-static -static-libgcc -static-libstdc++` (sous
  `if(MINGW)`). Bonus : exe autonome. *Vrai* correctif si possible :
  réinstaller la toolchain (`pacman -S mingw-w64-ucrt-x86_64-gcc
  mingw-w64-ucrt-x86_64-binutils`). Le `-static` peut rester quoi qu'il arrive.
- **Validation layers** : activées en build Debug (désactivées si `NDEBUG`).
  La layer MSYS2 n'est pas enregistrée auprès du loader → lancer via `./run.sh`
  qui met `ucrt64/bin` en tête du PATH (sinon erreur 127 : la layer charge un
  `libstdc++` incompatible) et définit `VK_LAYER_PATH`. Le moteur passe la
  validation sans erreur.

## Quand tu modifies le projet
- Toujours **compiler** après changement (`cmake --build build`) pour valider.
- Ne pas lancer l'exe toi-même (boucle GUI bloquante) ; demander à l'utilisateur
  de tester visuellement, ou utiliser le skill de vérification s'il est dispo.
- Respecter l'esprit « léger » : préférer la solution simple et lisible.
- Mettre à jour la feuille de route ci-dessus quand une étape avance.
