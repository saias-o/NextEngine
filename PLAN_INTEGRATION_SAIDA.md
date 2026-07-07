# Saida — Plan d'intégration & backlog production

> **Document unique de pilotage** (fusion de l'ancien plan et de l'audit
> plateforme du 2026-07-05, points fermés retirés). Mis à jour : **2026-07-07**.
>
> Deux repos : **`NextEngine`** (moteur C++/Vulkan+WebGPU, nom final *Saida
> Engine*) et **`GitHub/saida`** (plateforme web : `apps/web` Next.js,
> `apps/api` Fastify, `apps/workers` Temporal, `packages/db` Prisma).
>
> Répartition de travail : **Claude** = code complexe cross-brique (moteur
> C++/WASM ↔ backend collaboration ↔ client live-edit + contrat d'ops).
> **Codex** = UI React, infra web, CLI, cloud/prod, billing UI.
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
  → **API 84/84, 0 skip ; shared 14/14**. Typecheck + build verts.
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

- [ ] **Build Linux de `saida_tool`** (cible CMake `saida_tool` seule ; deps
      vendorées, pas de Vulkan requis pour l'outil headless). Vérifier :
      `ctest` vert sur Linux + un `apply-ops` byte-identique à un snapshot de
      référence généré sous Windows.
- [ ] **Packaging** : images Docker `apps/api` et `apps/workers` embarquant le
      binaire + `apps/api/vendor/saida-authoring/` (wasm + manifest), avec
      `SAIDA_TOOL_PATH` pointé dedans. Le wasm runtime web
      (`public/engine/dev/`) est **gitignoré** : `npm run engine:sync` doit
      faire partie du build de l'image web (artefact moteur en entrée).
- [ ] **Cache de scène reconstruite** : `GET /scene` fait aujourd'hui un
      `execFile` + fichiers temp à chaque ouverture. Cacher le doc reconstruit
      par (projectId, sceneId, révision) et l'invalider à chaque op commitée.
- [ ] **Dégradation gracieuse** : si le fold échoue (binaire absent/mort),
      `GET /scene` doit servir le **dernier snapshot persisté** (avec sa
      révision) au lieu d'un 500 — l'éditeur s'ouvre, les ops continuent.

### P0.2 — CI qui prouve la recette verte (2 repos)

- [ ] **NextEngine** : job Linux qui build `saida_tool` + lance `ctest`, et
      publie en artefacts versionnés : le binaire Linux, le wasm runtime web,
      le wasm authoring + `engine-manifest.json`.
- [ ] **saida** : job avec services (postgres, redis, minio, temporal) qui
      récupère l'artefact `saida_tool` Linux, puis exécute
      `db:migrate:deploy` → `RUN_E2E=1 SAIDA_TOOL_PATH=… npm run test`
      (attendu : **API 84/84, 0 skip ; shared 14/14**) → `npm run verify:v1`.
- [ ] **saida** : job Playwright `npm run test:e2e:web` (2 sessions, navigateur
      réel) dans la même CI.
- [ ] Le smoke Temporal (`apps/api/scripts/smoke-agent-temporal.ts`) tourne en
      CI avec le worker démarré (prouve queue → worker → pipeline → DB).

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

Décisions à confirmer d'abord (cf. §6) : hébergeur (Coolify/Kamal +
Hetzner/OVH proposés), stockage S3 (R2 proposé), domaines. Ensuite :

- [ ] **Topologie** : 1 conteneur web (Next), **1 seule instance API**
      (contrainte assumée : gateway WS in-process, pas de pub/sub multi-gateway
      — ne pas mettre l'API derrière un autoscaler), 1+ worker Temporal,
      Postgres managé ou backupé, Redis, S3, Temporal (self-host
      docker-compose ou Temporal Cloud — à décider).
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

| Quoi | Lane | Détail |
|---|---|---|
| **Gizmos move/rotate (D6/D9)** | Claude (UI) | Les outils Move/Rotate de la toolbar ne font rien. Débloqué le 07-07 : le runtime web expose `saida_camera_state` (position/base caméra, fov, aspect — typé côté client `EngineRuntime.cameraState()`). Reste : convertir le drag viewport en `ops.setTransform` via `previewLocal` (drag) → `commit` (relâchement) → `cancelPreview` (Échap). |
| **Rollback de revision qui mute le head (D8)** | Claude (backend) | La lecture time-travel existe (`?at=N`) ; reste l'op de rollback live (ré-appliquer un état passé comme nouvelles révisions, sans réécrire le journal). |
| **Broadcast live des settings (D10)** | Codex | Les changements de settings ne sont pas poussés aux clients connectés. |
| **Pub/sub multi-gateway (2.1 complet)** | Codex (cloud) | Une instance API unique est l'hypothèse actuelle (documentée, collision de révision durcie). Pour scaler : fan-out Redis pub/sub entre instances. |
| **Présence/curseurs Yjs (C6) + texte collaboratif (D7)** | à cadrer | Zéro Yjs dans le repo. Gros morceau : décision produit sur la portée (curseurs seulement ? édition scripts/UI ?) avant d'attaquer. |

### P2 — vibecoding avancé

| Quoi | Lane | Détail |
|---|---|---|
| **`write_script` / `write_ui`** | Claude (contrat) + Codex (stockage) | Bloqués par le stockage durable des fichiers projet dans le fold (`saida_tool apply-ops` n'a pas de filesystem projet). Design retenu : ops `{path, code}` via `resolveSandboxedProjectPath` (B5), fichiers matérialisés depuis `ProjectFile`/R2-S3 au fold. Débloque aussi la fin de l'extraction A3 (scripts). |
| **`set_mesh` / `set_material`** | Claude (contrat) | `create_node` MeshNode n'a pas d'op pour assigner un mesh/matériau. À cadrer avec l'import d'assets web. |
| **Registre DesignSkill (E0)** | Codex | 4 skills codés en dur dans l'UI. Modèle DB/API (`DesignSkill` : version, auteur, visibilité, prompt pipeline), skill officiel par défaut, marketplace communautaire. |
| **Gate post-apply bloquante (E4)** | Claude | Brancher `validate-scene`/`validate-ops` en vérification bloquante après l'apply d'une action agent (les briques headless existent). |
| **Coût par tour fonction des tokens réels** | décision produit | Aujourd'hui forfait fixe (`AGENT_TURN_CREDIT_COST`) — le risque est borné ; le raffinement tarifaire est un choix business, pas une dette. |

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
- **Hébergement** : Coolify/Kamal + Hetzner/OVH + Cloudflare R2 (proposé) — à
  confirmer **avant P0.4** (c'est le premier prérequis de la mise en prod).
- **Temporal en prod** : self-host (docker-compose durci) vs Temporal Cloud.
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
  npm run test                        # attendu : API 84/84 0 skip, shared 14/14
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

- Mandat d'architecture : [ARCHITECTURE_PRODUCTION_CLAUDE.md](ARCHITECTURE_PRODUCTION_CLAUDE.md)
- Spike live-edit (historique) : [PLAN_LIVE_EDIT_WEB.md](PLAN_LIVE_EDIT_WEB.md)
- Export web moteur : [PLAN_WEB_EXPORT.md](PLAN_WEB_EXPORT.md)
- Contrat d'authoring moteur : [CLAUDE.md](CLAUDE.md) (« Comment coder un jeu »)
- Architecture plateforme : `GitHub/saida/docs/architecture.md`
- Runtime web dans la plateforme : `GitHub/saida/docs/engine-web-runtime.md`
- Audit UI moteur (RmlUi, distinct) : [AUDIT_UI_WEB_V1.md](AUDIT_UI_WEB_V1.md)
