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
soit la swapchain OpenXR (XR)**. Le moteur doit s'appuyer sur **un unique pipeline
de rendu universel** (comme Godot et Unity) qui gère tous les cas d'usage avec le
même code de rendu. Garder cette dualité en tête en concevant le
`Renderer`/`Swapchain` : ne pas câbler en dur l'hypothèse « une fenêtre, une
vue mono ». Le rendu XR est stéréo (deux vues) — viser le *multiview* Vulkan
au sein de ce pipeline unique.

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
- **Scripting/UI cible : QuickJS + RmlUi.** Décision active : QuickJS devient
  l'unique runtime JavaScript du moteur (gameplay, autoloads, outils, UI web) ;
  RmlUi remplace Ultralight pour le rendu HTML/CSS léger en Screen Space et
  World Space. L'ancienne intégration Ultralight/JavaScriptCore/WebCore/AppCore
  est supprimée du build et de `third_party` ; aucune nouvelle fonctionnalité ne
  doit être construite dessus.
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
(tout `src/` sauf `main.cpp`) ; l'exécutable `NextEngine` ne contient que
`main.cpp` et linke la lib (via `ne_editor`). Conséquence : itérer le jeu ne
recompile/relink que l'exe, jamais le moteur. Lancé directement (sans
`--project`), l'éditeur ouvre une **scène vierge** ; le Hub passe un projet via
`--project`. Le moteur reste sans contenu en dur : `Engine` accepte un
`SceneSetup` optionnel (peupleur de scène fourni par l'exe), aujourd'hui non
utilisé par défaut.

Tout est dans le namespace `ne`. Chaque classe possède ses handles Vulkan et
les détruit (RAII), tout emprunte un `VulkanDevice&`, copies interdites.
**L'ordre de déclaration des membres compte** (destruction en ordre inverse :
`device_` doit survivre aux ressources qui le référencent).

```
src/
  main.cpp              Point d'entrée : parse les args (--project/--scene/--xr),
                        crée l'Engine (scène vierge par défaut), lance l'éditeur.
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
                            `MaterialType` (Lit/Unlit, façon Unity) choisit le
                            pipeline de scène au draw : Lit = PBR complet, Unlit =
                            albedo*baseColor+emissive (pas d'éclairage, moins cher).
                            Lit et Unlit partagent vertex shader + layout set 0/1/2
                            + push constants ; seul le fragment diffère
                            (shader.frag vs unlit.frag). Ajouter un modèle =
                            1 valeur d'enum + 1 fragment + 1 pipeline (Renderer).
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

## Comment coder un jeu (contrat — règles dures)

> Ce contrat s'applique à **tout** code de gameplay (humain ou IA). Il existe pour
> qu'un jeu ambitieux ne dégénère pas en god-classes + events ingérables.
> **Un seul paradigme : nœuds + behaviours + signaux.**

**Les 5 règles :**
1. **Toute logique = un `Behaviour`.** Un nœud, c'est de la composition + des
   données. Jamais de classe « Manager ».
2. **Composer, pas grossir.** Une fonctionnalité = une petite scène/prefab +
   des behaviours focalisés (un `Player` = `Movement` + `Health` + `Inventory`,
   pas un `PlayerController` géant).
3. **Call down, signal up.** On pilote son nœud + ses descendants par appel
   direct ; pour communiquer vers le haut/à distance, on **émet un signal**
   (`Signal<…>` + `behaviour->listen(...)`, cf. `core/Signal.hpp`).
4. **Pas de global sauf services + autoloads.** Seuls `Input`, `Time`, `Audio`,
   `node()->tree()` (le `SceneTree`) sont globaux. L'état persistant du jeu va
   dans un **autoload** (`tree()->autoload<T>()`), jamais un singleton codé à la
   main.
5. **Trouver un nœud = groupe ou requête scopée, jamais par son nom.**
   `tree()->firstInGroup("player")` ou `findBehaviourInChildren<T>()` —
   il n'existe **pas** de `findByName` global, volontairement.

**Contraint par construction** (l'API ne fournit pas les outils du code sale) :
- pas de recherche globale de nœud par nom → seulement groupes + requêtes
  descendantes ;
- pas de `T::instance()` à écrire → on **déclare** un autoload (UI projet ou
  `tree.registerAutoload<T>(name)`), le moteur gère instance/persistance ;
- pas de type « EventBus » → les events transverses sont des **signaux typés
  portés par un autoload** ;
- `listen()` gère la durée de vie des connexions (zéro fuite, zéro dangling) ;
- changement de scène = `tree()->changeScene(path)` (différé) ; suppression =
  `node->queueFree()` (différé). Jamais de mutation de l'arbre en plein update.

**Modèle d'exécution** : au Play, le moteur monte un **World persistant** (cf.
`scene/SceneTree`) qui porte les autoloads + la sous-scène de jeu courante ;
`changeScene` ne permute que la sous-scène (World + autoloads survivent). Une
sous-scène avec `SceneSettings::changeRenderingAtLoad` impose son ambiance au
World au chargement.

## Avancement (feuille de route)

Le moteur est construit par étapes numérotées. **Détails des tâches restantes : voir [TODO.md](TODO.md).**

- [x] **Étape 1 — Découpage modulaire.** Classes RAII (Window, VulkanDevice, Swapchain, Pipeline, Buffer, Mesh, Engine).
- [x] **Étape 2 — Gestion mémoire via VMA.** Allocateur unique, `vkAllocateMemory` supprimés.
- [x] **Étape 3 — Chargement d'assets.** Meshes `.obj` (tinyobjloader), textures (stb_image).
- [x] **Étape 4 — Caméra & inputs.** Caméra fly + clavier/souris.
- [x] **Étape 5 — Multi-objets / scène.** Graphe à nœuds, hiérarchie parent/enfant, behaviours attachés.
- [x] **Étape 6 — Éclairage.** Temps réel (Blinn-Phong), ombres (shadow mapping), lightmap baking GPU, PBR + IBL, post-processing (AO/fog/bloom), Vulkan 1.3 Dynamic Rendering.
- [x] **Étape 7 — Outillage.** Pipeline cache, MSAA, log minimal, Dear ImGui avec backends GLFW+Vulkan.
- [~] **Étape 8 — Couche jeu.** 95% : split moteur/jeu, sérialisation JSON, undo/redo, contrat authoring, signaux typés, World persistant, autoloads, groups, instanciation runtime, timers/tweens. **Manque** : runtime standalone (→ [TODO.md](TODO.md)).
- [x] **Étape 8b — Scripting JavaScript (QuickJS).** Fondation QuickJS, `ScriptBehaviour` MVP, bindings moteur (node/time/input/tree), propriétés inspectables, hot-reload transactionnel, modules ES.
- [x] **Étape 9 — Rendu global / GI pragmatique.** DDGI, Realtime/Baked unified, IBL + AO + fog + bloom. **Future research** : Radiance Cascades 2D / World Cache / froxels (→ [TODO.md](TODO.md)).
- [x] **Étape 10 — Animation System.** Data-oriented Rig/Pose, GPU skinning (LBS), glTF/GLB loader (vrai cubic-spline Hermite), BVH loader, retargeting par noms, skeleton viewer, drag-drop BVH.
- [x] **Étape 11 — Simulation Physique.** Jolt Physics, nodes (StaticBody/RigidBody/CharacterBody/Area), auto-détection colliders, triggers, raycasting, éditeur Physics.
- [~] **Étape 12 — UI 2D (Screen & World Space).** 95% : RmlUi + QuickJS, Ultralight supprimé, `WebCanvasNode` avec rendu CPU réel, hot-reload transactionnel. **Future** : backend GPU/Vulkan (→ [TODO.md](TODO.md)).
- [x] **Étape 13 — Intégration LLM Native.** 95% : M1-M6 complets (réflexion+manifeste, signaux data-driven, serveur MCP, outils code+validation, primitives FSM/Blackboard/Scénario, token-opt). **Manque** : inspecteur behaviours réfléchis, world model, skills, agents autonomes (→ [TODO.md](TODO.md)).
- [~] **Étape 14 — XR / OpenXR.** 85% : OpenXR SDK, module `src/xr/`, VulkanDevice/Engine integration, multiview stereo 1-pass, controllers (action sets). **NEXRTK** 75% : Phase 1-3 complets (interaction, locomotion, anchors&passthrough). **Manque** : hand tracking, MSAA multiview, ImGui overlay, Renderer DRY refactor, anchor backends (→ [TODO.md](TODO.md)).
- [~] **Étape 15 — Build & Release Windows.** 80% : export-template pipeline, runtime dédié, packager, UI Build Settings. **Manque** : version management, executable metadata/icon, LTO build optimization (→ [TODO.md](TODO.md)).

**Voir [TODO.md](TODO.md) pour les tâches restantes détaillées par priorité.**

Note : l'ensemble des étapes vise à construire un unique pipeline de rendu universel et partagé (Desktop et XR). L'intégration XR (Étape 14) doit réutiliser l'intégralité du pipeline de scène et n'avoir de différent que la présentation (swapchain OpenXR). Toutes les étapes intermédiaires doivent être conçues sous cette contrainte d'unification (comme Godot et Unity).

## Décision scripting (prise, à implémenter)

Comment les développeurs écriront la logique de jeu et l'UI dynamique.
**Décidé** : NextEngine utilise **JavaScript via QuickJS** comme unique langage
de scripting moteur.

- **Logique de jeu en C++ `Behaviour`** pour le moteur, les systèmes bas niveau
  et le perf-critique, **+ `ScriptBehaviour` JavaScript** pour la logique itérée,
  les prototypes, les comportements générés par LLM, les autoloads de gameplay
  et les outils éditeur.
- **QuickJS est le seul runtime JS du moteur.** Il sert aux behaviours JS, aux
  autoloads JS, à la console/outillage et au JS de l'UI web. Ne pas introduire
  un second moteur JS ou une dépendance de type navigateur complet.
- **UI web cible : RmlUi + QuickJS.** RmlUi fournit le rendu/layout HTML-CSS
  léger ; QuickJS exécute le JS d'UI et expose un DOM minimal adapté aux jeux
  (`document`, `querySelector`, events, `classList`, `style`, `textContent`,
  etc.). L'ancien chemin Ultralight/JavaScriptCore/WebCore/AppCore doit être
  supprimé complètement.
- **Pourquoi QuickJS** : runtime C léger, vendable en source, sans JIT, sans DLL
  propriétaire, compatible build statique, suffisamment complet pour ES moderne
  (modules, classes, async/pending jobs) et beaucoup plus naturel pour du code
  généré par LLM qu'un langage de scripting spécialisé.
- **Contrat d'architecture inchangé** : les scripts JS suivent les mêmes règles
  que les behaviours C++ : logique attachée à des nodes, composition, signaux,
  autoloads pour l'état persistant, groupes/requêtes scopées, pas de recherche
  globale par nom, pas de singleton gameplay.
- **Sécurité obligatoire** : les scripts manipulent des handles de nodes sûrs
  (id/génération), pas des `Node*` bruts ; les callbacks sont déconnectés à la
  destruction ; le runtime impose limites mémoire/stack et interrupt handler
  pour éviter qu'une boucle JS bloque le moteur.
- **Hot-reload texte** : modifier un `.js`, `.mjs`, `.html/.rml` ou `.css` ne doit pas
  nécessiter de recompiler. Les `ScriptBehaviour` sont déjà hot-reloadés de
  manière transactionnelle : un script invalide ne casse pas le contexte live.
  Les `WebCanvasNode` hot-reloadent aussi leur document principal `.rml/.html`
  et les dépendances RmlUi réellement ouvertes, sans casser l'UI affichée en cas
  d'erreur. Les imports relatifs des modules JS participent aussi au hot-reload.
- **Pourquoi PAS un hot-reload DLL natif C++** : il impose le **linkage
  dynamique de libstdc++**, ce qui réveille le bug `ld` (exit 116) de cette
  toolchain MSYS2 qu'on contourne avec `-static` (cf. « Pièges connus »). Trop
  fragile ici. À reconsidérer seulement si la toolchain change.
- **Pourquoi PAS C#** : héberger .NET + marshalling + bindings + friction XR =
  effort disproportionné qui dépasserait la taille du moteur ; contredit
  l'objectif « léger ».

Implémentation **terminée pour la fondation Étape 8b + migration Étape 12** :
QuickJS, RmlUi/freetype et la suppression complète d'Ultralight sont en place.
Les `ScriptBehaviour` supportent scripts classiques, modules ES `.mjs`, imports
relatifs, propriétés exportées inspectables/sérialisées, bindings moteur de base
et hot-reload transactionnel. Les `WebCanvasNode` ont un DOM minimal, un rendu
réel vers texture et un hot-reload transactionnel des documents/dépendances RmlUi.

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
