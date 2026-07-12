# SaidaEngine

> Fichier de contexte pour assistants IA (Codex & co). Lis-le en premier.

## But du projet

SaidaEngine est un **moteur de rendu 3D léger, propre et robuste** écrit en
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
`SAIDA_PROJECT_ROOT`, `SAIDA_SHADER_DIR`), donc l'exe se lance **depuis n'importe quel
répertoire** :

```sh
./build/bin/SaidaEngine.exe
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
`assetPath(rel)` (sous `SAIDA_PROJECT_ROOT` : `models/`, `assets/`) et
`shaderPath(name)` (sous `SAIDA_SHADER_DIR` = `build/shaders/`). Tout est absolu,
donc indépendant du cwd. (Pour packager un jeu plus tard : remplacer ces racines
par un dossier d'assets relatif à l'exe.)

## Architecture

**Split lib/exe** : le moteur compile en bibliothèque statique `saida_engine`
(tout `src/` sauf `main.cpp`) ; l'exécutable `SaidaEngine` ne contient que
`main.cpp` et linke la lib (via `saida_editor`). Conséquence : itérer le jeu ne
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
- [x] **Étape 6 — Éclairage.**
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
            + bouton « Generate Bake » (`SceneSettings`). Canal `lightmapUV` :
            **unwrap auto via xatlas** (vendu MIT, `third_party/xatlas` ; tout `.obj`
            sous le seuil de triangles devient bakeable, split aux seams de charts)
            + **dilation de seams** (passe plein-écran `lightmap_dilate.frag` :
            couverture en alpha, croissance des texels dans le gutter → plus de
            seams noirs en bilinéaire). *Limite restante* : lightmaps non
            sérialisées (re-bake au chargement) ; gros meshes (> seuil) gardent le
            fallback UV texture.
      - [x] PBR (metallic-roughness) + normal mapping, tonemapping HDR.
      - [x] **Rendu canonique validé jusqu'à nouvel ordre** : pipeline unique
            Desktop/XR/mobile, même équation visuelle partout ; seules les
            résolutions internes (render scale, shadows, lightmaps, AO/bloom)
            peuvent varier par plateforme.
      - [x] **IBL léger PBR** : l'environnement équirectangulaire de skybox est
            aussi la source d'image-based lighting (diffuse + spéculaire
            roughness-aware, BRDF d'environnement approximée, exposition skybox
            partagée). Objectif : matériaux moins plats, reflets cohérents, même
            rendu entre Realtime/Baked.
      - [x] **Post-processing canonique** : AO écran depth-based (un seul type
            d'AO, appliqué pareil en Realtime et Baked), fog distance simple,
            bloom HDR léger, tous pilotés par `SceneSettings`, sérialisés, et
            appliqués dans `tonemap.frag` pour Desktop et XR.
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
      - [x] **Split moteur (lib `saida_engine`) / jeu (exe)** : le moteur compile
            une fois, l'exe est un petit target (`main.cpp`). L'`Engine` accepte
            un `SceneSetup` optionnel pour peupler la scène ; par défaut (lancement
            direct sans `--project`) l'éditeur ouvre une scène vierge.
      - [x] **Sérialisation de scène** (JSON via nlohmann vendu) :
            `SceneSerializer` save/load + nœud↔JSON. Behaviours sérialisables
            via `BehaviourRegistry`. Round-trip vérifié (save→load→save identique).
      - [x] **Undo/redo** éditeur (`editor/Command*` : command pattern +
            `CommandHistory`) et branchement UI (menus File/Edit, copier/coller/
            dupliquer, raccourcis Ctrl+Z/Y/C/V/D/S, ops scene-tree → commandes).
      - [x] **Contrat d'autoring + 4 briques gameplay** (cf.
            « Comment coder un jeu » ci-dessus). Un seul
            paradigme nœuds+behaviours, *propre par construction* :
            - **Signaux typés** (`core/Signal.hpp` : `Signal<…>`/`Connection`
              lifetime-safe ; `Behaviour::listen`). `AreaNode` migré dessus.
            - **SceneTree + World persistant** (`scene/SceneTree`) : au Play, le
              moteur monte un World qui porte autoloads + sous-scène courante. La
              scène d'édition vivante est **déplacée** (pas copiée — sinon les
              ressources live type WebCanvas se dupliquent et crashent)
              dans le World ; à l'arrêt elle est reconstruite depuis un snapshot.
              `changeScene`/`queueFree` **différés** (après l'update, `Engine::run`).
              `Node::tree()`, flag `SceneSettings::changeRenderingAtLoad`.
            - **Autoloads** (singletons persistants, enfants du World, rendus) :
              code (`registerAutoload<T>`) **et** data-driven (`autoload_*` du
              `.saidaproj` + onglet éditeur). `tree()->autoload<T>()`.
            - **Groupes + requêtes scopées** : `addToGroup`/`tree()->firstInGroup`,
              `findBehaviourInChildren<T>`, `requireBehaviour<T>`. Pas de
              find-by-name global (volontaire).
      - [x] **Primitives gameplay** (« tout est scène ») :
            - **Instanciation runtime** : `tree()->instantiate("scenes/x.scene", parent)`
              (cache JSON + chemins relatifs au projet via `setProjectRoot`).
            - **Timers/tweens** possédés par nœud, frozen en pause :
              `Behaviour::wait/every/tween` → `SceneTree` (tické dans `Engine::run`) ;
              courbes dans `core/Easing.hpp`. Annulés à la mort du nœud.
            - **Callbacks de collision** : `CollisionObjectNode::collisionEntered/Exited`
              (corps solides) en plus de l'Area ; le contact listener tague `sensor`.
            - **Cycle de vie** : `Behaviour::onDestroy/onEnable/onDisable`.
            - **`SpawnerBehaviour`** (démo réutilisable, enregistrée) : spawn une
              scène sur timer + lifetime/`queueFree`.
      - [ ] *À faire (couche jeu)* : runtime standalone sans éditeur (Étape 15).
- [x] **Étape 8b — Scripting JavaScript (QuickJS).** Voir « Décision scripting »
      ci-dessous. Objectif : un seul runtime JS léger et complet pour SaidaEngine :
      `ScriptBehaviour`, autoloads JS, bindings moteur, hot-reload, inspector et
      console/outils éditeur.
      - [x] **Fondation QuickJS vendue et compilée** : `third_party/quickjs`,
            target statique `quickjs`, `src/scripting/JsRuntime` + `JsContext`
            (console, eval, erreurs avec stack, pending jobs).
      - [x] **`ScriptBehaviour` MVP** : behaviour sérialisable, visible dans
            l'inspector, champ `script`, bouton reload, hooks globaux
            `onReady/onUpdate/onDestroy/onEnable/onDisable`.
      - [x] **Bindings moteur de base** : `node` (`get/setName`,
            `get/setPosition`, `translate`, `setEnabled`, `queueFree`, groupes)
            `time` (`delta`, `elapsed`) et `input` action-based (`isHeld`,
            `justPressed`, `justReleased`, `strength`, `axis`, `vector`,
            `mousePosition`, `mouseDelta`) exposés aux `ScriptBehaviour`.
      - [x] **Timers JS possédés** : `time.wait/every/tween/cancel` utilisent la
            `SceneTimerQueue` commune desktop/Web ; callbacks libérés et timers
            annulés au hot-reload ou à la destruction du `ScriptBehaviour`.
      - [x] **Binding `tree` runtime** : `changeScene`, `reloadScene`, `quit`,
            `setPaused`, `paused`, en respectant le modèle `SceneTree` existant.
      - [x] **Propriétés JS inspectables** : un script peut déclarer
            `exportProperty("speed", 3.0)` puis lire `props.speed`. Types
            supportés : number, boolean, string. Les valeurs sont éditables dans
            l'inspector, sérialisées en scène et réinjectées au reload.
      - [x] **Hot-reload JS transactionnel** : les `ScriptBehaviour` surveillent
            leur fichier, rechargent automatiquement le contexte QuickJS et
            conservent l'ancien contexte si le nouveau script ne compile pas.
            Le bouton Reload utilise le même chemin. En Play, un reload réussi
            appelle proprement `onDestroy` sur l'ancien script puis `onReady`
            sur le nouveau.
      - [x] **Modules ES** : les `.mjs` sont évalués en module QuickJS, les hooks
            sont des exports (`export function onUpdate(dt) {}`), les imports
            relatifs (`./foo.mjs`) sont résolus depuis le fichier courant avec
            fallback projet, et les modules importés participent au hot-reload.
      - *Futurs bindings spécialisés* : physics/audio/UI/signaux peuvent être
        ajoutés au même pont natif quand un gameplay concret le demande.
- [x] **Étape 9 — Rendu global / GI pragmatique.** Validé jusqu'à nouvel ordre :
      l'objectif actif n'est plus d'empiler Radiance Cascades / World Cache /
      froxels, mais de garder un rendu moderne, léger, optimisé VR/mobile et
      **identique sur toutes les plateformes**.
      - [x] DDGI / irradiance volume existant conservé comme primitive GI unique
            pour l'indirect diffus dynamique/frozen.
      - [x] Realtime/Baked : même rendu final ; le choix ne change que la manière
            de produire/fixer la lumière indirecte, pas les post-process.
      - [x] IBL + AO + fog + bloom complètent le rendu perçu sans rendre le moteur
            lourd ni multiplier les chemins de shading.
      - [ ] Recherche différée, hors roadmap active : Radiance Cascades 2D,
            World Radiance Cache / spatial hashing, froxels volumétriques. À
            réévaluer seulement si un jeu concret prouve que le pipeline actuel
            ne suffit pas.
- [x] **Étape 10 — Animation System.** Animations squelettiques glTF/BVH :
      - [x] **Data-oriented** : `Rig` (bones plats, parents, inverse-bind), `Pose`
            (Local/Global + skinning matrices), `AnimationClip` (tracks T/R/S,
            interp linéaire + slerp). Graphe : `ClipNode`, `BlendNode`,
            `AnimStateMachine` (crossfade), `AnimBlackboard` (params hashés FNV).
      - [x] **Skinning GPU** : `shader.vert` (LBS via SSBO `boneMatrices`, set 0
            binding 3) ; le `Renderer` collecte l'`Animator` du parent, upload les
            `skinningMatrices` et passe `boneOffset` par instance.
      - [x] **Chargement glTF/GLB** : `GLTFLoader` parse `cgltf_skin` → `Rig`
            (joints, parents, inverse-bind), attache un `Animator`, et parse
            `cgltf_animation` → `AnimationClip` (CUBICSPLINE → **vrai Hermite** via
            tangentes in/out stockées par track). Les clips sont enregistrés sur
            l'`Animator` (`addClip`).
      - [x] **API runtime propre** : `Animator::play("Idle"/"Walk"/...)` (FSM
            interne + crossfade auto), `blackboard()`/`setFloat/Bool`,
            `setStateMachine`. `Animator` enregistré (`BehaviourRegistry`).
            `CharacterBehaviour` pilote idle/walk/jump selon l'état du
            `CharacterBody` (noms de clips configurables).
      - [x] **Chargement BVH** : `BVHLoader::load` → `AnimationClip` (tracks par
            nom de joint → jouable sur un squelette aux noms compatibles, sans
            retargeting). `AnimGraphParser` corrigé (clips réels, plus de fake/leak).
      - [x] **Finition (Lot A)** : **vrai cubic-spline** (Hermite glTF, tangentes) ;
            **retargeting par noms** (`RetargetMap` + `autoMap` normalisé
            mixamorig:/casse/synonymes ; `ClipNode`/`Animator::setRetarget`) ;
            **viewer de squelette** (pipeline `LINE_LIST` `debug_line.*`, toggle
            `SceneSettings::showSkeletons`, desktop) ; **drag-drop `.bvh`** (payload
            `FILE_BVH` → drop sur un `Animator` dans l'inspecteur → `addClip`).
            *Restes hors-périmètre* : retargeting proportionnel/rest-pose (notre
            `AnimationClip` ne stocke pas la bind pose source) — pass futur.
- [x] **Étape 11 — Simulation Physique.** Intégration d'un moteur physique robuste :
      - [x] **Jolt Physics vendu** (`third_party/jolt`, lib statique via
            `add_subdirectory`, SIMD/ABI propagés en PUBLIC). Wrapper moteur dans
            `src/physics/` : `PhysicsWorld` (Jolt `PhysicsSystem` + pas-fixe 1/60
            avec accumulateur, refcount global), `JoltGlue` (conversions glm↔Jolt,
            couches NON_MOVING/MOVING).
      - [x] **Logique par nœud, style Godot** (≠ Unity) : `CollisionObjectNode`
            (base) → `StaticBodyNode` / `RigidBodyNode` (mass, damping,
            gravityFactor, kinematic) / `AreaNode` (sensor/trigger) /
            `CharacterBodyNode`. Le collider est un **nœud enfant** :
            `CollisionShapeNode`. Collectés par `Scene::flattenHierarchy`, simulés
            dans `Scene::update` (sync→prePhysicsStep→step→sync) uniquement quand
            `dt>0` (Play). `Node::asCollisionObject()`/`asCharacterBody()`.
      - [x] **CharacterBodyNode** (Jolt `CharacterVirtual`) : capsule kinématique
            qui slide/monte les marches/colle au sol/pousse les RigidBody. Brique
            séparée minimale — le behaviour écrit `velocity` + lit `isOnFloor()` ;
            **pas de `moveAndSlide`** à appeler, le moteur fait le déplacement dans
            le pas physique (`prePhysicsStep`). `CharacterBehaviour` réécrit dessus.
      - [x] **Colliders complets** : box/sphere/capsule + **auto-détection**
            depuis l'AABB (`Mesh::bounds()`) : near-cubique→sphere, élancé→capsule,
            sinon→box ; **convex hull** (dynamique) et **triangle mesh** (statique)
            depuis la géométrie CPU retenue (`Mesh::collisionVertices/Indices`),
            bakée dans le repère du corps.
      - [x] **Triggers** (Area, contact listener thread-safe enter/exit),
            **Raycasting** (`PhysicsWorld::raycast`) et **callbacks de collision**
            (`CollisionObjectNode::collisionEntered/Exited`, le listener tague
            `sensor` pour router Area vs collision).
      - [x] **CharacterBody** : teleport par transform (divergence détectée dans
            `syncToPhysics` → `SetPosition`) en plus du pilotage en velocity.
      - [x] **Éditeur** : menu « Physics » (StaticBody/RigidBody/CharacterBody/
            Area, crée corps + cube + CollisionShape Auto en un geste), inspecteurs
            corps/shape (tous les types), sérialisation JSON, `NodeRegistry`.
      - **Étape 11 terminée.** *Restes hors-périmètre/futurs* : vérification
            visuelle en éditeur (test manuel) ; *Note perf* : build sans
            `CMAKE_BUILD_TYPE` → Jolt non optimisé ; passer en Release pour la perf.
- [~] **Étape 12 — UI 2D (Screen & World Space).** Migration active vers une UI
      web légère et libre : **RmlUi + QuickJS**, sans Ultralight.
      - Canvas 2D en Screen Space (overlay classique) et World Space (panneaux
        interactifs dans l'espace 3D, essentiels pour la VR/XR).
      - HTML/CSS/JS côté moteur via RmlUi pour le DOM/layout/style et QuickJS
        comme runtime JS unique. Le but n'est pas d'intégrer un navigateur
        complet, mais une Web UI de jeu stable, légère, multiplateforme et
        LLM-friendly.
      - [x] Ultralight/JavaScriptCore/WebCore/AppCore supprimés du CMake, des
            sources, du dossier `third_party` et des artefacts de build.
      - [x] `third_party/rmlui` + `third_party/freetype` vendus et compilés en
            statique.
      - [x] `WebCanvasNode` ne dépend plus d'Ultralight, crée un contexte RmlUi,
            charge RML depuis mémoire/fichier, exécute son JS via QuickJS et
            expose un DOM minimal (`document.setText/setHTML/reload`).
      - [x] Input souris/scroll routé vers le contexte RmlUi.
      - [x] **Rendu WebCanvas réel vers texture** : backend RmlUi CPU
            déterministe (`RmlUiRenderInterface`) qui rasterize les triangles
            RmlUi en RGBA, dirty-only, puis upload via staging buffer persistant
            dans la texture du `WebCanvasNode`. Ce chemin est léger, portable et
            garantit le même rendu sur desktop/mobile/VR.
      - [x] **Hot-reload WebCanvas transactionnel** : les documents `.rml/.html`
            chargés par URL/fichier et les dépendances réellement ouvertes par
            RmlUi (`.rcss`, imports, textures chargées au rendu) sont surveillés
            via `WatchedFile`. Si un fichier change, le nouveau document RmlUi
            est chargé avant de remplacer l'ancien ; en cas d'erreur, l'ancienne
            UI reste affichée. Les URLs `file:///...` sont normalisées pour
            l'interface fichier.
      - [ ] Optimisation future optionnelle : backend GPU/Vulkan pour très gros
            documents animés, sans changer l'API `WebCanvasNode`.
- [ ] **Étape 13 — Intégration LLM Native.** Support natif d'intelligence artificielle agentique dans le moteur :
      - World model (compréhension et représentation de l'état du monde par l'IA).
      - Protocole MCP (Model Context Protocol) pour connecter des outils.
      - Concept de "skills" (compétences exécutables par l'IA) et d'agents autonomes interagissant directement avec la scène.
- [~] **Étape 14 — XR / OpenXR.** Rendu et interactions XR via OpenXR (Objectif final du moteur).
      Cible : PCVR Quest Link, multiview (1 passe), auto-détection du casque. *Mise en
      route casque = itérative (le rendu/tracking ne se valide que dans le casque).*
      - [x] **Fondation** : OpenXR-SDK vendu (`third_party/openxr`, release-1.1.60),
            `openxr_loader` statique buildé + linké à `saida_engine`.
      - [x] **Module `src/xr/` (découpé, RAII, namespace `saida::xr` pour ne pas
            collisionner avec les handles OpenXR)** : `XrMath.hpp` (proj/view/pose →
            GLM, conventions Vulkan Y-down + depth 0..1), `xr::Instance` (instance
            `XR_KHR_vulkan_enable2` + system HMD + PFN
            d'extension chargées via `xrGetInstanceProcAddr`), `XrVulkanBinding`
            (fonctions libres `createVulkanInstance`/`pickPhysicalDevice`/
            `createVulkanDevice`), `xr::Swapchain` (un par œil), `xr::Session`
            (session + espace STAGE room-scale, fallback LOCAL + vues PRIMARY_STEREO + boucle
            waitFrame/locateViews/endFrame, poses tête → view/proj par œil, callback
            `RenderEyeFn` par œil). `XrPlatform.hpp` confine `<windows.h>` aux TUs xr.
      - [x] `VulkanDevice` : init pilotée par OpenXR en mode XR (ctor prenant un
            `xr::Instance*` ; pas de surface GLFW, present family = graphics).
            **Chemin desktop strictement intact** (ctor sans XR délègue avec `nullptr`).
      - [x] `Engine` : rôles de processus explicites — éditeur toujours Desktop/
            ImGui, preview `--xr` toujours OpenXR sans fallback silencieux — et
            boucle XR séparée (`runXr`) cadencée par
            `xrWaitFrame`, **pose tête → caméra** (loggée). Sanity A (compile, à
            valider casque) : chaque œil est **clear-color composité** dans le casque
            → valide tout le handshake device→session→swapchain→frame-loop→
            projection-layer + tracking, avant de brancher le rendu de scène.
      - [x] **XR Preview depuis l'éditeur** : Play sur une scène contenant un
            `XROrigin` sérialise un snapshot dans `build/xr_preview.scene` et lance
            un second `SaidaEngine.exe --xr-preview`. Projet + scène transitent par
            `build/xr_preview.launch` (pas par la command-line Windows, donc aucun
            bug de quoting sur les chemins avec espaces). Le processus enfant exige
            OpenXR (aucun fallback Desktop silencieux) et crée le device Vulkan via
            le runtime Meta dès son démarrage ; l'éditeur reste Desktop et ouvert.
            `Input` ne dépend pas d'ImGui : `ImGuiLayer` ne lui publie que les deux
            flags de capture clavier/souris. Les trackers internes SaidaXRTK sont
            explicitement masqués de l'inspecteur (aucun « Unknown Behaviour »).
      - [x] **Seam de rendu scène + multiview (Étapes A-rendu & B fusionnées)** :
            la scène est rendue **en stéréo, 1 passe multiview** (viewMask=0b11)
            dans une cible HDR **2-layers** (1 layer/œil) sizée à l'extent par œil,
            puis chaque layer est **tonemappé** dans l'image XR de l'œil. *Pipeline
            unique réutilisé* (shadows, GI/DDGI, HDR, tonemap) — seule la
            présentation diffère du desktop. Détails :
            - UBO caméra **unifié** `view[2]/proj[2]` (mono = index 0 ; XR = via
              `gl_ViewIndex`). Variantes shader `-DMULTIVIEW` (scene vert + skybox
              frag) ; `Pipeline` prend un `viewMask`. Chemin desktop **inchangé**.
            - `Renderer` **découplé du `Swapchain`** (2ᵉ ctor XR : pas de surface/
              ImGui). `xr::Session::renderFrame` passe désormais **tous les yeux**
              en un seul callback (acquire-all → 1 passe → release-all), pas par-œil.
            - Pas de culling par-œil (stéréo) : on dessine tout. Tête = centroïde
              des yeux pour le spéculaire. *Compile ; à valider au casque.*
            - *Limites v1 (suites)* : **pas de MSAA** en XR (cible 1 sample),
              **ImGui désactivé** en XR, skybox push-constant 136 o (OK PCVR ≥256 o).
      - [x] **Contrôleurs (action sets)** — `xr::Actions` (`src/xr/XrActions`) :
            action set + actions (grip/aim pose, trigger, squeeze, thumbstick, A/B),
            suggested bindings (Oculus Touch + Khronos simple), spaces grip/aim par
            main, attaché à la session ; `Session::syncActions()` (appelé par l'Engine
            avant l'update) `xrSyncActions` + locate + **alimente `saida::XRInput`** →
            grab/touch/teleport pilotés par les manettes. *À valider au casque.*
      - [x] **Hand tracking squelettique `XR_EXT_hand_tracking`** : extension
            optionnelle activée si le runtime la supporte, 26 joints par main
            localisés à chaque frame et publiés dans `XRInput`. `XRHandNode`
            affiche une main procédurale (joints + os, sans asset externe) ; pinch
            et fermeture alimentent trigger/squeeze pour réutiliser les interactors.
            Scène de validation `MyGame/scenes/XRSetup.scene` (XROrigin,
            contrôleurs/interactors, deux mains, sol et repères room-scale).
            *Compile ; disponibilité de l'extension et rendu à valider au Quest.*
      - [ ] *Suites perf/qualité XR* : MSAA multiview (+resolve par layer),
            overlay ImGui (quad/layer), culling stéréo combiné.
      - [~] **SaidaXRTK — SaidaEngine XR Toolkit** (`src/xr/toolkit/`, namespace `ne`).
            Package d'interaction VR/AR dans le style moteur (behaviours + nodes +
            signaux + groupes, zéro singleton de gameplay). Lit l'état des mains via
            le **service `XRInput`**, alimenté par les action sets OpenXR et, quand
            disponible, par le squelette `XR_EXT_hand_tracking`.
            - **Phase 1 (faite, compile)** : `XRInput` (service, edges des mains),
              `XRController` (node main, suit la pose grip via tracker interne),
              `XRGrabbable` (behaviour, grab **physics-aware** : kinématique pendant
              la prise + throw au lâcher), `XRTouchable` (behaviour poke, 2 mains),
              `XRDirectInteractor` (behaviour sur le controller, relie mains↔
              interactables via groupes `xr_grabbable`/`xr_touchable`, zéro pointeur
              stocké → zéro dangling). + `PhysicsWorld::set{Linear,Angular}Velocity`
              et `CollisionObjectNode::set{Linear,Angular}Velocity` (génériques).
            - **Phase 2 (faite, compile) — Locomotion** : `XROrigin` (node rig
              joueur ; `teleportTo`/`snapTurn` ; groupe `xr_origin`), `TeleportArea`
              (node surface valide + raycast analytique sur sa face, signal
              `teleported`), `XRRayInteractor` (behaviour sur controller : vise au
              thumbstick, téléporte au relâché). Le rig **recentre l'espace de
              référence OpenXR** (`xr::Session::setReferenceOffset` recrée l'`XrSpace`)
              → tête/mains/yeux reviennent world-space, compositeur cohérent, pas de
              hack de matrices. `XRInput::head()` (pose tête world) alimenté par
              l'Engine. *Convention de signe du recenter à confirmer au casque.*
            - **Phase 3 (faite, compile) — Anchors & passthrough** : `XRAnchor`
              (node ancré au réel ; tracker interne crée/locate via le **service
              `XRAnchors`** + `XRAnchorBackend` — seam pour `XR_MSFT_spatial_anchor`/
              `XR_FB_spatial_entity` ; sans backend → garde sa pose). Passthrough via
              le **service `XRPassthrough`** (toggle gameplay) : `xr::Session`
              énumère les blend modes, choisit ALPHA_BLEND/ADDITIVE si dispo et
              l'applique à `xrEndFrame` ; le renderer XR clear transparent + skip
              skybox, `tonemap.frag` préserve l'alpha. *Compositing AR à valider au
              casque.*
            - **Branchement (fait)** : `XRInput` est alimenté par `xr::Actions`
              (action sets OpenXR) et `xr::HandTracking` (squelette optionnel) →
              tout le toolkit est « vivant » avec contrôleurs ou mains nues.
              *Restant (optionnel, casque)* : backend d'anchors réel
              (`XRAnchors::setBackend` sur une extension), modèle de main skinné.
- [ ] **Étape 15 — Build & Release Windows.** Gestion de la release finale du jeu :
      - Pipeline de build autonome d'un projet (packaging des assets et shaders sans dépendances de développement).
      - Gestion des versions, métadonnées de l'exécutable, et icône du jeu.
      - Optimisation finale de build (Link-Time Optimization, suppression des traces de debug/ImGui si nécessaire).

Quand une étape est finie : cocher ici et compiler pour vérifier.

Note : l'ensemble des étapes vise à construire un unique pipeline de rendu universel et partagé (Desktop et XR). L'intégration XR (Étape 14) doit réutiliser l'intégralité du pipeline de scène et n'avoir de différent que la présentation (swapchain OpenXR). Toutes les étapes intermédiaires doivent être conçues sous cette contrainte d'unification (comme Godot et Unity).

## Décision scripting (prise, à implémenter)

Comment les développeurs écriront la logique de jeu et l'UI dynamique.
**Décidé** : SaidaEngine utilise **JavaScript via QuickJS** comme unique langage
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

- **Codex + MSYS2** : depuis PowerShell, ne pas appeler `c++.exe` directement.
  Utiliser `C:\msys64\usr\bin\bash.exe -lc`, mettre `/ucrt64/bin` en tete du
  `PATH`, et rediriger `HOME`, `TMPDIR`, `TMP`, `TEMP` vers `build/msys_home`
  et `build/tmp`. Pour `build-web`, ajouter aussi `/c/Python313` au `PATH` avant
  `/ucrt64/bin`, sinon Emscripten peut echouer avec `pylauncher: CreateProcess
  failed (2): "python.exe"`. Voir la section "Consignes build Codex" du
  README pour les commandes copiables.
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
