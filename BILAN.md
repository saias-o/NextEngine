# NextEngine — Bilan & pistes d'amélioration

> Document de travail pour plus tard. État au terme de l'étape 6 (éclairage
> temps réel). Aucune action requise — c'est une liste de réflexion priorisée.
> Voir `CLAUDE.md` pour l'architecture et la feuille de route officielle.

---

## 1. État des lieux

Ce qui est solide aujourd'hui :
- Architecture modulaire RAII propre (`core/`, `graphics/`, `scene/`), ownership
  clair, copies interdites, ordre de destruction maîtrisé.
- Mémoire GPU via VMA, abstraite derrière `MemoryUsage`.
- Chargement de meshes (`.obj`) et textures (`stb_image`).
- Graphe de scène à nœuds + `Behaviour` (façon Godot/Unity).
- Caméra fly + inputs, éclairage Blinn-Phong temps réel pensé pour le baked.
- Build reproductible (deps vendues), git + LFS en place.

Ce document liste ce qui reste fragile, améliorable, ou à construire.

---

## 2. Prochaines étapes (suite logique de la roadmap)

Dans l'ordre de valeur conseillé :

1. **Outillage (étape 7)** — le plus rentable maintenant.
   - ~~**Pipeline cache**~~ **[FAIT]** (`VkPipelineCache` sérialisé sur disque
     dans `VulkanDevice`).
   - ~~**MSAA**~~ **[FAIT]** (couleur multisamplée + resolve dans `Swapchain`,
     sample count auto plafonné à 4×).
   - ~~**Dear ImGui**~~ **[FAIT]** `graphics/ImGuiLayer` + backends GLFW/Vulkan
     vendus ; panneau debug (FPS, caméra, lumières en direct) ; toggle curseur
     (TAB) dans `Window`.
   - ~~**Vulkan SDK / validation layers**~~ **[FAIT]** Layers MSYS2 activées via
     `./run.sh` (`VK_LAYER_PATH` + `ucrt64/bin` en tête du PATH). Le moteur passe
     la validation sans erreur.

2. **Couche jeu (étape 8)** — boucle update/render claire, `dt` fixe vs
   variable, point d'entrée utilisateur pour écrire un jeu sans toucher l'Engine.
   Inclut le **split moteur (lib) / jeu (exe)** : ne plus relier le moteur à
   chaque changement de code de jeu.

3. **Scripting Lua (étape 8b)** — *décidé, différé* (voir « Décision scripting »
   dans `CLAUDE.md`). Lua vendu (source C, zéro DLL) + sol2 + `ScriptBehaviour`
   + hot-reload par file-watcher → itérer la logique sans recompiler/relier le
   moteur. À faire après que `Material`/`ResourceManager` aient stabilisé l'API.
   (Pas de hot-reload DLL natif : réveille le bug `ld`/libstdc++. Pas de C#.)

4. **XR / OpenXR (étape 9, objectif final)** — voir §6.

5. **Matériaux & PBR** — ~~`Material` + `ResourceManager` + chemins unifiés~~
   **[FAIT]** (set 0 global / set 1 matériau, cache d'assets, `core/Paths.hpp`).
   Reste le **PBR** (metallic-roughness) par-dessus le `Material` actuel.

6. **Baked GI** (la suite du « pense déjà au baked » déjà amorcé) : étape de
   bake offline, UV de lightmap (2e jeu d'UV dans `Vertex`), texture lightmap
   échantillonnée dans le terme *indirect* du fragment shader. Le hook
   `LightNode::bakeMode` et la séparation indirect/direct sont déjà là.

---

## 3. Bouts de code à améliorer

### Correctness (à traiter en priorité)
- ~~**Sémaphore `renderFinished` par frame au lieu de par image**~~ **[FAIT]**
  Déplacé dans `Swapchain` (un sémaphore par image, indexé par `imageIndex`,
  recréé avec la swapchain).
- ~~**Macros GLM non globales**~~ **[FAIT]** Définies globalement dans
  `CMakeLists.txt` (`target_compile_definitions`), `#define` locaux retirés.

### Perf (pas urgent à cette échelle, mais à garder en tête)
- **3 traversées de scène par frame** : `updateTree` (behaviours),
  `gatherLights`, puis `recordCommandBuffer`. Acceptable maintenant ; à terme,
  une passe de collecte unique produisant une liste de draw + une liste de
  lumières (un « RenderView ») serait plus propre et plus rapide.
- **Pas de frustum culling** : tout est dessiné. `Mesh` calcule déjà une AABB
  au chargement mais la **jette** — la conserver permettrait le culling.
- **Copies one-time synchrones** (`VulkanDevice::copyBuffer` /
  `Texture` transitions) font un `vkQueueWaitIdle` à chaque fois. OK à l'init ;
  à éviter si on charge des assets en cours de jeu (→ transferts asynchrones).
- **Matrice normale recalculée par sommet** dans `shader.vert`
  (`transpose(inverse(model))`). La passer en push constant ou la précalculer
  serait moins coûteux sur de gros meshes.

### Qualité / hygiène
- ~~**Aucun warning compilateur activé**~~ **[FAIT]** `-Wall -Wextra` activés
  sur les TU du moteur (third-party restent en `-w`) ; build sans warning.
- ~~**Pas de système de log**~~ **[FAIT]** `core/Log.hpp` (info/warn/error) ;
  les `cout`/`cerr` épars y sont routés.
- ~~**Chemins d'assets** incohérents (modèles absolus, shaders relatifs au
  cwd)~~ **[FAIT]** `core/Paths.hpp` : tout absolu (`assetPath`/`shaderPath`),
  cwd-indépendant. (Reste à rendre relatif-à-l'exe pour packager un jeu.)
- **Couleurs / gamma** : albédo en sRGB, cible sRGB, mais l'éclairage est
  additionné sans vraie gestion linéaire ni tone mapping. À formaliser quand on
  passera au PBR.

---

## 4. Classes / abstractions à ajouter

- ~~**`Material`**~~ **[FAIT]** `Material` (texture + `baseColor`, descriptor
  set 1) référencé par `MeshNode` ; créés/cachés par `ResourceManager`. *Reste* :
  tri des draws par matériau, plusieurs pipelines (transparence), PBR.
- **`CameraNode`** — la caméra est un membre de `Engine`, pas un nœud. Pour
  rester cohérent avec « tout est nœud » (et pour le multi-caméra / XR), en
  faire un `CameraNode` dans le graphe, dont la transform donne la vue.
- ~~**`ResourceManager` / cache d'assets**~~ **[FAIT]** mutualise meshes/textures/
  matériaux par clé ; `core/Paths.hpp` centralise la résolution (assets +
  shaders, absolus, cwd-indépendant).
- **`Renderer`** — extraire de `Engine` toute la logique frame (command buffers,
  sync, draw, descriptors) pour ne laisser dans `Engine` que l'orchestration
  (fenêtre, scène, boucle). Prépare proprement le double chemin desktop/XR (§6).
- **`Input`** — abstraction d'entrée (actions, état clavier/souris/manette)
  accessible **depuis les `Behaviour`** (actuellement les inputs ne sont lus que
  dans `Engine::processInput`). Indispensable pour scripter le gameplay.
- **`Time`** — `dt`, temps écoulé, time scale, exposé globalement aux behaviours.
- **API scène manquante sur `Node`** : `removeChild`, suppression/`destroy`,
  `findChild(name)` / chemins, activation (`enabled`). Lifecycle behaviour :
  `onDestroy`, flag `enabled`.
- **`LightingSystem`** — sortir `gatherLights` + l'UBO de l'`Engine` quand le
  nombre de lumières et de types grandira (et pour les ombres).
- **`ScriptBehaviour` + binding Lua** (étape 8b, décidé/différé) — `Behaviour`
  qui délègue `onReady`/`onUpdate` à des fonctions Lua ; Lua vendu (source C) +
  sol2 + file-watcher hot-reload. Cf. « Décision scripting » dans `CLAUDE.md`.

---

## 5. Dette technique & environnement
- ~~**Validation layers absentes**~~ **[FAIT]** activées via `./run.sh`.
- **Tests / CI** : aucun. Au minimum, un smoke test « init + 1 frame headless »
  et un build CI.
- **Toolchain MSYS2** : link statique en contournement du crash `ld` (cf.
  `CLAUDE.md`). Vrai correctif : réinstaller `mingw-w64-ucrt-x86_64-gcc/binutils`.
- ~~**Curseur souris toujours capturé**~~ **[FAIT]** toggle TAB
  (`Window::setCursorCaptured`).

---

## 6. Spécifique XR / OpenXR (objectif final)

À garder en tête dès les refactos intermédiaires :
- **Rendu stéréo (2 vues)** : view/proj deviennent des tableaux [2]. Viser le
  **multiview** Vulkan (`VK_KHR_multiview`) — une seule passe, `gl_ViewIndex`
  dans le shader.
- **Présentation découplée** : aujourd'hui `Swapchain` suppose 1 fenêtre / 1 vue
  mono. Le chemin de présentation doit pouvoir être la **swapchain OpenXR** à la
  place de la swapchain GLFW. → motive l'extraction d'un `Renderer` (§4) qui rend
  vers des cibles abstraites, indépendamment de la source de présentation.
- **Poses** : casque + contrôleurs alimentent des `CameraNode` / nœuds de la
  scène — d'où l'intérêt d'avoir la caméra dans le graphe.
- Le pipeline de scène (graphe, matériaux, éclairage) est **partagé** entre
  desktop et XR ; seule la présentation diffère.

---

## 7. Ordre d'attaque conseillé (résumé)

1. ~~Vulkan SDK + validation layers, sémaphore `renderFinished`, macros GLM.~~ FAIT
2. ~~`-Wall -Wextra`, petit `Log`.~~ FAIT
3. ~~ImGui + pipeline cache + MSAA (étape 7).~~ FAIT
4. ~~`Material` + `ResourceManager` + unification des chemins d'assets.~~ FAIT
5. Extraire `Renderer` ; faire de la caméra un `CameraNode` ; `Input`/`Time`. ← prochain
6. Couche jeu + split moteur(lib)/jeu(exe) (étape 8).
7. Scripting Lua + `ScriptBehaviour` + hot-reload (étape 8b).
8. PBR, ombres, baked GI.
9. OpenXR (étape 9).
