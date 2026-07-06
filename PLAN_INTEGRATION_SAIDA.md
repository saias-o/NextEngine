# Plan d'intégration — SaidaEngine dans la plateforme Saida

> **Document maître de planification.** Consolide la vision, les invariants, l'état
> prouvé et la feuille de route pour intégrer **SaidaEngine** (moteur C++/WebGPU,
> repo `NextEngine` → nom final *Saida Engine*) **dans la plateforme web Saida**
> (`GitHub/saida`), afin d'obtenir un **éditeur web pour modifier des scènes,
> visualiser et vibecoder**.
>
> Date : 2026-07-04. Statut global : **spike live-edit web A.5 validé (S0→S4 vert)**,
> socle collaboration/backend largement amorcé, première UI produit de l'éditeur
> web en place côté plateforme, settings projet + invitations collaborateurs
> persistés côté Postgres/API ; le branchement aux snapshots/projets réels et
> aux agents IA reste le prochain verrou.
>
> Documents liés :
> - [ARCHITECTURE_PRODUCTION_CLAUDE.md](ARCHITECTURE_PRODUCTION_CLAUDE.md) — mandat
>   d'architecture détaillé (le « comment » technique, invariants §0).
> - [PLAN_LIVE_EDIT_WEB.md](PLAN_LIVE_EDIT_WEB.md) — spike du pivot technique
>   (paliers S0→S4, binding `applyOp`).
> - [PLAN_WEB_EXPORT.md](PLAN_WEB_EXPORT.md) — export web WASM/WebGPU (Étape 16, livré).
> - `GitHub/saida/docs/architecture.md` — architecture plateforme (control/data plane).

---

## 1. Objectif

Permettre à un créateur, depuis un navigateur, de :

1. **modifier des scènes** SaidaEngine (hiérarchie, transforms, propriétés,
   behaviours, signaux) ;
2. **visualiser** le rendu réel du moteur en direct (pas une approximation) ;
3. **vibecoder** : demander à une IA de produire ces modifications, revues et
   validées avant application.

Le tout sans quitter le contrat du moteur (nodes + behaviours + signaux), sans
recompiler le moteur à chaque changement, et à un coût opérationnel soutenable
pour un développeur solo.

---

## 2. Principe directeur

**La plateforme web n'invente pas un second moteur : elle orchestre le vrai.**

- Un seul renderer de scène : le **runtime SaidaEngine WASM/WebGPU** (Étape 16
  livrée), embarqué dans la page.
- Une seule vérité d'édition : un **journal de SaidaOp** versionnées, validées
  serveur, matérialisées en snapshots au format `.saidaproj` existant.
- Une seule surface d'autorité : l'**EngineManifest** borne ce que l'UI et l'IA
  peuvent créer/modifier.
- Un seul code d'authoring, partagé entre éditeur desktop, outils headless et
  runtime web.

Résumé opérationnel (mandat §0) : *Yjs synchronise l'atelier. SaidaOps protègent
le moteur. Temporal industrialise les travaux longs. R2/S3 porte les gros blobs.
Postgres garde la vérité métier.*

---

### 2.1 Note produit : modes de vibecoding

Le vibecoding Saida ne doit pas etre limite a un seul parcours.

Deux modes produit sont verrouillés pour l'initialisation d'un projet :

- **Mode Manuel** : l'utilisateur nomme le projet, choisit Manuel, puis arrive
  directement dans l'éditeur web sur un viewport noir / scène vide. Il part de
  zéro, peut manipuler directement la scène, et peut demander librement à l'IA
  de modifier la scène, les scripts JS ou l'UI RmlUi. L'IA agit comme un copilote
  généraliste, propose des SaidaOps, des scripts JS, de l'UI RmlUi, puis passe
  par validation/diff avant application.
- **Mode Guidé par skill** : l'utilisateur nomme le projet, choisit Guidé, puis
  sélectionne un skill de conception. Le choix du skill n'ouvre pas un formulaire
  lourd : il lance automatiquement le premier prompt de pipeline dans le chat de
  l'éditeur. Le chat gère ensuite les questions, les pièces jointes (GDD, notes,
  images, sons, références) et le découpage de production. Un skill peut proposer
  un parcours one-shot ("crée-moi un petit jeu complet"), un parcours en
  préproduction d'abord, puis une création étape par étape des niveaux,
  personnages, UI, gameplay, tests, publication, etc.

L'écran de choix Guidé/Manuel est une **surface de première initialisation
uniquement**. Une fois le projet démarré, l'utilisateur revient toujours à
l'éditeur web ; le mode et le skill choisis deviennent des métadonnées de projet
et/ou de conversation, pas une page récurrente.

Ces skills de guidage doivent etre communautaires : n'importe quel createur
pourra publier un skill qui decrit sa facon de fabriquer un jeu ou un genre de
jeu. La plateforme devra aussi proposer un **skill par defaut officiel**, pense
pour aider les artistes a creer facilement un jeu sans devoir connaitre le
moteur en profondeur.

Important : un skill de guidage ne contourne pas les invariants techniques. Il
oriente la conversation, le découpage et les validations, mais les mutations
durables restent des SaidaOps validées, des scripts JS QuickJS ou de l'UI RmlUi.

---

## 3. Invariants verrouillés

Rappel des sept invariants (détail : mandat §0). Ils priment en cas d'ambiguïté.

1. Un seul renderer de scène = runtime SaidaEngine WASM/WebGPU (Three.js réservé
   à la preview d'assets isolés).
2. L'authoring-core est compilé **dans** le runtime d'édition web.
3. Une seule représentation durable de scène (`SceneSerializer`/`.saidaproj`).
4. Manipulations continues = optimiste local + commit SaidaOp unique au relâchement.
5. Sur le web, vibecoder = SaidaOp + JS QuickJS + UI RmlUi, **jamais** du C++.
6. Le pont inter-versions du moteur est le **snapshot**, pas l'op-log.
7. La Collaboration Gateway est reconstructible depuis Postgres (zéro état
   critique en mémoire).

---

## 4. Architecture cible (vue d'ensemble)

```text
Navigateur
  Next.js UI (panels React : hierarchy, inspector, assets, console, chat)
  Runtime SaidaEngine WASM/WebGPU  ── applyOp(SaidaOp) ─► mute la scène vivante
  Yjs (présence, curseurs, édition de texte scripts/UI)
        │  SaidaOp proposée
        ▼
Fastify API (auth / billing / projects / assets / jobs)
        ├─► Postgres  (vérité : ops, revisions, credits, audit)
        ├─► Redis     (présence, locks courts, cache, rate limit)
        ├─► R2/S3     (blobs : assets, snapshots, builds)
        └─► Temporal  (jobs longs : agents IA, exports, builds)
        │
        ▼
Collaboration Gateway (WebSocket)
  authentifie → valide contre EngineManifest → append OperationLog
  → applique → incrémente revision → broadcast → snapshot périodique
        │
        ▼
Temporal Workers (isolés, Docker)
  ai-agent-worker · web/windows/android-build-worker · asset-opt-worker
        │
        ▼
Saida Headless Tools (saida_tool)
  describe-engine · validate-project · apply-ops · export-web · export-desktop
```

---

## 5. État actuel (2026-07-03)

### 5.1 Moteur (`NextEngine`)

- Moteur C++17/Vulkan complet jusqu'à l'Étape 16 : scene graph, PBR, ombres,
  DDGI, physique Jolt, animation, QuickJS, RmlUi (`WebCanvasNode`), MCP LLM, XR.
- **Export web livré** : vrai `Renderer` en WASM/WebGPU, parité desktop, ~213 Ko
  brotli, validé sur BeachDemo. Runtime web = `web/runtime/main.cpp`.
- Surface d'édition LLM existante : `src/mcp/McpBridge.cpp` (~976 lignes).
- **Spike live-edit web S0 : FAIT.** Authoring-core minimal
  (`src/authoring/EngineManifest.*`, `SaidaOpApplier.*`) linké dans le runtime
  web via les bindings `saida_apply_op`/`saida_engine_manifest`. Coût :
  **+4,22 % brotli** (181 361 → 189 010 o wasm+js).
- **Spike live-edit web S1 : FAIT.** Testé navigateur (WebGPU) sur BeachDemo :
  `set_transform` déplace `Palm_A` en live (sans re-export), `set_property`
  modifie le Soleil, ops invalides rejetées sans muter. Harness opt-in `?edit`
  dans `web/runtime/shell.html`.
- **A2 avance (2026-07-03).** `EngineManifest` expose maintenant, en headless,
  nodes reflechis, behaviours reflechis, signaux/slots et contrat scenario
  (actions/conditions) via `saida_tool describe-engine`. Sortie testee stable
  (hash identique sur deux exports) et couverte par `saida_authoring_op_tests`.

### 5.2 Plateforme (`GitHub/saida`)

- Monorepo npm : `apps/web` (Next.js), `apps/api` (Fastify), `apps/workers`,
  `packages/db` (Prisma), `packages/shared`.
- Infra locale Docker : Postgres, Redis, Temporal, MinIO.
- Auth complète, plans/abonnements, **credit ledger**, réservations, Stripe
  (Checkout, Portal, webhooks idempotents, validés en test), admin read-only.
- Pipeline jobs 2Dto3D (Temporal + worker), entitlements de plan appliqués API.
- SaidaEngine Online : première UI produit de l'éditeur web en place (2026-07-04)
  côté `apps/web` : création → écran d'initialisation Guidé/Manuel, choix de nom
  projet, choix de skill de conception en Guidé, bouton Start vers l'éditeur,
  éditeur responsive PC/téléphone avec viewport, chat IA avec bouton de pièce
  jointe, hiérarchie/inspecteur en panneau compact, browser fichiers Projet/Drive.
- Collaboration projet amorcée côté plateforme : bouton Share, modale d'invitation
  par pseudo Saida, stockage `ProjectCollaborator` / `ProjectInvitation`, email
  d'invitation, projets partagés visibles par les collaborateurs, authz du gateway
  ouverte aux collaborateurs éditeurs. Settings projet persistés (`Project.name`,
  modèle IA, FPS max, render scale, ombres) via API et synchronisables pour tous
  les collaborateurs.
  Reste à brancher cette UI au vrai chargement de snapshot, au WebSocket
  collaboration, aux SaidaOps émises par l'UI, aux uploads chat et au stockage
  Drive dédupliqué.

### 5.3 Ce que le spike a tranché

Le pivot de l'invariant 0.2 **tient** : un runtime web peut appliquer une SaidaOp
et re-render en live, à coût de taille acceptable. La vraie réflexion du spike
(`LightNode`, `Water`, `ParticleSystem`), le transport WebSocket local et le
round-trip snapshot web (`saida_scene_snapshot()` vs `SceneSerializer`) sont
validés sur BeachDemo.

### 5.4 D1 livré : canvas moteur dans la plateforme (2026-07-03)

Le runtime web (build `build-web/` du spike, non modularisé) est embarqué dans
la page Next.js de la plateforme (`GitHub/saida`) :

- artefacts servis depuis `apps/web/public/engine/dev/` (non commités),
  synchronisés par `npm run engine:sync` (`scripts/sync-engine-runtime.mjs`) ;
- loader singleton `apps/web/app/lib/engine-runtime.ts` (global `Module`,
  strict-mode safe, ré-adoption du canvas au re-montage) + composant
  `EngineViewport` ;
- route `/saidaengine/workspace` ouverte depuis la liste des projets ; panneau
  manifest (`saida_engine_manifest`) + démo live-edit (`set_transform` Palm_A,
  `set_property` Sun) vérifiée navigateur : BeachDemo rendu in-app, ops
  appliquées sans re-export. Cette démo reste un outil de validation technique,
  pas une surface produit finale ;
- doc plateforme : `GitHub/saida/docs/engine-web-runtime.md` (contraintes :
  WebGPU requis, pas de COOP/COEP tant que pas de pthreads, 1 runtime par
  chargement de page tant que le build n'est pas `-sMODULARIZE`).

### 5.5 D0/D3 produit : première UI d'éditeur web (2026-07-04)

Une première UI produit a été posée dans `GitHub/saida` :

- `/saidaengine` ne crée plus immédiatement un projet. Le bouton "Créer un
  nouveau projet" ouvre d'abord `/saidaengine/workspace?setup=1`.
- L'écran d'initialisation, affiché seulement à la première création, demande le
  nom du projet et le mode **Guidé** ou **Manuel**. En Guidé, il affiche les
  design skills disponibles avec une courte description de pipeline. Le bouton
  Start crée le projet et ouvre l'éditeur.
- En **Manuel**, l'éditeur ouvre un viewport noir / scène vide pour partir de
  zéro. En **Guidé**, le chat reçoit le premier prompt de pipeline lié au skill
  choisi.
- L'éditeur web est responsive PC/téléphone : viewport en haut sur mobile, chat
  IA juste dessous, puis panneau hiérarchie/inspecteur à bascule, puis browser
  de fichiers Projet/Drive.
- Le chat n'a pas de titre décoratif et expose déjà un bouton de pièce jointe.
  Les boutons de debug live-edit (`Move the palm tree`, `Dim the sun`) ont été
  retirés de l'UI produit.
- La barre haute expose maintenant Settings et Share. Settings modifie le nom
  cosmétique du projet (sans changer l'id interne), le modèle IA et les paramètres
  graphiques globaux. Share invite des pseudos Saida par email ; les invitations
  et collaborateurs sont persistés côté API/Postgres.

Limite volontaire : cette UI reste encore majoritairement une façade produit.
Le prochain verrou côté plateforme est D2/D4 : charger un ProjectSnapshot réel
dans le runtime, puis faire produire les SaidaOps par l'inspecteur, les gizmos,
le chat et les manipulations directes dans le viewport.

---

## 6. Feuille de route par phase

Statuts : ✅ fait · 🔵 en cours · ⬜ à faire.
Dépôt : **E** = moteur (`NextEngine`) · **P** = plateforme (`GitHub/saida`).

### Phase A — Contrat moteur (E) 🔵 (A1 ✅)

| # | Tâche | Dépend | Critère de fait |
|---|---|---|---|
| A1 | Définir le type `SaidaOp` (schéma, versionné, sérialisable) | — | ✅ `src/authoring/SaidaOp.{hpp,cpp}` : struct {opVersion, type, sceneId, payload}, parse strict, `toJson` canonique, registre `knownOpTypes()` (source unique manifest+applier), round-trip testé (2026-07-03) |
| A2 | Définir `EngineManifest` complet (nodes, behaviours, **propriétés réfléchies**, signaux, actions scénario, versions) | — | 🔵 fait : `saida_tool describe-engine` produit un manifest deterministe avec versions, ops, nodes/proprietes reflechies, behaviours, signaux/slots et scenario actions/conditions ; reste : schema formel final + extension au contrat assets/scripts |
| A3 | Extraire `SaidaAuthoringCore` depuis `src/mcp/McpBridge.cpp` vers `src/authoring/`, sans dépendance ImGui/éditeur | A1 | MCP + éditeur + headless appellent le même core |
| A4 | Validation stricte des ops (types/behaviours/propriétés inconnus, cycles, racine, chemins hors projet) | A1,A2 | 🔵 fait : forme (version/type/payload), nodes/propriétés inconnus, racine protégée, `reparent_node` avec rejet self/descendant (cycles) — testé ; reste : behaviours, chemins hors projet |
| A5 | Ops inversibles (undo/redo) et diff lisible | A1 | ✅ chaque op de modif renvoie une SaidaOp `inverse` re-appliquable (set_transform/rename/reparent/set_property/create→delete) ; apply→invert round-trip testé desktop + navigateur ; `delete_node` marqué `invertible:false` (restauration par snapshot, invariant 0.6) (2026-07-03) |
| A6 | Round-trip ops → snapshot → `.saidaproj` → reload → re-serialize **stable** (invariant 0.3) | A3 | sortie identique à `SceneSerializer` |

> Amorce déjà en place (spike) : `src/authoring/EngineManifest.*` +
> `SaidaOpApplier.*` (minimal). La Phase A les élargit au contrat complet.

### Phase A.5 — Spike live-edit web (E) ✅ (S0→S4 ✅)

| # | Tâche | Statut |
|---|---|---|
| S0 | Linker l'authoring-core dans le runtime web, mesurer la taille | ✅ +4,22 % |
| S1 | `applyOp` + `set_transform` en live depuis JS | ✅ Palm_A live |
| S2 | **Vraie réflexion** `set_property` dans le wasm + re-mesure taille (**vrai GO/NO-GO taille**) | ✅ |
| S3 | Ops via WebSocket (echo local) au lieu de la console | ✅ |
| S4 | `saida_scene_snapshot()` == `SceneSerializer` (round-trip web) | ✅ |

Détail : [PLAN_LIVE_EDIT_WEB.md](PLAN_LIVE_EDIT_WEB.md).

### Phase B — Headless & snapshots (E) 🔵 (B1 amorcé, B2 ✅, B3 ✅, B4 ✅)

| # | Tâche | Dépend | Critère de fait |
|---|---|---|---|
| B1 | Outil `saida_tool` (sans fenêtre, exit codes fiables, `--json-logs`) | A3 | 🔵 binaire `saida_tool` créé (`src/tools/saida_tool_main.cpp`, cible CMake, link `saida_engine` headless) : exit 0=ok / 1=invalide / 2=usage, stdout machine / stderr diag, `help`. Reste : `--json-logs`, autres sous-commandes |
| B2 | `validate-ops` / `validate-scene` / `validate-script` / `validate-scenario` | A4 | ✅ quatre validateurs headless : `validate-ops` (dry-run `validateOpShape()`), `validate-scene` (structure snapshot : type/name, ids **uniques**, forme transform/children), `validate-script` (compile-check QuickJS `compileCheck`, sans exécution ni bindings, mode module/global auto sur `.mjs`, stdout gardé pur en routant les logs d'init vers stderr) et `validate-scenario` (actions/conditions). Rapport JSON, exit 0/1/2, stdin `-`, CTests valide/invalide pour chacun (2026-07-04) |
| B3 | `apply-ops <proj> <ops.json> --out <snapshot>` | A3,A6 | ✅ `saida_tool apply-ops <scene> <ops> [--out]` : deserialisation **sans GPU** (`deserializeSceneSnapshot`, inverse du snapshot headless — hierarchie, transforms, propriétés réfléchies, groupes, settings, connexions), applique le batch de façon **atomique** (tout op rejeté → exit 1, rien écrit), renumérote les nodes créés de façon déterministe (ids chargés préservés) → snapshot **byte-reproductible** ; `--skip-invalid` (fold tolérant pour le journal de collaboration, C7) ; round-trip serialize→deserialize stable, testé (CTest apply_ops valid/invalid/skip_invalid + `saida_authoring_op_tests`) (2026-07-04) |
| B4 | `describe-engine --json` (export manifest) | A2 | ✅ `saida_tool describe-engine [--pretty]` : manifest JSON sur stdout, **déterministe/hashable** (sha256 stable, invariant 0.6), vérifié (2026-07-03) |
| B5 | Sécurité des chemins (pas de `..`, pas d'absolu, sandbox) | — | 🔵 primitive `resolveSandboxedProjectPath()` faite dans `core/Paths`, tests d'échappement purs (`..`, absolu, drive Windows, file URL) ; pont MCP branché pour scripts/UI/scénarios/import modèle (chemins relatifs conservés pour attachements). Reste : CLI apply-ops + asset loaders bas niveau |

### Phase C — Collaboration SaaS minimale (P) 🔵 (C1–C5 ✅, C7/C8 ✅, reste C6)

| # | Tâche | Dépend | Critère de fait |
|---|---|---|---|
| C1 | Tables Postgres : `Project`, `ProjectRevision`, `OperationLog`, `SceneDocument`, `CollaborationSession` (Prisma) | — | ✅ `Project` existant ; ajout `OperationLog` (journal append-only, revision monotone + inverse), `ProjectRevision` (checkpoint → snapshot, raison OP_THRESHOLD/PERIODIC/PRE_BUILD/MANUAL), `SceneDocument` (snapshot `.saidaproj` inline ou R2/S3 + sha256), `CollaborationSession` (audit connexion + `lastRevisionAck` pour resync) — pensé invariant 0.7 (reconstructible depuis Postgres). Migration écrite main, **byte-identique** à `prisma migrate diff` (zéro drift), `prisma validate`+`generate`+typecheck db verts (2026-07-04, `GitHub/saida`) |
| C2 | Collaboration Gateway (Fastify WebSocket) | A2,A4 | ✅ **Décision** : validation des ops via **WASM headless de l'authoring-core C++** en process (`web/authoring/` → `apps/api/vendor/saida-authoring`, `apps/api/src/authoring.ts`, `initAuthoring`+`validateOp` **synchrone** après init) — zéro duplication, zéro latence, léger. Adaptateur Fastify `@fastify/websocket` (`collaboration-ws.ts`) : authn cookie session, authz par org du projet ou `ProjectCollaborator` éditeur/owner, ligne `CollaborationSession` durable, monté dans `createApp`. Testé (`authoring.test.ts`) |
| C3 | Boucle : auth → valide (manifest+état) → append → apply → revision++ → broadcast | C1,C2 | ✅ `CollaborationHub` (`collaboration.ts`) transport/store-agnostique : sérialisation par projet → revisions **strictement monotones** sous concurrence, persist-avant-ack, broadcast aux pairs (pas à l'auteur), resync du joiner via `opsSince`. 7 tests DB-free (`collaboration.test.ts`) |
| C4 | Revision monotone + snapshots périodiques (N ops / X min / avant build) | C1 | ✅ `materializeSnapshot` + `createSnapshotScheduler` (`collaboration-snapshot.ts`) : fold `base + ops-since` → `SceneDocument` au head, tous les N ops (coalescé par projet), via le **vrai applier natif** (`saida_tool apply-ops`, `SaidaToolSceneFolder`) — travail de fond, jamais sur le chemin chaud, zéro duplication. Testé (fake folder + fold réel via binaire) |
| C5 | Reconstruction gateway depuis Postgres au redémarrage (invariant 0.7) | C1,C3 | ✅ `reconstructScene` = dernier snapshot + fold(ops depuis) ; head rechargé du store à l'ouverture d'un room ; persist-avant-ack ⇒ aucune op ackée perdue au restart. Testé |
| C6 | Présence/curseurs via Yjs awareness | C2 | 2 curseurs visibles |
| C7 | Résolution de conflits (transform/property last-wins, delete/reparent gardés) | C3 | ✅ ordre total serveur ⇒ last-writer-wins (transform/property) ; guards delete/reparent délégués au **vrai applier** via `saida_tool apply-ops --skip-invalid` (les ops devenues invalides — ex. transform sur un nœud supprimé — sont **droppées**, pas abortées) : zéro logique dupliquée côté serveur. Matrice testée engine (CTest `skip_invalid`) + plateforme (`scene-fold.test.ts` : delete-guard, last-wins, rejet atomique) |
| C8 | Collaboration humaine : partage, invitations, droits projet | C1,C2 | ✅ `ProjectCollaborator` + `ProjectInvitation` (Prisma/Postgres), owner créé à la création projet, projets partagés listés pour le collaborateur, invitation par pseudo Saida avec email et token 7 jours, API `GET/PATCH /v1/projects/:id/settings`, `GET /collaborators`, `POST /invitations`, UI Share branchée. Reste : page d'acceptation du token, révocation/roles avancés, présence Yjs visible |

### Phase D — Éditeur web (P + E) 🔵 (D0/D1/D3 amorcés ; backends D2/D8 faits)

| # | Tâche | Dépend | Critère de fait |
|---|---|---|---|
| D0 | Initialisation produit d'un projet Saida Engine (nom + Guidé/Manuel + skill) | C1 | 🔵 façade faite : `/saidaengine` ouvre un setup de première création, choix nom/mode, skills de conception avec descriptions, Start → création projet → éditeur ; reste : persister `creationMode`, `designSkillId`, état d'initialisation, empêcher la réouverture du setup sur projets déjà démarrés, lancer le premier message agent côté serveur |
| D1 | Intégrer le canvas runtime WASM/WebGPU dans la page Next.js | A.5(S1) | ✅ scène rendue in-app (2026-07-03, voir §5.4) |
| D2 | Charger un ProjectSnapshot dans le runtime d'édition | B3,S4 | 🔵 backend fait : `GET /v1/projects/:id/scene` renvoie la scène courante reconstruite (`reconstructScene` = dernier snapshot + fold des ops depuis) `{revision,doc}` ; l'éditeur charge ce snapshot puis se connecte au WS `?since=revision` (rejoue seulement les ops récentes → ouverture rapide quel que soit l'historique). Reste : chargement du doc dans le runtime côté client (frontend) |
| D3 | Panels React pilotés par EngineManifest : hierarchy, inspector, assets, chat, console | A2 | 🔵 amorcé : `EngineManifestPanel` rend le contrat moteur ; `SceneHierarchyPanel` + `SceneInspectorPanel` affichent hiérarchie et inspecteur en panneau compact avec retour mobile ; browser Projet/Drive posé ; chat IA posé avec bouton pièce jointe ; layout responsive PC/téléphone validé. Reste : inspecteur éditable, édition→SaidaOp, vrai browser projet/Drive, uploads chat, console, états vides/chargement branchés au projet réel |
| D4 | Émettre des SaidaOps depuis l'UI (gizmos, inspector) | C3 | édition → op acceptée |
| D5 | Recevoir et appliquer les ops distantes (multi-clients) | C3,S3 | 2 users, même scène |
| D6 | **Optimistic-local** pour manipulations continues (invariant 0.4) | D4 | drag fluide, commit au relâchement |
| D7 | Édition texte scripts/UI via Yjs + commit `write_script`/`write_ui` | C6 | script hot-reload |
| D8 | Historique des opérations + rollback de revision | C4 | 🔵 lecture faite : `GET /v1/projects/:id/revisions` (timeline des checkpoints) + `GET /v1/projects/:id/scene?at=N` (reconstruction time-travel = snapshot ≤ N + fold des ops jusqu'à N), sans réécrire le journal append-only ; store `listRevisions`/`snapshotAtOrBefore`, testé. Reste : rollback qui mute le head (live) |
| D9 | Picking/manipulation directe dans le viewport | D2,D4 | ⬜ toucher/click sur objet → sélection node ; gizmo move/rotate tactile + souris ; preview optimiste locale pendant drag ; commit SaidaOp unique au relâchement |
| D10 | Settings projet dans l'éditeur | C8 | ✅ UI Settings en barre haute + API persistée : nom cosmétique projet, modèle IA, FPS max, render scale, ombres. Reste : broadcast live des changements aux clients déjà connectés |

### Phase E — IA agentique / vibecoding (P + E) 🔵 (socle E2/E3 ✅)

| # | Tâche | Dépend | Critère de fait |
|---|---|---|---|
| E0 | Registre de design skills (officiels + communauté) | D0 | 🔵 façade UI faite avec quelques skills statiques ; reste : modèle DB/API (`DesignSkill`, version, auteur, visibilité, description, prompt système/pipeline), skill officiel par défaut, marketplace/community skills, sélection persistée par projet |
| E1 | Agent lit EngineManifest + scène compacte | A2,D2 | 🔵 contexte prêt côté serveur : `GET /v1/engine/manifest` sert le manifest déterministe (vendé depuis `describe-engine`, `getEngineManifest`) + `GET /v1/projects/:id/scene` la scène courante. Reste : l'agent qui consomme ce contexte |
| E2 | Agent produit des SaidaOps (pas de JSON de scène libre) | A1 | ✅ socle : `POST /v1/projects/:id/ops` applique un batch d'ops via `hub.applyExternalOp` (même ordre total que les humains, broadcast live) ; chaque op passe la validation WASM du contrat. Reste : l'agent producteur lui-même |
| E3 | Dry-run obligatoire + diff visible avant application | A5,B3 | ✅ `POST /v1/projects/:id/ops/dry-run` : validation de forme (WASM) + fold **atomique** des ops proposées sur la scène courante (vrai applier) sans persister → snapshot résultant pour prévisualiser le diff (`ops-preview.ts`, testé) |
| E4 | Validation headless post-application | B2 | 🔵 briques prêtes (`validate-scene`/`validate-ops`/`apply-ops` atomique) ; reste : brancher la vérif post-apply bloquante dans le pipeline agent |
| E5 | Crédits (réserve→commit/refund) + audit (`AiConversation`/`AiToolCall`) | — | ledger cohérent |
| E6 | Garde-fous : pas de shell, pas de C++ web, pas de cross-projet (invariant 0.5) | — | tentatives rejetées |
| E7 | Chat projet + pièces jointes | D0,E0 | ⬜ conversations persistées par projet, upload de fichiers dans le chat (GDD, images, sons, refs), stockage S3/R2 dédupliqué par hash, attachements visibles par l'agent, quotas/credits appliqués |

### Phase F — Export serveur (P + E) ⬜

| # | Tâche | Dépend | Critère de fait |
|---|---|---|---|
| F1 | Workflow Temporal `web-export` depuis snapshot immuable | B1,C4 | artefact produit |
| F2 | Template WASM/WebGPU précompilé, **pinné par `engineVersion`** (invariant 0.6) | — | pas de recompile moteur/jeu |
| F3 | Workers isolés (`web/windows/android-build-worker`) : quotas CPU/RAM/disk/time | — | jobs bornés |
| F4 | Artefacts → R2/S3 + `BuildArtifact` + logs persistants + checksum | C1 | téléchargeable |
| F5 | Crédits réserve/commit/refund sur build | E5 | marge protégée |
| F6 | Migration inter-versions par snapshot (invariant 0.6) | A6 | projet ré-ouvert sur moteur v+1 |

### Phase G — Durcissement production (P) ⬜

| # | Tâche | Critère de fait |
|---|---|---|
| G1 | Quotas + rate limits (jobs/h, concurrence, storage, upload) | limites appliquées API |
| G2 | Backups Postgres + R2/S3, **restauration testée au moins une fois** | runbook restore OK |
| G3 | Observabilité (logs structurés, métriques jobs/credits, traces) | dashboards |
| G4 | Alertes (build failures, queue bloquée, coût IA anormal, ledger imbalance) | alerting actif |
| G5 | RGPD : suppression projet/compte, export projet téléchargeable | procédure documentée |
| G6 | Tests de charge collaboration + export | rapport de charge |

---

## 7. Ordre d'exécution recommandé

1. **D0 côté plateforme** : finir le modèle de première initialisation
   (mode Guidé/Manuel, skill choisi, projet déjà initialisé) et le faire piloter
   la création réelle de projet.
2. **D2** : charger le vrai `ProjectSnapshot` dans le runtime web au lieu de
   l'état de démo/viewport noir. C'est le verrou qui transforme l'UI en éditeur.
3. **D4 + D9** : brancher inspecteur/gizmos/picking viewport vers des SaidaOps,
   avec optimistic-local et commit au relâchement.
4. **E0/E7** : créer le registre de design skills, les conversations projet et
   les pièces jointes de chat, puis lancer le premier prompt de pipeline en mode
   Guidé.
5. **E1→E4** : connecter l'agent aux manifest/scènes, dry-run, diff, puis
   application auditée des SaidaOps.
6. **F/G** : export serveur, quotas, sauvegardes, observabilité et production.

Règle d'or : l'IA ne doit être qu'un producteur de SaidaOps de plus. Même en
mode Guidé, le chat oriente la création et manipule des fichiers, mais les
mutations durables du projet passent par le même journal validé que l'UI humaine.

---

## 8. Risques & mitigations

| Risque | Impact | Mitigation |
|---|---|---|
| **Réflexion couplée à l'éditeur** (le loader web l'évite exprès) | bloque S2/A3 | découpler la réflexion d'ImGui en pré-requis Phase A |
| **Coût taille wasm de la réflexion** | build web trop lourd | mesurer en S2 ; fallback authoring-core « allégé web » |
| Gateway = service stateful (charge solo) | fragilité opé | invariant 0.7 : reconstructible depuis Postgres |
| Latence collaboration sur manipulations continues | éditeur injouable | invariant 0.4 : optimistic-local + commit au relâchement |
| Moteur alpha très mouvant (formats qui bougent) | projets cassés | invariant 0.6 : migration par snapshot, op-log intra-version |
| Coût IA/build non borné | marge ruinée | crédits réserve/commit/refund + kill switch par outil |
| Deux modèles de scène qui divergent | dette ingérable | invariant 0.3 : `SceneSerializer` seul format durable |

---

## 9. Critères d'acceptation « production-ready »

Repris du mandat §9, l'intégration est prête quand :

- deux utilisateurs modifient la même scène sans corruption ;
- une IA ne modifie scène/script/UI que via des SaidaOps validées et auditées ;
- chaque op est auditable et une revision est restaurable ;
- un snapshot se matérialise en projet local `.saidaproj` ;
- un export web serveur fonctionne depuis un snapshot ;
- un crash serveur ne perd aucune op validée ;
- les assets lourds sont dans R2/S3, dédupliqués par hash ;
- les builds sont isolés et bornés ;
- les crédits couvrent IA/build/stockage ;
- les permissions org/projet sont testées ;
- les backups ont été restaurés au moins une fois ;
- desktop et éditeur web partagent le même contrat d'authoring.

---

## 10. Décisions ouvertes (à trancher)

- **Phase A** : jusqu'où élargir le sous-ensemble spike (`set_transform`,
  `create_node`, `delete_node`, `set_property`) avant d'extraire totalement le
  noyau `SaidaOp` depuis MCP ? *Contrat actuel (2026-07-04)* : `set_transform`,
  `create_node` (palette réfléchie complète via NodeRegistry), `delete_node`,
  `rename_node`, `reparent_node`, `set_property`, `set_scene_setting`,
  `add_behaviour`, `remove_behaviour`, `set_behaviour_property` — tous validés
  (forme WASM), inversibles, foldables ; les behaviours round-trippent dans le
  snapshot headless. Restent à cadrer : `write_script`, `write_ui`, connexions
  de signaux.
- **Identité des nodes côté ops** : le spike référence par nom ; le système final
  doit utiliser `NodeId` (id/génération) — figer le schéma en A1.
- **Import d'asset en édition web** : MEMFS preload vs fetch/IDBFS — hors spike,
  à cadrer en Phase D/F.
- **Hébergement** : Coolify/Kamal + Hetzner/OVH + Cloudflare R2 (proposé mandat
  §4.1) — à confirmer avant Phase F.
- **Périmètre `write_script` web** : QuickJS hot-reload existe déjà côté moteur ;
  définir jusqu'où l'IA génère du JS en Phase E.
- **Initialisation de projet** : ajouter ou non les champs dédiés
  `creationMode`, `designSkillId`, `initializedAt`, `initialPromptSentAt` sur
  `Project`/`EngineWorkspace`, ou stocker cela dans une table de conversation
  initiale. Le setup Guidé/Manuel ne doit être accessible qu'avant
  initialisation.
- **Scène vide en mode Manuel** : définir le snapshot initial canonique
  (racine seule, camera/lumière par défaut ou vraiment noir/vide) et la manière
  dont le runtime web le charge sans BeachDemo.
- **Design skills communautaires** : définir le format durable d'un skill de
  conception (métadonnées, prompt système, étapes, permissions, versioning,
  publication communautaire, skill officiel par défaut).
- **Chat + pièces jointes** : définir le modèle `AiConversation`/`AiMessage`/
  `AiAttachment`, le stockage hashé/dédupliqué et le lien avec le Drive Saida.

---

## 11. Références

- Mandat technique : [ARCHITECTURE_PRODUCTION_CLAUDE.md](ARCHITECTURE_PRODUCTION_CLAUDE.md)
- Spike pivot : [PLAN_LIVE_EDIT_WEB.md](PLAN_LIVE_EDIT_WEB.md)
- Export web moteur : [PLAN_WEB_EXPORT.md](PLAN_WEB_EXPORT.md)
- Contrat d'authoring moteur : [CLAUDE.md](CLAUDE.md) (« Comment coder un jeu »)
- Architecture plateforme : `GitHub/saida/docs/architecture.md`
- Fondations locales plateforme : `GitHub/saida/docs/local-foundations.md`

---

## 12. Plan « code complexe » (session Claude, 2026-07-04)

> Répartition : **Codex** = UI, infra web, CLI, prod cloud. **Claude** = morceaux
> **complexes cross-brique** reliant moteur C++/WASM, backend collaboration et
> client web. Décisions utilisateur : (1) commencer par l'éditeur réel
> (D2→D4→D9), puis ops moteur manquantes, puis agent IA ; (2) Claude livre la lib
> client live-edit + bindings WASM + API d'émission d'ops ; Codex branche/finit
> les widgets React.

### Constat d'exploration (2026-07-04)

- **Backend collaboration = fonctionnel** : validation WASM in-process, hub à
  révisions monotones, fold natif (`saida_tool apply-ops`), reconstruction,
  dry-run, endpoints `/scene`, `/ops`, `/ops/dry-run`, `/revisions`,
  `/engine/manifest` (`apps/api/src/{authoring,collaboration,collaboration-ws,collaboration-snapshot,scene-fold,ops-preview}.ts`).
- **Frontend = façade** : pas de client WS, pas de chargement de snapshot dans le
  runtime, pas d'émission d'op, pas de picking ; le runtime boote sur
  `/project/beach.scene` en dur (`apps/web/app/lib/engine-runtime.ts`,
  `app/saidaengine/workspace/page.tsx`).
- **Moteur = socle solide, 3 trous web** : (1) réflexion web réduite à un stub
  (`EngineManifest.cpp`, `#ifndef __EMSCRIPTEN__`, ~l.62/74-81) ; (2) pas de
  binding de chargement de snapshot (runtime via `loadNode()` custom,
  `web/runtime/main.cpp` l.211-238, pas `deserializeSceneSnapshot`) ; (3) pas de
  picking exposé au web (raycast Jolt `PhysicsWorld.hpp` l.88 + picking éditeur
  `EditorUI.cpp` existent mais non exposés WASM).

### Contrainte cross-brique majeure (à respecter)

Le snapshot **headless** durable (`serializeSceneWithoutResources` /
`deserializeSceneSnapshot`, `SceneSnapshot.cpp`) **omet mesh & material** ; le fold
serveur (`saida_tool apply-ops`) passe par ce format → il ne transporte pas les
données de rendu. ⇒ Le premier livrable end-to-end est scopé sur la **scène
vide/Manuel** (racine + defaults), ce qui évite le round-trip matériaux tout en
livrant la vraie boucle d'édition. Le transport des refs d'assets dans le snapshot
est traité en **Track 1-F** (design), hors MVP.

### Track 1 — Éditeur réel (D2 → D4 → D9) — priorité 1

- **1-A · Moteur : réflexion web — CONSTAT.** Le build web
  (`web/runtime/CMakeLists.txt`) est une **liste de sources triée à la main** qui
  exclut volontairement physics/Jolt, audio, QuickJS, scénario-behaviours,
  éditeur, et ne compile même pas `ReflectedTypes.cpp`. Activer
  `registerReflectedTypes()` tirerait tout ce graphe dans le WASM (risque taille/
  couplage §8) **et est inutile pour le MVP** : le manifest web actuel
  (`EngineManifest.cpp`, branche `#else`) expose déjà Node/MeshNode/Light/Water/
  Particle + leurs propriétés réfléchies + name/enabled/transform, ce qui suffit à
  l'inspecteur de la scène vide/Manuel. ⇒ **Parité réflexion complète reportée**
  (à faire quand on amènera les behaviours au web) ; le premier pas réel est 1-B.
- **1-B · Moteur : `saida_load_snapshot(docJson)`** (`web/runtime/main.cpp`) :
  détruit la scène vivante, reconstruit hiérarchie/transforms/propriétés/settings/
  behaviours/connexions, redéclenche le rendu ; réutilise la logique de
  `deserializeSceneSnapshot` en résolvant les ressources GPU via `ResourceManager`.
  Définir le snapshot **scène vide canonique** (aligné `emptySceneSnapshot()`
  `scene-fold.ts` l.28-42). Critère : `/scene` → scène vide rendue sans BeachDemo.
- **1-C · Moteur : `saida_pick(x,y)`** (`web/runtime/main.cpp` + `PhysicsWorld::raycast`) :
  écran→rayon→nodeId JSON (null si rien). Critère : clic renvoie le NodeId attendu.
- **1-D · Frontend : lib `apps/web/app/lib/live-edit-client.ts`** : bootstrap
  `GET /scene` → `saida_load_snapshot` ; WS `/ws/projects/:id?since=rev`
  (`welcome`/`op`/`ack`) ; ops distantes → `applyOp` (D5) ; ops locales optimistes
  + commit au relâchement + réconciliation via `inverse` (invariant 0.4, D6).
  Critère : 2 onglets synchronisés, drag fluide, 1 commit au relâchement.
- **1-E · Frontend : API d'émission d'ops + hooks** (`emitOp`,
  `beginContinuous/commit`, helpers typés alignés `knownOpTypes()`) ; hooks
  inspecteur/gizmo/picking que Codex câble sur les widgets. Sortir les types
  SaidaOp/manifest/scene dans `packages/shared`.
- **1-F · (design, hors MVP)** : round-trip des refs d'assets dans le snapshot
  (étendre serialize/deserialize headless + résolution runtime) — lié à l'open
  decision « Import d'asset en édition web ».

### Track 2 — Ops moteur manquantes + extraction authoring (A3) — priorité 2

- **A3** : extraire `write_script`/`write_ui`/`connect_signal` de `McpBridge.cpp`
  (éditeur-only, couplés undo/ImGui) vers `src/authoring/`, appelés par MCP **et**
  l'applier.
- Nouveaux SaidaOp (`knownOpTypes()`, applier, inverse, validation) :
  `write_script`/`write_ui` (branchés hot-reload `ScriptBehaviour`/`WebCanvasNode`),
  `add_signal_connection`/`remove_signal_connection` (via `SignalConnectionDef`/
  `SignalWiring`, déjà sérialisés).
- Round-trip A6 (ops→snapshot→`.saidaproj`→reload stable) + sécurité chemins B5
  (brancher `resolveSandboxedProjectPath()` sur apply-ops CLI + asset loaders).

### Track 3 — Agent IA producteur d'ops (E1 → E4) — priorité 3

- E1 contexte (prompt depuis `/engine/manifest` + scène compacte), E2 extraction
  d'ops validées WASM (pas de JSON de scène libre), E3 dry-run+diff
  (`/ops/dry-run` existant) → apply (`/ops`), E4 gate post-apply bloquante
  (`validate-scene`/`validate-ops`). Garde-fous E6 au niveau du contrat d'ops.
- Handoff Codex : worker Temporal (patron 2Dto3D), LLM/quota, modèles
  `AiConversation`/`AiMessage`, crédits E5.

### Vérification

- Moteur headless : `cmake --build build` + CTest (`saida_authoring_op_tests`,
  `saida_tool`) verts. Web : sourcer `emsdk_env.sh`, rebuild `build-web/`
  (`web/build_web.sh`), mesurer brotli. Frontend : preview MCP (charger workspace
  Manuel, éditer une prop, 2 onglets). Backend : `apps/api/src/*.test.ts` verts.

### Avancement session Claude (2026-07-04)

**Track 1 enablers moteur + lib client = FAITS et compilés :**

- **1-B `saida_load_snapshot(docJson)` ✅** — `web/runtime/main.cpp` : fonction
  `loadSceneDoc()` (reconstruit la scène vivante depuis un doc `/scene`) + binding
  extern C exporté. Compatible avec `emptySceneSnapshot()` (scène vide/Manuel).
- **1-C `saida_pick(ndcX, ndcY)` ✅** — picking ray-AABB (`Mesh::bounds()` +
  `worldTransform()`, pas de Jolt sur web) → renvoie `{ok, hit, nodeId(name), id}`.
- **Réparation préexistante ✅** — le build web n'était plus relinkable (le contrat
  `create_node` référence `NodeRegistry`/`registerReflectedTypes` absents du
  sous-ensemble web). Ajout de `src/scene/NodeRegistry.cpp` + nouveau
  `src/scene/ReflectedTypesWeb.cpp` (impl. web minimale, `#ifdef __EMSCRIPTEN__`,
  enregistre seulement Light/Water/Particle, zéro behaviour). `build-web` link OK,
  wasm ~169 Ko brotli.
- **1-D lib client ✅** — `apps/web/app/lib/live-edit-client.ts` :
  `connectLiveEdit(runtime, projectId, events, opts)` = bootstrap `/scene` →
  `loadSnapshot`, WS `/v1/projects/:id/collab?since=rev`, replay welcome, ops
  distantes appliquées (D5), ops locales optimistes + rollback via `inverse` (D6),
  helpers d'ops typés (`ops.setTransform/setProperty/...`, champs alignés sur
  l'applier : `nodeId`=nom, `newParent`, `setting`/`value`). Loader étendu
  (`engine-runtime.ts` : `loadSnapshot`, `pick`, types). `npm run typecheck` vert.

Vérifié : build-web link+exports (`saida_load_snapshot`, `saida_pick`), sync vers
`apps/web/public/engine/dev/`, typecheck web, desktop intact + `saida_authoring_op_tests`
vert. **Non vérifié E2E navigateur** (nécessite API+Postgres+Redis+Temporal+auth+
projet+`saida_tool` sur PATH = infra Codex).

**Track 2 — ops moteur (avancement) :**
- **Ops de connexion de signaux ✅ (bout en bout)** — `add_signal_connection` /
  `remove_signal_connection` ajoutées à `knownOpTypes()`, `validateOpShape`,
  `SaidaOpApplier` (mutation de `scene.connections()`, inverses réciproques,
  dédup + rejet nœud inconnu). Round-trip snapshot vérifié. Câblé partout :
  `saida_authoring_op_tests` (nouveau `testSignalConnectionOps`), `saida_tool`
  (rebuild + `apply-ops`/`validate-ops` OK), **WASM du gateway** rebuild+resync
  (`build-authoring-wasm` → `apps/api/vendor/saida-authoring`) + manifest
  régénéré, test API `authoring.test.ts` étendu (8/8). Helpers client
  `ops.addSignalConnection/removeSignalConnection`. Les nœuds sont référencés par
  **nom** (cohérent avec toutes les SaidaOps ; `from`/`to` résolus en NodeId).
- **`write_script` / `write_ui` : REPORTÉ (décision cross-brique requise).** Ces
  ops ne rentrent pas encore proprement dans le modèle collaboration : le
  `ScriptBehaviour`/`WebCanvasNode` chargent la source depuis des **fichiers**,
  et le fold headless (`saida_tool apply-ops`) ne reconstruit que le doc de
  **scène**, sans filesystem projet. Les ajouter maintenant créerait des op
  types non round-trippables (violant l'invariant 0.7). **Débloqué par** : le
  modèle de stockage durable des fichiers projet côté serveur (Drive/assets
  dédupliqués R2/S3 — infra Codex + open decisions §10 « write_script web » et
  « Chat + pièces jointes »). Une fois ce stockage décidé, `write_script`/
  `write_ui` s'implémentent comme ops `{path, code}` appliquées via
  `resolveSandboxedProjectPath` (B5). L'extraction A3 pour les signaux est de
  fait acquise (le core authoring possède désormais la mutation signal) ; A3 pour
  les scripts est couplée à cette même décision.

**Track 3 — agent IA producteur d'ops (avancement) :**
- **Seam agent LLM-agnostique ✅** — `apps/api/src/agent-ops.ts` (testé,
  `agent-ops.test.ts` 5/5) : `buildAgentSystemPrompt(manifest)` (E1 : contrat
  d'ops/nodes/propriétés + garde-fous invariant 0.5 en dur),
  `compactSceneForAgent(doc)` (E1 : arbre compact id/nom/type, tokens bas),
  `extractProposedOps(modelOutput)` (E2 : extrait un `SaidaOp[]` d'une sortie
  modèle — array brut, `{ops}`, ou bloc ```json — rejette tout le reste),
  `validateProposedOps(ops, validateOp)` (E6 : chaque op passe le vrai contrat
  WASM avant d'atteindre le hub).
- **Déjà en place (socle backend, pas moi) :** `POST /ops/dry-run` (E3, fold
  atomique + diff), `POST /ops` (application ordonnée + broadcast),
  `validate-scene`/`validate-ops` (briques E4).
- **Handoff Codex :** l'appel LLM lui-même, le worker Temporal (patron 2Dto3D),
  les clés/quota modèle, les crédits (E5), les modèles `AiConversation`/
  `AiMessage`/`AiAttachment` (E7). Le pipeline : prompt =
  `buildAgentSystemPrompt` + `compactSceneForAgent` → LLM → `extractProposedOps`
  → `validateProposedOps` → `POST /ops/dry-run` (diff, validation E3) → revue →
  `POST /ops`.

**Point de branchement pour Codex (1-E, UI) :** dans
`apps/web/app/saidaengine/workspace/page.tsx`, quand le runtime est prêt et
`projectId` connu, appeler `connectLiveEdit(runtime, projectId, { onRemoteOp,
onRevision, onStatus })` au montage (cleanup: `session.close()`), remplacer le
polling `engine.snapshot()` par le bootstrap de la session, et câbler :
inspecteur `onChange` → `session.emitOp(ops.setProperty|setTransform(...))` ;
gizmo `onDrag` → `session.previewLocal(...)`, `onRelease` → `session.commit(...)` ;
viewport `onClick(px,py)` → convertir en NDC (`x=px/w*2-1`, `y=1-py/h*2`) →
`runtime.pick(x,y)` → sélection du node par `nodeId`.

### Notes d'exécution

- Branche par track ; pas de commit/push sans demande. Ne pas lancer l'exe desktop
  (boucle GUI). emsdk présent (`/c/Users/evand/emsdk`), `emcc` pas dans le PATH par
  défaut → `source /c/Users/evand/emsdk/emsdk_env.sh` avant `cmake --build build-web`,
  puis `node scripts/sync-engine-runtime.mjs` côté `GitHub/saida`. Tout passe par
  SaidaOps validées (invariants §3).

---

### Avancement Codex - Lot 1 (2026-07-04)

**Livre cote plateforme et moteur :**

- **CI minimale ajoutee dans les deux repos.** `GitHub/saida/.github/workflows/ci.yml` installe Node 22, valide Prisma, genere le client Prisma, lance typecheck, tests et build. `NextEngine/.github/workflows/ci.yml` configure un runner Windows/MSYS2 UCRT64, installe la toolchain moteur, build `saida_tool` + `saida_authoring_op_tests`, puis lance les CTests authoring/headless. Cette CI garde le contrat `SaidaOp` et la plateforme dans le meme filet de verification.
- **D2/D5 branche cote UI.** `apps/web/app/lib/use-live-edit-session.ts` isole l'etat live React : bootstrap par `connectLiveEdit`, chargement du snapshot serveur via `runtime.loadSnapshot`, ouverture WS `/v1/projects/:id/collab?since=revision`, refresh snapshot apres ops locales, ops distantes et rollback local. `workspace/page.tsx` ne poll plus `engine.snapshot()` en mode demo ; il depend du snapshot serveur et du log collaboration.
- **D9 selection viewport amorcee.** `EngineViewport` expose un hook de picking propre (`onPick`) qui convertit le clic canvas en NDC puis appelle `runtime.pick(x,y)`. Le workspace selectionne le node correspondant dans la hierarchie via nom/id. Les modes move/rotate restent a brancher en gizmos continus (D6/D9 suite).
- **D4 inspecteur editable.** `SceneInspectorPanel` edite maintenant `name`, `enabled`, transform (`position`, `rotation`, `scale`) et proprietes reflechies scalaires/bool/enum du `EngineManifest`. Les mutations passent par les helpers types `ops.renameNode`, `ops.setTransform`, `ops.setProperty`, puis par `session.emitOp` : meme chemin valide que les peers et futurs agents.
- **Nettoyage repo.** Suppression de l'artefact `apps/web/public/logo.png~` et ajout de `*~` au `.gitignore`.

**Verification locale effectuee :**

- `npm run typecheck` : vert.
- `npm run test` : vert (`44` tests API passes, `5` skips attendus sans binaire natif sur PATH ; `7/7` shared).
- `npm run build` : vert.
- `npx prisma validate --schema packages/db/prisma/schema.prisma` : vert.

**Non encore prouve dans ce lot :**

- E2E navigateur a deux onglets avec infra locale complete demarree (Postgres/Redis/Temporal/MinIO/API/Web/auth/projet). Le code de branchement est en place, mais il faut une passe Playwright/manuelle avec sessions reelles pour valider visuellement : snapshot serveur charge, WS connecte, op inspecteur visible dans l'autre onglet, selection par picking.
- Manipulations continues move/rotate : le mode select picke ; les gizmos avec preview locale + commit unique au relachement restent le prochain morceau D6/D9.

---

### Avancement Codex - Lot 2 (2026-07-04)

**Livre cote plateforme :**

- **Chat IA projet persistant.** Ajout des modeles Prisma `AiConversation`, `AiMessage`, `AiAgentRun`, `AiAction` et migration `20260704170000_ai_conversations`. Les conversations sont scopees par projet, historisees, et reliees aux propositions/actions agent.
- **Agent LLM explicite, sans fake.** `apps/api/src/llm.ts` appelle un endpoint `openai-compatible` configure par `AGENT_LLM_*`. Si la cle ou le modele manque, le run echoue avec `AGENT_LLM_NOT_CONFIGURED` et aucune modification de scene n'est faite.
- **Pipeline agent SaidaOps.** `apps/api/src/project-agent.ts` branche `POST /v1/projects/:id/agent/messages` sur le contexte moteur reel : `EngineManifest` + scene compacte courante. La sortie modele passe par `extractProposedOps`, validation WASM `validateOp`, puis `dryRunOps` atomique avant d'etre stockee comme `AiAction.PROPOSED`.
- **Application revue -> live edit.** `POST /v1/projects/:id/agent/actions/:actionId/apply` re-dry-run l'action contre le head courant, applique chaque op via `hub.applyExternalOp`, et broadcast aux clients connectes dans le meme ordre total que les edits humains.
- **Frontend chat branche.** Le panneau chat de `saidaengine/workspace` charge l'historique, envoie les prompts, affiche les runs echoues/proposes/appliques, et expose un bouton `Apply` pour appliquer explicitement les ops proposees.
- **Configuration prod.** `.env.example`, `config.ts` et `env.ts` documentent/verrouillent `AGENT_LLM_ENABLED`, `AGENT_LLM_BASE_URL`, `AGENT_LLM_API_KEY`/`OPENAI_API_KEY`, `AGENT_LLM_MODEL`, timeout et taille d'historique. En production, si l'agent est active, la config LLM doit etre presente.

**Verification locale effectuee :**

- `npx prisma validate --schema packages/db/prisma/schema.prisma` : vert.
- `npm run db:generate` : client Prisma regenere.
- `npm run typecheck -w @saida/api` : vert.
- `npm run typecheck -w @saida/web` : vert.
- `npm run test -w @saida/api` : vert (`46` passes, `5` skips attendus sans binaire natif sur PATH).
- `npm run build -w @saida/web` : vert.

**Non encore prouve / reporte proprement :**

- E2E navigateur complet avec API + Postgres + auth + deux sessions : a faire au prochain lot pour prouver visuellement chat -> proposition -> apply -> scene update live chez plusieurs collaborateurs.
- Pieces jointes de chat (`AiAttachment`/S3/R2/dedupe), credits/quota par run et worker Temporal pour jobs longs : hors Lot 2, a implementer avant ouverture publique large.
- `write_script`/`write_ui` restent bloques par le modele durable de fichiers projet : l'agent peut deja produire/appliquer les SaidaOps actuellement supportees par le contrat moteur, mais l'ecriture de scripts doit passer par un stockage projet round-trippable avant d'entrer dans le contrat.

---

### Avancement Codex - Lot 3 (2026-07-04)

**Livre cote plateforme :**

- **Fichiers projet durables.** `GET/POST /v1/projects/:id/files` liste et cree des versions de `ProjectFile` a partir d'assets disponibles de l'organisation. Les chemins sont normalises et refuses s'ils sont absolus, contiennent `..`, backslash, segments invalides ou extensions non autorisees.
- **Telechargement fichier projet.** `GET /v1/projects/:id/files/:fileId/download` retourne une URL signee S3/R2 pour un fichier projet autorise.
- **Uploads generiques controles.** `ALLOWED_UPLOAD_MEDIA_TYPES` complete `ALLOWED_IMAGE_TYPES` pour autoriser textes, scripts, JSON, PDF, audio et glTF utiles a l'editeur/chat. Les limites de taille, quota stockage et statut `AssetStatus` restent ceux du pipeline existant.
- **Marketplace protegee.** Comme les uploads ne sont plus seulement image, `marketplace.ts` impose maintenant que `coverAssetId` pointe vers un asset image ; builds/downloads restent des assets disponibles.
- **Pieces jointes chat persistantes.** Ajout de `AiAttachment` dans Prisma/migration. `POST /v1/projects/:id/agent/messages` accepte jusqu'a 8 attachments `{assetId,path?}`, cree les `AiAttachment`, et peut lier chaque asset a un `ProjectFile` versionne sous son chemin projet.
- **Contexte agent enrichi.** Le prompt agent recoit la liste recente des pieces jointes projet/chat (nom, projectFileId, mediaType, taille) sans ingester le contenu complet : pas de fuite implicite ni explosion tokens.
- **Frontend branche.** Le trombone du chat upload de vrais fichiers via `/v1/uploads/*`, les envoie avec le prompt, affiche les chips de pieces jointes dans le fil, et l'onglet Project du browser charge les vrais `ProjectFile` au lieu d'une liste statique.

**Verification locale effectuee :**

- `npx prisma validate --schema packages/db/prisma/schema.prisma` : vert.
- `npm run db:generate` : client Prisma regenere.
- `npm run typecheck -w @saida/api` : vert.
- `npm run typecheck -w @saida/web` : vert.
- `npm run test -w @saida/api` : vert (`46` passes, `5` skips attendus).
- `npm run build -w @saida/web` : vert.

**Non encore prouve / suite logique :**

- E2E navigateur complet avec MinIO/S3 local : uploader un fichier chat, verifier creation `Asset` + `ProjectFile` + `AiAttachment`, puis voir l'onglet Project se rafraichir apres envoi.
- Lecture selective du contenu texte par l'agent : a faire avec limites strictes (taille, media types, redaction eventuelle) avant `write_script`/`write_ui`.
- Modeles credits/quota par run agent et worker Temporal restent a faire avant prod publique.

---

### Avancement Codex - Lot 4 (2026-07-04)

**Livre cote plateforme :**

- **Lecture selective des attachments par l'agent.** `apps/api/src/agent-attachments.ts` determine quels fichiers sont lisibles par le LLM : media types texte connus ou extensions texte sous `application/octet-stream`, taille strictement bornee, et nombre d'extraits limite.
- **Reader S3 borne.** `apps/api/src/s3.ts` expose `getObjectBytes({bucket,key,maxBytes})` avec range request. L'agent ne telecharge jamais un objet complet par accident.
- **Contexte non fiable explicite.** Les extraits injectes dans le prompt sont encadres par `BEGIN/END UNTRUSTED ATTACHMENT` et precedes d'une consigne systeme : le contenu fichier est contexte utilisateur, pas instruction prioritaire.
- **Limites configurables.** `.env.example` et `config.ts` ajoutent `AGENT_ATTACHMENT_MAX_BYTES`, `AGENT_ATTACHMENT_MAX_SNIPPETS`, `AGENT_ATTACHMENT_MAX_TOTAL_CHARS`.
- **Integration agent.** `project-agent.ts` enrichit maintenant le prompt avec metadata + extraits bornes des derniers attachments disponibles, en reliant `AiAttachment` -> `Asset` -> S3/R2. Les fichiers binaires/audio/PDF restent visibles en metadata mais ne sont pas lus comme texte.
- **Tests de garde-fou.** `agent-attachments.test.ts` couvre media types lisibles, refus des binaires/trop gros, nettoyage des caracteres de controle, troncature, et balisage non fiable.

**Verification locale effectuee :**

- `npm run typecheck -w @saida/api` : vert.
- `npm run typecheck -w @saida/web` : vert.
- `npm run test -w @saida/api` : vert (`49` passes, `5` skips attendus).
- `npm run build -w @saida/web` : vert.

**Non encore prouve / suite logique :**

- E2E avec MinIO reel : verifier que les range requests fonctionnent sur l'object store cible et que le prompt contient bien les extraits attendus.
- Redaction/filtrage avance des secrets dans fichiers texte avant prompt LLM.
- Ops `write_script`/`write_ui` : maintenant debloquables cote plateforme, mais le contrat moteur doit encore definir l'application durable fichier+scene sans casser le round-trip.

---

### Avancement Codex - Lot 5 (2026-07-05)

**Livre cote plateforme :**

- **Redaction secrets avant prompt LLM.** `agent-attachments.ts` masque les patterns usuels avant injection dans le contexte agent : assignments sensibles (`*_SECRET`, `*_TOKEN`, `*_API_KEY`, `PASSWORD`, `PRIVATE_KEY`, etc.), Bearer longs, cles OpenAI/Stripe/AWS/GitHub/Google/Slack/JWT.
- **Tests redaction.** `agent-attachments.test.ts` verifie que les secrets sont masques dans les snippets et que le contexte garde les valeurs non sensibles.
- **Verification V1 reproductible.** `package.json` expose `npm run verify:v1` : Prisma validate, Prisma generate, typecheck workspaces, tests workspaces, build workspaces.

**Verification locale effectuee :**

- `npm run typecheck -w @saida/api` : vert.
- `npm run test -w @saida/api -- --test-name-pattern "Attachment|redact"` : vert.
- `npm run verify:v1` : vert (`51` tests API passes, `5` skips attendus ; `7` tests shared passes ; build API/Web/Workers vert).

**Suite logique :**

- Collaboration humaine complete : acceptation invitation par token, revocation/changement de role, viewer read-only cote API/UI.
- E2E avec infra locale complete pour prouver upload chat + agent + apply + multi-collab.

---

### Avancement Codex - Lot 6 (2026-07-05)

**Livre cote plateforme :**

- **Acceptation invitation par token.** `POST /v1/project-invitations/:token/accept` valide le token hash, l'expiration, le compte invite, puis cree/upsert le `ProjectCollaborator` et marque l'invitation `ACCEPTED`.
- **Page invitation web.** Nouvelle route `apps/web/app/saidaengine/invitations/page.tsx`, cible des emails existants `/saidaengine/invitations?token=...`, qui accepte l'invitation puis redirige vers le workspace du projet.
- **Gestion collaborateurs.** Ajout API `PATCH/DELETE /v1/projects/:projectId/collaborators/:userId` pour changer `EDITOR/VIEWER` ou retirer un collaborateur. Le owner est verrouille.
- **Revocation invitation.** Ajout `DELETE /v1/projects/:projectId/invitations/:invitationId` pour passer une invitation `PENDING` en `REVOKED`.
- **UI roles/revocation.** Le dialogue Share affiche les collaborateurs avec role, permet de choisir le role des nouvelles invitations, changer un role, retirer un collaborateur, et revoquer une invitation en attente.
- **Viewer read-only.** Le gateway collaboration autorise maintenant `VIEWER` pour `GET /scene` et `GET /revisions`, mais garde `EDITOR` requis pour WebSocket live-edit et application d'ops. Le client live-edit refuse maintenant une op si le socket n'est pas ouvert/pret, au lieu de laisser une modification locale non durable.

**Verification locale effectuee :**

- `npm run typecheck -w @saida/api` : vert.
- `npm run typecheck -w @saida/web` : vert.
- `npm run test -w @saida/api -- --test-name-pattern "protected|invitation|collaboration|applyExternalOp"` : vert.
- `npm run build -w @saida/web` : vert, route `/saidaengine/invitations` incluse.

**Non encore prouve / suite logique :**

- E2E multi-compte reel : invitation email/console -> acceptation -> ouverture projet -> viewer sans edition -> editor avec edition live.
- Presence live minimale et affichage read-only explicite dans l'UI workspace.

---

### Avancement Codex - Lot 7 UI + E2E (2026-07-05)

**Livre cote plateforme :**

- **Role projet remonte a l'UI.** `GET/PATCH /v1/projects/:projectId/settings` renvoie maintenant le role effectif (`OWNER`, `EDITOR`, `VIEWER`) pour que le workspace puisse decider localement ce qui est editable.
- **Workspace viewer read-only.** Le workspace part en `VIEWER` par defaut pendant le chargement d'un projet persistant, puis active l'edition uniquement si le role settings est `OWNER`/`EDITOR`.
- **Live-edit sans modification fantome.** `useLiveEditSession` et `connectLiveEdit` acceptent un mode `readOnly` : la scene est chargee, le statut passe `ready`, mais `emitOp/commit` renvoient `LIVE_EDIT_READ_ONLY` et aucune op locale optimiste n'est appliquee.
- **Inspector read-only.** `SceneInspectorPanel` affiche les champs de manifeste en lecture seule quand le role ne permet pas l'edition ; rename, transform et property edits ne sont plus rendus en inputs mutables.
- **Chat agent read-only.** Le chat IA affiche l'historique mais desactive prompt, upload de fichiers et apply d'actions pour les viewers, afin de rester coherent avec les protections API `EDITOR`.
- **Settings/share par role.** Les settings sont consultables mais non sauvegardables en viewer. Share affiche les collaborateurs ; invitation/revocation restent reservees aux roles autorises, changement/suppression de role reserve au owner.
- **E2E API multi-compte.** Ajout `apps/api/src/project-collaboration.e2e.test.ts`, skippable par defaut et activable avec `RUN_E2E=1`, qui teste signup owner/viewer, creation projet, invitation viewer, acceptation token, lecture settings/scene, refus `PATCH settings` et refus `ops/dry-run`.
- **Script E2E.** Ajout `npm run test:e2e` a la racine et dans `@saida/api`.

**Verification locale effectuee :**

- `npm run typecheck -w @saida/api` : vert.
- `npm run typecheck -w @saida/web` : vert.
- `npm run test -w @saida/api -- --test-name-pattern "project-collaboration|invited viewer|redactAttachmentSecrets"` : vert (`51` passes, `6` skips dont l'E2E DB-backed non active).
- `docker compose up -d postgres` + `npm run db:migrate:deploy` : Postgres local healthy, migrations appliquees.
- `RUN_E2E=1 npm run test:e2e -w @saida/api` : vert (`1` E2E pass, `0` skip). Scenario couvert : signup owner/viewer, creation projet, acceptation invitation viewer, lecture settings/scene, refus settings patch et ops dry-run.
- `npm run build -w @saida/web` : vert, route `/saidaengine/invitations` incluse. Warnings restants : racine Turbopack inferee et convention Next `middleware` depreciee.

**Suite logique :**

- Ajouter un E2E navigateur a deux sessions pour prouver visuellement : invitation -> ouverture workspace -> viewer read-only -> editor apply agent -> scene visible chez l'autre collaborateur.

---

### Avancement Codex - Lot 8 E2E navigateur + nettoyage (2026-07-05)

**Livre cote plateforme :**

- **E2E navigateur deux sessions.** Ajout `apps/web/e2e/collaboration.spec.ts` et `apps/web/playwright.config.ts` pour demarrer API + Web, creer owner/viewer, accepter une invitation, verifier le workspace viewer en lecture seule, appliquer une action agent cote owner, puis verifier la propagation live dans la session viewer.
- **Bootstrap E2E API/Web.** Ajout `scripts/start-api-e2e.mjs` avec fallback vers `NextEngine/build/bin/saida_tool.exe`, `AGENT_LLM_ENABLED=false`, `EMAIL_DELIVERY_MODE=console` et fallback dev compte desactive.
- **Selectors UI stables.** Ajout de `data-testid` cibles sur auth, hierarchy, settings, chat agent et apply action pour eviter les tests fragiles bases sur du texte traduit.
- **Live-edit read-only receive-only.** Les viewers ouvrent maintenant le socket collaboration pour recevoir les ops, mais le serveur refuse toute op inbound `VIEWER` avec `PROJECT_READ_ONLY`. Le client refuse aussi `emitOp/commit/previewLocal` en read-only.
- **Fallback snapshot UI.** Le workspace garde une representation JS de la scene issue du snapshot serveur et applique les ops distantes dessus, ce qui evite une hierarchy vide quand le runtime WebAssembly/WebGPU refuse momentanement `loadSnapshot/applyOp`.
- **Nettoyage bruit.** Les rapports Playwright et `test-results` sont ignores ; `next-env.d.ts` a ete remis sur le chemin dev apres le build.

**Verification locale effectuee :**

- `npm run typecheck -w @saida/api` : vert.
- `npm run typecheck -w @saida/web` : vert.
- `npm run test -w @saida/api -- --test-name-pattern "collaboration|redact|protected"` : vert (`51` passes, `6` skips attendus).
- `npm run build -w @saida/web` : vert. Warnings restants : racine Turbopack inferee a cause de plusieurs lockfiles, convention Next `middleware` depreciee.
- `npm run test:e2e:web` : harness execute et corrige jusqu'au scenario navigateur ; la derniere relance est bloquee par Docker Desktop/Postgres indisponible (`dockerDesktopLinuxEngine` pipe introuvable), pas par une assertion applicative. Quand Docker et Postgres sont relances, c'est le test a rejouer pour obtenir le vert final navigateur.

**Suite logique :**

- Relancer `docker compose up -d postgres`, `npm run db:migrate:deploy`, puis `npm run test:e2e:web` des que Docker Desktop est de nouveau disponible.
- Apres vert navigateur, faire une passe UX rapide avec screenshots desktop/mobile sur workspace owner/viewer avant mise en production.

---

### Avancement Claude - Session « Correction & cohérence » (2026-07-06)

Ferme quatre points de [AUDIT_WEB_PLATFORM_IMPROVMENTS.md](AUDIT_WEB_PLATFORM_IMPROVMENTS.md)
(tout côté `GitHub/saida`, zéro changement moteur) :

- **Audit 1.2 fermé — divergence de manifest.** Nouveau
  `apps/api/src/web-runtime-contract.ts` : `validateOpForWebClients` (validation
  WASM de forme **+** borne web : `add_behaviour`/`remove_behaviour`/
  `set_behaviour_property` rejetées tant que le runtime web n'a aucun behaviour,
  `create_node.nodeType` gaté sur la palette du manifest) branché sur **tous**
  les chemins d'entrée du journal (hub WS, `/ops`, `/ops/dry-run`, pipeline
  agent). `getWebEngineManifest()` (behaviours vides, ops bornées,
  `webRuntimeBounded:true`) sert le prompt agent et `/v1/engine/manifest`
  par défaut (`?full=1` pour le contrat desktop complet). Constat utile : la
  palette de nœuds native == web (Node/MeshNode/LightNode/ParticleSystem/Water) ;
  la seule divergence réelle était les 14 behaviours. Testé
  (`web-runtime-contract.test.ts`).
- **Audit 2.2 fermé — batchs atomiques.** `CollaborationStore.appendOps()`
  (createMany, all-or-nothing) + `CollaborationHub.applyExternalBatch(projectId,
  ops, author, {expectedRevision})` : sérialisé sur `room.tail`, validation
  complète avant persistance, persistance en une transaction, broadcast en ordre
  de révision, `onCommitted` par révision (le scheduler N-ops reste correct).
  `expectedRevision` (= `baseRevision` du dry-run) ferme le TOCTOU : si la tête
  a bougé depuis le dry-run → `409 REVISION_CONFLICT`, rien d'appliqué,
  retryable. Branché sur `POST /ops` et l'apply d'action agent (l'action reste
  `PROPOSED` sur conflit). 4 nouveaux tests hub.
- **Audit 2.6/5.2 fermé — mirror JS structurel.** La logique du mirror est
  extraite dans `packages/shared/src/scene-mirror.ts` (aligné 1-E « types dans
  packages/shared ») et gère maintenant `create_node`/`delete_node`/
  `reparent_node` (payloads/défauts alignés sur `SaidaOpApplier.cpp`, mêmes
  guards anti-cycle). La hiérarchie React reste juste en multi-client même
  quand le runtime WASM est indisponible. 7 tests (`scene-mirror.test.ts`) ;
  `use-live-edit-session.ts` ré-exporte depuis `@saida/shared`.
- **Audit 4.2 fermé — état d'init persisté.** `Project.creationMode`
  (enum GUIDED/MANUAL), `designSkillId`, `initializedAt` (migration
  `20260706120000_project_initialization`, SQL canonique `prisma migrate diff`).
  Renseignés à la création (`POST /v1/projects`), exposés en lecture par
  GET/PATCH settings. Le setup ne se rouvre plus jamais avec un `projectId`
  (`?setup=1` ignoré) ; le mode/skill affichés par l'éditeur viennent des
  settings persistés (fallback URL juste après création).
- **Audit 4.7 (bonus) — backdoor dev isolée.** `?projectId=test` ne force plus
  OWNER qu'en build development (`isDevSandboxProject`, `projects.ts`).

**Vérification :** `npm run verify:v1` exit 0 (API 66 tests dont 60 pass /
6 skips attendus sans binaire natif ni DB ; shared 14/14 ; typecheck + build
API/Web/Workers verts). **Non prouvé :** migration non appliquée (Docker/Postgres
down pendant la session → `npm run db:migrate:deploy` au prochain démarrage) ;
E2E navigateur multi-onglets toujours à rejouer avec l'infra locale.

---

### Avancement Claude - Session « Track 1-F : refs d'assets » (2026-07-06)

**Le bloquant produit n°1 de l'audit (1.1) est levé côté moteur + runtime web :
le snapshot headless transporte maintenant les refs mesh/matériau, le fold
serveur les préserve, et le runtime web les résout en vraies ressources.**

Côté moteur (`NextEngine`) :

- **`MeshNode.durableResourceRefs`** — nouveau membre data-only (string JSON) :
  le sous-ensemble sérialisé `{mesh, texture, baseColor, metallic, roughness,
  ao, emissive, shader, lods}` est capturé tel quel par les deux chemins de
  désérialisation (`captureDurableResourceRefs`, appelé par le chemin complet
  et le chemin headless) et ré-émis par la sérialisation headless. Aucune
  ressource GPU n'est chargée en headless.
- **`SceneSnapshot.cpp`** — la sérialisation headless émet aussi les flags de
  rendu MeshNode (`castShadows`, `meshEnabled`, `outline*`) depuis le node, et
  la désérialisation les restaure. Round-trip byte-stable testé
  (`testMeshResourceRefsRoundTrip` dans `saida_authoring_op_tests`), y compris
  refs survivant à une op puis re-sérialisation. Fold réel vérifié avec le
  binaire : `saida_tool apply-ops` sur une scène à refs → refs intactes, ops
  appliquées. CTests 10/10.
- **Runtime web (`web/runtime/main.cpp`)** — `AssetRegistry` monté sur
  `/project` (lit `asset_registry.json` si présent) + `chdir("/project")` ;
  `resolveMeshRef`/`resolveAssetRef` résolvent les refs (AssetID numérique ou
  chemin relatif, "cube" builtin) via ResourceManager ; **garde d'existence de
  fichier avant `Mesh::fromObjFile`** (qui throw → abort wasm sinon) ; texture
  manquante → placeholder magenta (comportement rhi existant) ; mesh manquant →
  cube placeholder + entrée `missingAssets` dans le résultat de
  `saida_load_snapshot` (jamais silencieux). `shader:"unlit"` respecté.
  L'export runtime ajoute `FS` (`EXPORTED_RUNTIME_METHODS`) pour que la
  plateforme écrive les fichiers projet dans MEMFS avant `loadSnapshot`.

**Vérifié E2E navigateur sur cette machine (première fois)** : boot BeachDemo
(47 meshes, rendu correct), parité S4 `saida_scene_snapshot_compare` **verte**
(17/17, zéro diff), `saida_load_snapshot` avec un vrai `.obj` écrit en MEMFS →
mesh réel rendu, ref manquante → `missingAssets:["mesh:models/absent.obj"]`,
op `set_transform` appliquée post-load. Artefacts resynchronisés dans
`saida/apps/web/public/engine/dev/` (`npm run engine:sync` avec
`SAIDA_ENGINE_BUILD_DIR`).

Corrections d'environnement/toolchain au passage (cette machine) :

- emsdk installé (`~/emsdk`, latest) + rustup/naga-cli (`~/.cargo/bin`) pour la
  chaîne WGSL ; `build-web` reproductible ici.
- `web/runtime/CMakeLists.txt` : options de link **quotées** (checkout sous un
  chemin avec espace : « Projets Dev ») ; **`-sALLOW_MEMORY_GROWTH` remplacé
  par `-sINITIAL_MEMORY=512MB`** — les Chrome récents rejettent les vues sur un
  ArrayBuffer resizable dans `writeTexture` (« must not be resizable ») et le
  port emdawnwebgpu bundlé ne fait pas la copie → l'init GPU mourait en
  unhandledrejection silencieuse. À réévaluer quand le port sera corrigé.
- `web/runtime/shell.html` : hook `error`/`unhandledrejection` → statut visible
  (fini le canvas noir sans diagnostic) ; commentaire contenant un tag script
  littéral corrigé (il cassait le parseur HTML du shell généré).

**Limites connues / handoff Codex (plateforme)** :

- Livraison des assets au navigateur : écrire les `ProjectFile` (et
  `asset_registry.json` si ids numériques) dans MEMFS via `Module.FS` sous
  `/project/…` avant `runtime.loadSnapshot()` ; surfacer `missingAssets` dans
  l'UI (badge/console) ; invalider/re-loader quand un asset arrive.
- Formats : `.obj` + textures stb (png/jpg…) seulement — pas de GLTF dans le
  wasm (hors périmètre, taille) ; un `.obj` corrompu (présent mais invalide)
  fait encore aborter le wasm (exceptions off) — à durcir si besoin.
- `saida_scene_snapshot` (mirror UI) écrit des AssetID de session pour les
  meshes chargés par chemin : ne jamais persister ce snapshot côté serveur (la
  vérité durable reste le journal d'ops + fold `saida_tool`).
- `create_node` MeshNode n'a pas encore d'op pour assigner un mesh (`set_mesh`
  / `set_material` restent à cadrer dans le contrat d'ops).

---

### Avancement Claude - Session « Hygiène audit : 3.5 + 2.5 » (2026-07-06)

Suite du nettoyage [AUDIT_WEB_PLATFORM_IMPROVMENTS.md](AUDIT_WEB_PLATFORM_IMPROVMENTS.md)
(tout côté `GitHub/saida`, zéro moteur). D'abord un re-constat du code réel : les
points **1.1, 1.2, 2.2, 2.6/5.2, 4.2** sont déjà fermés par les sessions
précédentes, et **3.3** (fuite de contexte inter-conversations) et **5.3**
(statut `"error"` émis) le sont aussi désormais dans le code — l'audit du 07-05
est sur ces points périmé. Deux smells restants du périmètre Claude fermés :

- **Audit 3.5 fermé — troncature du contexte de scène.** L'agent injectait
  `JSON.stringify(sceneContext).slice(0, 24_000)` : une coupe au caractère qui
  peut produire du JSON invalide dans le prompt. Remplacé par
  `boundCompactScene(root, maxNodes)` (`agent-ops.ts`) : élagage en profondeur
  sur un **budget de nœuds** (`config.agent.sceneMaxNodes`, défaut 400,
  `AGENT_LLM_SCENE_MAX_NODES`), racine toujours conservée, drapeau `truncated`
  passé au modèle (« only the first N nodes are shown »). Le contexte est
  désormais **toujours du JSON valide** et borné. 3 tests
  (`agent-ops.test.ts`).
- **Audit 2.5 fermé — `lastRevisionAck` n'est plus un champ mort.** La connexion
  WS (`makeConnection`, `collaboration-ws.ts`) suit la **révision maximale
  réellement délivrée** (welcome/op/ack) ; à la fermeture du socket, la
  `CollaborationSession` est mise à jour avec `lastRevisionAck =
  connection.deliveredRevision` + `lastSeenAt`, au lieu de figer la valeur
  `since` du join. Zéro changement de schéma ni de protocole client, aucune
  écriture DB par op (une seule à la fermeture). Le serveur connaît exactement
  jusqu'où chaque session a été informée — pas besoin d'un ack client.

**Vérification :** `npm run typecheck` + `npm run test` + `npm run build` verts
(API 69 tests : 63 pass / 6 skips attendus sans binaire natif ; shared 14/14).

**Restants de l'audit — hors périmètre Claude (lane Codex cloud/prod/billing) :**
`3.2` (run agent en worker Temporal), `2.1` (multi-gateway : tête de révision en
RAM → Redis/pub-sub), `2.4` (GC du journal d'ops). Laissés en handoff Codex.

---

### Avancement Claude - Session « Audit 3.1 : crédits IA branchés » (2026-07-06)

**Le risque business n°1 de l'audit (3.1 🔴, « coût IA non borné ») est fermé :
chaque tour d'agent réserve des crédits avant tout appel LLM, les capture dès
que l'appel aboutit, les libère sinon.** Même cycle de vie
reserve/capture/release que ToolRun, sous le même verrou advisory
par organisation (`pg_advisory_xact_lock`) — zéro double-comptabilité.

- **Schéma** (migration `20260706150000_agent_credit_reservations`, SQL
  canonique `prisma migrate diff`) : `CreditReservation.toolRunId` devient
  optionnel + `agentRunId String? @unique` (une réservation débite exactement
  un sujet facturable : ToolRun **ou** AiAgentRun) ;
  `CreditLedger.agentRunId` + index (piste d'audit symétrique) ;
  `AiAgentRun.creditsCharged Int?` (compteur métier, exposé automatiquement
  par le payload conversation).
- **`apps/api/src/agent-credits.ts`** : `reserveAgentCredits` (disponible =
  ledger − réservations ACTIVE, calculé sous verrou org →
  `INSUFFICIENT_CREDITS` sinon), `captureAgentCredits` (CAPTURED + entrée
  ledger `DEBIT` négative avec `agentRunId`/model/tokens en métadonnées +
  `creditsCharged` tamponné sur le run ; **no-op si la réservation n'est plus
  ACTIVE** → jamais de double débit), `releaseAgentCredits` (updateMany
  conditionnel atomique). Surface DB structurelle injectable (patron DI de
  `llm.ts`) → la logique monétaire est **testée unitairement sans Postgres**
  (6 tests, `agent-credits.test.ts`). `db-locks.ts` généralisé en type
  structurel au passage (aucun changement d'appelants).
- **Câblage `runAgentTurn`** (`project-agent.ts`) : réservation **avant tout
  travail** (insuffisant → `failRun AGENT_INSUFFICIENT_CREDITS`, message clair
  en conversation, **aucun appel LLM** — le kill-switch demandé) ; capture
  juste après `completeAgentChat` (le fournisseur a facturé même si la sortie
  échoue ensuite extraction/validation) ; release en `finally` si le tour
  meurt avant/pendant l'appel (une release qui échoue laisse la réservation
  ACTIVE → balayée EXPIRED par le sweep admin existant après 24 h, vérifié
  sujet-agnostique). Coût par tour : `config.agent.turnCreditCost`
  (`AGENT_TURN_CREDIT_COST`, défaut 1 ; 0 = désactivé pour le dev local).
  `.env.example` documenté.

**Vérification (infra locale démarrée cette session)** : `prisma validate` +
typecheck + build verts ; **migrations appliquées sur Postgres réel**
(`db:migrate:deploy` : `project_initialization` en attente de la session
précédente **+** `agent_credit_reservations`) ; tests API 75 : 74 pass avec
`SAIDA_TOOL_PATH` (le seul skip restant est l'e2e DB gaté `RUN_E2E=1`,
**relancé avec `RUN_E2E=1` : vert lui aussi**) ; shared 14/14.

Correction de dette au passage : en mode `RUN_E2E=1`, le runner de tests ne
terminait jamais (aucun résumé imprimé) — le client Prisma partagé gardait son
pool de connexions ouvert. `after(() => prisma.$disconnect())` ajouté dans
`project-collaboration.e2e.test.ts` ; jamais vu en CI car ce chemin n'y est
jamais exercé (constat déjà pointé par l'audit 1.3).

**Hors périmètre volontaire (suite logique, lane Codex)** : UI « solde de
crédits / recharger » dans l'éditeur ; coût par tour fonction des tokens réels
(aujourd'hui forfait fixe par tour — le forfait rend le risque borné, le
raffinement tarifaire est une décision produit) ; 3.2 (worker Temporal).

---

### Avancement Claude - Session « Audit 2.1 + 3.4 + 5.1 + trouvailles » (2026-07-06)

Suite et fin du passage d'audit côté Claude. Trois points fermés + deux bugs
trouvés en chemin :

- **Audit 2.1 durci — collision de révision ≠ op perdue.** La contrainte
  unique `(projectId, revision)` du journal devient l'arbitre réel : quand un
  `appendOp`/`appendOps` échoue, le hub **re-synchronise sa tête depuis le
  store** et rejoue une fois à la révision fraîche au lieu de dropper l'op
  (écrivain concurrent : 2ᵉ instance, worker, insert externe). Pour les batchs
  sous `expectedRevision`, la collision rend le même `REVISION_CONFLICT`
  retryable que le gate d'entrée — jamais d'apply sur un état non prouvé.
  Un échec de persistance réel (store injoignable, tête inchangée) rejette
  comme avant. 3 tests hub. **Le plafond multi-gateway reste documenté** (pas
  de broadcast cross-instance — Redis pub/sub = lane Codex cloud/prod).
- **Audit 3.4 fermé — le choix de modèle IA n'est plus cosmétique.**
  `AGENT_LLM_MODEL_MAP` (« Label=model-id,… ») mappe les labels persistés
  `Project.aiModel` vers de vrais identifiants ; `runAgentTurn` résout le
  modèle du projet et le passe à `completeAgentChat({model})` (override
  par appel, défaut `AGENT_LLM_MODEL` sinon). Le run et les métadonnées du
  débit de crédits enregistrent le modèle réellement appelé. Labels non
  mappés → défaut (dégradation douce). 3 tests (`config.test.ts` + llm).
  La liste UI codée en dur reste à brancher côté Codex.
- **Audit 5.1 fermé — sémantique d'inverse du geste continu.** Le client
  live-edit tient une **fenêtre de manipulation** : le premier `previewLocal`
  depuis le dernier commit capture l'inverse qui restaure l'état
  *d'avant-manipulation* ; `commit`/`emitOp` la consomment → un rejet serveur
  annule tout le drag, pas la dernière frame prévisualisée. Nouveau
  `cancelPreview()` (Échap pendant un drag : restaure localement sans rien
  envoyer) — l'API continue begin implicite/commit/cancel est prête pour les
  gizmos Codex (D6/D9).
- **Trouvaille 1 — le runner de tests ne terminait jamais avec l'infra up.**
  `redis.ts` ouvre un socket brut jamais `unref()` : tout process ayant touché
  le rate-limiter (signup → e2e) restait vivant pour toujours. `unref()` posé
  (en prod, c'est le listener Fastify qui possède la boucle). Avec le
  `prisma.$disconnect()` en `after()` de l'e2e : `RUN_E2E=1` termine
  proprement, exit 0.
- **Trouvaille 2 (session précédente, confirmée utile)** : `db-locks.ts`
  généralisé en type structurel — les verrous advisory sont maintenant
  testables sans Postgres.

**Vérification — première suite 100 % verte de l'histoire du repo :**
`RUN_E2E=1` + `SAIDA_TOOL_PATH` + infra Docker up → **API 81/81, 0 skip** ;
shared 14/14 ; typecheck + build verts. (Avant cette session : 6 skips
structurels jamais exercés en CI.)

**Reste ouvert de l'audit (lane Codex, infra/UI)** : 3.2 (worker Temporal),
2.1-complet (pub/sub multi-gateway), 2.3 (présence Yjs C6), 2.4 (GC du journal
— conflit assumé avec l'historique de révisions D8, décision produit), 4.x
(façades UI : Save, gizmos, Drive, premier prompt guidé, réglages graphiques),
5.4 (runtime singleton — limite structurelle documentée).
