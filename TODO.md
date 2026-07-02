# SaidaEngine — Roadmap des tâches restantes

> Détail des tâches non terminées, par étape. La vue d'ensemble (étapes
> cochées) est dans [CLAUDE.md](CLAUDE.md). Ce fichier liste ce qui **manque**.

---

## Étape 8 — Couche jeu
- [ ] Runtime standalone sans éditeur

---

## Étape 9 — Rendu global / GI pragmatique
- [ ] Radiance Cascades 2D / World Radiance Cache / froxels volumétriques (recherche future)

---

## Étape 12 — UI 2D (Screen & World Space)
- [ ] Backend GPU/Vulkan RmlUi pour gros documents animés (optionnel)

---

## Étape 13 — Intégration LLM Native
- [ ] Inspecteur générique pour behaviours réfléchis
- [ ] World model (état du monde pour l'IA)
- [ ] Skills exécutables
- [ ] Agents autonomes

---

## Étape 14 — XR / OpenXR
- [ ] Hand tracking skeletal (`XR_EXT_hand_tracking`)
- [ ] MSAA multiview + resolve par layer
- [ ] ImGui overlay en XR
- [ ] Refactor DRY du Renderer (couture desktop/XR)
- [ ] Backend d'anchors réel

---

## Étape 15 — Build & Release Windows
- [ ] Gestion versions, métadonnées executable, icône du jeu
- [ ] LTO build optimization

---

## Étape 16 — Export Web (WASM + WebGPU)

> Cible : **Emscripten (C++17 → WASM) + backend WebGPU**, parité rendu complète.
> Approche : **RHI propre** (abstraction GPU avec backends Vulkan + WebGPU),
> dans l'esprit « pipeline universel ». Plan détaillé : [PLAN_WEB_EXPORT.md](PLAN_WEB_EXPORT.md).

- [x] **16.0 — Spike faisabilité** : toolchain Emscripten 6.0.1 + port `emdawnwebgpu` validés ; `web/spike/` compile GLFW+WebGPU+boucle RAF (clear animé) → `index.html`. (Shader WGSL repoussé en 16.2 avec les vrais shaders.)
- [~] **16.1 — Pipeline assets web** (indépendant du backend, utile aussi desktop) :
  - [x] `meshoptimizer` 0.22 vendu + câblé au build
  - [x] Décodage `EXT_meshopt_compression` au chargement GLB (`scene/GltfMeshopt`)
  - [x] Exporteur GLB meshopt (`tools/MeshoptGlbExporter`) : optimise (vertex cache/fetch) + encode, écrit un GLB valide
  - [x] Test de round-trip encode→GLB→décode sans GPU (`tests/MeshoptExportTests`) — boucle prouvée
  - [x] Quantification des attributs à l'export : normals/tangents int8 normalized (KHR_mesh_quantization), UV [0,1] uint16 normalized, couleur droppée (loader = blanc), positions float exactes — ~100 → 28-36 o/vertex avant entropie. Décodage transparent (cgltf résout les accessors normalized). Test round-trip quantifié ajouté.
  - [x] Déprécation `.obj` : warning au chargement pointant vers l'export GLB meshopt.
  - [x] Système de fichiers web v1 : MEMFS preload (`--preload-file`), `Paths.hpp` backend `__EMSCRIPTEN__` (`/assets`, `/shaders` + swap `.spv`→`.wgsl`). Fetch/IDBFS streaming = évolution ultérieure.
  - [ ] Brancher l'export GLB sur l'UI d'import de l'éditeur (le packager web est branché, voir 16.6)
  - [ ] Textures **KTX2 / Basis Universal** (transcodage GPU) — nécessite de vendre basis_universal
  - [ ] Fetch + IDBFS streaming (gros jeux) en remplacement du MEMFS preload
- [x] **16.2 — Passe shader GLSL → SPIR-V → WGSL** : **TERMINÉ. Les 33 shaders transpilent en WGSL** via naga (`cmake --build build --target WebShaders`), **desktop identique au bit près partout** (vérifié via arbre git HEAD reconstruit ; `ninja: no work to do` après conversion), 14/14 tests OK.
  - Constat : ni naga ni Tint (testé via tint-wasm, parser IR + AST) ne convertissent les combined image samplers GLSL ni les push constants → adaptation shader incontournable, mais **naga suffit** une fois les shaders adaptés (Tint abandonné).
  - Couche `shaders/web_compat.glsl` : macros `DECL_TEX2D/3D/CUBE/SHADOW2DARRAY` + `TEX2D/…` + `PUSH_QUALIFIER`. Desktop = branche `#else`, inchangé. `-DWEB` → texture+sampler séparés + UBO.
  - Cas spéciaux gérés : `ui.frag`/`web_canvas_world.frag` (bindless → `#ifdef WEB` texture unique), `culling.comp` (`writeonly` gardé desktop, readwrite sur web), `ddgi_trace` (`texelFetch(giVoxels)` séparé).
  - CMake : `WEB_READY_SHADERS` (= les 33) → `web.<name>.spv` (-DWEB) → `build/shaders/wgsl/`. Cible `WebShaders` gardée par `find_program(naga)`, sans impact desktop.
  - **Note 16.4** : les bindings web divergent (sampler séparé, push→UBO set 3) — le backend WebGPU devra aligner ses bind group layouts dessus.
- [x] **16.3 — Extraction RHI** : **TERMINÉ.** Tout le Vulkan de `render/` et `fx/` est derrière `rhi::*`, sans changement de comportement desktop. **Design : [PLAN_RHI.md](PLAN_RHI.md)** — RHI mince, backend au **compile-time** (pas de vtable), on abstrait ressources+commandes, pas la logique de rendu.
  - [x] 16.3.a — `rhi::Capabilities` backend-neutre + `Rhi.hpp` (ancre compile-time).
  - [x] 16.3.b — `rhi::Buffer` (`rhi::BufferUsage` neutre, 27 sites).
  - [x] 16.3.c — `rhi::Texture` (+ `rhi::Format` neutre, sampler embarqué).
  - [x] 16.3.d — `rhi::BindGroup(Layout)` (pool Vulkan caché growable, groupes immutables — on recrée au lieu de `vkUpdateDescriptorSets`) + `Pipeline::Desc`/`ComputePipeline` neutres. **Les ctors legacy Vulkan-typés sont supprimés** ; `BindGroupLayoutRef` couvre l'interop du set bindless brut.
  - [x] 16.3.e — `rhi::CommandEncoder` / passes / `ResourceState` : **plus un seul `vkCmd*` dans `render/` et `fx/`**. ShadowMap (pilote), copies/staging (`Device::withSingleTimeEncoder`, `copyBufferToTexture`, `clearColorTexture`), features + `Mesh::bind/draw(RenderPassEncoder&)` + UIRenderer, PostProcessor (états `ResourceState` trackés), GIVolume (compute + voxelize via `Pipeline::Desc` attachment-less), ParticleRuntime (indirect), Renderer (~50 sites, `drawIndexedIndirectCount` derrière `caps.drawIndirectCount` avec fallback).
  - [x] 16.3.f — `rhi::Device` (= VulkanDevice, alias), `rhi::Surface` (= Swapchain : fences + sémaphores acquire absorbés, API `waitFrame`/`acquire`/`submitAndPresent` — le Renderer et le Hub n'émettent plus un seul submit/present), `rhi::RenderTexture` (render targets neutres : HDR desktop/XR, shadow array, bloom, atlases DDGI, voxel 3D ; `StorageImage` supprimé), `rhi::Sampler` (desc neutre : tonemap, bloom, GI, shadow PCF).
  - **Escape hatches assumés (v1, documentés)** : set bindless UPDATE_AFTER_BIND (ResourceManager) en Vulkan brut → chemin `caps.bindless` de 16.4 ; ImGuiLayer + GpuProfiler sur `handle()` (desktop-only) ; blits de mipmaps dans `Texture` (backend, pas d'abstraction neutre par design) ; lifecycle des command buffers de frame (`vkAllocate/Begin/End/ResetCommandBuffer` dans Renderer) → repris avec `Engine::tick()` + encoder-par-frame WebGPU en 16.4 ; le compute des particules s'enregistre dans la passe HDR (préexistant) → à hoister pour WebGPU en 16.4.
  - **Note 16.4** : flags `caps.pushConstants`/`caps.bindless` à ajouter quand le backend WebGPU les lira (aujourd'hui `descriptorIndexing`/`drawIndirectCount` couvrent les consommateurs existants).
- [~] **16.4 — Backend WebGPU** : **backend complet écrit et compilant** (`rhi/webgpu/*` : Device (init async), Surface (canvas, waitFrame/acquire/submitAndPresent), Buffer (shadow CPU + writeBuffer), Texture (mips CPU), RenderTexture, Sampler, BindGroup(Layout) avec hints web (dimension/comparison/unfilterable — divergence bindings anticipée §7.4), Pipeline/ComputePipeline (.wgsl, entry `main`), CommandEncoder/passes (barriers no-op), **émulation push constants** = ring uniform 256B-aligné + dynamic offsets sur `@group(3)`, caps web). `Engine::run()` scindé en `tick()` (piège 5). Cible `web/runtime/` : scène **Lit + tonemap avec les vrais WGSL** (set 0 global avec dummies neutres, set 1 matériau, set 3 push-UBO), wasm 176 Ko + js 159 Ko. **Reste : validation visuelle dans Chrome/Edge (l'agent ne peut pas exécuter WebGPU) + corrections d'itération navigateur.**
- [ ] **16.5 — Parité rendu** : porter le vrai `Renderer`/`GIVolume`/features sur le backend web (les interfaces render/ trafiquent encore des handles Vk* — il faut les neutraliser en types rhi:: pour compiler sous Emscripten), puis shadows → DDGI (hoister le compute particules hors de la passe HDR, clearColorTexture non-zéro via pass) → particules GPU → post-process, une feature à la fois.
- [~] **16.6 — Packaging web** : `web/build_web.sh` (emcmake, -Oz), `web/serve.py` (COOP/COEP + .br), `web/compress_brotli.sh`, `BuildExporter::exportWebBuild` branché sur la plateforme Web du dialog Build Settings (copie build-web + serve.py + README). **Reste : mesure/optimisation taille en conditions réelles (wasm-opt, code splitting) une fois 16.5 avancé.**

**Pièges Vulkan→WebGPU à régler dès le design RHI** (voir plan) :
- [ ] Push constants → storage buffer indexé (pas d'équivalent WebGPU)
- [ ] Multi-draw-indirect-count → draws dégénérés `instanceCount=0` (pas de `DrawIndirectCount`)
- [ ] Bindless → capability + fallback bind-group-par-texture
- [ ] Sync explicite (semaphores/fences) → cachée dans la RHI (no-op côté WebGPU)
- [ ] MSAA + depth resolve → vérifier les limites WebGPU

---

## Priorité de travail

**TRÈS HAUTE** : Export Web 16.0 (spike) → 16.1 (assets) → 16.2 (shaders)
**HAUTE** : Export Web 16.3 (RHI) → 16.4 (backend WebGPU)
**MOYENNE** : Export Web 16.5/16.6, Runtime standalone, XR handtracking/MSAA/ImGui, Versions
**BASSE** : Refactor DRY Renderer, anchors backend, LTO, GPU RmlUi, World model, Skills, Agents
**FUTURE** : Radiance Cascades, recherche GI avancée

---

## Dettes techniques repérées
- [ ] `TimelinePropertyTrack::evaluate()` est encore un placeholder no-op ; implémenter le binding réflexion + interpolation de propriétés.
- [ ] `GLTFLoader` fournit une tangente par défaut `(1,0,0,1)` quand le mesh n'en a pas ; ajouter MikkTSpace ou désactiver proprement le normal mapping dans ce cas.
- [ ] Certaines mutations éditeur marquent seulement le document dirty sans être undoables (scripts WebCanvas, changements de `CollisionShape` avec `resetAuto`) ; les raccorder au système de commandes.
- [ ] Renommage de projet dans le Hub : vérifier la synchronisation entre le dossier, l'entrée Hub et le fichier `.neproj`.
- [ ] Finir la migration des behaviours built-in restants vers la réflexion/registry unifiée.
