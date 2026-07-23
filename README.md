# SaidaEngine

SaidaEngine est le moteur 3D de Saida. Il est écrit en C++17, utilise Vulkan
sur desktop, WebGPU via WebAssembly dans le navigateur et OpenXR pour la VR/AR.
Le projet vise un moteur léger, lisible et pilotable par des humains comme par
des assistants IA, sans multiplier les implémentations de gameplay.

**Statut au 2026-07-24 : V1.0 close côté moteur — toutes les gates techniques
sont fermées et le refactor V1 est terminé. NO-GO pour publication tant que
l'installeur n'est pas signé Authenticode avec la clé de publication.**

## Documents canoniques

Le dépôt n'a que trois documents, ce README compris :

- [SPEC.md](SPEC.md) : architecture, contrats publics, formats, sous-systèmes,
  plateformes, procédures techniques, support/promotion/retrait d'une release
  et limites actuelles — la vérité de ce qui *existe* ;
- [ROADMAP.md](ROADMAP.md) : backlog unique de ce qui *reste à faire* — dernière
  étape avant publication, dette structurelle du code, décomposition différée du
  Renderer et ses prérequis, P1/P2 et décisions closes.

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
tests/         tests natifs et corpus figé des formats V1
tools/         harnais d'export et de smoke
```

`saida_engine` est la bibliothèque statique commune. `SaidaEngine` est
l'éditeur, `SaidaEngineHub` le gestionnaire de projets,
`SaidaEngineRuntime` le player desktop sans éditeur et `saida_tool` la CLI
headless utilisée par la CI et la plateforme.

`saida_tool describe-engine` publie aussi `runtimeTypeMatrix`, l'inventaire V1
unique des factories natif, headless, authoring WASM et player Web. Chaque
runtime vérifie son registre effectif contre cette matrice avant de devenir
utilisable; le headless round-trippe actuellement le HUD
`UINode`/`UICanvasNode`/`UITextNode` et les corps/colliders V1 sans GPU.
`saida_tool verify-manifest` prouve, depuis le binaire livré, que chaque type
annoncé par le manifeste appartient à cette matrice et round-trippe par le codec
snapshot headless.

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
  mingw-w64-ucrt-x86_64-vulkan-validation-layers \
  mingw-w64-ucrt-x86_64-mesa \
  mingw-w64-x86_64-nsis
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
paquets MSYS2 ci-dessus sont utilisés. Mesa fournit l'ICD logiciel Lavapipe
réservé aux preuves CI. Le paquet MINGW64 NSIS est un outil de packaging
autonome (version 3.12 minimum) : il ne change pas l'ABI UCRT64 du moteur.

## Compiler et vérifier

Depuis un terminal MSYS2 UCRT64 :

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Pour une release Windows qualifiée, configurer avec
`-DCMAKE_BUILD_TYPE=RelWithDebInfo` : le packaging sépare ensuite les symboles
et dépouille les copies distribuées. `Release` reste utilisable lorsqu'aucun
symbole n'est attendu, mais ne ferme pas la gate de diagnostic V1. Les shaders
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

Le corpus sémantique complet natif se lance sans projet :

```sh
./build/bin/SaidaEngine.exe --verify-runtime-contract
```

Le player Web active le même contrôle avec le paramètre URL
`?verify-runtime-contract`; il publie le décompte dans `[CONTRACT] PASS`.
L'authoring Web exécute systématiquement son corpus snapshot avant de publier
`ready` et expose le même verdict dans la console.

Les bindings de jeu peuvent être remplacés à l'exécution depuis QuickJS avec
`input.rebindKey`, `input.rebindMouse`, `input.rebindGamepadButton` et
`input.rebindGamepadAxis`. `input.rebindTouch` ajoute press/tap/swipes dans une
zone normalisée du canvas. `input.exportProfile(name)` renvoie le profil JSON
versionné; le jeu le sauvegarde avec `storage.prefs.save`, le recharge avec
`storage.prefs.load`, puis appelle `input.applyProfile`. Un profil invalide est
refusé en bloc et laisse les bindings courants inchangés.
Sur Web, `input.rumble(low, high, durationMs)` utilise le `dual-rumble` W3C du
pad actif et renvoie `false` si son navigateur ou sa manette ne l'expose pas;
`input.stopRumble()` arrête l'effet. GLFW desktop ne fournit pas ce backend.

Les SaidaOps V1 utilisent `opVersion: 2` et ciblent les nœuds par identifiant
64 bits stable encodé en chaîne décimale (`nodeId`, `parentId`, `newParentId`,
`fromNodeId`, `toNodeId`). Cet encodage évite toute perte de précision
JavaScript; les noms de nœuds ne sont pas des références d'opération.

```sh
./tools/witness_e2e.sh
./tools/witness_editor_play.sh
./tools/witness_editor_build.sh
./tools/witness_web_stage.sh
```

Le harnais Build éditeur exige le run puis le restart de l'artefact exact. En
CI, `SAIDA_WINDOW_HIDDEN=1` garde la fenêtre native cachée et `VK_DRIVER_FILES`
épingle Mesa/Lavapipe, ce qui rend ce parcours reproductible sur un runner
Windows propre sans GPU physique.

La recette P0.1 complète construit les deux artefacts par le chemin du bouton
Build, crée les archives, inventorie chaque fichier et écrit leurs SHA-256 :

```powershell
.\tools\witness_release_candidate.ps1
```

Elle exige un worktree Git propre par défaut et produit
`build/release/witness-v1/` avec `release-manifest.json`, les archives Windows
et Web, le bundle de symboles Windows, `WitnessGame-Setup.exe`, son manifeste
et leurs vérificateurs autonomes.
Les ZIP sont canoniques : ordre ordinal, timestamps épinglés au commit, chemins
ambigus/reparse points refusés et contenu revérifié sans extraction par
`tools/verify_deterministic_zip.ps1`. Deux exécutions sur les mêmes octets
produisent donc le même SHA-256.
L'installeur NSIS est lui aussi byte-reproductible avant signature. Il installe
par utilisateur, inventorie chaque octet du payload, refuse les symlinks et
collisions de casse, et sa désinstallation ne supprime que les fichiers
inventoriés et les deux caches runtime régénérables explicitement nommés. Sa
signature Authenticode reste une opération de publication
séparée qui requiert la clé de signature.
`-AllowDirty` est réservé aux preuves de développement et inscrit explicitement
`dirty: true` dans le manifest. Sur une autre machine Windows, aucun checkout
moteur, MSYS2 ou SDK n'est requis : extraire/copier ce dossier puis lancer :

```powershell
powershell -ExecutionPolicy Bypass -File .\verify_witness_windows.ps1
powershell -ExecutionPolicy Bypass -File .\verify_witness_installer.ps1 -RunWitness
powershell -ExecutionPolicy Bypass -File .\verify_witness_web.ps1 -Browser Chrome
powershell -ExecutionPolicy Bypass -File .\verify_witness_web.ps1 -Browser Edge -Port 18081
```

Les deux preuves Windows exigent seulement PowerShell et un pilote Vulkan
fonctionnel. Le vérificateur d'installeur contrôle le SHA-256, réalise une
installation silencieuse isolée, compare exactement chaque fichier, exécute
gameplay puis restart et exige une désinstallation propre.
Les preuves Web exigent Python 3 et le navigateur indiqué. Elles vérifient
automatiquement SHA-256, COOP/COEP, MIME WASM, gameplay/UI et save+HUD après
redémarrage; aucune lecture manuelle de console n'est nécessaire.

Le release manifest du moteur — l'identité immuable d'un bundle consommé par la
plateforme — se produit une fois les artefacts natif et Web construits :

```powershell
.\tools\engine_release_manifest.ps1
```

Il écrit `build/release/engine/release-manifest.json` : commit, versions de
formats lues depuis `saida_tool describe-engine`, et SHA-256 de `saida_tool`, du
runtime desktop, du player Web, de l'authoring WASM, du runtime d'authoring et de
chaque fixture immuable, ainsi que du bundle de conformité exact. `-AllowDirty`
marque `dirty: true`. Les exécutables doivent provenir d'un build
`RelWithDebInfo`; le manifeste inventorie également leurs copies dépouillées,
leurs `.dbg` et la fermeture DLL. La plateforme épingle ce manifeste et le
rejoue via `tools/verify_engine_release.ps1`, qui échoue au moindre écart
d'octet, de version ou d'inventaire.

Le bundle de conformité peut aussi être produit seul :

```powershell
.\tools\generate_release_compliance.ps1
```

Il écrit sous `build/release/compliance/` le SBOM SPDX 2.3, les notices
GPL/tiers, l'inventaire hashé des assets et modèles et leur manifeste. La
génération échoue si une nouvelle racine `third_party` ou un nouvel asset suivi
n'a pas de décision explicite de licence, provenance et distribution.

Les builds Windows `RelWithDebInfo` séparent les exécutables distribuables de
leurs symboles de diagnostic :

```powershell
.\tools\package_release_symbols.ps1
```

Le bundle `build/release/windows-symbols/` contient quatre `.exe` dépouillés,
leurs `.dbg`, un lien GNU debug vérifié et un manifeste SHA-256 lié au commit.
`tools/verify_release_symbols.ps1` en refuse tout octet ou fichier inattendu.
Le même bundle contient `windows-dependencies.json`, preuve récursive que chaque
import PE x64 est une DLL système autorisée ou une DLL effectivement livrée;
les runtimes dynamiques MinGW sont interdits.
Les applications desktop installent leur crash reporter au tout début du
processus : un fatal écrit un `.crash.log` et un minidump `.dmp` sous
`%LOCALAPPDATA%\SaidaEngine\CrashReports\<produit>\` (override CI :
`SAIDA_CRASH_DIR`). Le log nomme le commit et l'artefact
`windows-symbols-<commit>` exact à utiliser.

AutoLOD se compile séparément :

```sh
cmake -S autolod -B build/autolod -G Ninja
cmake --build build/autolod --parallel
```

## Règles de contribution

- Lire [SPEC.md](SPEC.md) avant de modifier un contrat ou un format.
- Mettre à jour [ROADMAP.md](ROADMAP.md) dans le même changement lorsqu'une
  entrée est close ou qu'un nouveau bloqueur est prouvé.
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
- Les loaders n'acceptent que le schéma courant exact. Un changement de format
  avant publication remplace le fixture V1 et adapte tous ses producteurs.
- Après un changement de contrat d'authoring, reconstruire natif, authoring WASM
  et player Web.
- Ne pas modifier les sources vendues sous `third_party` pour contourner un
  problème local de toolchain.
- Ne pas déclarer une capacité supportée sans backend réel et test associé.

## Licence

Le projet est sous GPL-3.0. Les dépendances et assets gardent leurs licences.
L'inventaire, les notices et le SBOM sont générés et vérifiés par la CI. Les
assets explicitement non distribuables restent hors des bundles V1; consulter
le guide de release ([SPEC.md](SPEC.md) §17) avant toute promotion.
