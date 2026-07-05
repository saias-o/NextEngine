# Audit — Plateforme web Saida (éditeur + LLM + collaboration cloud)

> Audit de l'état **réel du code** face aux affirmations de
> [PLAN_INTEGRATION_SAIDA.md](PLAN_INTEGRATION_SAIDA.md).
> Périmètre : `GitHub/saida` (apps/api, apps/web, apps/workers, packages) +
> couche web du moteur `NextEngine/web/runtime` + `src/authoring`.
> Date : 2026-07-05. **Aucune modification de code** — constat uniquement.
>
> Verdict court : le **backend collaboration** (hub à révisions monotones,
> validation WASM in-process, fold natif, reconstruction, dry-run) est
> réellement solide et bien testé. Le **frontend éditeur reste largement une
> façade** posée sur ce backend : la boucle d'édition ne fonctionne
> end-to-end que sur la **scène vide/Manuel**, plusieurs contrôles UI sont
> morts ou cosmétiques, et des morceaux « production » (crédits IA, worker
> agent, présence, export, scaling) sont absents. Le plan est plus optimiste
> que le code : beaucoup de lignes « ✅ » sont en fait « socle posé, pas
> prouvé bout en bout ».

Légende sévérité : 🔴 bloquant / architecture · 🟠 important · 🟡 dette / smell ·
⚪ hygiène.

---

## 1. Bloquants d'architecture (le cœur du produit)

### 1.1 🔴 Le snapshot durable ne transporte ni mesh ni matériau → seule la scène vide fonctionne vraiment

Le code le documente lui-même :
`web/runtime/main.cpp:462-465` — *« MVP scope is the empty/Manual scene (root +
settings, no meshes) — headless snapshots omit mesh/material refs
(SceneSnapshot.cpp) »*.

- `loadNode()` reconstruit tout `MeshNode` avec **le cube builtin en dur**
  (`main.cpp:216`, `gApp.cubeMesh`) et un matériau reconstruit depuis des champs
  PBR inline qui, dans le snapshot headless, **n'existent pas**.
- Conséquence : dès qu'un projet contient du vrai contenu, `saida_load_snapshot`
  produit une scène de cubes identiques, sans géométrie ni texture réelle. Le
  round-trip `serialize → /scene → loadSnapshot` **perd le rendu**.

Or D2 est coché « backend fait » et l'ordre d'exécution recommandé (§7) dit
« charger un vrai `ProjectSnapshot` … c'est le verrou qui transforme l'UI en
éditeur ». **Ce verrou n'est pas levé** : il l'est seulement pour la scène vide.
Toute la promesse produit n°1 (« modifier des scènes ») est en attente de
**Track 1-F** (refs d'assets dans le snapshot), explicitement hors MVP.

**Impact** : l'éditeur « réel » ne l'est que sur une scène vide. À prioriser
avant toute autre finition D/E, sinon on peaufine une coquille.

### 1.2 🔴 Manifest moteur divergent entre le runtime web (stub) et l'API (complet)

`src/authoring/EngineManifest.cpp:73-82` (branche `#else` Emscripten) n'expose
que `Node/MeshNode/Light/Particle/Water` et **zéro behaviour**. La branche
desktop/headless (`#ifndef __EMSCRIPTEN__`) expose tout le graphe réfléchi.

- Le panneau UI `EngineManifestPanel` + l'inspecteur consomment
  `runtime.manifest()` → **manifest réduit**.
- L'agent IA côté serveur consomme `getEngineManifest()`
  (`apps/api/src/authoring.ts:78`) → **manifest complet** (vendé depuis
  `describe-engine`).

Donc l'agent peut légitimement proposer un `set_property` sur un behaviour ou un
type de nœud que le runtime web **ne connaît pas** : l'op passe la validation
serveur (WASM), est appliquée côté serveur, puis **échoue localement** à
`runtime.applyOp` chez chaque client → divergence silencieuse (le client
`console.warn` et continue, cf. `live-edit-client.ts:150`). Deux « vérités » du
contrat moteur coexistent.

**Correction** : soit amener la parité de réflexion au web (coût taille, cf.
risque §8 du plan), soit **borner explicitement** ce que l'API accepte au
sous-ensemble réellement supporté par le runtime web, tant que la parité n'est
pas là.

### 1.3 🔴 `saida_tool` (binaire natif) est une dépendance runtime dure de l'API, non portable

`apps/api/src/scene-fold.ts` shelle `saida_tool apply-ops` pour **chaque** :
reconstruction de scène (`GET /scene`, à chaque ouverture d'éditeur et chaque
join WS), dry-run (chaque tour d'agent, chaque apply), snapshot périodique.

- Si le binaire est absent/mal configuré, `reconstructScene` **jette** →
  `GET /scene` renvoie 500 → l'éditeur ne charge plus rien (le snapshot n'est
  pas « dégradé gracieusement » ici, contrairement au scheduler).
- Le binaire est buildé sous **Windows/MSYS2 UCRT64** (cf. `CLAUDE.md`,
  pièges linker). Un déploiement API Linux (Coolify/Hetzner, mandat §4.1)
  exige un **build Linux de `saida_tool`** qui n'existe pas dans le pipeline.
- Coût perf : **un `execFile` + `mkdtemp` + 3 fichiers temp par requête**
  concernée. La reconstruction est sur le chemin d'ouverture (pas « background »
  comme le commentaire le suggère). À charge, c'est un goulot (fork process par
  bootstrap).

Les 5 tests « skips attendus sans binaire natif sur PATH » (tous les lots)
masquent que **ce chemin n'est jamais exercé en CI**.

---

## 2. Collaboration & correction

### 2.1 🟠 Hypothèse mono-gateway : la révision vient de la mémoire, pas de Postgres

`collaboration.ts:50` — `revision` est « in-memory head; **authoritative for a
single gateway** ». L'assignation est `room.revision + 1` (`:174`) et
`OperationLog` a `@@unique([projectId, revision])` (schema `:651`).

Avec **≥ 2 instances API** : les deux salles ont chacune leur `room.revision` en
RAM, assignent la même révision, la 2ᵉ `appendOp` viole la contrainte unique →
`persist` renvoie une erreur → l'op est **perdue** côté client (aucun retry,
aucune renumérotation). Pas de coordination Redis/pub-sub, et le broadcast ne
traverse pas les instances (un peer sur l'instance B ne voit pas l'op de
l'instance A). L'invariant 0.7 (« reconstructible depuis Postgres, zéro état
critique en mémoire ») est **partiellement faux** : la tête de révision *est* un
état critique en mémoire.

C'est acceptable pour un déploiement solo mono-process, mais c'est un **plafond
de scaling** à documenter, pas une propriété « production-ready » (cf. §9 du
plan qui la revendique).

### 2.2 🟠 Application de batch non atomique malgré le commentaire « transactional »

`collaboration-ws.ts:238-276` (`POST /ops`) et
`project-agent.ts:693-712` (apply action) font : `dryRunOps` (gate) **puis**
boucle `hub.applyExternalOp` op par op.

- Entre le dry-run et la boucle, un client WS peut committer une op → l'état
  live diverge de l'état dry-runné (**TOCTOU**). Le « gate » ne garantit donc
  pas l'application propre.
- Si l'op *i* échoue en milieu de boucle, les ops `0..i-1` sont **déjà
  persistées et broadcastées**, sans rollback. La réponse renvoie
  `{ failedIndex, applied }` mais la scène reste **à moitié mutée**. L'action
  agent est marquée `FAILED` alors que des révisions ont été appliquées.

Le commentaire `:233-237` (« the REST/agent path is transactional ») est
trompeur : il n'y a pas de transaction couvrant le batch.

### 2.3 🟠 Présence / curseurs (C6, Yjs) : totalement absents

Aucune trace de **Yjs** dans le repo (`grep` vide), alors que le mandat le place
au centre (« Yjs synchronise l'atelier », plan §2 & §4). C6 (« 2 curseurs
visibles ») et D7 (édition texte scripts/UI via Yjs) ne sont pas commencés. À
noter : il n'y a donc **aucune** collaboration texte (scripts/UI), seulement des
SaidaOps de scène.

### 2.4 🟡 Le journal d'ops accumule des no-ops (par design, mais à surveiller)

Le chemin WS ne fait que valider la **forme** (`persist` → `validate`) avant
`appendOp`. Une op valide en forme mais invalide en état (ex. `set_property` sur
un nœud supprimé) est **appendée quand même** puis droppée au fold
`--skip-invalid`. C'est le design C7, mais l'`OperationLog` grossit de déchets
non compactés (aucune purge/GC après snapshot). À terme : coût stockage + fold.

### 2.5 🟡 `CollaborationSession.lastRevisionAck` est un champ mort

Écrit une seule fois au `join` (`collaboration-ws.ts:303-310`), **jamais mis à
jour** quand le client ack des révisions. Le champ est prévu pour le resync mais
n'est pas entretenu ; le resync réel passe par `?since=` côté client. Soit le
câbler, soit le retirer (fausse impression de fonctionnalité).

### 2.6 🟡 Multi-clients incomplet pour les ops structurelles (mirror JS partiel)

`use-live-edit-session.ts:59-85` (`applyOpToSnapshot`) ne gère que
`rename_node` / `set_property` / `set_transform`. Les ops **`create_node` /
`delete_node` / `reparent_node`** ne modifient pas le snapshot React mirroir.

Pour une op **distante** (`onRemoteOp`, `:181-184`), le hook fait uniquement
`setSnapshot(applyOpToSnapshot(...))` — **sans** `refreshSnapshot()`. Donc quand
un pair crée/supprime/reparent un nœud, l'autre client applique bien l'op dans
le runtime WASM mais **sa hiérarchie React ne change pas** (le panneau lit le
mirror JS). D5 « 2 users, même scène » n'est pas vraiment vrai pour les
changements de structure.

---

## 3. Agent IA / vibecoding

### 3.1 🔴 Aucune réservation de crédits sur les runs d'agent (E5)

`grep reserve/creditLedger` dans le chemin agent = vide. Le seul garde-fou est
un rate-limit **20 req/min/projet** (`project-agent.ts:613-618`). Le ledger de
crédits existe (billing) mais **n'est pas branché** à l'IA. Le risque explicite
du plan (« Coût IA non borné → marge ruinée ») est **non mitigé**. Chaque tour
appelle un LLM externe facturable sans compteur métier ni kill-switch.

### 3.2 🟠 Le run d'agent tourne **dans la requête HTTP**, pas dans un worker

`project-agent.ts:620` — `await runAgentTurn(...)` est appelé de façon synchrone
dans le handler `POST /agent/messages`, qui appelle le LLM (`completeAgentChat`,
timeout 45 s) + reconstruction de scène (subprocess) + dry-run (subprocess).
Aucun worker Temporal (Phase E handoff jamais fait ; `apps/workers` ne contient
que le pipeline **img-to-3d**). Conséquences : requêtes longues qui tiennent la
connexion, pas de file, pas de retry, timeout dur, pas d'isolation. C'est le
patron que le plan disait explicitement vouloir sur Temporal.

### 3.3 🟠 Le contexte agent fuit entre conversations d'un même projet

`project-agent.ts:224-233` (`buildModelMessages`) charge les messages récents et
attachements **`where: { projectId }`**, sans filtrer par `conversationId`. Deux
conversations distinctes du même projet **se contaminent** dans le prompt. (Idem
`recentAttachments`.) Sous-jacent : le modèle multi-conversations est amorcé
(schema) mais l'agent ne le respecte pas.

### 3.4 🟡 Le sélecteur de modèle IA de l'UI est purement cosmétique

`Project.aiModel` (schema `:442`, défaut `"GLM 5.2"`) est persisté et affiché
dans Settings (`workspace/page.tsx:571-581`, liste **codée en dur**
`aiModels = ["GLM 5.2","Mistral Large","Claude Sonnet","GPT-5"]`, `:100`). Mais
`completeAgentChat` utilise **`config.agent.model`** (env `AGENT_LLM_MODEL`,
`llm.ts:71`). Le choix de modèle **n'a aucun effet** sur le LLM réellement
appelé. Façade à assumer ou à câbler (passer `project.aiModel` à l'appel LLM +
mapper vers de vrais identifiants).

### 3.5 🟡 Contexte de scène tronqué brutalement

`project-agent.ts:278` — `JSON.stringify(sceneContext).slice(0, 24_000)` coupe
au caractère, pouvant produire un JSON invalide injecté dans le prompt. Idem la
troncature de scène compacte n'a pas de stratégie de résumé (juste un cut). OK
tant que les scènes sont petites (scène vide), fragile ensuite.

---

## 4. Frontend — façades, boutons morts, données mock

### 4.1 🟠 Mode Guidé : le « premier prompt de pipeline » n'est jamais envoyé

`workspace/page.tsx:912-915` — en mode guidé, `firstMessage` est **un simple
texte de bienvenue** affiché comme bulle assistant quand la conversation est
vide (`:1002-1007`). Rien n'appelle l'agent au démarrage. Le `skill` ne pilote
que **quel texte statique** s'affiche. D0 « lancer le premier message agent »
= non fait. Le distinguo Guidé/Manuel est donc **cosmétique**.

### 4.2 🟠 État d'initialisation non persisté → le setup peut se rouvrir

Le schema `Project` (`:436-450`) n'a **ni `creationMode`, ni `designSkillId`,
ni `initializedAt`**. Le mode/skill ne vivent que dans l'URL
(`?startMode=&skill=`). Donc :
- le setup Guidé/Manuel peut être **ré-ouvert** sur un projet déjà démarré
  (`isSetup = setup===1 || !projectId`, `:106`) ;
- aucune trace durable de « ce projet a été initialisé en Guidé avec skill X ».

C'est l'« open decision » §10 du plan, resté ouvert.

### 4.3 🟠 Registre de design skills inexistant (E0)

`guideSkills` = **4 entrées codées en dur** (`:93-98`, `official/narrative/
combat/verticalSlice`). Pas de table `DesignSkill`, pas d'API, pas de skill
officiel réel, pas de marketplace. Toute la section « skills communautaires »
(plan §2.1) est de la maquette.

### 4.4 🟡 Boutons / interactions morts

- **Save** (barre de commande) : `<button aria-label={t("saveProject")}>` sans
  `onClick` (`:435-437`). Décoratif (la scène s'auto-snapshot, mais l'utilisateur
  ne le sait pas).
- **Outils Move / Rotate** du viewport : `ViewportToolbar` change juste
  `toolMode`, qui ne sert qu'à **désactiver le picking**
  (`pickEnabled={toolMode==="select"}`, `:482`). **Aucun gizmo** (D6/D9 non
  faits). Sélectionner Move/Rotate ne fait donc *rien* d'utile.
- **Lignes d'assets** (`AssetBrowser`, `:1296-1305`) : `<button>` sans `onClick`
  — cliquer un fichier projet ou Drive ne télécharge/insère rien.
- Marketing : `saida-editor` bouton « downloadSoon » sans handler ; marketplace
  `playComingSoon` (« Play online (coming soon) ») — fonctions annoncées non
  livrées.

### 4.5 🟡 Onglet « Saida Drive » = données mock en dur

`driveAssets` (`:86-91`) est une liste factice (`forest_props.glb`,
`hero_portrait.png`, `menu_theme.ogg`, `prototype_notes.md`) rendue telle quelle
dans l'onglet Drive (`:1271`). Le vrai stockage Drive dédupliqué (invariant
produit, plan §5.2) n'existe pas côté UI. À retirer ou brancher.

### 4.6 🟡 Réglages graphiques persistés mais inertes

`maxFps` / `renderScale` / `shadowsEnabled` sont persistés (schema + API) et
éditables, mais **jamais transmis au runtime WASM** (aucun binding, aucun appel
dans `workspace/page.tsx`). Ils ne changent donc rien au rendu. D10 note aussi
qu'ils ne sont pas broadcastés live. Réglages « pour la déco » aujourd'hui.

### 4.7 ⚪ Backdoor `projectId === "test"`

`workspace/page.tsx:132` et `use-live-edit-session.ts:154` court-circuitent :
rôle forcé `OWNER`, pas de session live. Naviguer vers `?projectId=test` ouvre
un éditeur « propriétaire » sans backend. Affordance de dev laissée dans le code
produit — à isoler derrière un flag dev.

---

## 5. Client live-edit (lib) — smells

### 5.1 🟡 Manipulation continue : sémantique d'inverse douteuse, `emitOp === commit`

`live-edit-client.ts` : `commit` est **le même** `sendOptimistic` que `emitOp`
(`:261-263`). Le patron continu prévu (D6) est :
`previewLocal` (applique sans envoyer, `:247`) répété pendant le drag, puis
`commit`. Mais `commit`/`sendOptimistic` **ré-applique l'op** (`runtime.applyOp`,
`:233`) et capture l'`inverse` **relatif à l'état déjà prévisualisé** — donc en
cas de rejet serveur, le rollback (`:179`) restaure la **dernière preview**, pas
l'état d'avant manipulation. Pour des ops absolues (`set_transform`) c'est
souvent bénin, mais la sémantique undo est subtilement fausse et il n'existe pas
de vrai « begin/commit continu » (D6 à moitié fait). De toute façon les gizmos
n'appellent jamais `previewLocal` (cf. 4.4) — le chemin continu est **du code
non exercé**.

### 5.2 🟡 Deux sources de vérité pour le snapshot (mirror JS vs runtime)

`use-live-edit-session.ts` maintient à la fois un **mirror JS** (`applyOpToSnapshot`,
partiel — cf. 2.6) et lit `runtime.snapshot()` (`refreshSnapshot`). Le fallback
Lot 8 (« garder une représentation JS quand le runtime refuse loadSnapshot/
applyOp ») institutionnalise cette double comptabilité, qui **peut diverger**
(mirror qui ignore create/delete/reparent, runtime qui peut être vide). Source
de bugs d'affichage difficiles à reproduire.

### 5.3 ⚪ Statut `"error"` jamais émis par le client

`LiveEditStatus` inclut `"error"` et le hook le traite (`:173`), mais
`connectLiveEdit` ne fait **jamais** `setStatus("error")` — seule la rejection de
la promesse mène à `"error"` (dans le hook, `:206`). Branche morte dans le
type/handler.

### 5.4 ⚪ Runtime = singleton dur, non ré-instanciable

`engine-runtime.ts:91` — `runtimePromise` global. Un seul runtime/canvas par
chargement de page (contrainte `-sMODULARIZE` absent, assumée). En cas d'échec
d'init, la promesse **reste rejetée** → seul un reload complet répare (commenté
`:114-118`). Pas de multi-scène/multi-projet simultané. Limite connue mais
structurante pour la suite.

---

## 6. Écarts plan ↔ réalité (résumé des statuts « ✅ » à requalifier)

| Réf | Plan dit | Réalité code |
|---|---|---|
| D1/D2 | « scène chargée in-app », backend fait | vrai **seulement scène vide** ; contenu perd mesh/mat (1.1) |
| D4 | « inspecteur éditable → SaidaOp » | ok pour scalaires ; structurel non reflété multi-client (2.6) |
| D5 | « 2 users, même scène » | ops transform/prop ok ; create/delete/reparent invisibles côté pair (2.6) |
| D6/D9 | optimistic + gizmos | picking select seulement ; **pas de gizmos** ; chemin continu mort (4.4, 5.1) |
| D10 | Settings projet ✅ | persistés mais **inertes** (graphique) + modèle IA cosmétique (4.6, 3.4) |
| C6 | présence Yjs | **absent** (2.3) |
| C7 | conflits résolus | ok au fold, mais batch non atomique + TOCTOU (2.2) |
| E2/E3 | agent producteur d'ops ✅ | pipeline réel **mais** sans worker, sans crédits, contexte qui fuit (3.1-3.3) |
| E5 | crédits IA | **non branchés** (3.1) |
| E7 | chat + pièces jointes ✅ | fait et plutôt propre (rare vrai ✅ côté produit) |
| D0/E0 | init Guidé/Manuel + skills | **façade** ; rien persisté ni envoyé (4.1-4.3) |
| Phase F | export serveur | **rien** (workers = img-to-3d seul) |
| Phase G | quotas/backups/observabilité/charge | **rien** de spécifique éditeur |

---

## 7. Recommandations priorisées

1. **Débloquer 1.1 (refs d'assets dans le snapshot, Track 1-F)** *avant* toute
   autre finition D/E : sans ça l'éditeur reste une démo de scène vide. C'est le
   vrai chemin critique, pas D4/D9.
2. **Fermer la divergence de manifest (1.2)** : borner l'API au sous-ensemble
   web tant que la parité de réflexion n'est pas au runtime, sinon les ops agent
   casseront chez les clients.
3. **Rendre `saida_tool` déployable Linux + le sortir du chemin chaud (1.3)** :
   build CI Linux, cache de scène reconstruite, et faire échouer *gracieusement*
   `/scene` (fallback dernier snapshot) au lieu d'un 500.
4. **Brancher les crédits IA + passer l'agent en worker (3.1/3.2)** avant toute
   ouverture publique : coût non borné = risque business n°1 du plan.
5. **Atomiser les batches d'ops (2.2)** : appliquer sous une vraie sérialisation
   par projet avec rollback, ou refuser le batch si la tête a bougé depuis le
   dry-run (compare revision).
6. **Nettoyer les façades UI (4.x)** : soit câbler (Save, gizmos, Drive, modèle
   IA, réglages graphiques, premier prompt guidé), soit les masquer. Livrer une
   UI où *chaque bouton fait quelque chose* réduit la dette de confiance.
7. **Corriger le mirror snapshot (2.6/5.2)** : appeler `refreshSnapshot()` sur
   op distante, ou étendre `applyOpToSnapshot` aux ops structurelles — sinon la
   collaboration multi-client ment visuellement.
8. **Persister l'état d'init projet (4.2)** : `creationMode`/`designSkillId`/
   `initializedAt` + verrouiller le setup après initialisation.

---

## 8. Ce qui est réellement bon (à ne pas casser)

- `CollaborationHub` : sérialisation par projet via `room.tail`, révisions
  strictement monotones, persist-avant-ack, reconstruction snapshot+fold —
  propre et bien testé (`collaboration.test.ts`, `collaboration-snapshot.test.ts`).
- Validation d'op **via le vrai contrat C++ en WASM in-process** (`authoring.ts`)
  — zéro duplication, la meilleure décision d'archi du lot.
- `agent-ops.ts` : extraction/validation d'ops LLM-agnostique, garde-fous
  invariant 0.5 en dur, propre et testé.
- Séparation transport/hub/store (testabilité sans WS ni DB) : bon découpage.
- Chat + pièces jointes (lecture bornée, redaction de secrets, contexte non
  fiable balisé) : parmi les rares briques produit vraiment finies.
- Rôles/authz projet (owner/editor/viewer) cohérents API+WS+UI, avec E2E.

---

### Annexe — fichiers clés inspectés

`apps/api/src/{collaboration,collaboration-ws,collaboration-store,collaboration-snapshot,scene-fold,ops-preview,authoring,agent-ops,project-agent,llm,config}.ts`,
`apps/web/app/lib/{engine-runtime,live-edit-client,use-live-edit-session}.ts`,
`apps/web/app/components/EngineViewport.tsx`,
`apps/web/app/saidaengine/workspace/page.tsx`,
`packages/db/prisma/schema.prisma`,
`NextEngine/web/runtime/main.cpp`, `NextEngine/src/authoring/EngineManifest.cpp`.

Voir aussi l'audit UI antérieur [AUDIT_UI_WEB_V1.md](AUDIT_UI_WEB_V1.md).
