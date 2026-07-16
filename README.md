# SaidaEngine

SaidaEngine est le moteur 3D de Saida. Il est écrit en C++17, utilise Vulkan
sur desktop, WebGPU via WebAssembly dans le navigateur et OpenXR pour la VR/AR.
Le projet vise un moteur léger, lisible et pilotable par des humains comme par
des assistants IA, sans multiplier les implémentations de gameplay.

**Statut au 2026-07-16 : Alpha, non publiable comme V1 stable.**

## Documents canoniques

Il n'existe que deux documents de vérité en plus de ce README :

- [SPEC.md](SPEC.md) : architecture, contrats publics, formats, sous-systèmes,
  plateformes, procédures techniques et limites actuelles ;
- [PLAN_V1.md](PLAN_V1.md) : unique checklist pour atteindre la V1.

La plateforme web, le backend et l'exploitation vivent dans
[`saias-o/saida`](https://github.com/saias-o/saida). Son `PLAN_V1.md` porte le
go/no-go global de production.

## Principes

- Un seul modèle de scène et de gameplay pour éditeur, desktop, Web et XR.
- Un seul runtime JavaScript : QuickJS. RmlUi est le système UI HTML/CSS.
- Un seul contrat d'authoring : SaidaOps validées vers des snapshots versionnés.
- RAII, ownership explicite, composants simples et pas d'abstraction lourde
  sans besoin mesuré.
- Toute capacité absente est annoncée et échoue explicitement. Aucun contenu
  durable ne doit être silencieusement appauvri.
- Les formats publics migrent; les caches régénérables ne sont pas garantis.

## Dépôt

```text
src/
  authoring/   manifest, snapshots, SaidaOps
  core/        fenêtre, input, temps, chemins, capacités
  editor/      Hub et éditeur ImGui
  graphics/    ressources GPU, meshes, matériaux
  physics/     intégration Jolt
  render/      renderer, GI, post-process, features
  runtime/     player desktop autonome
  scene/       scène, nœuds, behaviours, animation
  scripting/   QuickJS et bindings gameplay
  ui/          RmlUi, WebCanvas et interaction
  xr/          OpenXR et SaidaXRTK
web/
  authoring/   validateur/fold WASM headless pour Saida
  player/      player de jeu WASM/WebGPU
  runtime/     runtime d'authoring WASM/WebGPU
WitnessGame/   jeu témoin de la V1
tests/         tests natifs et corpus de compatibilité
tools/         harnais d'export et de smoke
```

`saida_engine` est la bibliothèque statique commune. `SaidaEngine` est
l'éditeur, `SaidaEngineHub` le gestionnaire de projets,
`SaidaEngineRuntime` le player desktop sans éditeur et `saida_tool` la CLI
headless utilisée par la CI et la plateforme.

## Prérequis Windows

Toolchain supportée : **MSYS2 UCRT64, GCC, CMake et Ninja**. Ne pas mélanger
MSVC, MINGW64 et UCRT64.

```sh
pacman -Syu
pacman -S --needed \
  git git-lfs \
  mingw-w64-ucrt-x86_64-gcc \
  mingw-w64-ucrt-x86_64-cmake \
  mingw-w64-ucrt-x86_64-ninja \
  mingw-w64-ucrt-x86_64-pkgconf \
  mingw-w64-ucrt-x86_64-glfw \
  mingw-w64-ucrt-x86_64-glm \
  mingw-w64-ucrt-x86_64-shaderc \
  mingw-w64-ucrt-x86_64-vulkan-headers \
  mingw-w64-ucrt-x86_64-vulkan-loader \
  mingw-w64-ucrt-x86_64-vulkan-validation-layers
git lfs install
```

FreeType, QuickJS et RmlUi sont des sous-modules pinnés :

```sh
git clone --recurse-submodules https://github.com/saias-o/NextEngine.git
cd NextEngine
git submodule sync --recursive
git submodule update --init --recursive
git lfs pull
```

Un pilote GPU Vulkan récent est requis. Le SDK LunarG est optionnel lorsque les
paquets MSYS2 ci-dessus sont utilisés.

## Compiler et vérifier

Depuis un terminal MSYS2 UCRT64 :

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Pour une Release, configurer avec `-DCMAKE_BUILD_TYPE=Release`. Les shaders
GLSL sont générés dans `build/shaders`. Un build complet produit notamment :

```text
build/bin/SaidaEngine.exe
build/bin/SaidaEngineHub.exe
build/bin/SaidaEngineRuntime.exe
build/bin/saida_tool.exe
build/tests/*.exe
```

Depuis PowerShell/Codex, mettre explicitement UCRT64 en tête du `PATH` et garder
les temporaires dans le workspace :

```powershell
New-Item -ItemType Directory -Force -Path build\tmp, build\msys_home | Out-Null
$env:PATH = 'C:\msys64\usr\bin;C:\msys64\ucrt64\bin;' + $env:PATH
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Les symptômes `Cannot create temporary file`, `cc1plus` silencieux ou
`pylauncher: CreateProcess failed` indiquent généralement un `PATH`, `HOME` ou
`TEMP` incorrect. Pour Emscripten, Python doit aussi être visible.

## Web

Activer l'environnement emsdk, puis construire séparément les deux surfaces :

```sh
emcmake cmake -S web/player -B build-web-player -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-web-player

emcmake cmake -S web/authoring -B build-authoring-wasm -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-authoring-wasm
```

Le runtime d'authoring visuel utilise `web/runtime`. `naga` et `glslc` doivent
être disponibles pour régénérer les shaders WGSL. Les builds Web se servent par
HTTP, jamais via `file://`.

## Exécuter

```sh
./build/bin/SaidaEngineHub.exe
# ou
./build/bin/SaidaEngine.exe --project /chemin/jeu.saidaproj
```

L'éditeur est une application GUI à boucle infinie. En automatisation, utiliser
les tests, `saida_tool` et les harnais plutôt que de laisser l'exécutable ouvert.

`./run.sh` lance un Debug avec le `PATH` UCRT64 et `VK_LAYER_PATH` corrects pour
les validation layers MSYS2. Sans ce script, une layer peut charger un runtime
`libstdc++` incompatible.

```sh
./tools/witness_e2e.sh
./tools/witness_editor_build.sh
./tools/witness_web_stage.sh
```

AutoLOD se compile séparément :

```sh
cmake -S autolod -B build/autolod -G Ninja
cmake --build build/autolod --parallel
```

## Règles de contribution

- Lire [SPEC.md](SPEC.md) avant de modifier un contrat ou un format.
- Mettre à jour [PLAN_V1.md](PLAN_V1.md) dans le même changement lorsqu'une
  gate est fermée ou qu'un nouveau bloqueur est prouvé.
- Donner à chaque module et chaque classe une responsabilité claire. Scinder
  les classes omniscientes et les fichiers qui mélangent plusieurs domaines.
- Remplacer les nombres et chaînes magiques par des constantes nommées, des
  types ou de la configuration lorsque leur sens n'est pas intrinsèque.
- Refuser la duplication, les dépendances cachées, l'état global injustifié et
  les fonctions longues comme solutions permanentes.
- Préférer du code explicite et testable aux commentaires narratifs. Un
  commentaire de code n'est utile que pour expliquer un invariant, une
  contrainte externe ou une décision non évidente; il ne renvoie pas vers les
  documents Markdown.
- Ne jamais réécrire un ancien fixture de compatibilité; ajouter une migration
  et un nouveau fixture.
- Après un changement de contrat d'authoring, reconstruire natif, authoring WASM
  et player Web.
- Ne pas modifier les sources vendues sous `third_party` pour contourner un
  problème local de toolchain.
- Ne pas déclarer une capacité supportée sans backend réel et test associé.

## Licence

Le projet est sous GPL-3.0. Les dépendances et assets gardent leurs licences.
L'inventaire final, les notices/SPDX et la SBOM restent une gate de distribution
stable dans [PLAN_V1.md](PLAN_V1.md).
