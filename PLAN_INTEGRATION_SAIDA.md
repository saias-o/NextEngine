# Saida — Plan d'intégration & backlog production

> **Document unique de pilotage** (fusion de l'ancien plan et de l'audit
> plateforme du 2026-07-05, points fermés retirés). Mis à jour : **2026-07-06**.
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
        └─► Temporal  (jobs longs : img-to-3d ; à venir : agent IA, exports)
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

## 4. Ce qui est FAIT et prouvé (état 2026-07-06)

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

### Qualité / vérification

- **Suite 100 % verte** possible localement : `npm run infra:up` +
  `RUN_E2E=1 SAIDA_TOOL_PATH=<NextEngine/build/bin/saida_tool.exe> npm run test`
  → **API 81/81, 0 skip ; shared 14/14**. Typecheck + build verts.
- E2E navigateur Playwright 2 sessions écrit (`apps/web/e2e`), harness prêt.
- CI GitHub sur les deux repos (mais elle n'exerce **ni** le binaire natif
  **ni** l'e2e DB — gap listé en P0).

---

## 5. RESTE À FAIRE — backlog priorisé

### P0 — bloquants avant production publique

| Quoi | Lane | Détail |
|---|---|---|
| **Agent en worker Temporal** | Codex (infra) | Le run d'agent tourne dans la requête HTTP (LLM 45 s + subprocess) : pas de file, pas de retry, pas d'isolation. Patron 2Dto3D à répliquer ; l'UI passe en polling du run (statuts `RUNNING/PROPOSED/FAILED` déjà persistés). Le pipeline est déjà LLM-agnostique et découplé de Fastify. |
| **`saida_tool` déployable Linux + hors chemin chaud** | Codex (cloud) + Claude (build moteur si besoin) | Dépendance runtime dure de l'API, buildée Windows/MSYS2 uniquement. Un déploiement Linux exige un build Linux. Perf : un `execFile` + fichiers temp par `GET /scene` (chemin d'ouverture) → cache de scène reconstruite ; `/scene` doit dégrader en dernier snapshot au lieu d'un 500 si le binaire meurt. |
| **CI qui exerce binaire + DB + e2e** | Codex | La CI actuelle laisse 6 tests structurellement non exercés (`SAIDA_TOOL_PATH`, `RUN_E2E=1`). La recette 100 % verte existe — la mettre en CI. |
| **E2E navigateur 2 sessions : vert final + passe UX** | Codex | Infra Docker up désormais ; rejouer `npm run test:e2e:web`, puis screenshots desktop/mobile owner/viewer avant prod. |
| **Phase G minimale** | Codex (cloud) | G1 quotas/rate-limits étendus, G2 backups Postgres+R2 avec **restore testé une fois**, G3 logs structurés/métriques (jobs, crédits, coût IA), G4 alertes (échecs builds, queue bloquée, coût IA anormal, ledger déséquilibré). |

### P1 — la boucle d'édition complète (produit)

| Quoi | Lane | Détail |
|---|---|---|
| **Gizmos move/rotate (D6/D9)** | Codex (UI) | Les outils Move/Rotate de la toolbar ne font rien. L'API client est prête et correcte : `previewLocal` (drag) → `commit` (relâchement, rollback = état d'avant-drag) → `cancelPreview` (Échap). Convertir le drag viewport en `ops.setTransform`. |
| **Façades UI : câbler ou masquer** | Codex (UI) | Save sans handler (l'auto-snapshot existe mais l'utilisateur ne le sait pas) ; onglet Drive = données mock ; réglages graphiques persistés mais **inertes** (`maxFps`/`renderScale`/`shadowsEnabled` jamais transmis au runtime) ; liste de modèles IA codée en dur (le backend `AGENT_LLM_MODEL_MAP` est prêt — servir les labels disponibles) ; lignes d'assets non cliquables ; « download soon »/« play coming soon » marketing. Chaque bouton doit faire quelque chose ou disparaître. |
| **Premier prompt Guidé (D0)** | Codex (UI→API) | En mode Guidé, le skill n'affiche qu'un texte statique : rien n'appelle l'agent au démarrage. Envoyer le premier message de pipeline serveur à l'initialisation. |
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
| **Durcissements runtime web** | `.obj` corrompu (présent mais invalide) aborte encore le wasm (exceptions off) ; MSAA web ; fetch/IDBFS streaming d'assets ; réévaluer `-sINITIAL_MEMORY=512MB` quand le port emdawnwebgpu acceptera les ArrayBuffers resizables. |
| **Réflexion complète au web** | Le manifest web n'expose aucun behaviour (l'API borne le contrat agent en conséquence — divergence fermée). La parité de réflexion (behaviours au web) reviendra avec son coût taille quand le produit en aura besoin. |

## 6. Décisions ouvertes

- **NodeId vs nom** : toutes les ops référencent les nœuds par nom (résolution
  `findByName` côté applier). Migrer vers `NodeId` (id/génération) = changement
  de contrat à planifier.
- **Hébergement** : Coolify/Kamal + Hetzner/OVH + Cloudflare R2 (proposé) — à
  confirmer avant la Phase F.
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
  npm run test                        # attendu : API 81/81 0 skip, shared 14/14
npm run verify:v1                     # validate + typecheck + test + build
npm run test:e2e:web                  # Playwright 2 sessions (navigateur)
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
