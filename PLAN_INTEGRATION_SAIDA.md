# Saida — Plan d'intégration & backlog production

> **Plan d'intégration historique, resynchronisé le 2026-07-15.** Les travaux
> des 3-8 juillet ont livré beaucoup de fondations, mais plusieurs éléments
> marqués « fermés/prouvés » ne résistent pas aux scénarios de crash ou aux
> allers-retours cross-runtime. La source de vérité pour le go/no-go est
> l'[audit avant production](https://github.com/saias-o/saida/blob/main/AUDIT_BEFORE_PROD.md).
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
  describe-engine · validate-* · apply-ops · export-game (desktop/web)
```

## 4. Fondations implémentées et limites actuelles (2026-07-15)

### Moteur (`NextEngine`)

- Le vrai Renderer WASM/WebGPU, l'authoring-core, `saida_tool`, les bindings
  `applyOp/manifest/snapshot/loadSnapshot/pick` et le contrat SaidaOp existent.
- Les SaidaOps sont partagées en code, mais **le contrat effectif ne l'est pas** :
  le manifest API est dérivé du natif, l'authoring web enregistre moins de
  types, le loader web a encore d'autres fallbacks et le headless ne reconstruit
  pas tous les behaviours/nœuds.
- `SceneSnapshot` peut transformer des types en `Node` ou ignorer un behaviour.
  `MeshNode` exige un `ResourceManager` que le fold n'a pas. Le mode
  `--skip-invalid` rend alors la perte silencieuse ; il ne doit pas être utilisé
  pour un snapshot durable.
- Les tests/validations BeachDemo et fold documentés restent des preuves
  historiques ciblées, pas un round-trip sémantique de tous les types.
- Les nœuds sont encore adressés par nom mutable, pas par NodeId stable.

### Plateforme — backend (`apps/api`)

- Auth, projets, fichiers, invitations, plans, Stripe, ledger/réservations,
  uploads, ToolRuns, agent, collaboration et admin read-only ont des routes et
  modèles réels.
- Ce n'est pas « auth complète » : un EDITOR peut inviter OWNER, la suppression
  omet les nouvelles tables et Stripe, `disabledAt` ne coupe pas une session
  existante et l'admin dépend d'une allowlist email sans MFA.
- Le hub persiste avant ack, utilise des révisions et des batchs avec
  `expectedRevision`, ce qui est une bonne base. Mais `lastRevisionAck` mesure
  l'envoi socket, pas l'ack client ; clientOpId n'est pas persisté ; WS n'a pas
  de dry-run d'état, rate limit, backpressure ou révocation live ; les rooms ne
  sont pas évincées.
- L'agent valide/propose/applique des SaidaOps et réserve des crédits, mais ses
  transitions DB/Temporal/LLM/OperationLog ne sont pas une saga atomique. Un
  crash peut facturer sans proposition, appliquer sans marquer l'action ou
  exécuter après expiration de la réservation.
- Stripe est signé et journalisé, mais `PROCESSING` peut rester bloqué et
  l'ordre des webhooks n'est pas réconcilié.
- Les uploads ne sont pas « contrôlés » au sens production : PUT réutilisable
  après confirm, checksum non recalculé et quotas concurrents.

### Plateforme — frontend (`apps/web`)

- Viewport réel, hiérarchie, inspecteur scalaire/bool/enum, picking, settings,
  Save, modèle IA, chat proposition/apply, viewer et multi-client existent.
- Move/Rotate restent des modes sans vrais gizmos ; scripts/UI/assets/builds et
  undo/redo complet ne sont pas livrés.
- Le bootstrap/replay n'est pas fiable : un `loadSnapshot` ou `applyOp` en échec
  peut quand même avancer la révision. Les pending ops ne sont pas réconciliées
  après ack perdu.
- Le timer de reconnexion n'est pas annulé par `close()` et peut rouvrir une
  ancienne session sur le runtime singleton après navigation de projet.
- Les réglages FPS/ombres et downloads existent ; `renderScale` n'a pas de
  chemin runtime complet et le player marketplace reste absent.
- Le polling agent peut rester bloqué après une erreur transitoire.

### Qualité / vérification

- Des suites API/shared/C++ et un scénario Playwright collaboration existent.
- Les résultats datés de juillet sont des exécutions historiques et n'ont pas
  été rejoués lors de la synchronisation documentaire.
- La CI Saida peut sauter l'E2E moteur/web quand `ENGINE_REPO_TOKEN` manque tout
  en restant verte ; le workflow web NextEngine est séparé et build-oriented.
- Aucun gate ne couvre encore le round-trip exhaustif, les fenêtres de crash,
  Stripe désordonné, suppression complète, upload immuable, vrai GPU ou package
  marketplace.

---

## 5. RESTE À FAIRE — checklist de mise en production

> **P0 = tout ce qui sépare l'état actuel d'une prod publique.** La checklist
> historique ci-dessous n'est plus exhaustive ; plusieurs éléments de §5bis
> bloquent eux aussi la promesse de « SaaS complet ». Utiliser l'audit Saida
> comme gate de release.

### P0.1 — Binaire `saida_tool` déployable (API **et** worker agent)

Dépendance runtime dure : `GET /scene` (reconstruction), snapshots périodiques,
dry-run agent, `POST /scene/snapshot`. Un build Linux existe désormais ; le
blocage actuel est la livraison pinée et la parité de contrat, pas l'absence de
compilation Linux.

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
- [ ] **Packaging reproductible** : les Dockerfiles existent, mais la gate
      n'est pas fermée. `apps/web/Dockerfile` exige `engine-web/index.wasm`
      gitignoré que Render ne télécharge pas ; API/workers consomment par défaut
      `saida_tool:latest`; images root/tsx et absence de release manifest.
      Historique de l'implémentation initiale : `apps/api/Dockerfile` et
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
      **Limite actuelle :** le LRU est borné en nombre d'entrées, pas en octets ;
      scènes volumineuses et time-travel peuvent donc épuiser la mémoire.
- [x] **Dégradation gracieuse** (fermé 07-07) : `reconstructSceneResilient`
      (`scene-cache.ts`) — si le fold échoue (binaire absent/mort), sert le
      **dernier snapshot persisté** (`latestSnapshot`/`snapshotAtOrBefore`) avec
      sa révision + `degraded: true` au lieu d'un 500 ; le client reconnecte le
      WS `?since=révision` et rejoue les ops manquantes (self-heal). 500 seulement
      s'il n'existe aucun snapshot de repli.
      **Limite actuelle :** ce fallback ne protège pas d'un snapshot déjà
      appauvri par le fold et le client peut avancer malgré un échec d'apply ;
      « self-heal » n'est donc pas encore une garantie.

### P0.2 — CI qui prouve la recette verte (2 repos)

> Workflows écrits et résultats historiques consignés le 07-08. Le problème
> actuel est que `ENGINE_REPO_TOKEN` reste conditionnel : sans lui, la CI Saida
> dégrade en unit-tests + warning au lieu d'échouer. Une release ne doit jamais
> être verte sans ses tests/artefacts moteur obligatoires.

- [x] **NextEngine** — **run vert 07-08**. Workflow `ci.yml` : job `linux-tool`
      (conteneur bookworm, même glibc que les images plateforme) — build complet
      + `ctest` complet + **fold byte-identique** au fixture Windows versionné
      (`tests/fixtures/fold-determinism/`) + artefacts `saida-tool-linux` et
      `engine-manifest`. Workflow **séparé** `web-artifacts.yml` (isolé pour ne
      pas gater le binaire) : emsdk 6.0.1 + naga + glslc, `lfs: true` (asset
      BeachDemo préembarqué), scripts `web/*.sh` en +x — artefacts
      `engine-web-runtime` et `saida-authoring-wasm`. **Vert.**
- [ ] **saida** job `verify` comme gate obligatoire. Un run historique vert est
      documenté, mais le workflow peut dégrader en unit tests si
      `ENGINE_REPO_TOKEN` manque. Exiger l'artefact moteur piné et faire échouer
      la CI s'il ne peut pas être téléchargé. Historique : infra par le docker-compose du
      repo (postgres, redis, minio, temporal), artefact `saida-tool-linux`
      téléchargé depuis la CI moteur (+ `libvulkan1/libglfw3` runtime installés
      sur le runner), `db:migrate:deploy` → `RUN_E2E=1 npm run test` vert →
      typecheck + build. Secret `ENGINE_REPO_TOKEN` configuré côté saida.
- [ ] Le smoke Temporal existe dans `verify`, mais il doit devenir obligatoire
      et être complété par kill/retry/reconciliation. Historique : worker démarré en
      arrière-plan (`npx tsx apps/workers/src/worker.ts &`) — vert (prouve
      queue → worker → pipeline → DB).
- [ ] **saida** job `web-e2e` — Playwright chromium a un résultat local
      historique, mais il peut être sauté en CI et ne couvre qu'un scénario
      étroit. Il doit devenir un gate obligatoire. Historique :
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
      fichier projet depuis le panneau assets. Le run doit utiliser le vrai
      backend GPU, jamais le stub JSON.
- [ ] **Marketplace** : d'abord implémenter l'attachement build depuis l'UI, un
      package web multi-fichiers immuable, un player isolé/sandboxé, scan et
      modération ; ensuite tester draft → review/publish → play → rollback.

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

- [ ] **Topologie déployable** : `render.yaml` décrit web + **API 1
      instance** (WS in-process, pas d'autoscale horizontal — verticale seulement)
      + workers + Postgres/Redis/R2/Temporal, mais le web manque l'artefact
      moteur, le worker réel 2D→3D demande Windows/GPU, les migrations sont
      manuelles et le healthcheck masque les dépendances en échec.
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
- [ ] **Env web** : `NEXT_PUBLIC_API_URL` (build-time) et bundle `engine-web`
      téléchargé par SHA/hash avant `engine:sync` ; le checkout Render seul ne
      contient pas ces fichiers.
- [ ] **Base** : `npm run db:migrate:deploy` au déploiement + migration/référence
      plans-only. Ne pas utiliser le seed actuel en prod (compte dev + faux jeux) ;
      stratégie de migration documentée (rollback = restore).
- [ ] **Stripe live** : endpoint webhook enregistré sur le domaine API, secret
      en env, prix live créés, un checkout de bout en bout vérifié.
- [ ] **`NODE_ENV=production` vérifié** : `assertProductionEnv` doit refuser
      explicitement stub 2D→3D, `ALLOW_DEV_ACCOUNT_FALLBACK=true`, placeholders,
      `latest`, prix manquants et contrats moteur divergents ; sandbox dev
      inerte, admin debug 404.

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
      réservations ACTIVE > 24 h tourne bien en prod. Le sweep actuel ne suffit
      pas : il libère la réservation sans terminer/annuler le run ou workflow,
      ce qui peut autoriser un calcul tardif gratuit. Rendre expiration,
      cancellation et transition du run atomiques.

## 5bis. Backlog produit et gates de promesse

Ces éléments ne sont pas tous des bloqueurs de sécurité, mais ils bloquent une
sortie présentée comme **SaaS complet** lorsqu'ils correspondent à une promesse
publique (éditeur complet, queue paid, scripts/UI, export, marketplace).

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
| **Coût par tour fonction des tokens réels** | Aujourd'hui forfait fixe (`AGENT_TURN_CREDIT_COST`) sans `max_tokens` strict ni budget fournisseur démontré. Le risque n'est pas suffisamment borné : plafonner la sortie/coût et mesurer avant de décider le modèle tarifaire. |

### P3 — long terme

| Quoi | Détail |
|---|---|
| **Phase F — export serveur** | Workflow Temporal `web-export` depuis snapshot immuable, template WASM pinné par `engineVersion`, workers isolés bornés, artefacts R2/S3 + checksum, crédits sur build, migration inter-versions par snapshot (F6, invariant 0.6, jamais prouvée). |
| **GC du journal d'ops (2.4)** | Le journal accumule les no-ops droppées au fold (design C7). Conflit assumé avec l'historique/time-travel D8 : décision produit de rétention avant toute purge. |
| **Runtime web multi-instance (5.4)** | Un seul runtime/canvas par page (`-sMODULARIZE` absent) ; échec d'init = reload complet. Limite structurelle documentée. |
| **Durcissements runtime web** | `.obj` corrompu (présent mais invalide) aborte encore le wasm (exceptions off) ; MSAA web ; fetch/IDBFS streaming d'assets ; réévaluer `-sINITIAL_MEMORY=512MB` quand le port emdawnwebgpu acceptera les ArrayBuffers resizables ; `renderScale` (resize des render targets web — la surface est figée 1280×720, le slider est masqué dans l'UI en attendant). |
| **Réflexion complète au web** | Le manifest API est dérivé du natif puis filtré, tandis que le bundle web enregistre un autre sous-ensemble. La divergence n'est pas fermée : générer le contrat depuis le bundle livré et couvrir tous les types annoncés par round-trip. |

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
- Architecture animation : [CLAUDE.md](CLAUDE.md) (Étape 10 — implémentation large, gates produit restantes)
- Architecture plateforme : `GitHub/saida/docs/architecture.md`
- Runtime web dans la plateforme : `GitHub/saida/docs/engine-web-runtime.md`
- Audit UI moteur (RmlUi, distinct) : [AUDIT_UI_WEB_V1.md](AUDIT_UI_WEB_V1.md)
