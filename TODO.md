# SaidaEngine — Roadmap des tâches restantes

> Détail des tâches non terminées, par étape. La vue d'ensemble (étapes
> cochées) est dans [CLAUDE.md](CLAUDE.md). Ce fichier liste ce qui **manque**.

---

## Étape 8 — Couche jeu
- [x] Runtime standalone sans éditeur (`SaidaEngineRuntime` + `game.saida`, packagé par Build Settings)
- [ ] Valider le chemin « ship » de bout en bout sur machine vierge avec le jeu témoin (→ [PLAN_V1_ENGINE.md](PLAN_V1_ENGINE.md), chantier 1). Le jeu témoin existe ([WitnessGame/](WitnessGame/README.md), smoke test packagé validé) ; frictions restantes dans [docs/WITNESS_GAME.md](docs/WITNESS_GAME.md) (UI-depuis-JS, comms inter-nœuds JS, player web incomplet, animation/audio à brancher)

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
- [x] Gestion versions, métadonnées executable, icône du jeu (`ExeMetadata` : VERSIONINFO + RT_GROUP_ICON patchés dans le `<Game>.exe`, champs UI Build Settings)
- [ ] LTO build optimization

---

## Étape 16 — Export Web (WASM + WebGPU)

- [x] **Terminé (16.0 → 16.6).** RHI compile-time Vulkan/WebGPU, 33 shaders transpilés en WGSL (naga, desktop identique au bit près), backend `rhi/webgpu/*` complet, le vrai `Renderer` tourne dans le navigateur (shadows, DDGI, eau, skybox, particules GPU, AO, bloom, tonemap), validé sur la scène de référence BeachDemo (rendu identique au desktop), packaging brotli (~213 Ko).
- [ ] Brancher l'export GLB meshopt sur l'UI d'import de l'éditeur (le packager web est déjà branché)
- [ ] Textures KTX2 / Basis Universal (transcodage GPU)
- [ ] Fetch + IDBFS streaming (remplacement du MEMFS preload pour les gros jeux)
- [ ] MSAA sur le backend web (actuellement forcé à off)

---

## Priorité de travail

**HAUTE** : Runtime standalone, XR handtracking/MSAA/ImGui
**MOYENNE** : Versions/packaging Windows, KTX2/Basis + streaming web
**BASSE** : Refactor DRY Renderer, anchors backend, LTO, GPU RmlUi, World model, Skills, Agents
**FUTURE** : Radiance Cascades, recherche GI avancée

---

## Dettes techniques repérées
- [ ] `TimelinePropertyTrack::evaluate()` est encore un placeholder no-op ; implémenter le binding réflexion + interpolation de propriétés.
- [ ] `GLTFLoader` fournit une tangente par défaut `(1,0,0,1)` quand le mesh n'en a pas ; ajouter MikkTSpace ou désactiver proprement le normal mapping dans ce cas.
- [ ] Certaines mutations éditeur marquent seulement le document dirty sans être undoables (scripts WebCanvas, changements de `CollisionShape` avec `resetAuto`) ; les raccorder au système de commandes.
- [ ] Renommage de projet dans le Hub : vérifier la synchronisation entre le dossier, l'entrée Hub et le fichier `.neproj`.
- [ ] Finir la migration des behaviours built-in restants vers la réflexion/registry unifiée.
