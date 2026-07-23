# SaidaEngine — Plan d'exécution du refactor V1

Date : 2026-07-21. Dérivé de [AUDIT_V1.md](AUDIT_V1.md). Objet : transformer le
moteur en une base **lisible, efficiente et navigable** — pour un contributeur
humain comme pour un LLM — sans changer un seul contrat public.

Ce document est un **plan de travail**, pas une source de vérité produit
(celles-ci restent [SPEC.md](SPEC.md) et [PLAN_V1.md](PLAN_V1.md)).

## 1. Objectifs & principes non négociables

1. **Aucun changement de comportement** dans un commit de refactor. Un changement
   de contrat (format, API JS, snapshot) est un commit séparé et explicite. Les
   fixtures V1, la matrice de types et les harnais Witness sont le filet.
2. **Une responsabilité par fichier**, fichier nommé d'après sa classe. L'en-tête
   de chaque fichier énonce en une phrase la responsabilité **et l'invariant**
   qu'elle protège.
3. **Pas de classe gratuite.** Une nouvelle classe n'existe que si elle possède un
   **état + un invariant** réels (un cache, une table GPU, une passe). Découper un
   fichier en morceaux sans frontière d'état est interdit.
4. **Réduire la LOC là où il y a de la duplication** (templates, source unique),
   pas en compressant du code lisible. Ailleurs, le découpage redistribue la LOC
   pour la clarté — c'est assumé.
5. **Tests verts à chaque étape.** Rebuild natif + Web + suite CTest + Witness
   desktop/Web avant de fermer une phase.

## 2. Métriques cibles (baseline 2026-07-21 → résultat)

| Métrique | Baseline | Cible initiale | Résultat / disposition |
|---|---|---|---|
| Plus gros fichier first-party | 1957 l. (`Renderer.cpp`) | ≤ 600 l. | 1975 l. ; Renderer transféré dans `TODO.md` avec prérequis visuel |
| Fichiers > 800 l. | 9 | 0 | 9 ; les candidats restants sont hors des unités retenues dans ce plan |
| Plus longue fonction | 213 l. (`gatherScene`) | ≤ 80 l. | heuristique : 483 l. dans `InspectorPanel` ; Renderer et autres candidats restent audités |
| `src/scene/` | 80 fichiers / 7954 l. | scindé en 3-4 dossiers | atteint structurellement : `scene/`, `nodes/`, `behaviours/`, `animation/` |
| Listes de registre de types parallèles | 3 (`ReflectedTypes*`) | 1 source filtrée | 3 conservées : leurs cibles ne lient pas les mêmes sous-systèmes |
| LOC first-party | ~73 000 | en baisse mesurable (dédup) | 74 864 ; clarté et tests privilégiés à une compression artificielle |

Phase 0 pose un script qui imprime ces chiffres pour objectiver chaque phase.

## 3. Réorganisation des dossiers

Frontières visées (un dossier = une couche claire) :

- **`src/rhi/`** — abstraction matérielle (Vulkan/WebGPU). Déjà fin (383 l.),
  **conserver tel quel**. C'est la référence de propreté.
- **`src/graphics/`** — ressources GPU : meshes, textures, matériaux, bindless,
  et les caches extraits de `ResourceManager` (§5.3). Pas de logique de frame.
- **`src/render/`** — le renderer et ses passes. Sa décomposition (`Renderer`
  mince + `SceneGatherer`/`TonemapPass`/`GIRenderer`/`GpuDrivenCuller`/`XrRenderer`)
  est différée dans [TODO.md](TODO.md) (risque : exactitude pixel non testée).
- **`src/scene/` (surchargé, 80 fichiers) → scinder** :
  - `scene/` : le graphe (`Node`, `Scene`, `SceneTree`, hiérarchie, signaux) ;
  - `nodes/` : les types de nœuds (`MeshNode`, `LightNode`, `CameraNode`,
    `WebCanvasNode`, `CollisionShapeNode`, `JointNode`, nœuds UI…) ;
  - `behaviours/` : les behaviours réfléchis ;
  - `animation/` : inchangé (déjà un sous-dossier cohérent).
- **`src/tools/` → `src/cli/`** : lève la confusion avec le dossier racine
  `tools/` (scripts de release). Contient `saida_tool` et les exporteurs.

Tous les mouvements se font par `git mv` + mise à jour des `#include` et des
listes de sources CMake, **sans changement de logique** (Phase 0).

## 4. Inventaire des fonctions trop longues

| Fonction | Lignes | Fichier | Découpe visée |
|---|---|---|---|
| `EditorUI::drawSettingsWindow` | 300 | editor/EditorUI.cpp:1518 | une section par onglet de réglages |
| `EditorUI::drawBuildWindow` | 273 | :935 | UI séparée de l'orchestration (`BuildController`) |
| `EditorUI::draw` | 171 | :233 | orchestration de panneaux, corps réduit |

Les fonctions longues du `Renderer` (`gatherScene` 213 l., `recordCommandBuffer`
164, `drawFrame` 118, `rebuildGlobalSet` 108) sont traitées avec la décomposition
Renderer dans [TODO.md](TODO.md).

## 5. Décompositions par god class

Convention des tableaux : *méthode actuelle → unité cible (état possédé)*.

### 5.1 `render/Renderer.cpp` (1957 l.) — **déplacé dans [TODO.md](TODO.md)**

La décomposition du Renderer est **sortie de ce plan** : sa correction n'est pas
couverte par le filet automatique (l'exactitude pixel du rendu), l'état est très
intriqué et le code a des branches `#ifdef` plateforme/XR. La faire à l'arrache
créerait du spaghetti. [TODO.md](TODO.md) garde la décomposition cible, détaille
les risques, et surtout **ce qu'il faut mettre en place avant** (un filet de vérif
visuelle golden-image + les deux règles anti-spaghetti) pour que ce soit propre et
tienne à long terme.

### 5.2 `editor/EditorUI.cpp` (1933 → 384 l., 31 → 17 méthodes) → shell + contrôleurs

| Unité | Méthodes reprises | État |
|---|---|---|
| **EditorShell** — boucle de dessin et routage commandes (`EditorUI`) | `draw`, `canEdit`, `markDirty`, `execute`, `drawAboutWindow` | ✅ **Fait** |
| **ProjectDialogs** | `drawNewProjectDialog`, `drawOpenProjectDialog`, `drawSaveSceneAsDialog`, `startProjectScan`, `loadProjectMainScene`, `resolveScenePath` | ✅ **Fait** |
| **SceneDocumentActions** (étend `editor/SceneDocument`) | `saveScene`, `loadScene`, `copySelected`, `pasteClipboard`, `duplicateSelected` | ✅ **Fait** |
| **BuildController** — UI + orchestration au-dessus de `BuildExporter` | `refreshBuildScenes_`, `executeBuild`, `runAutomatedBuild`, `drawBuildWindow` | ✅ **Fait** |
| **GizmoController** — état de manipulation + rendu gizmo | `drawGizmo`, `updateGizmoHover`, `handleGizmoDrag`, `performRaycastSelection`, `renderGizmoRotationRings`, `renderGizmoTranslateScale`, `drawColliderGizmos` | ✅ **Fait** |
| **SettingsWindow** — préférences persistantes + style | `applyEditorStyle`, `drawSettingsWindow` (découpée par section) | ✅ **Fait** |
| **ModelImporterPanel** | `openModelImporter`, `closeModelImporter` | ✅ **Fait** |

**`GizmoController` (fait).** Nouvelle classe `editor/GizmoController.{hpp,cpp}`. Elle
**possède** l'état de manipulation (le drag transactionnel `grabbedAxis_` +
snapshot `dragStart*`) et le cache géométrique écran reconstruit par frame
(`gizmoNodePos_`/`gizmoWorldLength_`/`gizmoCenter2D_`/`gizmoEnds2D_`/
`gizmoLocalAxes_`/`gizmoAxisValid_`). Invariant : `grabbedAxis_ != None ⟺ un drag
est en cours et `dragStart*` tient la transform d'origine (base de l'undo). Les
helpers libres gizmo (`intersectRayPlane`, `distanceToSegment`, `GizmoConfig`,
`kPi`, `projectPoint`) descendent dans le TU du contrôleur. L'état **partagé**
reste dans `EditorUI` : `gizmoMode_` (posé par la toolbar du viewport + clavier),
`viewportPos_`/`viewportSize_`, `selectedNode_`, `execute()` ; le contrôleur y
accède en `friend` via une réf `EditorUI&` (même patron que les panneaux). Move
mécanique pur (logique inchangée). *Vérifié : build natif propre (`-Wall -Wextra`,
zéro warning), 69/69 CTest, `witness_editor_play` PASS (run+restart). Reste non
couvert par l'auto — l'interaction gizmo en mode Scene (drag T/R/S, clic-sélection,
wireframe colliders) demande une vérif manuelle au viewport.*

**`BuildController` (fait).** Nouvelle classe
`editor/BuildController.{hpp,cpp}`, sans référence vers `EditorUI` et sans
`friend`. Elle possède l'intégralité de l'état du modal (plateforme,
configuration, chemins/métadonnées, scènes, résultat et demande d'ouverture).
Invariant : le clic interactif et `--build` convergent vers le même
`executeBuild()` ; la sélection de scène, les options `BuildExporter` et le
reporting ne peuvent donc pas diverger. `EditorUI` ne garde qu'une façade
`runAutomatedBuild` pour son contrat avec `EditorApp`, et le menu ne fait que
demander l'ouverture au contrôleur. Les deux anciens booléens de scène jamais lus
ont été supprimés. *Vérifié : build natif propre, 69/69 CTest,
`witness_editor_build` PASS sur le chemin éditeur exact puis run+restart de
l'artefact. Reste hors auto : navigation et édition manuelles du modal ImGui.*

**`SceneDocumentActions` (fait).** `SceneDocument` possède maintenant le chemin
courant, le presse-papiers sérialisé et les opérations save/load/copy ainsi que
paste/duplicate via `CommandHistory`. Invariant : sélection et commandes
continuent de cibler des `NodeId` stables ; aucun nœud du presse-papiers ou de
l'historique n'est conservé par pointeur brut. Les façades `EditorUI` ne font plus
que préserver le verrou Play et synchroniser son pointeur de sélection legacy.
Les panneaux ne transportent plus de `Scene*`/`ResourceManager*` inutiles pour ces
actions. *Vérifié : build natif sans warning, 69/69 CTest,
`witness_editor_play` PASS (run+restart, UI incluse).*

**`ModelImporterPanel` (fait).** Le panneau persistant possède désormais
`active_`, le chemin source, la scène de preview, la vitesse d'animation et son
état d'export. Invariant : `active() == true` implique que `previewScene()` est la
scène exacte installée comme override par `EditorApp`; `close()` détruit cette
scène et désactive l'override. `EditorUI` ne conserve qu'une façade `open` pour
les panneaux appelants. La durée d'affichage du message d'export reste identique
à l'ancien panneau recréé par frame. *Vérifié : build natif sans warning, 69/69
CTest, `witness_editor_play` PASS (run+restart, UI incluse). La sélection de clip
et le bouton d'export du panneau restent hors automatisation.*

**`ProjectDialogs` (fait).** La classe possède les trois demandes d'ouverture,
leurs buffers, le cache de projets et l'unique `future` de scan récursif.
Invariant : le worker ne touche qu'à son argument racine et retourne une liste
triée ; seul le thread UI remplace le cache une fois le future prêt. Les dialogs
remontent des `Actions` (`browseRoot`, `sceneToSave`) au lieu d'accéder à
`EditorUI`. La résolution déterministe de la scène principale est également
centralisée (déclaration → `scenes/main.scene` → première scène triée).
*Vérifié : build natif sans warning, 69/69 CTest, `witness_editor_play` PASS
(run+restart, UI incluse). Les clics manuels des trois modals restent hors auto.*

**`SettingsWindow` + `EditorShell` (faits).** `SettingsWindow` possède la demande
d'ouverture, le thème persistant, les préférences UI temporaires et les buffers
d'alias/autoload. Invariant : un changement de thème applique ensemble layout et
couleurs puis persiste la préférence ; chaque onglet est une section bornée
(≤ 75 lignes, sauf la table de couleurs déclarative à 83 lignes). Les copies de
chemins déposés garantissent désormais explicitement la terminaison des buffers.
`EditorUI` est le shell final : il orchestre les panneaux et contrôleurs existants,
garde l'état réellement partagé et ne contient plus aucun grand modal. Créer une
classe `EditorShell` supplémentaire n'aurait ajouté ni état ni invariant et aurait
violé la règle « pas de classe gratuite ». La boucle de frame et les UI de
`BuildController`, `ProjectDialogs`, `ModelImporterPanel` et `GizmoController`
sont découpées en sections nommées de 80 lignes maximum. *Vérifié : build natif
sans warning, 70/70 CTest, `witness_editor_play` PASS (run+restart, UI incluse).
Les interactions manuelles dans chaque onglet Settings restent hors
automatisation.*

### 5.3 `graphics/ResourceManager.cpp` (1102 → 414 l.) → façade + caches

**Fait (6 unités, chacune un concept + invariant, vérifiées natif+web+Witness) :**

| Unité (état/invariant) | Rôle |
|---|---|
| **`AsyncAssetCache<T>`** (`badcac3`) | cache async générique {Loading/Ready/Failed} ; absorbe la triplication `load/get/state/error` de Rig/ClipView/AnimGraph |
| **`BindlessTables`** (`8f772d5`) | set descripteur bindless (texture-array binding 0 + SSBO matériaux binding 1) + alloc/recyclage d'index & slots ; **MaterialTable absorbée dedans** ; slot/index 0 = fallbacks jamais recyclés |
| **`GpuGraveyard`** (`5b7f13c`) | destruction GPU différée sûre pour les frames en vol (`Retired` + `retire`/`drain`, invariant `kRetireFrames`) + recyclage des slots |
| **`MeshCache`** (2026-07-23) | cache mesh complet : proxies async, index inverse, sous-total GPU et LRU ; chaque entrée possède un `Mesh*` stable, indexé par son `AssetID`, et les candidats/évictions restent encapsulés |
| **`TextureCache`** (2026-07-23) | cache texture complet : décodage async, échecs, fallbacks non évictables, inscription bindless, sous-total GPU et LRU ; les IDs finalisés remontent sans dépendance vers `Material` |
| **`GpuBudget`** (2026-07-23) | politique de résidence GPU : horloge de frame, photographie mesh/texture vivante, merge LRU concret, limite, compteurs et warning ; ne possède aucune ressource |

Les deux caches ont la même frontière sans héritage : `ResourceManager` ne voit
aucune map de ressources. `GpuBudget` coordonne les deux caches concrets par
références, fusionne leurs candidats et porte toute la politique ; aucune
interface virtuelle `ResourceCache`, aucun accès aux représentations internes.

**Ownership (frontières d'état) :**
- ✅ `MeshCache` possède `meshes_`, `reverseMap_`, `pending_`, **son**
  `residentBytes_` (sous-total mesh) et **son** `lastUse_` (LRU mesh). API interne :
  `get`/`load`/`finalizePending`/`registerMemory`×2/`idFor`, plus
  `sweepUnused`/`collectEvictionCandidates`/`evict` pour la coordination.
- ✅ `TextureCache` possède `textures_`, `pending_`, `failed_`, les trois
  fallbacks, **son** `residentBytes_` et **son** `lastUse_`. API :
  `get`/`finalizePending`/`registerMemory`/`registerGenerated`, plus la même
  interface `sweepUnused`/`collectEvictionCandidates`/`evict`. Il remonte les IDs
  terminés ; `ResourceManager` garde seulement le rebind des matériaux.
- ✅ `GpuBudget` possède `frameClock_`, la limite, les compteurs, le warning et
  la photographie vivante limitée aux meshes/textures. **Aucune ressource** :
  il tient seulement `MeshCache&`, `TextureCache&` et `GpuGraveyard&`.

**Interface concrète cache→budget (zéro virtuel — le budget tient `MeshCache&` +
`TextureCache&` et appelle les deux nommément) :**
```
uint64_t residentBytes() const;                        // total du cache
uint64_t sweepUnused(const LiveSet&, GpuGraveyard&, uint64_t frame);
void     collectEvictionCandidates(const LiveSet&, vector<Candidate>&) const;
uint64_t evict(AssetID, GpuGraveyard&, uint64_t frame);
```
`Candidate { AssetID id; uint64_t lastUse; CacheTag tag; }`. Le budget **merge**
les candidats des deux caches, **trie** par `lastUse`, et évince LRU-first en
dispatchant sur `tag` (2 branches concrètes, zéro dispatch virtuel).

**Coordination (`GpuBudget`) :**
- `gpuResidentBytes()` public = `mesh.residentBytes() + tex.residentBytes()`.
- `trimUnused(live)` = `mesh.sweepUnused(...)` + `tex.sweepUnused(...)` + log agrégé.
- `enforce()` par frame (si sur-budget) = merge → tri LRU → evict jusqu'à
  sous-budget, sinon 1 warning mesuré. Évincés → `GpuGraveyard` (déjà partagé).
- `frameClock_` possédé par le budget ; il passe sa valeur aux caches pour dater
  `lastUse` dans `get()` — pas de référence stockée, donc pas de cycle.

**Pourquoi ça améliore (et n'ajoute pas de spaghetti) :** chaque unité a un état
et une responsabilité explicites ; le budget ne connaît que l'API publique des
deux caches, jamais leurs maps. `ResourceManager` conserve son contrat externe et
les responsabilités transversales qui lui appartiennent encore (rebind matériaux,
rigs/clips), sans porter la stratégie de résidence GPU.

### 5.4 `mcp/McpBridge.cpp` (1401 → 77 l.) → bridge mince + modules d'outils

Les outils, déjà fonctions libres, sont regroupés par domaine dans `mcp/tools/`.
Chaque module enregistre son schéma et son handler dans le même `ToolRegistry` ;
`McpBridge` ne fait plus que construire le contexte éditeur et router le protocole.
Invariant : un nom est unique et `tools/list` ne peut pas diverger de `tools/call`.

| Module `mcp/tools/` | Outils |
|---|---|
| **IntrospectionTools** (154 l.) | manifeste, listes de types, guide et recettes |
| **SceneTools** (130 l.) | lecture de scène/nœud, recherche, signaux, réglages |
| **NodeTools** (273 l.) | mutations de nœuds/behaviours, propriétés, import modèle |
| **AssetTools** (230 l.) | écriture JS/UI/C++, build, check headless, logs |
| **ScenarioTools** (152 l.) | catalogue, lecture, validation, édition, attachement |
| **AnimationTools** (464 l.) | inspection, validation, création, patch et preview |
| **ToolContext** (106 l.) | contexte partagé, garde d'édition, IDs et chemins sandboxés |
| **ToolRegistry** (60 l.) | catalogue unique schéma + handler, rejet des doublons |

Les 45 noms historiques sont couverts par `saida_mcp_tool_registry_tests`, qui
vérifie aussi les schémas de base, l'unicité et les erreurs de dispatch. *Vérifié :
build natif propre, 70/70 CTest, smoke TCP réel `initialize` + `tools/list` +
`tools/call` (45 outils), `witness_editor_play` PASS (run+restart).*

### 5.5 Registre de types — décision : conserver les 3 listes

`ReflectedTypes.cpp`, `ReflectedTypesPlayer.cpp` et `ReflectedTypesWeb.cpp`
restent distincts. Les trois cibles ne compilent ni ne lient les mêmes
sous-systèmes : physique/Jolt, audio, QuickJS et scénario sont volontairement
absents de certaines variantes Web. Une liste source unique imposerait donc des
`#ifdef` par type ou des symboles non liés, ce qui rendrait le code plus fragile.
La `runtimeTypeMatrix` reste le garde-fou de parité. Les deux petits templates
d'enregistrement identiques ne sont pas extraits : le gain ne justifie pas un
header transversal supplémentaire sans état ni invariant.

## 6. Phasage d'exécution

Ordonné par isolement et par gain, chaque phase reste verte de bout en bout.

- **Phase 0 — Garde-fous. ✅ Fait (2026-07-22).** Script de métriques
  [`tools/code_metrics.sh`](tools/code_metrics.sh) (baseline : 497 fichiers,
  73 215 LOC, 17 fichiers > 600 l.). Baseline verte confirmée (build natif propre,
  69/69 CTest). Déplacements de dossiers (§3) faits par `git mv` purs +
  réécriture `#include`/CMake, zéro logique, chacun revérifié build + 69/69 :
  `src/tools`→`src/cli` ; split de `src/scene/` (130 → 88 fichiers) en
  `scene/` (graphe core), `nodes/` (14 types de nœuds), `behaviours/` (7
  behaviours concrets), `animation/` (déjà là). *Vérifié via build + CTest ; les
  moves étant des relocalisations sans changement de logique, les harnais Witness
  lourds n'ont pas été rejoués.*
- **Phase 1 — ResourceManager (§5.3). ✅ Fait (2026-07-23) — 6 extractions nettes.**
  `ResourceManager.cpp` **1102 → 414 lignes**. Faits : **`AsyncAssetCache<T>`**
  (`badcac3`), **`BindlessTables`** (`8f772d5`, MaterialTable absorbée — SSBO =
  binding 1 du set bindless), **`GpuGraveyard`** (`5b7f13c`, destruction différée
  in-flight-safe + recyclage des slots, invariant `kRetireFrames`) et
  **`MeshCache`** (proxies async + identité pointeur/ID + sous-total/LRU) et
  **`TextureCache`** (décodage/échecs + fallbacks + bindless + sous-total/LRU),
  puis **`GpuBudget`** (horloge, live-set GPU, limite, compteurs, merge LRU et
  éviction). Sweep, collecte de candidats et éviction restent encapsulés dans
  chaque cache ; le budget les coordonne sans connaître aucune map et sans
  interface virtuelle. *Vérifié le 2026-07-23 : build natif propre, 69/69 CTest,
  player Web, runtime Web et authoring WASM, Witness E2E PASS (run + restart).*
- **Phase 2 — Registre unique (§5.5). ✅ Clôturée par décision.** Les
  trois `ReflectedTypes*.cpp` diffèrent par **nécessité de build**, pas par
  accident : le player web et le viewer d'authoring excluent délibérément des
  sous-systèmes (physique/Jolt, audio, QuickJS, scénario) **non compilés/liés**
  dans ces cibles — on ne peut pas les `#include` puis filtrer sans casser le link.
  Une « source unique » forcerait des `#ifdef` par sous-système = moins lisible et
  risqué. Les templates `registerBehaviour<T>` / `registerNode<T>` identiques
  restent locaux : leur faible duplication est plus lisible qu'un header
  transversal sans invariant.
- **Phase 3 — Renderer. ✅ Clôturée dans ce plan → [TODO.md](TODO.md).** Trop risquée
  sans filet de vérif visuelle (exactitude pixel non testée, état intriqué,
  branches `#ifdef`). TODO.md dit pourquoi et ce qu'il faut d'abord (golden-image
  + règles anti-spaghetti). Ne pas l'attaquer avant d'avoir ce filet.
- **Phase 4 — EditorUI (§5.2). ✅ Faite — 7 unités cohérentes.**
  `EditorUI.cpp` **1933 → 384 l.** `GizmoController` possède l'état de
  manipulation, le rendu gizmo et les wireframes colliders ;
  `BuildController` possède le modal et l'orchestration au-dessus de
  `BuildExporter`, avec un chemin unique bouton/`--build` ; `SceneDocument`
  possède les actions et l'état sérialisé du document ; `ModelImporterPanel`
  possède la preview et sa fenêtre ; `ProjectDialogs` possède les modals et leur
  scan asynchrone ; `SettingsWindow` possède le thème et les préférences.
  `EditorUI` reste intentionnellement le shell final, sans classe miroir.
  Vérifié : build natif propre, 70/70 CTest,
  `witness_editor_play` pour le document/gizmo/importer/dialogs et
  `witness_editor_build` PASS (export éditeur exact + run/restart) pour le build.
  ⚠️ Rappel : aucun test auto n'exerce les gizmos/panneaux/dialogues en mode
  édition ; une vérification manuelle au viewport reste recommandée.
- **Phase 5 — McpBridge (§5.4). ✅ Faite — 6 domaines + contexte/registre.**
  `McpBridge.cpp` **1401 → 77 l.** Les 45 handlers ont été déplacés sans changer
  leur logique. Le registre associe chaque schéma à son handler et refuse les
  doublons ; `ToolContext` centralise la garde Play, les IDs et les chemins
  sandboxés. Vérifié : build natif propre, 70/70 CTest dont le nouveau test du
  catalogue complet, smoke TCP réel (initialize/list/call) et
  `witness_editor_play` PASS (run+restart).
- **Phase 6 — Magic numbers (AUDIT §D) + polish. ✅ Faite.** `InputLimits.hpp`
  fournit une source unique pour la deadzone par défaut, sa borne maximale et la
  distance touch maximale ; binding, backend gamepad et parser de profils
  utilisent les mêmes limites. Dans `Renderer.cpp`, les littéraux réellement
  sémantiques portent maintenant leur rôle : distance d'ombre par défaut, seuil
  de colinéarité de l'up vector, espacements des probes GI par tier, plancher AO
  et rayons de bounds. Les valeurs `1.2/1.5/2.4` étaient des espacements GI, pas
  des coefficients de tonemap. Aucun contrat ni calcul n'a changé.

## Exigences de vérification par phase (règle : ne jamais pousser du non-vérifié)

Le net disponible et prouvé sur la machine de dev : build natif UCRT64 +
`ctest` (70/70), **Witness E2E** `tools/witness_e2e.sh` (rendu +
gameplay + save/restart réels, ~1-2 min), et les **3 builds web** emsdk
(`build-web-player`/`build-web`/`build-authoring-wasm`, compile web, ~5-10 min).
Ce net couvre : compilation (natif+web), logique headless, gameplay, rendu HUD.
Il **ne couvre pas** automatiquement : exactitude pixel du rendu, GUI éditeur,
outils MCP, XR. Toute phase touchant ces zones doit être vérifiée **manuellement**
en plus du net, donc menée en session supervisée — pas en autonome.

**Piège des déplacements de dossiers** (rencontré en Phase 0) : les cibles web ont
des **listes de sources CMake séparées** (`web/*/CMakeLists.txt`) du `CMakeLists.txt`
principal. Tout `git mv` doit mettre à jour **les deux**, sinon le build web casse
sans que le build natif ne le voie. Toujours lancer un build web après un move.

## 7. Règles d'exécution (pour un LLM ou un contributeur)

- Un commit = une unité extraite, **sans changement de comportement**. Le message
  dit quoi a bougé et pourquoi, et confirme la suite verte.
- Après extraction, l'ancien fichier ne garde que l'orchestration ; les
  fonctions de l'unité extraite visent ~80 lignes. Les exceptions déclaratives
  et les gros fichiers hors périmètre sont nommés explicitement, jamais masqués.
- Rebuild **natif + authoring WASM + player Web** après tout ce qui touche un type
  réfléchi ou le registre (règle SPEC/README existante).
- Contrats publics inchangés : mêmes octets durables, mêmes formats, même API JS.
  Un changement de contrat sort du refactor.
- Cocher les cases correspondantes dans `AUDIT_V1.md` au fil des phases.

## 8. Ce que le refactor NE touche pas

`rhi/`, les contrats SaidaOps/snapshot, le runtime QuickJS, le fail-closed des
loaders, le corpus de formats figé et les harnais Witness : stables. Le refactor
est **interne** ; il rend le moteur lisible sans rouvrir la moindre gate V1.

## 9. Clôture

Ce plan ne contient plus de phase ouverte : 0, 1, 4, 5 et 6 sont implémentées ;
la Phase 2 est close par une décision d'architecture documentée ; la Phase 3 est
transférée dans `TODO.md` jusqu'à l'existence d'un filet golden-image.

Mesure finale de `tools/code_metrics.sh` : **529 fichiers / 74 864 LOC**,
`ResourceManager.cpp` **1102 → 414 l.**, `EditorUI.cpp` **1933 → 384 l.** et
`McpBridge.cpp` **1401 → 77 l.** Les objectifs globaux non atteints restent
visibles dans le tableau §2 : ils ne sont pas présentés comme terminés et n'ont
pas été « résolus » par compression ou par extraction sans invariant.

Validation finale : build natif propre, **70/70 CTest**, trois builds Web
(player, runtime, authoring WASM), Witness E2E, `witness_editor_play`,
`witness_editor_build`, staging Web et smoke MCP TCP réel
(`initialize`/`tools/list`/`tools/call`, 45 outils).
