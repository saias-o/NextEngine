# Plan complet — Export Web (Étape 16)

> **Statut : proposition à valider avant démarrage.**
> Cible : exporter un jeu Saida vers le navigateur, **optimisé**.
> Décisions actées : **parité rendu complète** + **RHI propre** (abstraction
> GPU avec backends Vulkan et WebGPU).

---

## 1. Décision technique & justification

### 1.1 Cible GPU : WebGPU (pas WebGL2)

Le moteur dépend des **compute shaders** partout — le `CMakeLists` liste
`culling.comp`, `particle_*.comp` (5), `ddgi_*.comp` (3), soit le culling
GPU-driven, les particules GPU et la GI DDGI. **WebGL2 n'a pas de compute
shaders** : ces systèmes ne pourraient pas exister. WebGPU est donc la seule
cible viable, et c'est aussi l'API la plus proche de Vulkan (command encoder,
render pass, bind groups, pipelines, indirect draw, compute).

### 1.2 Toolchain : Emscripten (C++17 → WASM)

- Compilation C++ → WebAssembly via **emsdk** (cross-compile, build séparé).
- Fenêtre/surface : **port GLFW d'Emscripten** (`-sUSE_GLFW=3`) → mappe sur un
  `<canvas>` HTML. Notre `core/Window` continue de parler GLFW.
- GPU : **WebGPU via Emscripten** (port `emdawnwebgpu` / `webgpu.h`), exécuté
  contre le WebGPU natif du navigateur.

### 1.3 Le périmètre web = le **runtime**, pas l'éditeur

Le build web cible **`SaidaEngineRuntime`** (`src/runtime/main.cpp`), pas
l'éditeur. Conséquence directe : **ImGui, l'éditeur, le serveur MCP et OpenXR
sont exclus du web** (`SAIDA_ENABLE_XR=OFF`, pas de `saida_editor`, pas de
`imgui_impl_*`). Ça réduit nettement la surface à porter.

---

## 2. État des lieux du couplage Vulkan

`172 occurrences Vulkan dans 40 fichiers`. Le rendu est câblé sur **Vulkan 1.3**
(dynamic rendering, multiview, VMA, timestamp queries, indirect-count). À
abstraire derrière la RHI :

| Fichier | Rôle | Difficulté WebGPU |
|---|---|---|
| `graphics/VulkanDevice` | device/queues/allocator/pipeline cache/MSAA | Moyen (concepts mappent) |
| `graphics/Swapchain` | présentation + depth + render pass | Moyen (la couture Desktop/XR/Web) |
| `graphics/Buffer` | VkBuffer + VMA (`MemoryUsage`) | Facile (l'enum reste, l'alloc devient WebGPU) |
| `graphics/Texture` `StorageImage` `ShadowMap` | images + views + samplers | Moyen |
| `graphics/Pipeline` `ComputePipeline` | pipelines + layouts | Moyen |
| `graphics/Material` `ResourceManager` | descriptor sets 1 / pools | Moyen (set → bind group) |
| `graphics/GpuProfiler` | timestamp queries | **timestamp-query optionnel en WebGPU → fallback** |
| `graphics/GeometryRegistry` | VBO/IBO partagés | Facile |
| `render/Renderer` | toute la machinerie de frame | **Le gros morceau** |
| `render/PostProcessor` `GIVolume` `fx/ParticleRuntime` | compute + images | Moyen |
| `render/features/*` (Water/Particle/Outline/Skybox/DebugLines) | scene-pass features | Facile une fois la RHI posée |
| `graphics/ImGuiLayer` | overlay debug | **Éditeur uniquement → hors build web** |

---

## 3. Architecture RHI

### 3.1 Principe : abstraire les **ressources**, pas la logique

`Renderer`, `GIVolume`, `ShadowMap`, `PostProcessor`, les features… **restent
inchangés dans leur logique**. Ils cessent juste de parler `Vk*` pour parler
`rhi::*`. On n'abstrait pas le rendu, on abstrait l'API GPU.

### 3.2 Surface RHI (≈12 types, `src/rhi/Rhi.hpp`)

```
rhi::Device          ← VulkanDevice         (device, queues, allocator, caps)
rhi::Surface         ← Swapchain            (présentation : seam Desktop/XR/Web)
rhi::Buffer          ← Buffer               (conserve enum MemoryUsage)
rhi::Texture         ← Texture/StorageImage (image + view + sampler)
rhi::Sampler         ← (extrait de Texture)
rhi::ShaderModule    ← SPIR-V (Vulkan) / WGSL (WebGPU)
rhi::Pipeline        ← Pipeline             (render)
rhi::ComputePipeline ← ComputePipeline
rhi::BindGroupLayout ← VkDescriptorSetLayout
rhi::BindGroup       ← VkDescriptorSet      (set 0/1 mappent 1:1)
rhi::CommandEncoder  ← VkCommandBuffer      (sync cachée à l'intérieur)
rhi::RenderPass / rhi::ComputePass ← begin/end rendering (encoders de passe)
```

Backends :
```
src/rhi/Rhi.hpp                interface + handles + RhiCapabilities
src/rhi/vulkan/*.cpp           implémentation Vulkan (= code actuel déplacé)
src/rhi/webgpu/*.cpp           implémentation WebGPU (neuve)
```

### 3.3 Capabilities (négociées au runtime)

```cpp
struct RhiCapabilities {
    bool bindless;            // false sur la plupart des WebGPU actuels
    bool drawIndirectCount;   // false en WebGPU
    bool timestampQueries;    // optionnel en WebGPU
    bool multiview;           // false sur web (XR-only)
    uint32_t maxMsaaSamples;
};
```

Le `Renderer` lit ces caps et choisit le chemin (ex. bindless vs bind-group par
texture). **Aucune branche `#ifdef WEBGPU` dans la logique de rendu** — tout
passe par les caps, donc le même code sert desktop et web.

---

## 4. Les 5 pièges Vulkan→WebGPU (à régler dès le design RHI)

1. **Push constants → inexistant en WebGPU.**
   `PushConstants {model, params}` par objet (cf. `Renderer.hpp:49`) devient un
   **storage buffer indexé par `gl_InstanceIndex`** (ou uniform à dynamic
   offset). On unifie les **deux** backends sur le storage buffer : plus rapide
   ici *et* rapproche du GPU-driven existant.

2. **`vkCmdDrawIndexedIndirectCount` → absent.**
   Le culling compute écrit un `countBuffer_` (cf. `Renderer.hpp:245`). WebGPU
   n'a pas de draw-indirect-**count**. Solution portable : le compute écrit les
   draws non-visibles avec `instanceCount = 0` (draws dégénérés), puis on boucle
   sur un `drawIndexedIndirect` simple. Marche identiquement sur Vulkan.

3. **Bindless → support WebGPU jeune.**
   Le chemin WebCanvas world utilise un index de texture bindless. La RHI expose
   `caps.bindless` + un fallback **un bind group par texture**.

4. **Sync explicite (semaphores/fences) → automatique en WebGPU.**
   `imageAvailableSemaphores_`, `inFlightFences_` (cf. `Renderer.hpp:256-257`)
   sont **cachés dans la RHI** : gérés côté Vulkan, no-op côté WebGPU. La RHI
   **n'expose jamais** `VkSemaphore`/`VkFence`.

5. **Boucle de rendu bloquante → interdite sur le web.**
   Emscripten ne tolère pas un `while(true)` qui ne rend jamais la main au
   navigateur. La boucle de l'`Engine` doit exposer un **tick par frame**
   appelable depuis `emscripten_set_main_loop` (callback RAF). Refactor ciblé
   d'`Engine::run()` en `Engine::tick(dt)` + un pilote desktop (`while`) et un
   pilote web (RAF).

---

## 5. Pipeline shaders : GLSL → SPIR-V → WGSL

Les navigateurs n'acceptent que **WGSL**. On garde **la source GLSL unique**
(34 shaders + variantes `-DBINDLESS`/`-DMULTIVIEW`) et on ajoute une passe au
build :

```
GLSL --glslc--> SPIR-V --tint(ou naga)--> WGSL
```

- Étendre le bloc shader du `CMakeLists` : après chaque `.spv`, une commande
  `tint <in>.spv -o <out>.wgsl` (ou `naga`) quand on configure pour le web.
- Les variantes `-DMULTIVIEW` ne sont pas générées pour le web (pas de XR).
- Risque à vérifier au spike : compatibilité des features GLSL avancées
  (subgroupes éventuels, atomics dans les compute particles/ddgi) avec WGSL.
  Tint couvre la grande majorité ; les cas tordus se règlent au cas par cas.

---

## 6. Optimisation (le cœur du « optimisé »)

C'est là qu'un jeu web se gagne ou se perd. **Indépendant du backend GPU**, donc
livrable tôt (phase 16.1) et bénéfique **aussi en desktop**.

### 6.1 Géométrie
- Le **bugatti 84 Mo en `.obj` est un non-départ** sur le web. Bascule vers
  **glTF/GLB** (`GLTFLoader` + `cgltf` déjà présents) + compression
  **meshopt** (`meshoptimizer` déjà vendu via AutoLOD).
- Quantification des attributs (positions/normales/uv) + cache-optimization
  meshopt → moins de bande passante et de VRAM.

### 6.2 Textures
- **PNG → KTX2 + Basis Universal** : transcodage runtime vers la compression GPU
  du device (BC sur desktop, ASTC/ETC sur mobile). Gains massifs **download
  ET VRAM**. Le `Texture`/`ResourceManager` apprend à charger du KTX2.

### 6.3 Assets & système de fichiers
- Pas de FS sur le web : les chemins absolus bakés (`SAIDA_PROJECT_ROOT`, cf.
  `core/Paths.hpp`) ne marchent pas. Deux options :
  - **MEMFS preload** (`--preload-file`) : assets empaquetés dans un `.data`,
    simple, bon pour petits jeux.
  - **Fetch + IDBFS** : streaming HTTP des assets à la demande + cache
    IndexedDB, pour gros jeux. **Recommandé** pour le « optimisé ».
- `Paths.hpp` gagne un backend web (résolution → URL/MEMFS).

### 6.4 Taille WASM & livraison
- `-Oz` (ou `-O3` + `wasm-opt -Oz`), LTO, strip.
- Compression **Brotli** servie par le serveur (en-tête `Content-Encoding`).
- Code-splitting éventuel (charger physique/audio à la demande).

### 6.5 Threads
- WASM threads = `-pthread` + **SharedArrayBuffer**, qui exige les en-têtes
  **COOP/COEP** côté hébergement (`Cross-Origin-Opener-Policy: same-origin`,
  `Cross-Origin-Embedder-Policy: require-corp`).
- Jolt et le job system peuvent tourner mono-thread en fallback si l'hébergeur
  ne fournit pas COOP/COEP.

---

## 7. Compatibilité des dépendances (WASM)

| Dépendance | Web | Note |
|---|---|---|
| **GLFW** | ✅ port Emscripten (`-sUSE_GLFW=3`) → canvas | windowing/input OK |
| **WebGPU** | ✅ natif navigateur via Emscripten | remplace Vulkan |
| **QuickJS** | ✅ C pur | surveiller taille de stack WASM |
| **Jolt** | ✅ support Emscripten officiel | mono ou multi-thread |
| **RmlUi + freetype** | ✅ C++ pur | rendu via RHI (WebCanvas) |
| **miniaudio** | ✅ backend Web Audio | déjà utilisé (`AudioUsage.cpp`) |
| **glm / stb / tinyobj / cgltf / json / VMA** | ✅ headers portables | VMA inutile côté WebGPU |
| **meshoptimizer** | ✅ déjà vendu | utilisé en 16.1 |
| **ImGui** | ⛔ éditeur seulement | hors build web |
| **OpenXR** | ⛔ `SAIDA_ENABLE_XR=OFF` | WebXR = chemin futur séparé |
| **MCP** | ⛔ éditeur seulement | hors build web |

---

## 8. Changements de build (CMake)

- **Fichier toolchain Emscripten** + build séparé `build-web`
  (`emcmake cmake -S . -B build-web -G Ninja`).
- Bloc `if(EMSCRIPTEN)` : remplacer `find_package(Vulkan)` par les flags WebGPU,
  désactiver XR/MCP/éditeur, cibler `SaidaEngineRuntime` uniquement, sortie
  `.html/.js/.wasm/.data`.
- Le bloc `if(MINGW) -static …` ne s'applique pas sous Emscripten (déjà gardé).
- Flags de link web : `-sUSE_GLFW=3 -sUSE_WEBGPU=1 -sASYNCIFY` (si besoin),
  `-pthread`, `-sALLOW_MEMORY_GROWTH`, `--preload-file` ou fetch.
- Génération WGSL branchée dans la cible `Shaders` quand `EMSCRIPTEN`.

---

## 9. Découpage en phases (livrables indépendants)

| Phase | Contenu | Risque | Dépend de |
|---|---|---|---|
| **16.0 Spike** | emsdk + WebGPU + 1 shader WGSL + triangle WASM | dé-risque la toolchain | — |
| **16.1 Assets** | glTF+meshopt, KTX2/Basis, FS web, streaming | faible (utile aussi desktop) | — |
| **16.2 Shaders** | GLSL→SPIR-V→WGSL (Tint) au build | moyen | 16.0 |
| **16.3 RHI** | extraire Vulkan derrière `rhi::*`, **desktop intact** | **élevé (gros refactor)** | — |
| **16.4 Backend WebGPU** | `rhi/webgpu/*`, chemin minimal Lit+tonemap | élevé | 16.2, 16.3 |
| **16.5 Parité** | shadows → DDGI → particules → post-process | moyen, incrémental | 16.4 |
| **16.6 Packaging** | BuildExporter web, COOP/COEP, Brotli, taille WASM | moyen | 16.4 |

### Ordre d'exécution recommandé

1. **16.1 + 16.2** d'abord : gains immédiats, **zéro risque** sur l'existant
   (les assets profitent aussi au desktop, la passe WGSL ne casse rien).
2. **16.3 (extraction RHI)** ensuite, **pendant que tout est encore Vulkan-only**
   → on valide à **chaque commit** que le desktop tourne identique (c'est le
   refactor à risque, on le sécurise par la non-régression desktop).
3. **16.0 spike** peut se faire en parallèle très tôt (isolé du moteur) pour
   verrouiller la toolchain.
4. **16.4 → 16.5 → 16.6** : le backend web et la montée en parité.

---

## 10. Critères de validation par phase

- **16.0** : un triangle s'affiche dans le navigateur (Chrome/Edge WebGPU).
- **16.1** : le bugatti chargé en GLB compressé, textures KTX2, **desktop
  inchangé** ; mesure du gain download/VRAM.
- **16.2** : les 34 shaders produisent du WGSL valide (Tint sans erreur).
- **16.3** : **build desktop identique** (visuel + perf), tous les tests passent
  (`saida_*_tests`) ; aucun `Vk*` hors de `src/rhi/vulkan/`.
- **16.4** : scène Lit + tonemap rendue dans le navigateur.
- **16.5** : parité visuelle desktop/web sur une scène de référence.
- **16.6** : un `.html` jouable produit par le bouton Build, servi avec
  COOP/COEP, taille WASM mesurée et optimisée.

---

## 11. Risques & points ouverts

- **Refactor RHI (16.3)** : c'est le poste le plus lourd (effort ≳ XR). Mitigé
  par la non-régression desktop à chaque étape.
- **Tint sur compute avancés** (atomics DDGI/particules) : à vérifier tôt.
- **Performance WebGPU** vs Vulkan natif : attendre des écarts ; le « optimisé »
  des assets (16.1) compense beaucoup côté download/VRAM.
- **Hébergement COOP/COEP** : nécessaire pour les threads ; sinon fallback mono-thread.
- **Boucle RAF** : le refactor `run()`→`tick()` touche `Engine` et les deux
  `main.cpp` (runtime/éditeur) — à faire proprement, sans dupliquer la logique.
- **WebXR** : explicitement **hors scope** de l'Étape 16 (chemin futur distinct
  du desktop web).

---

## 12. Prochaine action

Après validation de ce plan, démarrage par **16.0 (spike)** pour verrouiller la
toolchain Emscripten + WebGPU, **en parallèle de 16.1 (assets)** qui ne touche
pas au backend et bénéficie déjà au desktop.
