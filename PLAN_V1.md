# SaidaEngine - Plan unique vers la V1

Mise à jour : 2026-07-20.

**Verdict : NO-GO pour une V1 publique.** Ce fichier est l'unique todolist du
moteur. Les contrats et limites sont dans [SPEC.md](SPEC.md).

## Preuves acquises

- [x] Build natif complet Windows UCRT64.
- [x] Suite native : 67/67 tests le 2026-07-20 (corpus UI `saida_ui_corpus_tests`
  et son rasterizer HUD partagé inclus).
- [x] Player Web Release et authoring WASM Release compilés.
- [x] WitnessGame éditeur/desktop : Play éditeur automatisé via `--play`, export
  et runtime autonome, HUD vérifié, `E2E PASS` puis `RESTART PASS`.
- [x] WitnessGame Web : package exécuté, HUD `UICanvasNode`/`UITextNode`
  visible via RmlUi/WebGPU et harnais `[E2E] PASS` sur 16 cycles, puis
  `RESTART PASS` avec le HUD restauré après reload.
- [x] QuickJS : deadline 100 ms, 1024 jobs, microtasks interrompues, callbacks
  protégés et scripts/imports confinés à la racine canonique.
- [x] Export Windows : copie robuste du contenu, symlinks/spéciaux refusés.
- [x] Input desktop/Web : gamepad, touch, profils/rebinding, dernier périphérique
  actif et haptique Web à retour explicite; les pads physiques restent à valider.
- [x] Snapshot headless fail-closed sur son registre, Camera incluse, références
  Mesh préservées et création Mesh sans ResourceManager refusée.
- [x] AssetLoader async texture/OBJ et `.srig/.sclip/.sgraph`, plus déchargement
  GPU sur changement de scène; mémoire stable sur 16 cycles desktop/Web.
- [x] Schémas, migrations et corpus de compatibilité de base.
- [x] API JS cross-node : autoloads, groupes, résolution NodeId, signaux et
  appels JSON entre contextes, traversés par Witness desktop/Web.
- [x] Séquences `.sseq` au runtime : `SequenceDirector` fail-closed (animation,
  événements relayés en signaux, propriétés réfléchies), traversé par
  WitnessGame desktop et Web; ré-import d'un même glTF idempotent (rigs/clips
  des Animators jamais invalidés).
- [x] Clic Build éditeur automatisé : `SaidaEngine --build <out>` exécute le
  code exact du bouton; l'artefact produit passe le E2E complet
  (`tools/witness_editor_build.sh`).
- [x] Save/load après redémarrage : progression restaurée au boot par un
  second lancement desktop (`saves/`) et un reload navigateur (IDBFS),
  verdict `RESTART PASS` dans les deux harnais.
- [x] Recette P0.1 unique : Build éditeur Windows/Web, archives exactes,
  inventaire fichier par fichier, SHA-256, vérificateur machine Windows et
  vérificateur Chrome/Edge avec contrôle COOP/COEP + MIME WASM.
- [x] Matrice V1 canonique des factories natif/headless/authoring WASM/player
  Web, publiée dans `EngineManifest` et vérifiée au démarrage; round-trip
  headless exact du HUD et des cinq types physiques V1.

## Reprise pour les prochaines sessions

Point d'arrêt : ne pas rouvrir P0.5 sans régression reproductible. Les textures,
OBJ et animations autonomes `.srig/.sclip/.sgraph` passent par l'`AssetLoader`
asynchrone; Character consomme son graph à `ready`, le panneau Animation reste
non bloquant, et les cycles Witness desktop/Web prouvent mémoire et
déchargement. La qualification automatisée de P0.7 et son commit exact sont
consignés dans la section P0.7 ci-dessous.

La migration des behaviours built-in vers le registre unique est terminée :
plus aucune exception manuelle. `LODGroupBehaviour` et `ScriptBehaviour` portent
un descripteur réfléchi sans propriétés (leur sérialisation manuscrite —
`script`/`hotReload`/`properties` pour le script, données LOD sur `MeshNode` —
est inchangée, mêmes octets durables) et s'enregistrent par
`registerBehaviour<T>()` dans `ReflectedTypes.cpp` et
`ReflectedTypesPlayer.cpp`; les registrations manuelles de `src/Engine.cpp` et
`src/authoring/SceneSnapshot.cpp` ainsi que le cas spécial de
`isSupportedHeadlessBehaviourType` sont supprimés. Preuves dans la case P1
correspondante.

L'atomicité du renommage Hub est également fermée (voir la case P1 : opération
`renameProjectDirectory` avec rollback, testée par `saida_project_rename_tests`).

P0.3 UI est entamé (session 2026-07-20) : rendu CPU desktop finalisé et prouvé,
HUD unifié desktop/Web par `HudRasterizer`, fonts déclarées/packagées, corpus
headless `saida_ui_corpus_tests`. Restent dans P0.3 : contrat auteur Rml/CSS/JS
stabilisé, indirection AssetRegistry des assets UI, World Space prouvé,
unification hit-test/focus/input, lifecycle Play/Stop/reload, inspector/picking/
undo UI, corpus XR et mesure CPU→décision backend GPU.

Prochain chantier autonome conseillé, hors UI : compléments physique
(diagnostics, puis slider/cône/moteurs/breakables), rendu/lightmaps, puis LTO
seulement après stabilité. À réserver à une session assistée séparée : la suite
de P0.3 UI (adaptation visuelle des prompts, undo éditeur, World Space,
lifecycle), le rebuild emsdk du player Web/authoring WASM après la
refactorisation UI, pads physiques Xbox/PlayStation, signature Authenticode avec
la clé de publication, validations XR/casques et benchmarks sur GPU physique.
Lavapipe peut qualifier les contrats et le packaging CI, pas remplacer une
preuve matérielle. Toute case cochée doit conserver dans ce fichier le commit/run
ou le corpus exact qui la prouve.

## P0.1 - Jeu témoin et chemin de livraison

- [x] Porter `UICanvasNode`, `UITextNode` et le rendu RmlUi nécessaire dans le
  player WebGPU.
- [x] Obtenir WitnessGame complet, UI incluse, avec le même gameplay et les
  mêmes saves en éditeur, desktop autonome et Web.
- [x] Ajouter et traverser une séquence `.sseq` dans WitnessGame.
- [x] Remplacer le contournement storage par une vraie API JS
  autoload/cross-node/groupes/signaux.
- [x] Automatiser le clic Build de l'éditeur, pas seulement `saida_tool`.
- [x] Exécuter l'artefact Windows sur une machine vierge sans MSYS2, SDK ou
  checkout moteur.
- [x] Exécuter l'artefact Web servi avec les bons headers sur Chrome et Edge.
- [x] Vérifier save/load après redémarrage sur desktop et navigateur.
- [x] Éliminer toute étape manuelle non documentée entre projet et artefact.

Preuves externes acquises le 2026-07-18 sur une machine Windows vierge (aucun
MSYS2, SDK ni checkout moteur), depuis le bundle release-candidate du commit
`0808b636` (`release-manifest.json` schema 1, `dirty:false`) :
`verify_witness_windows.ps1` → `WINDOWS CLEAN-BUNDLE PASS` (gameplay/UI PASS,
save/UI restart PASS); `verify_witness_web.ps1` → `Edge WEB PASS` et
`Chrome WEB PASS` (COOP/COEP + MIME `application/wasm`, `[E2E] PASS` sur 16
cycles puis `[E2E] RESTART PASS`).

Gate : un commit propre produit des artefacts desktop/Web jouables et identifiés
par hash, avec WitnessGame PASS sur les deux. **Fermée le 2026-07-18.**

## P0.2 - Parité des contrats et données durables

- [x] Construire une matrice unique des types/behaviours enregistrés dans natif,
  headless, authoring WASM et player Web.
- [x] Ajouter explicitement UI et nœuds physiques requis au fold headless.
- [x] Prouver un round-trip sémantique de chaque type/propriété/behaviour sur les
  quatre runtimes; aucun fallback générique.
  Preuves : headless exhaustif sur 14 types de nœuds, 18 behaviours et 157
  propriétés réfléchies; serializer complet natif sur 26/22/151; snapshot
  Authoring Web sur 9/0/90; player Web sur 15/10/120. Chaque corpus utilise des
  valeurs non triviales et exige l'identité JSON après reconstruction.
- [x] Migrer l'adressage des SaidaOps du nom mutable vers `NodeId`.
  `opVersion: 2` impose des identifiants 64 bits en chaînes décimales (sans perte
  de précision JavaScript) pour toutes les cibles, parents et connexions; les
  inverses restent valides après renommage et les noms dupliqués ne sont plus
  ambigus.
- [x] Générer le manifeste depuis le bundle réellement livré et vérifier que
  tous les types annoncés round-trippent. `saida_tool verify-manifest` génère le
  manifeste, exige que chaque nœud/behaviour annoncé soit une ligne de la
  `runtimeTypeMatrix` round-trippée, vérifie le registre headless vivant contre
  la matrice et exécute le round-trip snapshot headless (14 nœuds, 18 behaviours,
  151 propriétés). Joué en CI sur l'artefact `saida_tool` (`saida_tool_verify_manifest`).
  Au passage, `verifySnapshotRoundTripContract` devient réellement resource-free
  et `RuntimeTypeMatrixTests` réutilise ce contrat partagé au lieu d'en dupliquer
  la boucle.
- [x] Étendre le corpus de compatibilité avec WitnessGame gelé et snapshots de
  chaque version publiée. `tests/fixtures/compat/witness_v1.*` sont des copies
  exactes des artefacts durables de WitnessGame (projet, registre, scènes hub et
  arena), chargées par leurs vrais loaders dans `saida_compat_corpus_tests` avec
  garde anti-réécriture; les scènes valident headless le HUD UI, la physique et
  les 18 types de nœuds/behaviours V1. Chaque version publiée suivante ajoute un
  jeu `witness_vN_*` immuable selon la même convention.
- [x] Refuser partout schémas futurs/contradictoires et données inconnues avec
  diagnostic exploitable. Le garde unique `format::schemaEnvelopeError` refuse,
  sur snapshot/scène/projet/registre/scénario, un `schema`/`version` non entier,
  une divergence `schema`≠`version` et une version future, avec un message
  nommant le format; les types de nœuds et behaviours inconnus étaient déjà
  refusés au chargement. Prouvé par `saida_format_version_tests` (helper + les
  trois formats-enveloppe) et le CTest headless `apply_ops_rejects_schema_version_conflict`.
- [x] Produire un release manifest avec versions et SHA des players, authoring
  WASM, `saida_tool`, formats et fixtures. `tools/engine_release_manifest.ps1`
  écrit `build/release/engine/release-manifest.json` (schéma 1) : commit moteur,
  versions de formats lues depuis `saida_tool describe-engine` (nouvelle section
  `formats` = source unique des 11 versions), et SHA-256 de `saida_tool`, du
  runtime desktop, du player Web, de l'authoring WASM, du runtime d'authoring et
  des 14 fixtures immuables. `tools/verify_engine_release.ps1` recalcule chaque
  hash et recompare les versions à l'outil, échec au moindre écart (prouvé
  positif et négatif). Section `formats` couverte par `saida_authoring_op_tests`.

Gate : un projet V1 ne peut être perdu, appauvri ou interprété différemment par
deux runtimes annoncés compatibles.

## P0.3 - UI V1

- [ ] Stabiliser le contrat auteur Rml/HTML, CSS et JS projet.
- [~] Charger fonts, images et feuilles de style depuis l'AssetRegistry avec
  erreurs visibles. Livré sauf l'indirection AssetID : les fonts par défaut du
  moteur ont un manifeste déclaré (`RmlUiRuntime::kEngineFonts`) résolu par
  fichier sous `assets/fonts/` puis le checkout dev, avec erreur explicite si
  une font requise manque et embarquement `BuildExporter` desktop/Web; une
  image UI absente rend le damier magenta (convention `missingTexture`); une
  feuille de style projet se charge du disque avec la propriété web non
  supportée filtrée. Tout prouvé par `saida_ui_corpus_tests`. Reste : router
  les assets UI par AssetID de l'AssetRegistry plutôt que par chemin projet.
- [x] Finaliser rendu RmlUi CPU desktop : géométrie, textures, clipping/scissor,
  blend, transforms, resize et DPI. Le corpus headless `saida_ui_corpus_tests`
  prouve chaque primitive sans GPU (72 checks) et le HUD desktop compose
  réellement via le rasterizer partagé. Preuves commit `8b9683f` (voir case P1).
- [~] Prouver Screen Space pour HUD/menu et World Space pour panneau 3D.
  Screen Space HUD prouvé (desktop `--play` + packagé + corpus); World Space
  (`WebCanvasNode` raycast plan) pas encore couvert par une preuve dédiée.
- [ ] Unifier hit-test, focus, clavier, souris, scroll, touch et capture UI.
- [ ] Brancher DOM ciblé et QuickJS sans API navigateur implicite.
- [ ] Garantir lifecycle Play/Stop/reload et sérialisation des documents.
- [ ] Ajouter inspector, picking et édition de chemins/modes avec undo/redo.
- [~] Créer un corpus UI desktop/Web/XR et des captures de référence.
  `saida_ui_corpus_tests` couvre le backend CPU partagé par desktop et Web avec
  des assertions de pixels calculées (plus robustes que des captures golden),
  dont la parité HUD `HudRasterizer`. Reste : corpus UI XR et parcours Web
  spécifique (rebuild emsdk).
- [ ] Mesurer le backend CPU avant de décider un backend GPU RmlUi.

Preuves de session (2026-07-20) :

- Rendu CPU desktop et parité HUD, commit `8b9683f` : le HUD texte
  (`UICanvasNode`/`UITextNode`) était rendu uniquement sur le player Web par un
  `gatherLegacyWebUI` web-only; desktop `traverseUI` ne dessinait jamais
  `UITextNode`, donc un jeu desktop packagé n'affichait aucun texte de HUD
  (violation de la parité SPEC §1, masquée par le harnais qui vérifiait
  `getText()` et non les pixels). Le markup + la rasterisation CPU sont
  extraits dans `ui/HudRasterizer`, partagé desktop/Web; seul l'upload texture
  diffère (quad bindless Vulkan vs texture+bindgroup WebGPU). Prouvé sur
  l'artefact réel : `[HUD] rasterized 1046/967 visible pixel(s)` puis
  `[E2E] PASS` + `RESTART PASS` en desktop packagé (`witness_e2e.sh`) et éditeur
  `--play` (`witness_editor_play.sh`), 67/67 CTest (corpus UI inclus). Au
  passage : blend CPU corrigé (couleurs de sommet et texels prémultipliés →
  produit simple; l'ancien code multipliait deux fois par l'alpha et
  assombrissait toute couleur semi-transparente), image manquante → damier
  magenta, et ordre de teardown `~Engine` corrigé (le contexte RmlUi du HUD
  desktop survivait à `Rml::Shutdown()` et crashait à la sortie — le renderer
  est désormais détruit avant `RmlUiRuntime::shutdown()`).
- Fonts moteur déclarées et packagées, commit `8526e8d`.
- Champ « Project Name » éditeur rendu lecture seule, commit `5e0a107`.

À refaire en session assistée toolchain : reconstruire player Web et authoring
WASM (emsdk absent de cette machine) pour reconfirmer le chemin `gatherHud` Web
après la refactorisation, et prouver le HUD Web packagé. Le chemin Web conserve
la logique d'origine (mêmes texture/bindgroup/draw); seul le compilateur emsdk
n'a pas revérifié le build.

Gate : WitnessGame et le corpus UI rendent et interagissent correctement sur
desktop et Web; l'absence XR éventuelle est un fallback déclaré.

## P0.4 - API gameplay et stockage

- [x] Exposer en JS l'accès aux autoloads, nœuds/groupes et signaux cross-node.
- [x] Ajouter les queries/contraintes physiques indispensables et leur parité JS.
  Queries : `PhysicsWorld::raycast`/`overlapSphere` avec `QueryFilter` (capteurs
  exclus par défaut, body ignoré), exposées en JS par le global `physics`
  (`available`/`raycast`/`overlapSphere`, résultats en `NodeRef`), même surface
  desktop et player Web. Contraintes : nœuds réfléchis `FixedJoint`/`PointJoint`/
  `HingeJoint` (corps par chemins de nœuds, `bodyA` = ancêtre par défaut, `bodyB`
  vide = monde; pivot/axe depuis le transform du joint), matrice `{R, R, A, R}`
  sur les 4 runtimes, round-trip automatique (headless 17/18/161, natif
  29/22/161, player Web 18/10/130); `PhysicsWorld` purge les contraintes d'un
  body retiré et réveille les corps survivants. Prouvé par
  `saida_physics_query_joint_tests` (queries filtrées, pendule retenu vs chute
  libre, rebuild après `markDirty`, retrait de corps sans contrainte pendante)
  et traversé par WitnessGame (pendule PointJoint dans l'arène +
  raycast/overlap dans le driver E2E) : PASS éditeur `--play`, desktop packagé
  (`witness_e2e.sh`, run + restart) et navigateur (PASS 16 cycles + RESTART
  PASS). Restent en P1 : slider/cône/moteurs/breakables et diagnostics.
- [x] Compléter les bindings animation, graph, sequence et blackboard.
  Sur `node` et `NodeRef` (résolution : behaviour du nœud, sinon premier
  descendant — la règle du SequenceDirector) : `playClip`/`currentClip`,
  `setAnimFloat`/`setAnimBool`/`setAnimTrigger` (paramètres du blackboard
  d'animation, pilotent un `.sgraph`), `playSequence`/`stopSequence`,
  `setData`/`getData`/`hasData` (Blackboard gameplay, number/bool/string);
  cible sans behaviour → false/null, jamais d'exception. Le signal
  `animationEvent` de l'Animator est réfléchi (descripteur signaux-seulement,
  sérialisation manuscrite intacte) donc abonnable par `node.on`. Prouvé par
  `saida_js_gameplay_tests` (traversée QuickJS headless complète) et par
  WitnessGame : `gameplay api ok` dans l'arène (Animator du Player trouvé en
  descendant, round-trip Blackboard, réponses négatives sur la caméra) et
  replay d'`intro.sseq` via `playSequence` au run RESTART.
- [x] Émettre un warning quand un module JS ne fournit aucun hook reconnu.
- [x] Définir la politique de permissions des scripts publics au-delà du
  confinement filesystem et du budget temps. Politique *capability-based*
  (SPEC 6.2) : aucune autorité ambiante au-delà des globals installés
  (`console` + `node/time/input/tree/assets/audio/physics/storage`); pas de
  réseau, pas d'OS/processus/env (quickjs-libc non lié), pas de filesystem
  hors `storage` quota-é, imports confinés à la racine projet, budget temps
  interruptible. Verrouillée par `saida_js_permission_policy_tests` : la
  surface globale d'un contexte moteur est diffée contre un contexte QuickJS
  nu et doit égaler exactement l'allowlist — toute capacité apparue ou
  disparue casse la suite.
- [x] Déplacer les saves vers l'emplacement utilisateur de chaque OS. Politique
  `core/Paths::userSaveRoot` : un jeu packagé écrit `saves/`/`prefs/` sous le
  dossier de données utilisateur de l'OS (`%APPDATA%\SaidaEngine\Games\<jeu>`,
  `$XDG_DATA_HOME`/`~/.local/share`, `~/Library/Application Support`) keyé par
  l'identité posée au boot par le runtime (nom du projet nettoyé), jamais à côté
  de l'exe; précédence `$SAIDA_SAVE_DIR` (override CI/tests) > dir OS > repli
  racine projet (éditeur/dev, web IDBFS inchangés). Prouvé headless
  (`saida_player_storage_tests` : précédence, keying, sanitisation anti-traversée)
  et bout-en-bout par `witness_e2e.sh` (export réel + run/restart, save écrite
  dans le dossier ciblé, aucune `saves/` dans le package).
- [x] Rendre les écritures atomiques avec conservation de l'ancien fichier si
  le remplacement échoue.
- [x] Versionner les saves, fournir migrations/rejet explicite et metadata de
  slots. Le service `PlayerStorage` écrit chaque slot dans une enveloppe
  `{schema, version, __saidaStore, kind, dataVersion, savedAt, bytes, payload}`
  (schéma 1) refusée fail-closed via `format::schemaEnvelopeError` si future ou
  incohérente; une save V0 héritée (chaîne brute) charge verbatim puis est
  promue en enveloppe à la réécriture; `storage.info(slot)` expose la metadata.
- [x] Séparer préférences et progression, ajouter quotas et erreurs explicites.
  `storage.*` = progression (`saves/`), `storage.prefs.*` = préférences
  (`prefs/`), namespaces indépendants; quotas par slot (1 MiB), par namespace
  (16 MiB) et nombre de slots (256); échec `false` avec statut typé consultable
  par `storage.lastError()` (`invalid_slot`/`quota_exceeded`/`not_found`/
  `corrupt`/`io_error`). Prouvé par `saida_player_storage_tests`.
- [x] Stabiliser le contrat asynchrone nécessaire à IDBFS/cloud save futur.
  Contrat : visibilité synchrone (un `load` après `save` rend la valeur),
  durabilité asynchrone — `storage.flush()` retourne une Promise résolue
  `true` quand les écritures en attente (saves et prefs) sont durables,
  `false` en échec, jamais rejetée. Desktop : écritures atomiques durables dès
  `save`, résolution au prochain drain de microtasks; Web : résolution par le
  callback `FS.syncfs` (IndexedDB), résolveurs en vol libérés au teardown du
  contexte (hot-reload sûr); un backend cloud futur s'insère derrière la même
  promesse. Prouvé par `saida_js_storage_flush_tests` et par le driver
  WitnessGame : le verdict PASS n'est émis qu'après un flush durable
  (`flush=durable`), et le run RESTART relit cette progression sur desktop
  (fichier) comme au reload navigateur (IndexedDB).

Gate : WitnessGame communique sans fichier détourné et récupère une sauvegarde
cohérente après crash/redémarrage. **Fermée le 2026-07-19** : cross-node,
signaux, Blackboard et storage versionné/quota-é remplacent tout fichier
détourné; la progression est relue au redémarrage par les harnais desktop
(`witness_e2e.sh`, run + restart), éditeur `--play` et navigateur (PASS puis
RESTART PASS via IndexedDB), le tout après flush durable explicite.

## P0.5 - Assets, mémoire et contenu hostile

- [x] Imposer le budget GPU pendant une scène avec LRU mesuré.
  `ResourceManager` applique `gpuBudgetBytes` (512 MiB par défaut,
  `assets.setGpuBudget` en JS) à CHAQUE frame : au-delà du budget, les
  textures/meshes ni référencés par la scène vivante (photographie d'usage
  rafraîchie par le SceneTree à chaque changement de hiérarchie) ni en cours
  de chargement sont évincés du moins récemment utilisé (`lastUse_` daté par
  frameClock) au plus récent, compteurs `gpuEvictedCount/Bytes` exposés dans
  `assets.stats()`; si tout le dépassement est référencé, warning unique
  mesuré, rien de cassé. Prouvé E2E desktop ET web : le driver serre le budget
  sous le résident, libère la sonde .obj de l'arène, et atteste l'éviction LRU
  mi-scène (`gpu budget ok (lru evicted 1, resident 5376 <= 39420)`).
- [x] Balayer rigs, clips et graphs par `trimUnused` (les caches d'animation ne
  croissent plus sans borne : rigs/clips détenus par des Animators vivants
  survivent — pointeurs marqués par le collecteur d'usage —, les caches
  ClipView/AnimGraph, purs caches fichier rechargés par chemin, sont balayés
  entièrement au changement de scène; visible dans le log d'éviction).
- [x] Stabiliser l'identité des meshes glTF mémoire après Play/Stop et
  changement de scène. `registerMemoryMesh` gagne une saveur à clé de
  sous-asset stable (`model.gltf#mesh2_prim0`), idempotente comme
  `registerMemoryRig` : un ré-import rend le même AssetID et ne remplace
  jamais une instance détenue — un snapshot restauré après éviction référence
  un id résoluble au lieu d'un compteur dynamique perdu.
- [x] Implémenter fetch/IDBFS streaming pour remplacer le preload MEMFS des gros
  jeux Web. `project-files.json` passe en schéma 2 : `files` (préchargé MEMFS
  avant `main()`) et `streamed`
  (`.png/.jpg/.jpeg/.obj/.srig/.sclip/.sgraph` — les types servis en async par
  l'AssetLoader), classés par l'exporteur web. Sur wasm, un miss
  fichier de l'AssetLoader déclenche `emscripten_async_wget_data` (l'entry
  reste `Loading`, le decode se fait au retour réseau — aucun blocage), compté
  dans `assets.stats().streamedFetches` et affiché dans le verdict E2E.
  Prouvé navigateur : `[E2E] PASS … streamed=18` (probe.obj re-fetché à chaque
  cycle de scène après éviction, corrupt.obj streamé puis refusé au decode,
  runtime vivant) puis `RESTART PASS`; desktop inchangé (`streamed=0`).
  Les scènes/scripts/glTF restent au boot (chargements synchrones).
- [x] Gérer un OBJ/glTF/GLB corrompu sans abort du player Web.
  `cgltf_validate` est appelé après chaque parse (un accessor hors limites —
  le cas malveillant type — est refusé AVANT toute lecture OOB fatale en
  wasm); un OBJ qui « parse » en zéro géométrie est refusé au décodage
  (`failedTotal` cumulatif dans `assets.stats()`) et `createMesh` refuse la
  géométrie vide (les buffers GPU vides étaient fatals — bug réel trouvé par
  la sonde). Prouvé par `saida_hostile_asset_tests` (GLB tronqué, glTF à
  accessor hors limites, poubelle binaire, fichier absent, OBJ hostile) et
  par WitnessGame : l'arène embarque `corrupt.obj`/`corrupt.glb` volontaires,
  le driver exige `failedTotal >= 1` avec le runtime vivant — PASS desktop,
  éditeur et navigateur.
- [x] Ajouter MikkTSpace ou désactiver explicitement le normal mapping sans
  tangentes valides. Option retenue : désactivation explicite — un matériau à
  normal map dont la primitive n'a pas de tangentes d'auteur perd sa normal
  map avec warning loggé (les tangentes reconstruites par moyenne de triangles
  ne sont pas MikkTSpace et fausseraient l'éclairage en silence). MikkTSpace
  reste P1.
- [x] Brancher l'export GLB meshopt dans l'UI d'import. Bouton « Export
  meshopt GLB » du panneau 3D Importer : `collectExportMeshes` relit la
  géométrie du fichier source (.gltf/.glb validé+décodé ou .obj — fidélité
  complète, pas de readback GPU) puis `exportMeshoptGlb` écrit
  `<source>.meshopt.glb` quantifié. Boucle collect → export → collect prouvée
  par `saida_meshopt_export_tests`.
- [x] Décider KTX2/Basis pour textures de release Web. Décision V1 : NON —
  les textures restent PNG/JPG (stbi) sur toutes les plateformes. Le corpus
  V1 n'a pas de contenu texturé lourd qui justifie le transcodeur basisu
  (dépendance + surface de code) ; l'upload RGBA8 est le comportement mesuré
  par les harnais. KTX2/Basis reste en P2, réévalué quand un jeu réel dépasse
  le budget texture web.
- [x] Faire passer les fichiers d'animation autonomes (.srig/.sclip/.sgraph)
  par le chargement asynchrone de l'AssetLoader. Trois payloads typés séparent
  ces formes du cache Raw/Image/Mesh; lecture+parse tournent sur le worker
  desktop ou dans `pump()` Web, puis `ResourceManager` finalise à `ready` et
  conserve erreurs/états sans attente. `CharacterBehaviour` demande son
  `.sgraph` et ne l'applique qu'à `ready`; le panneau Animation diffère
  Jouer/Éditer/Appliquer et inspecte aussi `.srig`. L'export Web classe ces
  trois extensions dans `streamed` : Witness fetch réellement
  `anim/locomotion.sgraph` après chaque sweep. Prouvé par
  `saida_asset_loader_tests` (succès des trois formats + graphe incohérent
  refusé, requêtes immédiates), build natif complet et 62/62 CTest, builds
  player Web/authoring WASM/runtime d'authoring, Witness desktop
  `PASS (run + restart)` et Chrome Web (`PASS`, 16 cycles, `streamed=36`,
  flush durable, puis vrai second processus `RESTART PASS`).
- [x] Mesurer hitch et mémoire sur N cycles avec seuils CI. Le driver E2E
  mesure `hitchMax` (dt max) et le nombre de frames > 100 ms sur les 16
  cycles hub↔arena, les publie dans le verdict
  (`hitchMax=0.049s@0` web, `0.072s@0` desktop) et échoue au-delà d'un
  plafond de 2 s; les critères mémoire (loader stable, GPU stable, budget)
  étaient déjà bloquants dans les trois harnais.

Gate : budget respecté, aucune croissance non bornée, contenu invalide refusé
sans tuer le runtime. **Fermée le 2026-07-19** : budgets CPU/GPU et sweeps sont
bloquants dans les harnais, les formats hostiles échouent sans abort, et tous
les assets V1 streamables — textures, OBJ et animation autonome — passent par
l'AssetLoader avec consommateurs non bloquants sur desktop et Web.

## P0.6 - Input et capacités

- [x] Implémenter un backend navigateur Gamepad API avant d'annoncer la capacité
  Web : polling Emscripten de `navigator.getGamepads`, mapping `standard`
  normalisé vers le contrat GLFW (dont triggers), hotplug, tests natifs du
  mapping et preuve navigateur `gamepad=yes` lorsque l'API est disponible.
- [ ] Tester le backend desktop avec manettes physiques Xbox/PlayStation
  reconnues par GLFW. À faire dans la session assistée matériel; aucun pad
  physique compatible n'est connecté à la machine de CI actuelle.
- [x] Ajouter rebinding runtime et profils sérialisés : API C++ et QuickJS,
  profil JSON schema 1 validé/appliqué atomiquement, noms de contrôles stables,
  round-trip exhaustif et persistance possible via `storage.prefs`.
- [x] Ajouter sélection de périphérique et deux joueurs locaux si promis en V1 :
  non promis; la V1 reste explicitement mono-joueur et utilise le premier pad
  standard disponible.
- [x] Transformer touch brut en bindings/zones/gestes réutilisables : vrai
  backend canvas Web start/move/end/cancel, zones normalisées, press/tap/swipes,
  seuil de distance, profils JSON et API C++/QuickJS. Tests purs et preuve
  navigateur `touch=yes`.
- [ ] Détecter le dernier périphérique actif et adapter les prompts UI.
  Le cœur est livré : transitions clavier/souris, manette anti-drift et touch
  alimentent `Input::lastActiveDevice` / `input.lastActiveDevice()`. L'adaptation
  des prompts reste réservée à la session assistée UI.
- [x] Ajouter haptique standard lorsque disponible : player Web branché sur
  `GamepadHapticActuator.playEffect('dual-rumble')`/`reset`, API C++ et QuickJS,
  bornes W3C et retour `false` si l'actuateur manque. Desktop reste explicitement
  non supporté car GLFW 3.x n'expose pas d'haptique.

Gate : la matrice publiée correspond exactement aux backends testés; aucune
valeur neutre trompeuse.

## P0.7 - Release, CI et exploitation moteur

- [x] Rendre obligatoires build natif, 50+ tests, corpus compat, fold
  déterministe, Witness desktop et Witness Web dans la CI.
- [x] Publier `saida_tool`, player Web et authoring WASM comme artefacts pinnés,
  jamais via `latest` seul.
- [x] Refaire la preuve Linux propre et byte-identique Windows/Linux sur les
  fixtures de fold.
- [ ] Produire archive/installeur Windows signé, validation DLL et rollback.
  Fermé hors signature : `validate_windows_dependencies.ps1`
  parse de façon bornée les tables d'imports PE x64 normales/différées, parcourt
  récursivement le bundle, autorise seulement les DLL système déclarées ou
  présentes dans le package et refuse les runtimes dynamiques MinGW; son rapport
  hashé entre dans le bundle de symboles et dans chaque archive Witness. Les ZIP
  Witness sont canoniques et reproductibles
  (ordre, timestamp du commit, refus des reparse points, vérification exacte).
  `build_witness_installer.ps1` produit avec NSIS 3.12+ un installeur par
  utilisateur byte-reproductible avant signature, inventorié et sans suppression
  récursive aveugle (payload exact et caches runtime nommés). Son vérificateur
  automatise SHA, installation silencieuse,
  payload exact, run + restart et désinstallation; la CI le reconstruit deux
  fois et publie l'artefact épinglé au SHA. Le rollback immuable est documenté.
  Reste uniquement la signature Authenticode avec la clé de publication.
- [x] Ajouter crash logs exploitables et symboles associés à la version.
  Les quatre entry points desktop installent `core/CrashReporter` avant le boot :
  exception fatale → `.crash.log` métadonné et minidump Windows sous le dossier
  utilisateur (override `SAIDA_CRASH_DIR`), avec commit, artefact de symboles,
  code/adresse/RVA/base module et logs récents non bloquants.
  `tools/package_release_symbols.ps1` produit de façon byte-reproductible les
  quatre exécutables dépouillés + `.dbg`/`.gnu_debuglink`, manifeste SHA-256 et
  vérificateur exact; la CI publie `windows-symbols-<SHA>` et le release
  manifest moteur ainsi que la recette Witness l'inventorient.
- [x] Générer SBOM, inventaire licences/assets/modèles et notices GPL/SPDX.
  `tools/generate_release_compliance.ps1` produit un document SPDX 2.3, les
  notices complètes, l'inventaire hashé de 23 assets/modèles et un manifeste
  déterministe. Les 19 composants (moteur inclus), chaque racine
  `third_party` et chaque extension d'asset suivie sont couverts en fail-closed;
  quatre assets legacy sans provenance sont `distribution:false` et le
  DamagedHelmet CC-BY-NC est signalé non commercial. Le bundle exact est inclus
  dans le release manifest moteur, les archives Witness et un artefact CI
  épinglé au SHA.
- [x] Tester le bouton Build et les packages sur runners propres.
  Preuve commune : commit `6ec91c2`, workflow
  [Saida Engine CI #29742700718](https://github.com/saias-o/NextEngine/actions/runs/29742700718).
  Runner Windows propre : Vulkan Lavapipe préflight, build, 65/65 tests, corpus,
  clic Build éditeur, Witness run + restart, installeur construit deux fois
  byte-identique puis install/run/restart/uninstall, compliance et symboles.
  Runner Web : Witness, player et authoring WASM publiés sous le SHA. Conteneur
  Debian 12 propre : build, 65/65 tests, corpus, fold identique à la fixture et
  byte-identique au résultat Windows, puis `saida_tool`/manifest épinglés.
- [x] Documenter support GPU/OS/navigateur et procédure de retrait d'une release.
  `docs/release-support.md` limite la V1 à Windows 11/Vulkan 1.3,
  Chrome/Edge desktop WebGPU et `saida_tool` Debian 12, publie les surfaces non
  qualifiées et impose promotion/retrait/rollback par manifeste, SHA et digest
  immuables sans jamais reconstruire une identité existante ni rétrograder
  silencieusement les données.

Gate : une release peut être reproduite, identifiée, diagnostiquée et retirée.

## P1 - Qualité des sous-systèmes

- [ ] XR : valider casques/runtimes ciblés, contrôleurs et hand tracking.
- [ ] XR : MSAA multiview/resolve, overlay ImGui et backend d'anchors réel.
- [ ] Éditeur : rendre undoables WebCanvas et `CollisionShape resetAuto`.
- [x] Hub : garantir renommage dossier, entrée Hub et `.saidaproj` atomique.
  `renameProjectDirectory` (`src/project/ProjectRename.*`) renomme ensemble le
  dossier, le `.saidaproj` (fichier + champ `name`) et l'entrée `hub.json`,
  avec validation du nom, refus fail-closed (cible existante, projet legacy ou
  futur, registre Hub corrompu) et rollback complet sur échec — chaque état
  intermédiaire reste chargeable. Le bouton Rename du Hub l'utilise et ne met
  à jour l'entrée qu'en cas de succès. Au passage, `Project::load` accepte le
  dossier du projet (résolution du `.saidaproj` unique), ce qui répare le
  lancement depuis le Hub qui passait un dossier à `--project`. Prouvé le
  2026-07-20 par `saida_project_rename_tests` (53 checks : succès bout-en-bout,
  normalisation, refus sans mutation, hub sans entrée correspondante, rollback
  Windows sur dossier verrouillé) et 66/66 CTest, commit `d0ed5c9`.
- [x] Finir la migration des behaviours built-in vers réflexion/registre unique.
  `ScriptBehaviour` et `LODGroupBehaviour` gagnent un descripteur réfléchi sans
  propriétés (sérialisation manuscrite et noms `"ScriptBehaviour"`/`"LOD Group"`
  préservés) et passent par `registerBehaviour<T>()` dans `ReflectedTypes.cpp`
  et `ReflectedTypesPlayer.cpp`; les registrations manuelles dans `Engine.cpp`
  et `SceneSnapshot.cpp` et le cas spécial headless sont supprimés. Preuves le
  2026-07-20, commit `620a976` : build natif complet, 65/65 CTest (corpus compat et matrice
  inclus), `saida_tool verify-manifest` OK avec 18 behaviours annoncés (dont
  les deux migrés, désormais publiés dans le manifeste), round-trip headless
  17/18/161, `SaidaEngine --verify-runtime-contract` PASS natif 29/22/161,
  player Web et authoring WASM reconstruits.
- [ ] Physique : compléter queries, contraintes et diagnostics.
- [ ] Stabiliser le flag GPU-driven et benchmarker chemin classique, bindless,
  indirect draw et compute culling sur un corpus reproductible.
- [ ] Rendu : point-light cubemap shadows et persistance des lightmaps si
  incluses dans la promesse V1.
- [ ] Mesurer et optimiser LTO seulement après stabilité.

## P2 - Hors V1, conservé comme décision

Ces éléments ne retardent pas la V1 sauf changement explicite de promesse :

- multiplayer réseau, grands terrains et frameworks de genre;
- SIMD animation généralisé, pose sharing massif et GPU crowds;
- Radiance Cascades et recherches GI avancées;
- backend GPU RmlUi si le backend CPU respecte les budgets;
- graph SaidaFX complet, trails/ribbons et collisions particules avancées;
- world model, skills et agents autonomes;
- store d'assets et services en ligne, portés par la plateforme Saida.

## Ordre de fermeture

1. Rendre WitnessGame Web jouable, UI incluse.
2. Fermer registres, snapshots et round-trips cross-runtime.
3. Fermer API gameplay, saves et assets hostiles/bornés.
4. Automatiser export UI et tests sur machines propres.
5. Publier des artefacts versionnés consommables par `saida`.
6. Finaliser licences, support, rollback et recette de release.

## Définition de V1

La V1 est atteinte uniquement si toutes les gates P0 sont cochées, depuis un
commit propre, avec artefacts exacts de release. Le même WitnessGame doit tourner
en éditeur, desktop autonome et Web; les anciens projets doivent migrer ou être
refusés sans corruption; la mémoire doit rester bornée; les limitations publiées
doivent correspondre au comportement observé.
