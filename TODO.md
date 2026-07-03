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
- [x] **16.4 — Backend WebGPU** : **backend complet écrit et compilant** (`rhi/webgpu/*` : Device (init async), Surface (canvas, waitFrame/acquire/submitAndPresent), Buffer (shadow CPU + writeBuffer), Texture (mips CPU), RenderTexture, Sampler, BindGroup(Layout) avec hints web (dimension/comparison/unfilterable — divergence bindings anticipée §7.4), Pipeline/ComputePipeline (.wgsl, entry `main`), CommandEncoder/passes (barriers no-op), **émulation push constants** = ring uniform 256B-aligné + dynamic offsets sur `@group(3)`, caps web). `Engine::run()` scindé en `tick()` (piège 5). Cible `web/runtime/` : scène **Lit + tonemap avec les vrais WGSL** (set 0 global avec dummies neutres, set 1 matériau, set 3 push-UBO), wasm 176 Ko + js 159 Ko. **VALIDÉ dans le navigateur** (cube checker Lit + sol, éclairé, tonemappé — rendu WebGPU réel vérifié pixel par pixel). Corrections de premier lancement : (1) l'analyse d'uniformité WGSL (Tint) rejette tout échantillonnage à dérivées implicites hors flux uniforme → `SAMPLE_SHADOW2DARRAY` (textureLod via `GL_EXT_texture_shadow_lod` sous -DWEB, desktop inchangé), `textureLod` pour la depth du tonemap, sample hors branche dans ui.frag, sample avant early-return dans voxelize.frag — 33/33 WGSL passent la validation Tint ; (2) le pipeline tonemap du runtime ne renseignait pas `bindGroupLayouts` ; (3) relink emcc dépend maintenant des .wgsl (`LINK_DEPENDS`) ; (4) shell.html explique quoi lancer si ouvert brut (au lieu de « loading… » infini).
- [x] **16.5 — Parité rendu** : le vrai `Renderer`/`GIVolume`/features tournent sur le backend web (scène de référence : cube Lit + sol + ombres + DDGI + skybox + particules GPU + AO + bloom + tonemap, validée pixel par pixel dans le navigateur, 40+ frames sans aucune erreur GPU).
  - [x] 16.5.a — **Interfaces neutralisées** : plus un type Vk* dans les en-têtes de `render/` et `fx/` hors blocs `SAIDA_ENABLE_XR`. Alias par backend `rhi::TextureView/TextureHandle/SamplerHandle/Extent2D/Rect2D/SampleCount` (`rhi/vulkan/Handles.hpp`, `rhi/webgpu/Handles.hpp`). `RenderContext` porte `rhi::Format` (les features n'appellent plus `fromVk`), `FrameContext.globalSet`/`GIVolume::update`/`ParticleRuntime::computeSet` passent des `rhi::BindGroup`, `PostProcessor`/`ShadowMap`/`UIRenderer` neutres. **Lifecycle des command buffers de frame déplacé dans la Surface** : `Swapchain::beginFrameCommands(frame)` → `rhi::CommandEncoder` + `submitAndPresent(encoder…)` (le Renderer n'a plus un seul vkAllocate/Begin/End/Reset) — même forme que le backend webgpu. Desktop validé (build XR ON, 14/14 tests, run 25 s OK).
  - [x] 16.5.b — Compiler la stack de rendu sous Emscripten : `Rhi.hpp` sélectionne `RhiWeb` sous `SAIDA_RHI_WEBGPU`; `Mesh`/`GeometryRegistry`/`Material`/`ResourceManager` compilent et tournent côté WebGPU (set 1 = BindGroup web, pas de bindless); `ImGuiLayer`/`GpuProfiler` ont des stubs web; la cible `web/runtime` lie maintenant la stack réelle (`ShadowMap`, `GIVolume`, `ParticleRuntime`, `PostProcessor`, `ParticleFeature`, `ResourceManager`) et build sous Emscripten.
  - [x] 16.5.c — **Le vrai `Renderer` tourne dans le navigateur** : `web/runtime/main.cpp` instancie `Renderer(device, surface, resources)` et appelle `drawFrame` — shadows + DDGI (voxelize/trace/blend) + passe HDR + bloom + tonemap, pixel-vérifié sans aucune erreur GPU ; le harness 16.4 est supprimé. Corrections backend/portage (le harness les masquait — une erreur de validation invalide le command buffer entier et le submit échoue **silencieusement**, emdawnwebgpu avale l'uncapturederror ; toujours valider sous `pushErrorScope` par frame) : (1) passe voxelize attachment-less interdite → cible dummy res×res + `colorWrite=false` (writeMask None) ; (2) `pushConstantSize` des pipelines scène rendu explicite (le défaut legacy 80 o du backend Vulkan n'existe pas côté web → group 3 absent) ; (3) `minBindingSize` du push-UBO arrondi à 16 (WGSL arrondit la taille des blocs) ; (4) atlas visibilité rg16f → rgba16f sous web (rg16f n'est pas un format storage WebGPU) ; (5) read-write storage réservé à r32* → blend en writeonly et copie du gutter **fusionnée dans ddgi_blend** sous -DWEB (passe borders inutilisée sur web ; SPIR-V desktop identiques au bit près) ; (6) le scope d'usage compute inclut TOUS les bind groups liés → set 0 « compute » dédié avec dummies aux bindings GI 4/5 (`giComputeGlobalGroups_`) ; (7) visibilité Compute ajoutée au layout global web (bindings 2/4-12, miroir desktop) ; (8) zero-clear textures via buffer zéro caché + copyBufferToTexture (voxel grid re-clearé chaque frame) ; (9) `cullMode=None` sur le tonemap (naga flippe Y en clip-space → winding inversé des triangles fullscreen à coordonnées brutes).
  - [x] 16.5.d — **Features web + AO + clears non-zéro** : le **système de features tourne sur web** via le registre (scopé : skybox + particules GPU sur web ; water/outline/debug-lines restent desktop-only). Le **compute des particules est hoisté hors de la passe HDR** : nouveau hook `ScenePassFeature::recordPrePass(PrePassContext)` appelé avant l'ouverture de la passe scène (le dispatch compute dans une render pass est interdit en WebGPU **et** en Vulkan dynamic rendering — bug latent desktop corrigé au passage). `ParticleFeature` : sim+compute en pré-passe, dessin indirect en passe. Skybox porté web (texture+sampler séparés). Particle render/compute set layouts : storage `readOnlyStorage` là où le WGSL est `readonly` (vertex-stage storage read-only obligatoire). **AO** activé et validé (le tonemap échantillonne la depth via `textureLod`, uniform-safe). **Clears non-zéro** implémentés via `queue.writeTexture` + encodeur float→half (init des constantes d'atlas GI). `Time::advance()` public pour l'hôte de frame autonome (le runtime web joue le rôle de l'Engine). Démo web : cube + sol + soleil (ombres) + émetteur de feu additif + ciel équirect procédural. Desktop inchangé (14/14 tests, SPIR-V DDGI identiques au bit près).
- [x] **16.6 — Packaging web** : `web/build_web.sh` (emcmake, -Oz + wasm-opt implicite), `web/serve.py` (COOP/COEP + sert `.br`), `web/compress_brotli.sh`, `BuildExporter::exportWebBuild` branché sur la plateforme Web du dialog Build Settings. **Tailles mesurées** (scène complète shadows+DDGI+particules+skybox+bloom) : wasm 438 Ko → **147 Ko brotli**, js 163 → 36 Ko, data 204 → 30 Ko = **~213 Ko brotli total** sur le réseau. `build_web.sh` purge désormais les `.br` périmés après build (sinon serve.py sert un artefact obsolète — piège rencontré). *Optionnel futur : code splitting / streaming assets si un vrai jeu grossit.*

**Pièges Vulkan→WebGPU** (tous adressés pendant 16.4/16.5) :
- [x] Push constants → émulation ring uniform 256 o-aligné + dynamic offsets sur `@group(3)` (`web_compat.glsl` `PUSH_QUALIFIER`, `Device::allocPushSlot`).
- [x] Multi-draw-indirect-count → `drawIndirect` en boucle (pas de `DrawIndirectCount` web) ; le GPU-driven culling reste desktop-only (`#ifndef SAIDA_RHI_WEBGPU`).
- [x] Bindless → capability + fallback : web utilise le set matériau par bind-group (pas de bindless), `caps.descriptorIndexing=false`.
- [x] Sync explicite (semaphores/fences) → cachée dans la RHI (no-op côté WebGPU ; frames sérialisées par le navigateur).
- [x] MSAA + depth resolve → web force MSAA off (`samples=1`) sur la cible ; le resolve n'est emprunté que sur desktop/XR. *(MSAA web = évolution ultérieure si besoin.)*

---

## Priorité de travail

**TRÈS HAUTE** : Export Web 16.0 (spike) → 16.1 (assets) → 16.2 (shaders)
**HAUTE** : Export Web 16.3 (RHI) → 16.4 (backend WebGPU)
**MOYENNE** : Runtime standalone, XR handtracking/MSAA/ImGui, Versions
**BASSE** : Refactor DRY Renderer, anchors backend, LTO, GPU RmlUi, World model, Skills, Agents
**FUTURE** : Radiance Cascades, recherche GI avancée

---

## Dettes techniques repérées
- [ ] `TimelinePropertyTrack::evaluate()` est encore un placeholder no-op ; implémenter le binding réflexion + interpolation de propriétés.
- [ ] `GLTFLoader` fournit une tangente par défaut `(1,0,0,1)` quand le mesh n'en a pas ; ajouter MikkTSpace ou désactiver proprement le normal mapping dans ce cas.
- [ ] Certaines mutations éditeur marquent seulement le document dirty sans être undoables (scripts WebCanvas, changements de `CollisionShape` avec `resetAuto`) ; les raccorder au système de commandes.
- [ ] Renommage de projet dans le Hub : vérifier la synchronisation entre le dossier, l'entrée Hub et le fichier `.neproj`.
- [ ] Finir la migration des behaviours built-in restants vers la réflexion/registry unifiée.
