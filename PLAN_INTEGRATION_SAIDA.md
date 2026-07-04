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
