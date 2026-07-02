# Mandat Claude - Architecture Production Saida Engine + Saida SaaS

## Objectif du document

Ce document decrit ce que Claude doit concevoir et implementer, dans le moteur
SaidaEngine et dans la plateforme SaaS Saida, pour arriver a une architecture
complete, scalable, sure et exploitable en production.

Le contexte produit est le suivant :

- SaidaEngine est un moteur C++17 avec Vulkan, WebGPU en cours, QuickJS, RmlUi,
  scene graph, behaviours, signaux, scripts, assets et exports.
- Saida SaaS existe deja avec Next.js, API Fastify, Prisma, Postgres, Redis,
  Temporal, MinIO local, Stripe, comptes, credits, uploads et jobs.
- L'objectif majeur est l'edition de jeux dans le navigateur via chatbot IA +
  editeur web collaboratif, avec rendu local dans le navigateur quand possible.
- Le developpeur est solo/micro-entrepreneur : l'architecture doit rester
  realiste, peu couteuse et maintenable.

Le principe le plus important :

**Yjs synchronise l'atelier. SaidaOps protegent le moteur. Temporal industrialise
les travaux longs. R2/S3 porte les gros blobs. Postgres garde la verite metier.**

---

## 1. Principe directeur

La plateforme web ne doit pas contourner le modele du moteur.

SaidaEngine possede deja un contrat fort :

- une scene est un arbre de nodes ;
- toute logique de gameplay est un Behaviour ;
- la communication se fait par signaux et slots ;
- les references se font par groupes, anchors ou requetes scopees, pas par nom
  global fragile ;
- les assets, scripts, scenes et projets sont serialises dans une structure
  reproductible ;
- l'editeur local a deja une surface MCP capable de manipuler le moteur par
  outils valides.

La plateforme SaaS doit donc devenir une extension web de ce contrat, pas un
second editeur avec son propre modele.

La source de verite collaborative ne doit pas etre un document Yjs libre. La
source de verite metier doit etre un journal d'operations Saida versionnees,
validees cote serveur et appliquees a des snapshots de projet.

---

## 2. Vocabulaire cible

### SaidaOp

Une operation metier atomique, versionnee, validee et auditable.

Exemples :

```ts
type SaidaOp =
  | CreateNodeOp
  | DeleteNodeOp
  | RenameNodeOp
  | ReparentNodeOp
  | SetTransformOp
  | SetPropertyOp
  | AddBehaviourOp
  | RemoveBehaviourOp
  | ConnectSignalOp
  | DisconnectSignalOp
  | WriteScriptOp
  | WriteUiOp
  | CreateScenarioOp
  | UpdateScenarioStepOp
  | AttachScenarioOp
  | ImportAssetOp
  | SetProjectSettingOp;
```

Forme commune :

```json
{
  "opVersion": 1,
  "id": "uuid",
  "orgId": "uuid",
  "projectId": "uuid",
  "sceneId": "main",
  "baseRevision": 42,
  "authorId": "uuid",
  "source": "human | ai | system",
  "type": "set_property",
  "payload": {},
  "createdAt": "2026-07-02T12:00:00.000Z"
}
```

### ProjectSnapshot

Etat materialise d'un projet Saida a une revision donnee.

Il doit pouvoir etre converti en arborescence classique :

```text
ProjectName/
  ProjectName.saidaproj
  asset_registry.json
  scenes/
  scripts/
  ui/
  assets/
```

### EngineManifest

Description stable du moteur exposee au SaaS et aux agents IA :

```json
{
  "engineVersion": "0.1.0",
  "opVersion": 1,
  "nodes": {},
  "behaviours": {},
  "properties": {},
  "signals": {},
  "slots": {},
  "assetTypes": {},
  "scenarioActions": [],
  "scenarioConditions": []
}
```

Le SaaS et les agents doivent utiliser ce manifest pour valider ce qu'ils ont le
droit de creer, modifier ou connecter.

---

## 3. Travail a faire dans SaidaEngine

### 3.1 Extraire un noyau d'authoring reutilisable

Le MCP local actuel contient deja une tres bonne surface d'edition pour les LLM :

- description de l'API reflechie ;
- creation/suppression/renommage/reparenting de nodes ;
- modification de transforms et proprietes ;
- ajout de behaviours ;
- connexion de signaux ;
- ecriture de scripts et UI ;
- scenarios declaratifs ;
- validation headless.

Claude doit extraire ce savoir-faire dans une couche reutilisable :

```text
src/authoring/
  SaidaOp.hpp
  SaidaOp.cpp
  SaidaOpValidator.hpp
  SaidaOpValidator.cpp
  SaidaOpApplier.hpp
  SaidaOpApplier.cpp
  EngineManifest.hpp
  EngineManifest.cpp
  AuthoringSession.hpp
  AuthoringSession.cpp
```

Objectif :

- l'editeur desktop appelle ce noyau ;
- le MCP appelle ce noyau ;
- les tests headless appellent ce noyau ;
- le futur serveur SaaS peut reproduire exactement le meme comportement ;
- les agents IA ne peuvent agir qu'a travers cette surface.

Le code existant ne doit pas etre duplique entre MCP, editeur et SaaS.

### 3.2 Definir et valider SaidaOp

Chaque operation doit etre :

- schema-validee ;
- versionnee ;
- auditable ;
- appliquee de maniere deterministe ;
- rejetee proprement si invalide ;
- convertible en diff utilisateur lisible ;
- si possible inversible pour undo/redo.

Regles de validation minimales :

- `create_node` refuse les types inconnus ;
- `add_behaviour` refuse les behaviours inconnus ;
- `set_property` refuse toute propriete non reflechie ;
- `connect_signal` refuse signal/slot inconnus ;
- `reparent_node` refuse les cycles ;
- `delete_node` refuse la suppression de racine ;
- `write_script` refuse les chemins hors projet ;
- `write_ui` refuse les chemins hors projet ;
- `import_asset` verifie type, taille, hash et chemin logique ;
- `update_scenario_step` valide strictement actions et conditions.

### 3.3 Produire un EngineManifest stable

Ajouter une commande ou fonction qui exporte le manifest moteur :

```sh
saida_tool describe-engine --json
```

ou cote C++ :

```cpp
nlohmann::json buildEngineManifest();
```

Ce manifest doit inclure :

- types de nodes instanciables ;
- behaviours attachables ;
- proprietes reflechies, types, bornes, tooltips ;
- signaux et slots ;
- actions/conditions scenario ;
- version de format scene/projet/asset ;
- version du protocole SaidaOp.

Le SaaS doit pouvoir cacher ce manifest par hash.

### 3.4 Creer un outil CLI/headless officiel

Claude doit ajouter un outil sans fenetre pour les serveurs de build et de
validation :

```sh
saida_tool validate-project <project.saidaproj>
saida_tool validate-scene <scene.scene>
saida_tool describe-engine --json
saida_tool apply-ops <project.saidaproj> <ops.json> --out <snapshot-dir>
saida_tool export-web <project.saidaproj> --out <dist>
saida_tool export-windows <project.saidaproj> --out <dist>
```

Contraintes :

- pas de GLFW visible ;
- pas de boucle de rendu infinie ;
- exit codes fiables ;
- logs machine-readable possibles (`--json-logs`) ;
- aucun acces reseau implicite ;
- chemins d'entree/sortie explicites ;
- validation de securite des chemins.

### 3.5 Finaliser le vrai runtime WebGPU

L'etat actuel du depot indique que WebGPU fonctionne deja comme validation
technique, mais le vrai export complet n'est pas encore termine.

Claude doit poursuivre :

- terminer le port du vrai Renderer sur le backend WebGPU ;
- eliminer les derniers handles Vulkan du chemin web ;
- verifier WebCanvasNode dans le runtime web ;
- supporter les scenes reelles, scripts, UI, audio, assets et settings ;
- definir des presets qualite web ;
- mesurer taille wasm/js/data ;
- servir avec COOP/COEP quand necessaire ;
- garder une strategie d'assets simple au debut, puis streaming.

Etapes conseillees :

1. runtime web charge un ProjectSnapshot minimal ;
2. rendu scene simple ;
3. scripts QuickJS ;
4. WebCanvas screen-space ;
5. input ;
6. assets projet ;
7. scenes plus lourdes ;
8. optimisation taille/perf.

### 3.6 Stabiliser WebCanvas comme UI de jeu

WebCanvasNode est la bonne direction pour UI screen/world space.

Claude doit verifier et completer :

- DOM minimal documente ;
- events click/input/change ;
- clavier, texte, scroll, touch ;
- world-space raycast ;
- rendu XR quand applicable ;
- hot reload transactionnel ;
- erreurs visibles ;
- compatibilite avec export web ;
- examples officiels propres.

Important : RmlUi n'est pas Chrome. Le but est une experience auteur proche du
web, mais avec un subset assume et teste.

### 3.7 Garder l'architecture moteur legere

Ne pas introduire :

- ECS lourd ;
- event bus global ;
- second modele de scene ;
- managers gameplay ;
- moteur d'UI parallele ;
- systeme de collaboration directement dans le Renderer.

Les nouvelles briques doivent rester dans le prolongement :

- nodes ;
- behaviours ;
- signaux ;
- autoloads ;
- scenes ;
- ResourceManager ;
- CLI/headless ;
- authoring core.

---

## 4. Travail a faire dans le SaaS

### 4.1 Architecture services cible

Services principaux :

```text
Next.js app
Fastify API
Fastify Collaboration Gateway (WebSocket)
Temporal server
Temporal workers
Postgres
Redis
Object storage S3/R2
Build workers
AI workers / agent workflows
```

Ne pas commencer par Kubernetes. Pour le MVP production :

- Coolify ou Kamal est acceptable ;
- Hetzner/OVH pour serveurs CPU/build ;
- Cloudflare R2 ou S3 compatible pour blobs ;
- Postgres manage recommande si budget possible ;
- Redis simple au debut ;
- backups automatiques et tests de restauration obligatoires.

### 4.2 Modele de donnees Postgres

Tables minimales :

```text
Organization
User
Membership
Project
ProjectRevision
SceneDocument
ProjectFile
Asset
AssetBlob
OperationLog
CollaborationSession
AiConversation
AiToolCall
BuildJob
BuildArtifact
CreditLedger
AuditEvent
```

Responsabilites :

- Postgres : metadonnees, droits, revisions, operations, billing, audit.
- R2/S3 : blobs assets, snapshots, builds, fichiers lourds.
- Redis : presence, locks courts, pub/sub, cache, rate limit.
- Temporal : jobs longs, exports, agents multi-etapes, retries.

### 4.3 Stockage objet

Structure conseillee :

```text
org/{orgId}/projects/{projectId}/assets/{hash}
org/{orgId}/projects/{projectId}/snapshots/{revision}.tar.zst
org/{orgId}/projects/{projectId}/builds/{buildId}/...
org/{orgId}/projects/{projectId}/uploads/{uploadId}/...
```

Regles :

- upload direct navigateur vers R2/S3 via URL signee ;
- hash cote serveur ;
- deduplication par hash ;
- taille limitee par plan ;
- metadata dans Postgres ;
- jamais de fichier projet durable uniquement dans le navigateur ;
- snapshots regulierement materialises.

### 4.4 Collaboration temps reel

Le service collaboration doit fonctionner ainsi :

```text
client propose SaidaOp
server authentifie + autorise
server valide contre EngineManifest + etat courant
server append OperationLog
server applique op a l'etat courant
server incremente revision
server broadcast op acceptee
clients appliquent op localement
server snapshot periodique
```

Yjs peut etre utilise pour :

- presence ;
- cursors ;
- selection ;
- awareness ;
- edition collaborative de texte pour scripts/UI ;
- reconciliation ergonomique cote client.

Mais les operations durables passent par SaidaOp.

Gestion conflits :

- `set_transform` : last accepted op wins ;
- `set_property` : last accepted op wins par propriete ;
- `delete_node` puis modification enfant : modification rejetee ;
- `reparent_node` : refuser cycle et parent supprime ;
- script/UI text : Yjs pendant edition, `write_script` ou `write_ui` au commit ;
- assets : remplacement par nouvelle reference, pas mutation du blob.

### 4.5 Snapshots et revisions

Le serveur doit garder :

- journal d'ops complet ou compactable ;
- revision numerique monotone ;
- snapshot regulier tous les N ops ou toutes les X minutes ;
- snapshot manuel avant build ;
- snapshot manuel avant grosse action IA ;
- rollback vers revision precedente.

Chaque snapshot doit pouvoir etre materialise en dossier Saida classique.

### 4.6 Editeur web

L'editeur web doit etre un client riche :

- UI React/Next.js ;
- canvas WebGPU/WASM du runtime Saida ;
- panels scene hierarchy, inspector, assets, scripts, console, jobs ;
- presence collaborative ;
- chatbot IA ;
- affichage des diffs IA avant application ;
- statut de validation ;
- historique operations.

Le canvas moteur ne doit pas etre le seul endroit ou l'etat existe. Il doit
recevoir les ops acceptees et afficher l'etat courant.

### 4.7 IA agentique

L'IA ne doit jamais modifier directement la base, R2 ou les fichiers projet.

Pipeline agent :

1. charger contexte projet minimal ;
2. lire EngineManifest ;
3. lire scene compacte ;
4. demander au modele un plan ;
5. demander au modele des SaidaOps ;
6. valider schema ;
7. dry-run apply ;
8. estimer cout credits ;
9. demander confirmation si necessaire ;
10. appliquer ;
11. lancer validation headless ;
12. afficher resultat et diff.

Tous les tool calls doivent etre audites :

```text
AiConversation
AiToolCall
OperationLog
CreditLedger
AuditEvent
```

Interdictions v1 :

- pas de shell libre ;
- pas de compilation C++ arbitraire pour utilisateurs web ;
- pas d'acces cross-project ;
- pas de secrets exposes ;
- pas de modification directe hors SaidaOp ;
- pas d'asset generation lourde tant que le modele economique n'est pas fixe.

### 4.8 Builds et exports serveur

Temporal doit piloter tous les exports.

Workflow standard :

```text
reserve credits
create immutable project snapshot
materialize snapshot in isolated workspace
download assets from R2/S3
validate project headless
optimize/package assets
run saida_tool export-*
upload artifact to R2/S3
write BuildArtifact
commit or refund credits
notify user
cleanup workspace
```

Workers separes :

```text
web-build-worker
windows-build-worker
linux-build-worker
android-build-worker
asset-opt-worker
ai-agent-worker
```

Securite worker :

- Docker image versionnee ;
- workspace temporaire par job ;
- quotas CPU/RAM/disk/time ;
- aucun secret inutile ;
- logs limites ;
- nettoyage garanti ;
- pas de montage du disque global ;
- sortie uniquement vers R2/S3 via credentials scopes.

### 4.9 Strategie d'export web

Priorite v1 :

- utiliser un template WASM/WebGPU precompile ;
- injecter assets, scenes, scripts et manifest ;
- eviter de recompiler le moteur pour chaque jeu ;
- compiler seulement quand le moteur ou les modules natifs changent.

Sortie :

```text
index.html
index.js
index.wasm
index.data ou assets streamables
manifest.json
README / metadata
```

Optimisations :

- Brotli ;
- KTX2/Basis plus tard ;
- meshopt ;
- limite taille gratuite ;
- gros jeux servis par lien ou plan payant ;
- mesure taille par build.

### 4.10 Billing et credits

Credits doivent couvrir :

- appels IA ;
- temps worker build ;
- stockage excedentaire ;
- bande passante si necessaire ;
- exports lourds ;
- generation assets future.

Chaque operation couteuse doit reserver puis commit/refund :

```text
CreditReservation
CreditLedger
BuildJob
AiToolCall
```

Ne pas facturer uniquement "abonnement". Les jobs IA/build peuvent ruiner la
marge si non bornes.

---

## 5. Securite production

### 5.1 Auth et permissions

Verifier partout :

- user appartient a organization ;
- user a droit sur project ;
- role permet lecture/ecriture/build/billing ;
- les jobs ne peuvent acceder qu'au projet demande ;
- URL signees limitees en duree et scope.

### 5.2 Validation chemins

Toute ecriture fichier doit etre sandboxee :

- chemin relatif projet uniquement ;
- pas de `..` ;
- pas de chemin absolu utilisateur ;
- pas d'ecriture hors workspace temporaire ;
- extension autorisee selon type.

### 5.3 Isolation agents et builds

Les agents IA et builds ne doivent pas avoir :

- acces shell libre ;
- acces base direct hors API ;
- acces a tous les buckets ;
- acces aux secrets Stripe ;
- acces aux projets des autres orgs.

### 5.4 Audit

Auditer :

- login/logout ;
- changement email/password ;
- creation/suppression projet ;
- operation IA ;
- operation collaborative ;
- export ;
- upload asset ;
- changement billing ;
- suppression compte/projet ;
- erreurs securite.

---

## 6. Observabilite

Minimum production :

- logs structures JSON ;
- request id ;
- job id ;
- project id ;
- user id ;
- trace id ;
- metrics jobs ;
- metrics credits ;
- erreurs exports ;
- latence websocket ;
- taille snapshots ;
- taille assets ;
- cout IA par org.

Alertes :

- build failures anormaux ;
- queue Temporal bloquee ;
- disque worker presque plein ;
- erreurs Postgres ;
- backup rate ;
- cout IA inhabituel ;
- explosion stockage.

---

## 7. Backups et disaster recovery

Obligatoire :

- backups Postgres automatiques ;
- backups R2/S3 ou versioning ;
- retention claire ;
- test de restauration regulier ;
- procedure de restauration documentee ;
- export complet projet telechargeable par l'utilisateur.

Un projet utilisateur ne doit jamais dependre uniquement de l'etat memoire d'un
serveur collaboration.

---

## 8. Roadmap technique recommandee

### Phase A - Contrat moteur

1. Definir `SaidaOp`.
2. Definir `EngineManifest`.
3. Extraire `SaidaAuthoringCore` depuis MCP.
4. Ajouter validation stricte.
5. Ajouter tests unitaires des ops.

### Phase B - Headless et snapshots

1. Creer `saida_tool`.
2. Valider projet/scene/script/scenario.
3. Appliquer ops sur snapshot.
4. Materialiser dossier projet.
5. Tests round-trip.

### Phase C - Collaboration SaaS minimale

1. OperationLog Postgres.
2. Service WebSocket.
3. Revision monotone.
4. Broadcast ops acceptees.
5. Snapshot periodique.
6. Presence Yjs.

### Phase D - Editeur web

1. Charger snapshot projet.
2. Afficher scene hierarchy + inspector.
3. Envoyer SaidaOps.
4. Recevoir ops distantes.
5. Integrer canvas WASM/WebGPU.
6. Edition scripts/UI avec Yjs + commit SaidaOp.

### Phase E - IA

1. Agent lit EngineManifest.
2. Agent produit SaidaOps.
3. Dry-run obligatoire.
4. Diff visible.
5. Validation headless.
6. Credits + audit.

### Phase F - Export serveur

1. Workflow Temporal web export.
2. Worker isole.
3. Upload artifact R2/S3.
4. Logs + status UI.
5. Credits reservation/commit/refund.

### Phase G - Production hardening

1. Quotas.
2. Rate limits.
3. Backups.
4. Observabilite.
5. Tests de charge.
6. Politique suppression RGPD.
7. Monitoring couts.

---

## 9. Criteres d'acceptation production

Saida peut etre considere production-ready quand :

- deux utilisateurs peuvent modifier la meme scene sans corruption ;
- une IA peut modifier scene/script/UI uniquement via SaidaOps ;
- chaque op est auditable ;
- une revision peut etre restauree ;
- un snapshot peut etre materialise en projet local ;
- un export web serveur fonctionne depuis snapshot ;
- un crash serveur ne perd pas les ops validees ;
- les assets lourds sont dans R2/S3 et deduplicables ;
- les builds sont isoles et limites ;
- les credits couvrent IA/build/stockage ;
- les permissions org/projet sont testees ;
- les backups ont ete restaures au moins une fois ;
- le moteur desktop et l'editeur web partagent le meme contrat d'authoring.

---

## 10. Pieges a eviter

Ne pas faire :

- faire de Yjs la source de verite metier ;
- laisser l'IA ecrire directement une scene JSON complete ;
- laisser l'IA executer du shell libre ;
- compiler du C++ utilisateur non sandboxe ;
- stocker les gros assets dans Postgres ;
- lancer Kubernetes trop tot ;
- maintenir deux modeles de scene differents ;
- casser le contrat nodes + behaviours + signaux ;
- faire du cloud rendering par defaut ;
- rendre obligatoire une recompilation moteur a chaque export web.

---

## 11. Architecture finale visee

```text
Browser
  Next.js UI
  Saida WASM/WebGPU runtime
  Yjs presence/text editing
  SaidaOp client
        |
        v
Fastify API
  Auth / Billing / Projects / Assets / Jobs
        |
        +--> Postgres
        +--> Redis
        +--> R2/S3
        +--> Temporal
        |
        v
Collaboration Gateway
  validates SaidaOps
  appends OperationLog
  broadcasts accepted ops
  writes snapshots
        |
        v
Temporal Workers
  AI agent workflows
  build/export workflows
  asset optimization
        |
        v
Saida Headless Tools
  describe-engine
  validate-project
  apply-ops
  export-web
  export-desktop
```

Le moteur reste le coeur du produit. Le SaaS orchestre, synchronise, facture,
stocke et securise. Les agents IA agissent avec des mains controlees.

