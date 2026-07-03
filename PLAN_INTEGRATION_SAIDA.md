# Plan d'intégration — SaidaEngine dans la plateforme Saida

> **Document maître de planification.** Consolide la vision, les invariants, l'état
> prouvé et la feuille de route pour intégrer **SaidaEngine** (moteur C++/WebGPU,
> repo `NextEngine` → nom final *Saida Engine*) **dans la plateforme web Saida**
> (`GitHub/saida`), afin d'obtenir un **éditeur web pour modifier des scènes,
> visualiser et vibecoder**.
>
> Date : 2026-07-03. Statut global : **spike live-edit web A.5 validé (S0→S4 vert)**,
> construction de la collaboration et de l'éditeur à venir.
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

Deux modes produit sont a prevoir, meme si ce n'est pas prioritaire pour la
premiere implementation technique :

- **Mode classique** : l'utilisateur part d'une page ou d'un projet vierge et
  demande librement a l'IA de "faire ca ou ca". L'IA agit comme un copilote
  generaliste, propose des SaidaOps, des scripts JS, de l'UI RmlUi, puis passe
  par validation/diff avant application.
- **Mode guide par skill** : l'utilisateur choisit un skill de guidage qui
  structure la creation du jeu. Un skill peut proposer un parcours one-shot
  ("cree-moi un petit jeu complet"), un parcours en preproduction d'abord
  (GDD, intentions, contraintes, style), puis une creation etape par etape des
  niveaux, personnages, UI, gameplay, tests, publication, etc.

Ces skills de guidage doivent etre communautaires : n'importe quel createur
pourra publier un skill qui decrit sa facon de fabriquer un jeu ou un genre de
jeu. La plateforme devra aussi proposer un **skill par defaut officiel**, pense
pour aider les artistes a creer facilement un jeu sans devoir connaitre le
moteur en profondeur.

Important : un skill de guidage ne contourne pas les invariants techniques. Il
oriente la conversation, le decoupage et les validations, mais les mutations
durables restent des SaidaOps validees, des scripts JS QuickJS ou de l'UI RmlUi.

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

### 5.2 Plateforme (`GitHub/saida`)

- Monorepo npm : `apps/web` (Next.js), `apps/api` (Fastify), `apps/workers`,
  `packages/db` (Prisma), `packages/shared`.
- Infra locale Docker : Postgres, Redis, Temporal, MinIO.
- Auth complète, plans/abonnements, **credit ledger**, réservations, Stripe
  (Checkout, Portal, webhooks idempotents, validés en test), admin read-only.
- Pipeline jobs 2Dto3D (Temporal + worker), entitlements de plan appliqués API.
- SaidaEngine Online : surface statique seulement — l'éditeur reste à construire.

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
  appliquées sans re-export ;
- doc plateforme : `GitHub/saida/docs/engine-web-runtime.md` (contraintes :
  WebGPU requis, pas de COOP/COEP tant que pas de pthreads, 1 runtime par
  chargement de page tant que le build n'est pas `-sMODULARIZE`).

Prochain verrou côté plateforme : D2 (charger un ProjectSnapshot réel au lieu
de BeachDemo), qui dépend des Phases A/B côté moteur.

---

## 6. Feuille de route par phase

Statuts : ✅ fait · 🔵 en cours · ⬜ à faire.
Dépôt : **E** = moteur (`NextEngine`) · **P** = plateforme (`GitHub/saida`).

### Phase A — Contrat moteur (E) 🔵 (A1 ✅)

| # | Tâche | Dépend | Critère de fait |
|---|---|---|---|
| A1 | Définir le type `SaidaOp` (schéma, versionné, sérialisable) | — | ✅ `src/authoring/SaidaOp.{hpp,cpp}` : struct {opVersion, type, sceneId, payload}, parse strict, `toJson` canonique, registre `knownOpTypes()` (source unique manifest+applier), round-trip testé (2026-07-03) |
| A2 | Définir `EngineManifest` complet (nodes, behaviours, **propriétés réfléchies**, signaux, actions scénario, versions) | — | `saida_tool describe-engine --json` produit le manifest |
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

### Phase B — Headless & snapshots (E) 🔵 (B1 amorcé, B4 ✅)

| # | Tâche | Dépend | Critère de fait |
|---|---|---|---|
| B1 | Outil `saida_tool` (sans fenêtre, exit codes fiables, `--json-logs`) | A3 | 🔵 binaire `saida_tool` créé (`src/tools/saida_tool_main.cpp`, cible CMake, link `saida_engine` headless) : exit 0=ok / 2=usage, stdout machine / stderr diag, `help`. Reste : `--json-logs`, autres sous-commandes |
| B2 | `validate-project` / `validate-scene` / `validate-script` / `validate-scenario` | A4 | codes retour stables |
| B3 | `apply-ops <proj> <ops.json> --out <snapshot>` | A3,A6 | snapshot déterministe |
| B4 | `describe-engine --json` (export manifest) | A2 | ✅ `saida_tool describe-engine [--pretty]` : manifest JSON sur stdout, **déterministe/hashable** (sha256 stable, invariant 0.6), vérifié (2026-07-03) |
| B5 | Sécurité des chemins (pas de `..`, pas d'absolu, sandbox) | — | tests d'échappement |

### Phase C — Collaboration SaaS minimale (P) ⬜

| # | Tâche | Dépend | Critère de fait |
|---|---|---|---|
| C1 | Tables Postgres : `Project`, `ProjectRevision`, `OperationLog`, `SceneDocument`, `CollaborationSession` (Prisma) | — | migration + seed |
| C2 | Collaboration Gateway (Fastify WebSocket) | A2,A4 | service démarrable |
| C3 | Boucle : auth → valide (manifest+état) → append → apply → revision++ → broadcast | C1,C2 | 1 client applique une op |
| C4 | Revision monotone + snapshots périodiques (N ops / X min / avant build) | C1 | snapshot matérialisable |
| C5 | Reconstruction gateway depuis Postgres au redémarrage (invariant 0.7) | C1,C3 | crash test sans perte |
| C6 | Présence/curseurs via Yjs awareness | C2 | 2 curseurs visibles |
| C7 | Résolution de conflits (transform/property last-wins, delete/reparent gardés) | C3 | matrice de conflits testée |

### Phase D — Éditeur web (P + E) 🔵 (D1 ✅)

| # | Tâche | Dépend | Critère de fait |
|---|---|---|---|
| D1 | Intégrer le canvas runtime WASM/WebGPU dans la page Next.js | A.5(S1) | ✅ scène rendue in-app (2026-07-03, voir §5.4) |
| D2 | Charger un ProjectSnapshot dans le runtime d'édition | B3,S4 | projet réel affiché |
| D3 | Panels React pilotés par EngineManifest : hierarchy, inspector, assets, console | A2 | inspection typée |
| D4 | Émettre des SaidaOps depuis l'UI (gizmos, inspector) | C3 | édition → op acceptée |
| D5 | Recevoir et appliquer les ops distantes (multi-clients) | C3,S3 | 2 users, même scène |
| D6 | **Optimistic-local** pour manipulations continues (invariant 0.4) | D4 | drag fluide, commit au relâchement |
| D7 | Édition texte scripts/UI via Yjs + commit `write_script`/`write_ui` | C6 | script hot-reload |
| D8 | Historique des opérations + rollback de revision | C4 | restaurer revision N |

### Phase E — IA agentique / vibecoding (P + E) ⬜

| # | Tâche | Dépend | Critère de fait |
|---|---|---|---|
| E1 | Agent lit EngineManifest + scène compacte | A2,D2 | contexte borné |
| E2 | Agent produit des SaidaOps (pas de JSON de scène libre) | A1 | ops schéma-valides |
| E3 | Dry-run obligatoire + diff visible avant application | A5,B3 | prévisualisation |
| E4 | Validation headless post-application | B2 | échec bloquant |
| E5 | Crédits (réserve→commit/refund) + audit (`AiConversation`/`AiToolCall`) | — | ledger cohérent |
| E6 | Garde-fous : pas de shell, pas de C++ web, pas de cross-projet (invariant 0.5) | — | tentatives rejetées |

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

1. **Phase A + B** — A.5 est vert ; le prochain verrou est de transformer le spike
   en contrat moteur complet (`SaidaOp`, validation stricte, headless).
2. **Phase C seulement après A/B avancées** : le contrat moteur (SaidaOp complet,
   manifest, headless) est le socle de tout ; il profite aussi au desktop/MCP.
3. **Phase D** (éditeur web) : la partie visible ; dépend de C3 et de S1/S3/S4.
4. **Phase E** (IA) : par-dessus un éditeur qui marche — l'IA n'est qu'un
   producteur de SaidaOps de plus.
5. **Phase F/G** : industrialisation et mise en production.

Règle d'or : ne pas démarrer C tant que le contrat moteur A/B n'est pas assez
strict pour valider/rejouer des ops, ni promettre l'IA (E) avant que l'édition
manuelle (D) soit solide.

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
  noyau `SaidaOp` depuis MCP ?
- **Identité des nodes côté ops** : le spike référence par nom ; le système final
  doit utiliser `NodeId` (id/génération) — figer le schéma en A1.
- **Import d'asset en édition web** : MEMFS preload vs fetch/IDBFS — hors spike,
  à cadrer en Phase D/F.
- **Hébergement** : Coolify/Kamal + Hetzner/OVH + Cloudflare R2 (proposé mandat
  §4.1) — à confirmer avant Phase F.
- **Périmètre `write_script` web** : QuickJS hot-reload existe déjà côté moteur ;
  définir jusqu'où l'IA génère du JS en Phase E.

---

## 11. Références

- Mandat technique : [ARCHITECTURE_PRODUCTION_CLAUDE.md](ARCHITECTURE_PRODUCTION_CLAUDE.md)
- Spike pivot : [PLAN_LIVE_EDIT_WEB.md](PLAN_LIVE_EDIT_WEB.md)
- Export web moteur : [PLAN_WEB_EXPORT.md](PLAN_WEB_EXPORT.md)
- Contrat d'authoring moteur : [CLAUDE.md](CLAUDE.md) (« Comment coder un jeu »)
- Architecture plateforme : `GitHub/saida/docs/architecture.md`
- Fondations locales plateforme : `GitHub/saida/docs/local-foundations.md`
