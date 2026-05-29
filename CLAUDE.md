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
actuel affiche un **cube texturé** autour duquel on **vole librement** (caméra
FPS : ZQSD/WASD + souris, Espace/Ctrl, Maj pour accélérer, Échap pour quitter),
avec depth buffer. Le bugatti reste chargeable via `Mesh::fromObjFile` (sans
texture, faute d'UV) — voir le commentaire dans `Engine::Engine()`.

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

L'exécutable lit ses shaders au chemin **relatif** `shaders/shader.vert.spv`,
or glslc les compile dans `build/shaders/`. **Lancer depuis `build/`** :

```sh
cd build && ./NextEngine.exe
```

C'est une application GUI à boucle de rendu infinie : elle ouvre une fenêtre et
ne rend pas la main (ne pas l'exécuter dans un contexte automatisé/headless).
Pour valider sans interaction : lancer avec un timeout (~60 s) et vérifier que
les logs de démarrage passent (`GPU: ...`, `loaded '...': N vertices`) — y
arriver signifie que l'init et le chargement ont réussi.

**Localisation des assets** : les modèles sont chargés via un chemin absolu
basé sur `NE_PROJECT_ROOT` (défini par CMake = racine du projet), donc
indépendant du cwd. Les **shaders**, eux, restent en chemin relatif → toujours
lancer depuis `build/`. (À unifier plus tard, p. ex. pour le packaging d'un jeu.)

## Architecture

Tout est dans le namespace `ne`. Chaque classe possède ses handles Vulkan et
les détruit (RAII), tout emprunte un `VulkanDevice&`, copies interdites.
**L'ordre de déclaration des membres compte** (destruction en ordre inverse :
`device_` doit survivre aux ressources qui le référencent).

```
src/
  main.cpp              Point d'entrée minimal (try/catch).
  Engine.{hpp,cpp}      Orchestration : boucle de rendu, frames-in-flight,
                        descriptor sets, uniform buffers, UBO/caméra, draw.
                        Contient pour l'instant la géométrie du cube en dur.
  core/
    Window.{hpp,cpp}    GLFW : fenêtre, surface, resize, et inputs (capture
                        curseur, delta souris, état clavier).
    Camera.{hpp,cpp}    Caméra fly yaw/pitch → matrices view/projection
                        (projection avec flip Y Vulkan intégré).
  graphics/
    VulkanDevice.{hpp,cpp}  Instance, debug messenger, surface, device,
                            queues, command pool, VmaAllocator. Helpers
                            (copyBuffer, createImageView, single-time cmds,
                            formats/depth). Objet « GPU » central.
    Swapchain.{hpp,cpp}     Swapchain + depth (VMA) + render pass + framebuffers.
                            recreate() au resize. La render pass est créée une
                            fois et survit aux recreate (pipelines restent valides).
    Pipeline.{hpp,cpp}      Pipeline graphique + layout (depuis chemins de shaders).
    Buffer.{hpp,cpp}        Wrapper RAII de VkBuffer via VMA. enum MemoryUsage
                            {GpuOnly, HostVisible}. HostVisible = mappé en permanence.
    Texture.{hpp,cpp}       Charge une image (stb_image) → VkImage échantillonnable
                            (VMA) + view + sampler. Staging + transitions de layout.
    Mesh.{hpp,cpp}          Vertex (pos, color) + VBO/IBO (indices uint32) via
                            staging + bind/draw. Mesh::fromObjFile() charge un .obj.
    VmaFwd.hpp              Forward-decls VMA (garde le gros header hors des en-têtes).
    VmaUsage.cpp            Seule TU définissant VMA_IMPLEMENTATION.
    TinyObjUsage.cpp        Seule TU définissant TINYOBJLOADER_IMPLEMENTATION.
    StbImageUsage.cpp       Seule TU définissant STB_IMAGE_IMPLEMENTATION.
  shaders/
    shader.vert / .frag     GLSL : transforme par UBO, échantillonne la texture.
third_party/vma/             VMA vendu.
third_party/tinyobjloader/   tinyobjloader vendu.
third_party/stb/             stb_image vendu.
models/bugatti/              Modèle de test (bugatti.obj, ~84 Mo, LFS).
assets/textures/             Textures (checker.png généré, LFS).
```

### Conventions de code
- Membres suffixés `_` (ex. `device_`). Namespace `ne`.
- Helpers VMA cachés derrière l'API moteur : le code applicatif parle de
  `MemoryUsage::GpuOnly`/`HostVisible`, **jamais** de types VMA directement
  (sauf dans les `.cpp` qui incluent `vk_mem_alloc.h`).
- Les en-têtes n'incluent pas `vk_mem_alloc.h` (711 Ko) : utiliser `VmaFwd.hpp`.

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
- [ ] **Étape 5 — Multi-objets / scène.** Plusieurs `RenderObject` avec
      transform par objet (push constants pour la matrice `model`).
- [ ] **Étape 6 — Éclairage.** Normales + Blinn-Phong puis PBR.
- [ ] **Étape 7 — Outillage.** Dear ImGui (debug/stats), pipeline cache, MSAA.
- [ ] **Étape 8 — Couche jeu.** Boucle de jeu (update/render séparés, delta
      time fixe/variable), abstraction « scène » jouable, point d'entrée
      utilisateur simple pour créer un jeu desktop.
- [ ] **Étape 9 — XR / OpenXR.** Intégration OpenXR : instance/session/espaces,
      swapchain XR, rendu stéréo (multiview), poses casque + contrôleurs.
      Le rendu desktop et XR partagent le même pipeline de scène ; seul le
      chemin de présentation diffère. Objectif final du moteur.

Quand une étape est finie : cocher ici et compiler pour vérifier.

Note : les étapes 1→7 construisent un renderer desktop solide ; l'étape 9 (XR)
réutilise tout sauf la présentation. Concevoir les étapes intermédiaires en
gardant la dualité desktop/XR à l'esprit (cf. « Objectif final »).

## Pièges connus / environnement

- **Linker MSYS2 cassé sur libstdc++ dynamique.** Sur cette machine, lier
  dynamiquement libstdc++ fait **crasher `ld` (exit 116, sans message)** dès
  qu'il y a des symboles `<iostream>` ou d'exceptions. Contournement en place
  dans `CMakeLists.txt` : `-static -static-libgcc -static-libstdc++` (sous
  `if(MINGW)`). Bonus : exe autonome. *Vrai* correctif si possible :
  réinstaller la toolchain (`pacman -S mingw-w64-ucrt-x86_64-gcc
  mingw-w64-ucrt-x86_64-binutils`). Le `-static` peut rester quoi qu'il arrive.
- **Pas (encore) de dépôt git** — `git init` recommandé avant d'aller plus loin.
- Validation layers Vulkan activées en build Debug (désactivées si `NDEBUG`).

## Quand tu modifies le projet
- Toujours **compiler** après changement (`cmake --build build`) pour valider.
- Ne pas lancer l'exe toi-même (boucle GUI bloquante) ; demander à l'utilisateur
  de tester visuellement, ou utiliser le skill de vérification s'il est dispo.
- Respecter l'esprit « léger » : préférer la solution simple et lisible.
- Mettre à jour la feuille de route ci-dessus quand une étape avance.
