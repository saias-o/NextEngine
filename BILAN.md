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
   - **Dear ImGui** : régler lumières/caméra/objets en direct, voir les FPS.
     Indispensable pour itérer. (Vendre `imgui` + backend GLFW/Vulkan.) Faire
     d'abord le **toggle de capture du curseur** (§5).
   - **Vulkan SDK / validation layers** : non installées → bugs Vulkan
     silencieux. À installer avant d'aller plus loin (gros multiplicateur de
     fiabilité). Cf. note dans `CLAUDE.md`.

2. **Couche jeu (étape 8)** — boucle update/render claire, `dt` fixe vs
   variable, point d'entrée utilisateur pour écrire un jeu sans toucher l'Engine.

3. **XR / OpenXR (étape 9, objectif final)** — voir §6.

4. **Matériaux & PBR** — prérequis pour des jeux qui ont de la gueule (voir §4).

5. **Baked GI** (la suite du « pense déjà au baked » déjà amorcé) : étape de
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
- **Chemins d'assets** : modèles via `NE_PROJECT_ROOT` (chemin **absolu** baked
  à la compilation) ; shaders en chemin **relatif** au cwd (→ obligation de
  lancer depuis `build/`). Incohérent et non packageable. Voir §4 (ResourceManager).
- **Couleurs / gamma** : albédo en sRGB, cible sRGB, mais l'éclairage est
  additionné sans vraie gestion linéaire ni tone mapping. À formaliser quand on
  passera au PBR.

---

## 4. Classes / abstractions à ajouter

- **`Material`** — la grosse pièce manquante. Aujourd'hui : une seule pipeline,
  une seule texture liée globalement, tous les `MeshNode` la partagent. Il faut
  un `Material` (texture(s) + paramètres + référence pipeline) que `MeshNode`
  référence, et un tri des draws par matériau. Débloque : plusieurs textures,
  PBR, transparence.
- **`CameraNode`** — la caméra est un membre de `Engine`, pas un nœud. Pour
  rester cohérent avec « tout est nœud » (et pour le multi-caméra / XR), en
  faire un `CameraNode` dans le graphe, dont la transform donne la vue.
- **`ResourceManager` / cache d'assets** — charge et **mutualise** meshes,
  textures, (futurs) matériaux par chemin ; centralise la résolution des chemins
  (assets + shaders) au même endroit, packageable.
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

---

## 5. Dette technique & environnement
- **Validation layers absentes** (Vulkan SDK non installé) → installer.
- **Tests / CI** : aucun. Au minimum, un smoke test « init + 1 frame headless »
  et un build CI.
- **Toolchain MSYS2** : link statique en contournement du crash `ld` (cf.
  `CLAUDE.md`). Vrai correctif : réinstaller `mingw-w64-ucrt-x86_64-gcc/binutils`.
- **Curseur souris** toujours capturé (pas de toggle) — gênant pour debugger /
  ImGui. Ajouter un raccourci pour libérer le curseur.

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

1. Vulkan SDK + validation layers, puis fixer le sémaphore `renderFinished` et
   les macros GLM (rapide, fiabilité).
2. `-Wall -Wextra`, petit `Log`.
3. ImGui + pipeline cache + MSAA (étape 7).
4. `Material` + `ResourceManager` + unification des chemins d'assets.
5. Extraire `Renderer` ; faire de la caméra un `CameraNode` ; `Input`/`Time`.
6. Couche jeu (étape 8).
7. PBR, ombres, baked GI.
8. OpenXR (étape 9).
