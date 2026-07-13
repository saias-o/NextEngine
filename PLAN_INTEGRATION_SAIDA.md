# Saida — Plan d'intégration & backlog production

> **Document unique de pilotage** (fusion de l'ancien plan et de l'audit
> plateforme du 2026-07-05, points fermés retirés). Mis à jour : **2026-07-07**.
>
> Deux repos : **`NextEngine`** (moteur C++/Vulkan+WebGPU, nom final *Saida
> Engine*) et **`GitHub/saida`** (plateforme web : `apps/web` Next.js,
> `apps/api` Fastify, `apps/workers` Temporal, `packages/db` Prisma).
>
> Périmètre : un seul intervenant sur tout — moteur C++/WASM, backend
> collaboration, client live-edit + contrat d'ops, UI React, infra web, CLI,
> cloud/prod, billing. Aucune répartition du travail.
>
> Historique détaillé des sessions (2026-07-03 → 07-06) : historique git de ce
> fichier. Audit distinct du système UI moteur (RmlUi, Étape 12) :
> [AUDIT_UI_WEB_V1.md](AUDIT_UI_WEB_V1.md).

---

## 1. Objectif produit

Depuis un navigateur, un créateur peut :

1. **modifier des scènes** SaidaEngine (hiérarchie, transforms, propriétés,
   behaviours, signaux) ;
2. **visualiser le rendu réel du moteur** en direct (pas une approximation) ;
3. **vibecoder** : une IA produit ces modifications, revues/validées avant
   application.

**Principe directeur : la plateforme n'invente pas un second moteur, elle
orchestre le vrai.** Un seul renderer (runtime WASM/WebGPU), une seule vérité
d'édition (journal de SaidaOps validées → snapshots `.saidaproj`), une seule
surface d'autorité (EngineManifest), un seul code d'authoring partagé
desktop/headless/web.

## 2. Invariants (règles dures — priment en cas d'ambiguïté)

1. Un seul renderer de scène = runtime SaidaEngine WASM/WebGPU.
2. L'authoring-core est compilé **dans** le runtime d'édition web (et dans le
   gateway, en WASM headless, pour la validation).
3. Une seule représentation durable de scène (`SceneSerializer`/`.saidaproj`).
4. Manipulations continues = optimiste local + **un** commit SaidaOp au
   relâchement.
5. Sur le web, vibecoder = SaidaOp + JS QuickJS + UI RmlUi, **jamais** du C++.
6. Le pont inter-versions du moteur est le **snapshot**, pas l'op-log.
7. La Collaboration Gateway est reconstructible depuis Postgres (zéro état
   critique en mémoire ; la contrainte unique du journal est l'arbitre réel des
   révisions).

Modes produit d'initialisation (verrouillés) : **Manuel** (scène vide, IA
copilote libre) et **Guidé par skill** (le skill lance le premier prompt de
pipeline dans le chat). L'écran de choix n'apparaît qu'à la première création ;
le mode/skill sont des métadonnées persistées du projet. Un skill ne contourne
jamais les invariants : les mutations durables restent des SaidaOps validées.

## 3. Architecture cible

```text
Navigateur
  Next.js UI (hierarchy, inspector, assets, console, chat)
  Runtime SaidaEngine WASM/WebGPU  ── applyOp(SaidaOp) ─► mute la scène vivante
  Yjs (présence, curseurs, texte scripts/UI)          [pas encore commencé]
        │  SaidaOp proposée
        ▼
Fastify API (auth / billing / projects / assets / jobs)
        ├─► Postgres  (vérité : ops, revisions, credits, audit)
        ├─► Redis     (rate limit ; à venir : présence, pub/sub multi-gateway)
        ├─► R2/S3     (blobs : assets, snapshots, builds)
        └─► Temporal  (jobs longs : img-to-3d, tours d'agent IA ; à venir : exports)
        │
        ▼
Collaboration Gateway (WebSocket, in-process API)
  authentifie → valide (WASM authoring-core) → append OperationLog
  → revision++ → broadcast → snapshot périodique (fold natif saida_tool)
        │
        ▼
Saida Headless Tools (saida_tool)
  describe-engine · validate-* · apply-ops · (à venir : export-web)
```

## 4. Ce qui est FAIT et prouvé (état 2026-07-07)

### Moteur (`NextEngine`)

- **Export web WASM/WebGPU livré** : le vrai `Renderer` dans le navigateur
  (ombres, DDGI, eau, particules, AO, bloom), parité desktop validée sur
  BeachDemo, ~170-213 Ko brotli.
- **Authoring-core extrait** (`src/authoring/`) : `SaidaOp` versionnées et
  inversibles, `EngineManifest` déterministe/hashable, validation stricte de
  forme, applier atomique. Compilé dans : l'éditeur desktop, `saida_tool`, le
  runtime web, le WASM de validation du gateway.
- **`saida_tool` headless** : `describe-engine`, `validate-ops/scene/script/
  scenario`, `apply-ops` (atomique, `--skip-invalid` pour le fold tolérant,
  snapshot byte-reproductible). CTests verts.
- **Runtime web d'édition** (`web/runtime/main.cpp`) : `saida_apply_op`,
  `saida_engine_manifest`, `saida_scene_snapshot`, `saida_load_snapshot`
  (charge un doc `/scene`), `saida_pick` (ray-AABB). **Refs mesh/matériau
  transportées** par le snapshot headless (round-trip byte-stable), résolues
  via AssetRegistry/MEMFS `/project`, placeholders + `missingAssets` jamais
  silencieux. Vérifié E2E navigateur (BeachDemo 47 meshes, parité S4 17/17).
- **Contrat d'ops actuel** (validés forme WASM, inversibles, foldables) :
  `set_transform`, `create_node`, `delete_node`, `rename_node`,
  `reparent_node`, `set_property`, `set_scene_setting`, `add/remove_behaviour`,
  `set_behaviour_property`, `add/remove_signal_connection`. Les nœuds sont
  référencés par **nom** (migration NodeId = décision ouverte).

### Plateforme — backend (`apps/api`)

- **Auth complète**, plans/abonnements Stripe (webhooks idempotents), credit
  ledger + réservations (ToolRun **et** tours d'agent), admin read-only,
  pipeline img-to-3d (Temporal), rôles projet owner/editor/viewer cohérents
  API+WS+UI, invitations/partage, `ProjectFile` versionnés, uploads contrôlés.
- **Collaboration** : hub à révisions strictement monotones (sérialisation par
  projet, persist-avant-ack), re-sync + retry sur collision de révision (op
  jamais perdue sur écrivain concurrent), validation des ops via **WASM
  in-process du vrai contrat C++**, snapshots périodiques par fold natif,
  reconstruction complète depuis Postgres, dry-run atomique, time-travel
  lecture (`?at=N`), batchs atomiques avec `expectedRevision` (TOCTOU fermé),
  `lastRevisionAck` réel.
- **Agent IA** : pipeline complet prompt (manifest **borné web** + scène
  compacte **bornée en nœuds**, JSON toujours valide) → extraction → validation
  WASM → dry-run → proposition → apply atomique. **Crédits réservés avant tout
  appel LLM, capturés à la réponse, libérés sinon** (kill-switch
  `AGENT_INSUFFICIENT_CREDITS`). Contexte scopé par conversation. Modèle par
  projet via `AGENT_LLM_MODEL_MAP`. Pièces jointes lues bornées, secrets
  masqués, contexte balisé non-fiable.

### Plateforme — frontend (`apps/web`)

- Éditeur web : viewport moteur réel, hiérarchie + inspecteur éditable
  (scalaires/bool/enum) → SaidaOps, picking sélection, chat agent avec
  proposition/apply, mode viewer read-only de bout en bout, multi-client (ops
  distantes + mirror JS structurel `create/delete/reparent` dans
  `packages/shared/scene-mirror`), setup Guidé/Manuel persisté
  (`creationMode`/`designSkillId`/`initializedAt`, ne se rouvre jamais).
- Client live-edit (`live-edit-client.ts`) : bootstrap `/scene` → WS
  `?since=rev`, optimiste + rollback correct (fenêtre de manipulation continue :
  le rollback restaure l'état d'avant-drag ; `cancelPreview()` pour Échap),
  reconnexion backoff, statuts fiables.
- **Façades UI fermées (07-07)** : réglages graphiques réellement appliqués au
  runtime (`saida_set_render_settings` : cap FPS par frame-skip rAF, toggle
  ombres Renderer ; `renderScale` masqué — pas de chemin de resize web, cf.
  P3) ; labels de modèles IA servis par l'API (`availableAiModels` dans
  GET/PATCH settings, validation dynamique depuis `AGENT_LLM_MODEL_MAP`) ;
  Save câblé (POST `/scene/snapshot`, reason `MANUAL`, feedback + hint
  auto-save) ; onglet Drive mock supprimé ; lignes d'assets = download signé
  (`/files/:id/download`) ; download d'output de job réel
  (`/v1/tool-runs/:id/download`) ; « download soon » retiré, « play coming
  soon » dégradé en note. **Premier prompt Guidé (D0)** : à la création d'un
  projet GUIDED, le ChatPanel envoie le prompt pipeline du skill (une seule
  tentative, garde skill inconnu) — l'agent ouvre la conversation.
- **Bindings runtime web ajoutés (07-07)** : `saida_set_render_settings`,
  `saida_camera_state` (base caméra pour les gizmos). WASM rebuildé + syncé
  (contrat d'ops inchangé → pas de rebuild du wasm de validation gateway).
- **Agent en worker Temporal (P0, fermé 07-07)** : `runAgentTurn` découpé en
  `prepareAgentTurn` (requête API : message user + attachments + run `QUEUED` +
  réservation de crédits — le 402 kill-switch reste synchrone) et
  `executeAgentTurn` (LLM → extraction → validation WASM → dry-run natif →
  `PROPOSED/FAILED`, capture/release des crédits). Claim atomique
  `QUEUED→RUNNING` = LLM at-most-once (un retry sur run `RUNNING` échoue en
  `AGENT_TURN_INTERRUPTED` au lieu de re-payer). `AGENT_RUNNER=temporal` →
  `AgentTurnWorkflow` sur la queue dédiée `agent-turn` (réponse 202, UI en
  polling du run) ; défaut `inline` = dev sans worker. Le worker partage le
  pipeline exact via l'export workspace `@saida/api/agent-runtime` (init WASM +
  folders saida_tool — `SAIDA_TOOL_PATH` requis sur l'hôte worker). Smoke test
  reproductible : `npx tsx apps/api/scripts/smoke-agent-temporal.ts`.

### Qualité / vérification

- **Suite 100 % verte** possible localement : `npm run infra:up` +
  `RUN_E2E=1 SAIDA_TOOL_PATH=<NextEngine/build/bin/saida_tool.exe> npm run test`
  → **API 93/93, 0 skip ; shared 14/14**. Typecheck + build verts.
- Smoke test du chemin Temporal agent : `npx tsx
  apps/api/scripts/smoke-agent-temporal.ts` (202 → QUEUED → worker →
  FAILED/AGENT_LLM_NOT_CONFIGURED vérifié le 07-07).
- E2E navigateur Playwright 2 sessions écrit (`apps/web/e2e`), harness prêt.
- CI GitHub sur les deux repos (mais elle n'exerce **ni** le binaire natif
  **ni** l'e2e DB — gap listé en P0.2).

---

## 5. RESTE À FAIRE — checklist de mise en production

> **P0 = tout ce qui sépare l'état actuel d'une prod publique.** Chaque case
> est vérifiable. P1/P2/P3 (§5bis) sont du produit — rien n'y bloque la prod.

### P0.1 — Binaire `saida_tool` déployable (API **et** worker agent)

Dépendance runtime dure : `GET /scene` (reconstruction), snapshots périodiques,
dry-run agent, `POST /scene/snapshot`. Aujourd'hui buildé **Windows/MSYS2
uniquement**.

- [x] **Build Linux de `saida_tool`** (fermé 07-08) : `docker/saida-tool.Dockerfile`
      — base **Debian bookworm exprès** (même glibc 2.36 que `node:24-slim` ;
      un build sur distro plus récente exige GLIBC_2.38+ et casse au
      `COPY --from`, vérifié). Deps apt : libvulkan-dev/libglfw3-dev/libglm-dev/
      glslc (Vulkan requis au link seulement — headless au run, aucun GPU).
      XR/MCP OFF (winsock). 2 fixes de portabilité seulement : `<cstring>`
      manquant (ResourceManager.cpp), disambiguïsateur `template` pour GCC 12
      (SceneTree.hpp). Prouvé : **ctest 24/24 sur Linux** (GCC 12 bookworm et
      GCC 13 ubuntu) + **apply-ops byte-identique Windows↔Linux** sur le fixture
      versionné `tests/fixtures/fold-determinism/` (sha256 d2205858…).
- [x] **Packaging** (fermé 07-08) : `apps/api/Dockerfile` et
      `apps/workers/Dockerfile` (monorepo via tsx + `COPY --from` du binaire →
      `SAIDA_TOOL_PATH=/usr/local/bin/saida_tool` fixé + vendor WASM commité +
      prisma generate) ; `apps/web/Dockerfile` : `npm run engine:sync` **dans**
      le build (artefacts moteur en entrée obligatoire via `engine-web/`,
      fail-fast sinon ; `next start 0.0.0.0`). Smokes passés : API `/health`
      200 avec checks db/temporal/s3 verts contre l'infra compose, web sert la
      page et `/engine/dev/index.wasm` en 200.
- [x] **Cache de scène reconstruite** (fermé 07-07) : `apps/api/src/scene-cache.ts`
      — LRU borné keyé par (projectId, sceneId, révision résolue). Le head se
      résout via un `headRevision` (aggregate cheap) au lieu d'un `execFile` +
      fichiers temp ; les entrées sont immuables par construction (journal
      append-only) donc jamais périmées, et le hook `onCommitted` du hub évince
      les entrées head du projet à chaque commit pour borner la mémoire. Le
      time-travel `?at=N` est caché quand `N ≤ head` (immuable). Câblé dans
      `reconstructSceneResilient` (route `GET /scene`).
- [x] **Dégradation gracieuse** (fermé 07-07) : `reconstructSceneResilient`
      (`scene-cache.ts`) — si le fold échoue (binaire absent/mort), sert le
      **dernier snapshot persisté** (`latestSnapshot`/`snapshotAtOrBefore`) avec
      sa révision + `degraded: true` au lieu d'un 500 ; le client reconnecte le
      WS `?since=révision` et rejoue les ops manquantes (self-heal). 500 seulement
      s'il n'existe aucun snapshot de repli.

### P0.2 — CI qui prouve la recette verte (2 repos)

> Workflows **écrits le 07-08** (YAML validés) — reste à prouver un run vert
> sur GitHub, et à créer le secret `ENGINE_REPO_TOKEN` (PAT lecture
> contents+actions sur NextEngine) dans les settings du repo saida pour les
> artefacts cross-repo. Sans lui, la CI saida dégrade en unit-tests + warning.

- [x] **NextEngine** — **run vert 07-08**. Workflow `ci.yml` : job `linux-tool`
      (conteneur bookworm, même glibc que les images plateforme) — build complet
      + `ctest` complet + **fold byte-identique** au fixture Windows versionné
      (`tests/fixtures/fold-determinism/`) + artefacts `saida-tool-linux` et
      `engine-manifest`. Workflow **séparé** `web-artifacts.yml` (isolé pour ne
      pas gater le binaire) : emsdk 6.0.1 + naga + glslc, `lfs: true` (asset
      BeachDemo préembarqué), scripts `web/*.sh` en +x — artefacts
      `engine-web-runtime` et `saida-authoring-wasm`. **Vert.**
- [x] **saida** job `verify` — **VERT 07-08**. Infra par le docker-compose du
      repo (postgres, redis, minio, temporal), artefact `saida-tool-linux`
      téléchargé depuis la CI moteur (+ `libvulkan1/libglfw3` runtime installés
      sur le runner), `db:migrate:deploy` → `RUN_E2E=1 npm run test` vert →
      typecheck + build. Secret `ENGINE_REPO_TOKEN` configuré côté saida.
- [x] Le smoke Temporal tourne dans `verify` avec le worker démarré en
      arrière-plan (`npx tsx apps/workers/src/worker.ts &`) — vert (prouve
      queue → worker → pipeline → DB).
- [x] **saida** job `web-e2e` — Playwright chromium, **vert 07-08 en local**
      (WebGPU headless OK en CI). L'assertion rouge (`collaboration.spec.ts:225`,
      rename non propagé au viewer) était un **vrai bug backend** : le handler WS
      `/collab` autorisait avec le rôle minimum par défaut (EDITOR), donc les
      collaborateurs **VIEWER étaient rejetés en 4403** et ne recevaient jamais
      les broadcasts. Fix : autoriser au minimum VIEWER (les writes restent gatés
      par-message → `PROJECT_READ_ONLY`). Diagnostic par instrumentation pas-à-pas
      (mirror OK, op reçue par l'owner mais WS viewer fermée 4403). Test vert,
      API 93/93 inchangée.

### P0.3 — Passe de test manuelle pré-prod (avec vraie clé LLM)

À dérouler une fois sur un environnement de staging identique à la prod :

- [ ] `npm run test:e2e:web` vert en local, puis captures desktop **et**
      mobile, parcours owner **et** viewer.
- [ ] **Éditeur** : création Manuel + Guidé (le premier prompt du skill part
      seul et l'agent répond), chat → proposition → apply visible dans le
      viewport des 2 clients, inspecteur/hiérarchie/picking, Save (point de
      contrôle + « déjà à jour »), settings FPS/ombres agissent sur le rendu,
      changement de modèle IA pris en compte (`AGENT_LLM_MODEL_MAP`).
- [ ] **Agent via Temporal** : `AGENT_RUNNER=temporal` en staging, message →
      202 → polling UI → proposition ; kill du worker en plein run → run
      `AGENT_TURN_INTERRUPTED`, crédits rendus, l'UI ne reste pas bloquée.
- [ ] **Crédits** : org à 0 crédit → réponse synchrone
      `AGENT_INSUFFICIENT_CREDITS` ; ledger équilibré après capture/release.
- [ ] **Comptes** : signup → email de vérification reçu (SMTP réel), reset de
      mot de passe, invitation collaborateur par email, rôles
      owner/editor/viewer respectés (WS compris).
- [ ] **Billing Stripe (mode test)** : checkout abonnement → webhook →
      crédits ; portal ; downgrade en fin d'abonnement.
- [ ] **Jobs** : un run 2Dto3D complet → download de l'output ; download d'un
      fichier projet depuis le panneau assets.
- [ ] **Marketplace** : publier un jeu (draft → publish), page publique.

### P0.4 — Environnement & déploiement (à mettre en place)

**Décisions prises (07-08)** : **Render** (calcul web/api/workers + Postgres +
Redis managés, autoscale web/workers, API épinglée 1 instance), **Cloudflare R2**
(stockage objet, coût linéaire au Go, zéro egress), **Temporal Cloud** (workflows
managés). Artefacts livrés dans le repo saida : `render.yaml` (blueprint),
`.env.production.example`, `docs/deployment.md` (runbook non-technique pas-à-pas).
Support **mTLS Temporal ajouté au code** (api `temporal.ts` + workers `worker.ts`,
via `TEMPORAL_TLS_CERT_PATH/KEY_PATH` + `TEMPORAL_NAMESPACE`) — requis par Temporal
Cloud, non-breaking en local. Image `saida-tool` publiée sur **GHCR** par la CI
moteur (job `publish-tool-image`), consommée au build des images api/workers
(build-arg `SAIDA_TOOL_IMAGE` par défaut `ghcr.io/saias-o/saida-tool:latest`).
Reste à faire **par le propriétaire** (comptes + secrets, cf. runbook) puis
dérouler :

- [x] **Topologie** définie dans `render.yaml` : web (autoscale) + **API 1
      instance** (WS in-process, pas d'autoscale horizontal — verticale seulement)
      + workers (autoscale) + Postgres managé + Redis managé + R2 + Temporal Cloud.
- [ ] **DNS + TLS** : domaine web, domaine API (CORS `WEB_ORIGIN` exact),
      WebSocket `wss://` de bout en bout (proxy avec upgrade + timeouts longs).
- [ ] **Env API** (checklist `.env` prod) : `NODE_ENV=production`,
      `API_HOST/API_PORT`, `WEB_ORIGIN`, `DATABASE_URL`, `REDIS_URL`,
      `S3_ENDPOINT/REGION/KEYS/BUCKETS` (buckets prod créés, pas les noms
      `-local`), `TEMPORAL_ADDRESS`, `SAIDA_TOOL_PATH`,
      `EMAIL_DELIVERY_MODE=smtp` + `SMTP_*` + `EMAIL_FROM`, `ADMIN_EMAILS`,
      `STRIPE_SECRET_KEY` (clé **restreinte** live) + `STRIPE_WEBHOOK_SECRET` +
      `STRIPE_PRICE_*` + labels, `AGENT_LLM_ENABLED=true` +
      `AGENT_RUNNER=temporal` + `AGENT_LLM_BASE_URL/API_KEY/MODEL/MODEL_MAP` +
      `AGENT_TURN_CREDIT_COST>0`, **pas** de `ALLOW_DEV_ACCOUNT_FALLBACK`.
- [ ] **Env worker** : `DATABASE_URL`, `TEMPORAL_ADDRESS`, S3, `SAIDA_TOOL_PATH`,
      mêmes `AGENT_LLM_*` que l'API (le LLM est appelé **depuis le worker**).
- [ ] **Env web** : `NEXT_PUBLIC_API_URL` (build-time), `engine:sync` dans le
      build.
- [ ] **Base** : `npm run db:migrate:deploy` au déploiement + seed des plans ;
      stratégie de migration documentée (rollback = restore).
- [ ] **Stripe live** : endpoint webhook enregistré sur le domaine API, secret
      en env, prix live créés, un checkout de bout en bout vérifié.
- [ ] **`NODE_ENV=production` vérifié** : `assertProductionEnv` passe, sandbox
      dev (`?projectId=test`) inerte, admin debug 404.

### P0.5 — Phase G minimale : observabilité & résilience

- [ ] **G1 quotas/rate-limits** : au-delà de l'existant (agent 20/min/user),
      borner par IP/user : auth (login/signup/reset), uploads (presign + PUT),
      `POST /ops` et WS ops/s, invitations. Vérifier que tout passe par Redis
      (multi-process worker/API).
- [ ] **G2 backups** : Postgres quotidien + rétention, versioning/replication
      S3 ; **un restore complet testé une fois** (DB + un asset) sur un env
      vierge, procédure écrite.
- [ ] **G3 logs/métriques** : logs structurés déjà là (pino) → agrégation
      centralisée ; métriques minimales : runs agent par statut, durée de tour,
      coût crédits/jour, jobs 2Dto3D par statut, profondeur des queues
      Temporal, erreurs 5xx, latence `GET /scene`.
- [ ] **G4 alertes** : échec de workflow répété, queue Temporal qui monte,
      coût IA journalier anormal, somme du ledger ≠ attendu (réservations
      ACTIVE > 24 h), disque/DB pleins, certificat TLS expirant.
- [ ] **Sweep des réservations** : vérifier que le job d'expiration des
      réservations ACTIVE > 24 h tourne bien en prod (il est la garantie de
      non-fuite des crédits en cas de crash).

## 5bis. Backlog produit (ne bloque pas la prod)

### P1 — la boucle d'édition complète

| Quoi | Détail |
|---|---|
| **Gizmos move/rotate (D6/D9)** | Les outils Move/Rotate de la toolbar ne font rien. Débloqué le 07-07 : le runtime web expose `saida_camera_state` (position/base caméra, fov, aspect — typé côté client `EngineRuntime.cameraState()`). Reste : convertir le drag viewport en `ops.setTransform` via `previewLocal` (drag) → `commit` (relâchement) → `cancelPreview` (Échap). |
| **Rollback de revision qui mute le head (D8)** | La lecture time-travel existe (`?at=N`) ; reste l'op de rollback live (ré-appliquer un état passé comme nouvelles révisions, sans réécrire le journal). |
| **Broadcast live des settings (D10)** | Les changements de settings ne sont pas poussés aux clients connectés. |
| **Pub/sub multi-gateway (2.1 complet)** | Une instance API unique est l'hypothèse actuelle (documentée, collision de révision durcie). Pour scaler : fan-out Redis pub/sub entre instances. |
| **Présence/curseurs Yjs (C6) + texte collaboratif (D7)** | Zéro Yjs dans le repo. Gros morceau : décision produit sur la portée (curseurs seulement ? édition scripts/UI ?) avant d'attaquer. |

### P2 — vibecoding avancé

| Quoi | Détail |
|---|---|
| **`write_script` / `write_ui`** | Bloqués par le stockage durable des fichiers projet dans le fold (`saida_tool apply-ops` n'a pas de filesystem projet). Design retenu : ops `{path, code}` via `resolveSandboxedProjectPath` (B5), fichiers matérialisés depuis `ProjectFile`/R2-S3 au fold. Débloque aussi la fin de l'extraction A3 (scripts). |
| **`set_mesh` / `set_material`** | `create_node` MeshNode n'a pas d'op pour assigner un mesh/matériau. À cadrer avec l'import d'assets web. |
| **Registre DesignSkill (E0)** | 4 skills codés en dur dans l'UI. Modèle DB/API (`DesignSkill` : version, auteur, visibilité, prompt pipeline), skill officiel par défaut, marketplace communautaire. |
| **Gate post-apply bloquante (E4)** | Brancher `validate-scene`/`validate-ops` en vérification bloquante après l'apply d'une action agent (les briques headless existent). |
| **Coût par tour fonction des tokens réels** | décision produit — aujourd'hui forfait fixe (`AGENT_TURN_CREDIT_COST`) — le risque est borné ; le raffinement tarifaire est un choix business, pas une dette. |

### P3 — long terme

| Quoi | Détail |
|---|---|
| **Phase F — export serveur** | Workflow Temporal `web-export` depuis snapshot immuable, template WASM pinné par `engineVersion`, workers isolés bornés, artefacts R2/S3 + checksum, crédits sur build, migration inter-versions par snapshot (F6, invariant 0.6, jamais prouvée). |
| **GC du journal d'ops (2.4)** | Le journal accumule les no-ops droppées au fold (design C7). Conflit assumé avec l'historique/time-travel D8 : décision produit de rétention avant toute purge. |
| **Runtime web multi-instance (5.4)** | Un seul runtime/canvas par page (`-sMODULARIZE` absent) ; échec d'init = reload complet. Limite structurelle documentée. |
| **Durcissements runtime web** | `.obj` corrompu (présent mais invalide) aborte encore le wasm (exceptions off) ; MSAA web ; fetch/IDBFS streaming d'assets ; réévaluer `-sINITIAL_MEMORY=512MB` quand le port emdawnwebgpu acceptera les ArrayBuffers resizables ; `renderScale` (resize des render targets web — la surface est figée 1280×720, le slider est masqué dans l'UI en attendant). |
| **Réflexion complète au web** | Le manifest web n'expose aucun behaviour (l'API borne le contrat agent en conséquence — divergence fermée). La parité de réflexion (behaviours au web) reviendra avec son coût taille quand le produit en aura besoin. |

## 6. Décisions ouvertes

- **NodeId vs nom** : toutes les ops référencent les nœuds par nom (résolution
  `findByName` côté applier). Migrer vers `NodeId` (id/génération) = changement
  de contrat à planifier.
- ~~**Hébergement**~~ **TRANCHÉ (07-08)** : Render (calcul + Postgres + Redis
  managés, autoscale) + Cloudflare R2 (stockage) — critères : managé, coût
  proportionnel à l'usage, zéro machine à gérer. Voir `render.yaml` +
  `docs/deployment.md`.
- ~~**Temporal en prod**~~ **TRANCHÉ (07-08)** : Temporal Cloud (managé) ;
  support mTLS ajouté au code.
- **Périmètre `write_script`** : jusqu'où l'IA génère du JS QuickJS en Phase E.
- **Format durable d'un design skill** communautaire (métadonnées, prompt,
  étapes, permissions, versioning, publication).
- **Import d'asset en édition web** : MEMFS (actuel) vs fetch/IDBFS streaming.
- **Rétention du journal d'ops** vs time-travel illimité (cf. P3/GC).

## 7. Recettes de vérification

**Plateforme (`GitHub/saida`)** :

```sh
npm run infra:up                      # Docker : postgres, redis, minio, temporal
npm run db:migrate:deploy
RUN_E2E=1 SAIDA_TOOL_PATH="C:\\Users\\evand\\Documents\\NextEngine\\build\\bin\\saida_tool.exe" \
  npm run test                        # attendu : API 93/93 0 skip, shared 14/14
npm run verify:v1                     # validate + typecheck + test + build
npm run test:e2e:web                  # Playwright 2 sessions (navigateur)
# Chemin Temporal de l'agent (worker démarré : npm run dev -w @saida/workers) :
npx tsx apps/api/scripts/smoke-agent-temporal.ts
```

**Moteur desktop (`NextEngine`, MSYS2 ucrt64)** :

```sh
export PATH="/c/msys64/ucrt64/bin:$PATH"   # sinon cc1plus crashe silencieusement
cmake --build build && ctest --test-dir build
# Ne jamais lancer l'exe (boucle GUI bloquante) — vérifier via CTest/saida_tool.
```

**Moteur web** (après tout changement du contrat d'ops, rebuild **les deux**
wasm) :

```sh
source /c/Users/evand/emsdk/emsdk_env.sh
emcmake cmake -S web/runtime -B build-web -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-web
cd ../GitHub/saida && npm run engine:sync      # → apps/web/public/engine/dev/
# WASM de validation du gateway :
#   build-authoring-wasm puis npm run authoring:sync (SAIDA_TOOL_PATH défini
#   pour régénérer engine-manifest.json)
```

## 8. Références

- Contrat d'authoring moteur : [CLAUDE.md](CLAUDE.md) (« Comment coder un jeu »)
- Architecture animation : [CLAUDE.md](CLAUDE.md) (Étape 10 — système livré)
- Architecture plateforme : `GitHub/saida/docs/architecture.md`
- Runtime web dans la plateforme : `GitHub/saida/docs/engine-web-runtime.md`
- Audit UI moteur (RmlUi, distinct) : [AUDIT_UI_WEB_V1.md](AUDIT_UI_WEB_V1.md)
