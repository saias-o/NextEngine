# SaidaEngine - Plan unique vers la V1

Mise à jour : 2026-07-18.

**Verdict : NO-GO pour une V1 publique.** Ce fichier est l'unique todolist du
moteur. Les contrats et limites sont dans [SPEC.md](SPEC.md).

## Preuves acquises

- [x] Build natif complet Windows UCRT64.
- [x] Suite native : 54/54 tests le 2026-07-17.
- [x] Player Web Release et authoring WASM Release compilés.
- [x] WitnessGame éditeur/desktop : Play éditeur automatisé via `--play`, export
  et runtime autonome, HUD vérifié, `E2E PASS` puis `RESTART PASS`.
- [x] WitnessGame Web : package exécuté, HUD `UICanvasNode`/`UITextNode`
  visible via RmlUi/WebGPU et harnais `[E2E] PASS` sur 16 cycles, puis
  `RESTART PASS` avec le HUD restauré après reload.
- [x] QuickJS : deadline 100 ms, 1024 jobs, microtasks interrompues, callbacks
  protégés et scripts/imports confinés à la racine canonique.
- [x] Export Windows : copie robuste du contenu, symlinks/spéciaux refusés.
- [x] Gamepad desktop : boutons, axes, triggers, deadzones, hotplug et agrégation
  multi-binding; le Web annonce encore explicitement `NO`.
- [x] Snapshot headless fail-closed sur son registre, Camera incluse, références
  Mesh préservées et création Mesh sans ResourceManager refusée.
- [x] AssetLoader async texture/OBJ et déchargement GPU sur changement de scène;
  mémoire GPU stable sur 16 cycles desktop/Web dans le harnais.
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
- [ ] Charger fonts, images et feuilles de style depuis l'AssetRegistry avec
  erreurs visibles.
- [ ] Finaliser rendu RmlUi CPU desktop : géométrie, textures, clipping/scissor,
  blend, transforms, resize et DPI.
- [ ] Prouver Screen Space pour HUD/menu et World Space pour panneau 3D.
- [ ] Unifier hit-test, focus, clavier, souris, scroll, touch et capture UI.
- [ ] Brancher DOM ciblé et QuickJS sans API navigateur implicite.
- [ ] Garantir lifecycle Play/Stop/reload et sérialisation des documents.
- [ ] Ajouter inspector, picking et édition de chemins/modes avec undo/redo.
- [ ] Créer un corpus UI desktop/Web/XR et des captures de référence.
- [ ] Mesurer le backend CPU avant de décider un backend GPU RmlUi.

Gate : WitnessGame et le corpus UI rendent et interagissent correctement sur
desktop et Web; l'absence XR éventuelle est un fallback déclaré.

## P0.4 - API gameplay et stockage

- [x] Exposer en JS l'accès aux autoloads, nœuds/groupes et signaux cross-node.
- [ ] Ajouter les queries/contraintes physiques indispensables et leur parité JS.
- [ ] Compléter les bindings animation, graph, sequence et blackboard.
- [x] Émettre un warning quand un module JS ne fournit aucun hook reconnu.
- [ ] Définir la politique de permissions des scripts publics au-delà du
  confinement filesystem et du budget temps.
- [ ] Déplacer les saves vers l'emplacement utilisateur de chaque OS.
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
- [ ] Stabiliser le contrat asynchrone nécessaire à IDBFS/cloud save futur.

Gate : WitnessGame communique sans fichier détourné et récupère une sauvegarde
cohérente après crash/redémarrage.

## P0.5 - Assets, mémoire et contenu hostile

- [ ] Imposer le budget GPU pendant une scène avec LRU mesuré.
- [ ] Faire passer rigs, clips et graphs par AssetLoader et `trimUnused`.
- [ ] Stabiliser l'identité des meshes glTF mémoire après Play/Stop et
  changement de scène.
- [ ] Implémenter fetch/IDBFS streaming pour remplacer le preload MEMFS des gros
  jeux Web.
- [ ] Gérer un OBJ/glTF/GLB corrompu sans abort du player Web.
- [ ] Ajouter MikkTSpace ou désactiver explicitement le normal mapping sans
  tangentes valides.
- [ ] Brancher l'export GLB meshopt dans l'UI d'import.
- [ ] Décider KTX2/Basis pour textures de release Web.
- [ ] Mesurer hitch et mémoire sur N cycles avec seuils CI.

Gate : budget respecté, aucune croissance non bornée, contenu invalide refusé
sans tuer le runtime.

## P0.6 - Input et capacités

- [ ] Implémenter un backend navigateur Gamepad API avant d'annoncer la capacité
  Web, ou exclure formellement la manette Web de la V1.
- [ ] Tester le backend desktop avec manettes physiques Xbox/PlayStation
  reconnues par GLFW.
- [ ] Ajouter rebinding runtime et profils sérialisés.
- [ ] Ajouter sélection de périphérique et deux joueurs locaux si promis en V1.
- [ ] Transformer touch brut en bindings/zones/gestes réutilisables.
- [ ] Détecter le dernier périphérique actif et adapter les prompts UI.
- [ ] Ajouter haptique standard lorsque disponible.

Gate : la matrice publiée correspond exactement aux backends testés; aucune
valeur neutre trompeuse.

## P0.7 - Release, CI et exploitation moteur

- [ ] Rendre obligatoires build natif, 50+ tests, corpus compat, fold
  déterministe, Witness desktop et Witness Web dans la CI.
- [ ] Publier `saida_tool`, player Web et authoring WASM comme artefacts pinnés,
  jamais via `latest` seul.
- [ ] Refaire la preuve Linux propre et byte-identique Windows/Linux sur les
  fixtures de fold.
- [ ] Produire archive/installeur Windows signé, validation DLL et rollback.
- [ ] Ajouter crash logs exploitables et symboles associés à la version.
- [ ] Générer SBOM, inventaire licences/assets/modèles et notices GPL/SPDX.
- [ ] Tester le bouton Build et les packages sur runners propres.
- [ ] Documenter support GPU/OS/navigateur et procédure de retrait d'une release.

Gate : une release peut être reproduite, identifiée, diagnostiquée et retirée.

## P1 - Qualité des sous-systèmes

- [ ] XR : valider casques/runtimes ciblés, contrôleurs et hand tracking.
- [ ] XR : MSAA multiview/resolve, overlay ImGui et backend d'anchors réel.
- [ ] Éditeur : rendre undoables WebCanvas et `CollisionShape resetAuto`.
- [ ] Hub : garantir renommage dossier, entrée Hub et `.saidaproj` atomique.
- [ ] Finir la migration des behaviours built-in vers réflexion/registre unique.
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
