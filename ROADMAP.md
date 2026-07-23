# SaidaEngine — Roadmap

Mise à jour : 2026-07-24. Ce fichier est le **backlog unique** du moteur : tout
ce qui reste à faire, différé ou décidé pour plus tard. Il ne décrit pas ce qui
existe — la vérité technique est dans [SPEC.md](SPEC.md), la prise en main dans
[README.md](README.md).

Règle : rien n'est coché ici sans le run, le commit ou le corpus exact qui le
prouve. Les travaux clos (gates V1, refactor V1) vivent dans l'historique Git et
dans les contrats correspondants de `SPEC.md`.

## 0. État

Le chantier **V1** est terminé côté moteur : les gates P0.1 à P0.6 sont fermées,
le refactor V1 dérivé de l'audit qualité est clos (ResourceManager 1102 → 414 l.,
EditorUI 1933 → 384 l., McpBridge 1401 → 77 l., découpage de `src/scene/`,
constantes nommées Input/Renderer), et la validation finale est passée : build
natif propre, 70/70 CTest, trois builds Web (player, runtime, authoring WASM),
Witness E2E, `witness_editor_play`, `witness_editor_build`, staging Web et smoke
MCP TCP réel (45 outils).

Il reste **une seule intervention avant publication** : §1.

Rappel des critères de release : le même WitnessGame doit tourner en éditeur,
desktop autonome et Web ; les anciens projets doivent migrer ou être refusés sans
corruption ; la mémoire doit rester bornée ; les limitations publiées doivent
correspondre au comportement observé ; les artefacts doivent venir d'un commit
propre.

## 1. Avant publication — signature de l'installeur

- [ ] Produire l'installeur Windows **signé Authenticode** avec la clé de
  publication, puis inventorier le SHA-256 des octets signés.

Le reste de la chaîne release est fermé : CI obligatoire (build natif, 70 tests,
corpus V1, fold déterministe, Witness desktop et Web) ; `saida_tool`, player Web
et authoring WASM publiés comme artefacts pinnés ; preuve byte-identique
Windows/Linux sur les fixtures de fold ; archives et installeur NSIS
byte-reproductibles **avant signature**, validation récursive des imports DLL,
désinstallation inventoriée, rollback immuable documenté ; crash logs et symboles
liés à la version ; SBOM SPDX + notices GPL/tiers + inventaire licences/assets.
La signature est une opération de publication séparée qui requiert un certificat
de confiance publique.

## 2. Dette structurelle du code

Constat de l'audit qualité : la codebase est saine et disciplinée (dette
TODO/FIXME quasi nulle, politique fail-closed systématique, corpus de formats
figé, capacités vérifiées au démarrage). Les points ci-dessous ne cassent rien ;
ils coûteront de plus en plus cher à mesure que le moteur s'enrichit — le flag
GPU-driven, le XR et le rendu avancé sont justement bloqués derrière la première
ligne du tableau.

### 2.1 God classes restantes

| Fichier | Lignes | Domaines mêlés |
|---|---|---|
| `src/render/Renderer.cpp` | 1975 | pipelines, descriptors, GI, tonemap, GPU-culling, shadows, XR, frame — voir §3 |
| `src/scripting/JsEngineBindings.cpp` | 1853 | tous les globals JS (`node`/`time`/`input`/`tree`/`assets`/`audio`/`physics`/`storage`) → un module par namespace |
| `src/editor/panels/InspectorPanel.cpp` | 1336 | inspecteur de tous les types de nœuds → une section par famille de nœud |
| `src/scene/WebCanvasNode.cpp` | 1280 | un seul nœud : RmlUi + panneau world + scripts + hot-reload + placeholder |
| `src/core/Input.cpp` | 1050 | clavier + souris + gamepad + touch + backend web + profils → un backend par périphérique |

Ces découpages violent une règle que le dépôt s'impose déjà (README, « Règles de
contribution ») : *scinder les classes omniscientes et les fichiers qui mélangent
plusieurs domaines*. Ils restent hors périmètre tant qu'aucun travail n'ouvre ces
fichiers.

**Contraintes à respecter pour toute extraction** (issues du refactor V1, elles
ont fait leurs preuves) :

- **Pas de classe gratuite.** Une nouvelle classe n'existe que si elle possède un
  **état + un invariant** réels. Découper un fichier sans frontière d'état est
  interdit.
- Une responsabilité par fichier, fichier nommé d'après sa classe ; l'en-tête
  énonce en une phrase la responsabilité **et l'invariant**.
- Un commit = une unité extraite, **sans changement de comportement**. Un
  changement de contrat (format, API JS, snapshot) est un commit séparé.
- Rebuild **natif + authoring WASM + player Web** dès qu'un type réfléchi ou le
  registre est touché. Les cibles web ont des listes de sources CMake séparées
  (`web/*/CMakeLists.txt`) : tout `git mv` doit mettre à jour **les deux**, sinon
  le build web casse sans que le natif ne le voie.
- `tools/code_metrics.sh` imprime les chiffres (fichiers, LOC, fichiers > 600 l.,
  heuristique de fonction longue) pour objectiver avant/après. Dernière mesure :
  **529 fichiers / 74 864 LOC**.

### 2.2 Fonctions trop longues

Au-delà de ~80 lignes une fonction devient difficile à tester et à raisonner. Les
plus longues connues sont dans `Renderer.cpp` et sont traitées avec §3 :
`gatherScene` (213 l., `src/render/Renderer.cpp:704`), `recordCommandBuffer`
(164, `:1328`), `drawFrame` (118, `:1492`), `rebuildGlobalSet` (108, `:477`).
`InspectorPanel` porte le maximum mesuré par l'heuristique (483 l.).

### 2.3 Hygiène

- **Fins de ligne** : plusieurs fichiers du working tree sont matérialisés en
  CRLF (`src/graphics/Mesh.{cpp,hpp}`, `VulkanDevice.hpp`, `hub/Hub.cpp`,
  `editor/BuildExporter.hpp`, …). `.gitattributes` (`* text=auto eol=lf`) les
  normalise au commit, mais l'incohérence traîne côté disque et pollue les diffs.
- **Dette TODO/FIXME quasi nulle** (2 `XXX`, 1 `BUG` sur tout l'arbre) : un acquis
  à préserver.

## 3. Renderer — décomposition différée (prérequis bloquant)

`src/render/Renderer.cpp` (1975 l.) est le plus gros god class et la
décomposition à plus forte valeur : elle débloque XR, le flag GPU-driven et les
lightmaps en les rendant testables isolément. Elle est **volontairement différée**
— elle a été sortie du refactor V1 parce qu'elle n'est pas sûre à faire
mécaniquement.

### Décomposition cible (à exécuter *quand le filet visuel existe*)

| Unité (état/invariant) | Méthodes reprises |
|---|---|
| **PipelineCache** — pipelines & layouts ; reconstruits sur changement de format/swapchain | `createGlobalSetLayout`, `createPipeline`, `createWebCanvasWorldPipeline`, `createCullingPipeline`, `createTonemapPipeline`, `createXrPipelines` |
| **FrameDescriptors** — sets descripteurs globaux + UBO par frame-in-flight | `createUniformBuffers`, `createGlobalDescriptorSets`, `rebuildGlobalSet`, `updateGlobalShadowDescriptor`, `updateUniformBuffer` |
| **GIRenderer** — descripteurs GI + cadence/signature de dirty | `shouldUpdateRealtimeGI`, `giDirtySignature`, `updateGIDescriptors`, `updateEnvironmentDescriptor` |
| **TonemapPass** — cibles HDR + pipeline/descripteurs tonemap | `createHdrResources`, `cleanupHdrResources`, `createTonemapPipeline`, `updateTonemapDescriptorSet`, `tonemapPushConstants`, `recordTonemapPass` |
| **GpuDrivenCuller** — buffers indirect draw + pipeline culling (derrière le flag) | `createGpuDrivenBuffers`, `uploadGpuDrivenDraws`, `featureDraws`, `buildFeatures`, `recordFeatures` |
| **XrRenderer** — cibles/pipelines XR + UBO multiview | `createXrTargets`, `cleanupXrTargets`, `updateXrTonemapDescriptorSets`, `updateUniformBufferXr`, `recordXrScenePass`, `recordXrWorldWebCanvases` |
| **SceneGatherer** — draw-list + lighting UBO depuis la scène | `gatherScene` (découpée), `recordMeshDraws`, `recordWorldWebCanvases`, `recordShadowPasses` |
| **Renderer** (reste, mince) — orchestration de frame | `setViewportRect`, `clearViewportRect`, `activeRenderRect`, `drawFrame`, `recordCommandBuffer` |

### Pourquoi c'est risqué (à ne pas faire à l'aveugle)

1. **Aucune vérif automatique de l'exactitude du rendu.** Witness E2E confirme
   que le HUD et la scène *s'affichent* (pixels non nuls, pas de crash), mais PAS
   l'exactitude colorimétrique/géométrique. Une erreur subtile — mauvais
   descripteur, ordre de passe inversé, biais d'ombre changé, blend faux —
   passerait le filet et casserait le rendu **en silence**.
2. **Branches `#ifdef` par plateforme/feature** (`SAIDA_RHI_WEBGPU`,
   `SAIDA_ENABLE_XR`). Le build natif n'exerce qu'une branche ; le build web
   compile l'autre mais ne l'exécute pas sur cette machine.
3. **État profondément intriqué.** GI, shadows, tonemap, GPU-culling et XR
   partagent les descriptor sets globaux et l'orchestration de frame
   (`drawFrame`/`recordCommandBuffer`). Couper au mauvais endroit crée des
   dépendances croisées entre passes : **exactement le spaghetti qu'on veut
   éviter.**
4. **Fonctions très longues** à découper en même temps (§2.2).

### Ce qu'il faut faire AVANT

1. **Établir un filet de vérif visuelle. Prérequis absolu.** Sans lui, ne rien
   refactorer dans le Renderer. Un harnais de rendu déterministe qui capture une
   frame de référence (*golden image*) d'une scène fixe et la compare
   pixel-à-pixel (tolérance bornée), lancé sur Lavapipe en CI **et** sur GPU réel.
   Le HUD a déjà des assertions de pixels calculés (`saida_ui_corpus_tests`) —
   étendre cette idée à une frame de scène complète (mesh + lumière + ombre +
   tonemap).
2. **Extraire une unité à la fois, en ordre feuille-d'abord**, un commit + une
   vérif visuelle par unité : `TonemapPass` (frontière nette : entrée = cible HDR,
   sortie = swapchain) → `GpuDrivenCuller` (derrière son flag) → `XrRenderer`
   (guardé) → `GIRenderer` → `PipelineCache`/`FrameDescriptors` → `Renderer` mince
   en dernier.
3. **Règle anti-spaghetti n°1 : les passes ne s'appellent jamais entre elles.**
   Seul `Renderer::drawFrame` séquence les passes. Chaque unité possède ses objets
   GPU (pipelines, descripteurs, cibles) avec un cycle create/destroy lié aux
   changements de swapchain/format, et expose seulement `record*`/`update*`.
   Aucune unité ne lit les descripteurs d'une autre.
4. **Règle anti-spaghetti n°2 : les variations `#ifdef` restent DANS chaque
   unité.** `TonemapPass` gère Vulkan vs WebGPU en interne ; l'orchestrateur de
   frame reste agnostique.
5. **Vérifier chaque unité** : build natif **+ web** + Witness E2E + **diff
   visuel** (point 1) + smoke XR si un casque est disponible.

## 4. Vérifications manuelles encore utiles

Le filet automatique couvre : compilation (natif + web), logique headless,
gameplay, rendu HUD. Il **ne couvre pas** : exactitude pixel du rendu, GUI
éditeur, XR, sémantique des mutations MCP. Toute session touchant ces zones doit
être supervisée, pas autonome.

- **Éditeur** — `witness_editor_play` et `witness_editor_build` couvrent les
  chemins Play/build, mais pas les clics ImGui en mode édition. Vérifier
  manuellement gizmos (drag T/R/S, clic-sélection, wireframe colliders), panneaux,
  onglets Settings et les trois modals de projet au viewport lors d'une prochaine
  session UI.
- **MCP** — le catalogue des 45 outils, l'unicité, le dispatch d'erreur et un
  appel TCP réel sont couverts. Ajouter progressivement des tests sémantiques par
  handler de mutation quand ces outils évoluent.

## 5. P1 — Qualité des sous-systèmes

Post-V1 sauf changement explicite de périmètre.

- [ ] XR : valider casques/runtimes ciblés, contrôleurs et hand tracking.
- [ ] XR : MSAA multiview/resolve, overlay ImGui et backend d'anchors réel.
- [ ] Physique : compléter queries, contraintes (slider, cône, moteurs) et
  diagnostics.
- [ ] Animation : API étendue (scrub, root motion JS) et retargeting BVH.
- [ ] Stabiliser le flag GPU-driven et benchmarker chemin classique, bindless,
  indirect draw et compute culling sur un corpus reproductible.
- [ ] Rendu : point-light cubemap shadows et persistance des lightmaps si
  incluses dans la promesse produit.
- [ ] Tangentes MikkTSpace (la normal map reste désactivée sans tangentes
  d'auteur).
- [ ] Adaptation visuelle des prompts aux manettes physiques Xbox/PlayStation.
- [ ] Mesurer et optimiser LTO seulement après stabilité.

## 6. P2 — Hors périmètre, conservé comme décision

Ces éléments ne retardent aucune release sauf changement explicite de promesse :

- multiplayer réseau, grands terrains et frameworks de genre ;
- SIMD animation généralisé, pose sharing massif et GPU crowds ;
- Radiance Cascades et recherches GI avancées ;
- backend GPU RmlUi tant que le backend CPU respecte les budgets ;
- KTX2/Basis (textures PNG/JPG partout aujourd'hui) ;
- graph SaidaFX complet, trails/ribbons et collisions particules avancées ;
- world model, skills et agents autonomes ;
- store d'assets et services en ligne, portés par la plateforme Saida ;
- éditeur/player Linux et macOS, Firefox/Safari et navigateurs mobiles.

## 7. Décisions closes — ne pas rouvrir sans raison nouvelle

- **Trois listes `ReflectedTypes*`** (`.cpp`, `Player`, `Web`) conservées
  distinctes. Les cibles ne compilent ni ne lient les mêmes sous-systèmes
  (physique/Jolt, audio, QuickJS, scénario volontairement absents de certaines
  variantes Web) : une source unique imposerait des `#ifdef` par type ou des
  symboles non liés. `runtimeTypeMatrix`, vérifiée au boot, reste le garde-fou de
  parité. Les templates `registerBehaviour<T>`/`registerNode<T>` identiques
  restent locaux : leur faible duplication est plus lisible qu'un header
  transversal sans invariant.
- **MCP (`SAIDA_ENABLE_MCP`)** est une capacité V1 assumée de l'atelier Saida,
  ON par défaut, couverte par un test de catalogue et un smoke TCP réel.
- **`EditorShell`** n'est pas créé : `EditorUI` est le shell final. Une classe
  miroir n'aurait ajouté ni état ni invariant.
- **`src/rhi/`** (383 l.) reste tel quel : c'est la référence de propreté du dépôt.
- **Métriques non atteintes assumées** : le refactor V1 visait 0 fichier > 800 l.
  et un plus gros fichier ≤ 600 l. ; on est resté à 9 fichiers > 800 l. avec
  `Renderer.cpp` en tête. Ces objectifs n'ont pas été « résolus » par compression
  artificielle ou par extraction sans invariant — ils restent ouverts en §2.

## 8. Plateforme Saida

La plateforme web, le backend et l'exploitation vivent dans
[`saias-o/saida`](https://github.com/saias-o/saida) et portent leur propre
roadmap ainsi que le go/no-go global de production.
