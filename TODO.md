# SaidaEngine — Roadmap des tâches restantes

> Détail des tâches non terminées, par étape. La vue d'ensemble (étapes
> cochées) est dans [CLAUDE.md](CLAUDE.md). Ce fichier liste ce qui **manque**.

---

## Étape 8 — Couche jeu
- [x] Runtime standalone sans éditeur (`SaidaEngineRuntime` + `game.saida`, packagé par Build Settings)
- [ ] Valider le chemin « ship » de bout en bout sur machine vierge avec le jeu témoin (→ [PLAN_V1_ENGINE.md](PLAN_V1_ENGINE.md), chantier 1). Le jeu témoin, le runtime standalone et les exports CLI Windows/Web existent. Restent : clic Build UI, machine vierge, UI du player web, communication inter-nœuds/autoloads JS et traversée d'une séquence `.sseq`.

---

## Cycle de vie des assets (→ [PLAN_V1_ENGINE.md](PLAN_V1_ENGINE.md), chantier 3)
- [x] **Cœur livré (2026-07-15).** Textures et meshes `.obj` transitent par l'`AssetLoader` : étape de décodage par requête (stbi/tinyobj) sur le worker desktop / dans `pump()` web, création GPU sur le thread principal, clé d'entrée `(AssetID, AssetPayloadKind)`. `getTexture` non-bloquant (fallbacks visibles : défauts pendant le chargement, damier magenta « missing » en échec) avec rebind des matériaux au ready. `getMesh` `.obj` = proxy `Mesh` stable rempli au ready, re-fit physique automatique (`CollisionShapeNode::meshPending` diffère le body, `ensureResolved`→`markDirty`→rebuild Jolt).
- [x] **Déchargement GPU réel.** Mark-and-sweep au `changeScene` (`SceneTree::applyDeferred`→`ResourceManager::trimUnused`) : graveyard différé (kRetireFrames=4), recyclage des index bindless et des slots matériaux (freelists), builtins/proxies exempts. `gpuResidentBytes` dans `assets.stats()` + compteur profiler `Assets/GpuResidentBytes`, asserté stable sur 16 cycles hub↔arena par l'E2E desktop **et** web.
- [ ] Budget GPU **contraignant en cours de scène** (éviction LRU quand on dépasse, pas seulement au `changeScene`).
- [ ] Faire passer rigs (`.srig`) et animations (`.sclip`/`.sgraph`) par le loader + les inclure dans le sweep `trimUnused`.
- [ ] Streaming fetch/IDBFS web (voir Étape 16) pour que le budget borne réellement les gros jeux.
- [ ] Risque connu (éditeur) : Stop après un `changeScene` en Play peut perdre un mesh glTF à id dynamique dans le snapshot restauré (id non stable → ressource évincée non re-résoluble). Stabiliser l'identité des assets mémoire glTF avant de fermer le chantier.

---

## Étape 9 — Rendu global / GI pragmatique
- [ ] Radiance Cascades 2D / World Radiance Cache / froxels volumétriques (recherche future)

---

## Étape 12 — UI 2D (Screen & World Space)
- [ ] Backend GPU/Vulkan RmlUi pour gros documents animés (optionnel)

---

## Étape 13 — Intégration LLM Native
- [ ] Permissions MCP par outil, transactions groupées, dry-run/diff, snapshot et rollback global
- [ ] Inspecteur générique pour behaviours réfléchis
- [ ] World model (état du monde pour l'IA)
- [ ] Skills exécutables
- [ ] Agents autonomes

---

## Étape 14 — XR / OpenXR
- [x] Hand tracking skeletal (`XR_EXT_hand_tracking`) implémenté dans `XrHandTracking` et branché à `XRHand`
- [ ] Valider le hand tracking sur la matrice de casques/runtime visée et documenter les fallbacks contrôleurs/mains
- [ ] MSAA multiview + resolve par layer
- [ ] ImGui overlay en XR
- [ ] Refactor DRY du Renderer (couture desktop/XR)
- [ ] Backend d'anchors réel

---

## Étape 15 — Build & Release Windows
- [x] Gestion versions, métadonnées executable, icône du jeu (`ExeMetadata` : VERSIONINFO + RT_GROUP_ICON patchés dans le `<Game>.exe`, champs UI Build Settings)
- [ ] Valider le bouton Build sur machine vierge, sans MSYS2/SDK
- [ ] Produire archive/installeur signé, crash logs, validation DLL, SBOM et procédure de rollback
- [ ] LTO build optimization

---

## Étape 16 — Export Web (WASM + WebGPU)

- [x] **Renderer Web 16.0 → 16.6 terminé.** RHI compile-time Vulkan/WebGPU, 33 shaders transpilés en WGSL, backend `rhi/webgpu/*`, vrai Renderer dans le navigateur et packaging brotli existent. Cette case ne signifie pas que le player gameplay ou l'authoring web sont complets : UI, input/capabilities, robustesse contenu et parité snapshot restent ouverts.
- [ ] Brancher l'export GLB meshopt sur l'UI d'import de l'éditeur (le packager web est déjà branché)
- [ ] Textures KTX2 / Basis Universal (transcodage GPU)
- [ ] Fetch + IDBFS streaming (remplacement du MEMFS preload pour les gros jeux)
- [ ] MSAA sur le backend web (actuellement forcé à off)

---

## Priorité de travail

**HAUTE** : intégrité snapshot/contrat cross-runtime, preuve ship sur machine
vierge, sandbox/interrupt QuickJS, UI du player web, axes gamepad
**MOYENNE** : XR MSAA/ImGui + validation hardware, KTX2/Basis + streaming web,
cycle de vie assets — finition (budget LRU en cours de scène, rigs/anims dans
le sweep, identité assets glTF ; le cœur async + déchargement GPU est livré)
**BASSE** : Refactor DRY Renderer, anchors backend, LTO, GPU RmlUi, World model, Skills, Agents
**FUTURE** : Radiance Cascades, recherche GI avancée

---

## Dettes techniques repérées
- [x] `TimelinePropertyTrack::evaluate()` utilise désormais la réflexion et interpole float/int/vec3/vec4/quat, avec tests de réflexion.
- [ ] `GLTFLoader` fournit une tangente par défaut `(1,0,0,1)` quand le mesh n'en a pas ; ajouter MikkTSpace ou désactiver proprement le normal mapping dans ce cas.
- [ ] Certaines mutations éditeur marquent seulement le document dirty sans être undoables (scripts WebCanvas, changements de `CollisionShape` avec `resetAuto`) ; les raccorder au système de commandes.
- [ ] Renommage de projet dans le Hub : vérifier la synchronisation entre le dossier, l'entrée Hub et le fichier `.neproj`.
- [ ] Finir la migration des behaviours built-in restants vers la réflexion/registry unifiée.
- [ ] Finir l'alignement des registres natif, authoring web, player web et fold headless. L'authoring Web et le player Web annoncent désormais leur sous-ensemble réel et refusent strictement les types absents ; restent l'implémentation UI Web et l'élargissement explicite du contrat headless aux types physiques/UI.
- [x] `SceneSnapshot` headless est fail-closed : round-trip canonique des types/behaviours enregistrés, refs Mesh préservées, Area/ScriptBehaviour couverts, rejet des types inconnus et des schémas futurs/contradictoires.
- [ ] Étendre explicitement le contrat headless aux types desktop encore exclus (UI, corps/shapes physiques), puis prouver la même sémantique dans authoring WASM et le player. `Camera` round-trippe désormais en natif et authoring WASM.
- [x] Fold Mesh sans `ResourceManager` interdit explicitement ; les folds persistants Saida sont atomiques par défaut et `--skip-invalid` reste réservé au diagnostic non durable.
- [ ] Ajouter un interrupt/deadline QuickJS et confiner strictement la résolution de modules au package projet.
- [ ] Implémenter réellement les axes gamepad avant d'annoncer `GamepadInput` dans les capacités desktop.
- [ ] Clarifier/appliquer les notices GPL-3.0 et produire un inventaire de licences des dépendances/assets avant release stable.
