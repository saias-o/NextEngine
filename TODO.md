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
  - [ ] Quantification des attributs à l'export (vrai gain de taille ; v1 est lossless float)
  - [ ] Brancher l'export sur l'UI d'import / le packager de build
  - [ ] Textures **KTX2 / Basis Universal** (transcodage GPU)
  - [ ] Retrait/déprécation du chemin `.obj` lourd
  - [ ] Système de fichiers web (MEMFS preload ou fetch/IDBFS) + streaming/lazy-load
- [x] **16.2 — Passe shader GLSL → SPIR-V → WGSL** : **TERMINÉ. Les 33 shaders transpilent en WGSL** via naga (`cmake --build build --target WebShaders`), **desktop identique au bit près partout** (vérifié via arbre git HEAD reconstruit ; `ninja: no work to do` après conversion), 14/14 tests OK.
  - Constat : ni naga ni Tint (testé via tint-wasm, parser IR + AST) ne convertissent les combined image samplers GLSL ni les push constants → adaptation shader incontournable, mais **naga suffit** une fois les shaders adaptés (Tint abandonné).
  - Couche `shaders/web_compat.glsl` : macros `DECL_TEX2D/3D/CUBE/SHADOW2DARRAY` + `TEX2D/…` + `PUSH_QUALIFIER`. Desktop = branche `#else`, inchangé. `-DWEB` → texture+sampler séparés + UBO.
  - Cas spéciaux gérés : `ui.frag`/`web_canvas_world.frag` (bindless → `#ifdef WEB` texture unique), `culling.comp` (`writeonly` gardé desktop, readwrite sur web), `ddgi_trace` (`texelFetch(giVoxels)` séparé).
  - CMake : `WEB_READY_SHADERS` (= les 33) → `web.<name>.spv` (-DWEB) → `build/shaders/wgsl/`. Cible `WebShaders` gardée par `find_program(naga)`, sans impact desktop.
  - **Note 16.4** : les bindings web divergent (sampler séparé, push→UBO set 3) — le backend WebGPU devra aligner ses bind group layouts dessus.
- [~] **16.3 — Extraction RHI** : déplacer le Vulkan existant derrière `rhi::*`, **sans changement de comportement** (desktop identique, validable à chaque commit). **Design : [PLAN_RHI.md](PLAN_RHI.md)** — RHI mince, backend au **compile-time** (pas de vtable), on abstrait ressources+commandes, pas la logique de rendu.
  - [x] Design + décision d'archi (compile-time backend alias, ce qu'on n'abstrait pas)
  - [x] 16.3.a — `src/rhi/` créé : `rhi::Capabilities` backend-neutre (`maxSamples` → uint32, plus de `Vk*`), `RenderCapabilities` devient un shim d'alias (consommateurs inchangés), `Rhi.hpp` ancre le backend compile-time. Build + 14/14 tests OK. (flags web `bindless`/`pushConstants` : ajoutés quand un consommateur les lira, 16.3.e)
  - [x] 16.3.b — `rhi::Buffer` : API de construction neutre (`rhi::BufferUsage` au lieu de `VkBufferUsageFlags`, tailles `uint64_t`), aliasé dans `Rhi.hpp`. 27 sites convertis (mapping 1:1). `handle()` reste Vulkan (backend-interne, dé-Vulkanisé en 16.3.e avec le CommandEncoder). Build + 14/14 tests OK.
  - [x] 16.3.c — `rhi::Texture` : `rhi::Format` neutre (+ mapping `rhi/vulkan/Format.hpp`), constructeur mémoire dé-Vulkanisé, `rhi::Texture` aliasé, 5 sites convertis. Sampler embarqué dans Texture. Build + 14/14 tests OK.
  - [ ] 16.3.d — `rhi::BindGroup(Layout)` + desc neutre `Pipeline`/`ComputePipeline`.
    Design détaillé : PLAN_RHI.md §7. Lots : **d.a** types + graphics/, **d.b**
    render/+fx, **d.c** Pipeline desc neutre (17 sites).
  - [ ] 16.3.e — `rhi::CommandEncoder` / passes / `ResourceState` (le gros, ~236
    sites). Clé : encoder = vue non-possédante sur `VkCommandBuffer` avec escape
    hatch → migration sous-système par sous-système, desktop-vert à chaque commit
    (PLAN_RHI.md §7.1). Lots : **e.a** encoder + pilote ShadowMap, **e.b**
    copies/staging, **e.c** features+UIRenderer, **e.d** PostProcessor, **e.e**
    GIVolume, **e.f** ParticleRuntime, **e.g** Renderer (+XR).
  - [ ] 16.3.f — `rhi::Device` / `Surface` (couture présentation, sync cachée ;
    les conversions `VkFormat↔rhi::Format` temporaires disparaissent)
- [ ] **16.4 — Backend WebGPU** : implémenter `rhi/webgpu/*` (Dawn / `webgpu.h` Emscripten), chemin minimal Lit + tonemap d'abord.
- [ ] **16.5 — Parité rendu** : shadows → DDGI → particules GPU → post-process, une feature à la fois.
- [ ] **16.6 — Packaging web** : intégration au `BuildExporter`/packager, en-têtes COOP/COEP (threads/SharedArrayBuffer), Brotli, optimisation taille WASM (LTO / `-Oz` / `wasm-opt`).

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
