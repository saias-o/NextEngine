# SaidaEngine — Audit qualité V1

Date : 2026-07-21. Portée : code first-party (`src/`, `web/`, `tools/`, `tests/`,
`WitnessGame/`) — ~73 000 lignes, 496 fichiers. Exclut `third_party/`, `build/`,
`_deps`, `node_modules`.

Ce document est un **backlog de qualité**, pas une source de vérité produit
(celles-ci restent [SPEC.md](SPEC.md) et [PLAN_V1.md](PLAN_V1.md)). Il liste ce
qui est perfectible avant/pendant la V1, classé par priorité.

## Verdict

Codebase globalement **saine et disciplinée** : dette TODO/FIXME quasi nulle
(2 `XXX`, 1 `BUG` sur tout l'arbre), politique fail-closed systématique, corpus
de formats figé, capacités annoncées vérifiées au démarrage, EOL normalisés par
`.gitattributes`. **Aucun bloqueur de correction n'a été trouvé** : le moteur
fonctionne et la suite passe.

Les points ci-dessous sont de la **dette structurelle et de maintenabilité**. Ils
ne cassent pas la V1 mais coûteront de plus en plus cher à mesure que le moteur
s'enrichit (le flag GPU-driven, le XR et le rendu avancé sont justement bloqués
derrière les god classes ci-dessous). Plusieurs violent une règle que le dépôt
s'impose déjà (README, « Règles de contribution ») : *« Scinder les classes
omniscientes et les fichiers qui mélangent plusieurs domaines »* et *« Remplacer
les nombres et chaînes magiques par des constantes nommées »*.

## État de résolution après REFACTOR_V1

- [x] `ResourceManager.cpp` : **1102 → 414 lignes**, caches et budget GPU
  extraits avec ownership explicite.
- [x] `EditorUI.cpp` : **1933 → 384 lignes**, shell et sept unités cohérentes.
- [x] `McpBridge.cpp` : **1401 → 77 lignes**, 45 outils répartis par domaine
  derrière un registre schéma/handler unique.
- [x] MCP confirmé comme capacité V1 de l'atelier, conservé ON et couvert par un
  test de catalogue ainsi qu'un smoke TCP réel.
- [x] Limites Input et seuils Renderer sûrs remplacés par des constantes nommées.
- [x] Registres de types distincts conservés par décision : les cibles ne lient
  pas les mêmes sous-systèmes et la matrice runtime vérifie leur parité.
- [ ] Renderer : extraction différée dans `TODO.md` jusqu'au filet golden-image.
- [ ] Autres gros candidats (`JsEngineBindings`, `InspectorPanel`,
  `WebCanvasNode`, `Input`) : hors périmètre de REFACTOR_V1, toujours visibles
  dans les métriques.

## A. God classes / fichiers omniscients — priorité haute

Les plus gros TU first-party portent plusieurs domaines dans un seul fichier :

| Fichier | Lignes | Domaines mêlés |
|---|---|---|
| `src/render/Renderer.cpp` | 1957 | pipelines, descriptors, GI, tonemap, GPU-culling, shadows, XR, frame (détail ci-dessous) |
| `src/editor/EditorUI.cpp` | 1933 | orchestration éditeur (31 méthodes dans une classe) |
| `src/scripting/JsEngineBindings.cpp` | 1853 | tous les globals JS (`node`/`time`/`input`/`tree`/`assets`/`audio`/`physics`/`storage`) |
| `src/mcp/McpBridge.cpp` | 1401 | dispatch de tous les outils MCP éditeur |
| `src/editor/panels/InspectorPanel.cpp` | 1336 | inspecteur de tous les types de nœuds |
| `src/scene/WebCanvasNode.cpp` | 1280 | un nœud : RmlUi + panneau world + scripts + hot-reload + placeholder |
| `src/graphics/ResourceManager.cpp` | 1102 | budget GPU + LRU + registres + chargement |
| `src/core/Input.cpp` | 1050 | clavier + souris + gamepad + touch + backend web + profils |

**Cas détaillé — `Renderer.cpp` (god class confirmée).** Une seule classe porte
~8 responsabilités distinctes :

- création de pipelines : `createPipeline`, `createWebCanvasWorldPipeline`,
  `createCullingPipeline`, `createTonemapPipeline`, `createXrPipelines` ;
- descriptor sets : `rebuildGlobalSet`, `createGlobalDescriptorSets`,
  `updateGlobalShadowDescriptor`, `updateGIDescriptors`, `updateEnvironmentDescriptor` ;
- GI temps réel : `shouldUpdateRealtimeGI`, `giDirtySignature` ;
- HDR / tonemap : `createHdrResources`, `recordTonemapPass`, `updateTonemapDescriptorSet` ;
- GPU-driven culling : `createGpuDrivenBuffers`, `uploadGpuDrivenDraws` ;
- shadow passes : `recordShadowPasses` ;
- rendu XR : `createXrTargets`, `createXrPipelines`, `recordXrScenePass`,
  `updateUniformBufferXr`, `recordXrWorldWebCanvases` ;
- orchestration de frame : `drawFrame`, `recordCommandBuffer`.

Piste : extraire `PipelineCache`, `GIRenderer`, `TonemapPass`, `GpuDrivenCuller`,
`XrRenderer` derrière un `Renderer` mince qui orchestre. Ce sont aussi les axes
d'enrichissement du PLAN (P1 : flag GPU-driven, XR, lightmaps) — les isoler les
rend testables sans monter toute la frame.

Les autres fichiers du tableau sont des **candidats à revoir** au même titre :
soit un domaine large qui gagnerait à être scindé par sous-domaine
(`Input` → un backend par périphérique ; `JsEngineBindings` → un module par
namespace ; `ResourceManager` → budget/LRU séparé du registre), soit un seul
type devenu trop gros (`WebCanvasNode`).

## B. Fonctions trop longues

Dans `Renderer.cpp` (mesuré) :

- `Renderer::gatherScene` — **213 lignes** ([Renderer.cpp:704](src/render/Renderer.cpp))
- `Renderer::recordCommandBuffer` — **164 lignes** ([:1328](src/render/Renderer.cpp))
- `Renderer::drawFrame` — **118 lignes** ([:1492](src/render/Renderer.cpp))
- `Renderer::rebuildGlobalSet` — **108 lignes** ([:477](src/render/Renderer.cpp))

Au-delà de ~80 lignes une fonction devient difficile à tester et à raisonner ;
à découper avec les god classes de la section A.

## C. Duplication structurelle du registre de types — priorité moyenne

`src/scene/ReflectedTypes.cpp` (27 appels `register*`),
`ReflectedTypesPlayer.cpp` (25) et `ReflectedTypesWeb.cpp` (8) enregistrent le
même registre de nœuds/behaviours pour trois runtimes (natif, player Web,
authoring Web), tenus **synchronisés à la main**. C'est la source des
désalignements de matrice de contrat déjà rencontrés en cours de route. Le
garde-fou `runtimeTypeMatrix` (vérifié au démarrage) rattrape les oublis mais ne
supprime pas le coût : ajouter un type impose d'éditer 2-3 listes parallèles sans
en oublier. L'étude d'exécution a toutefois montré que les cibles excluent
volontairement des sous-systèmes différents et ne peuvent pas toutes lier une
liste commune. Décision : conserver les trois listes lisibles, sans `#ifdef`
dispersés, et garder `runtimeTypeMatrix` comme garde-fou automatique.

## D. Magic numbers — priorité basse

Résolu pour les valeurs sûres. `InputLimits.hpp` centralise deadzone par défaut,
deadzone maximale et distance touch maximale entre bindings, backend et parser
de profils. `Renderer.cpp` nomme la distance d'ombre par défaut, le seuil de
colinéarité, les espacements de probes GI par tier, le plancher AO et les rayons
de bounds. Correction de l'audit initial : `1.2f`/`1.5f`/`2.4f` sont des
espacements de probes GI, pas des coefficients de tonemap.

## E. Surface & code mort

La purge open-source récente (commit du 2026-07-21 + `7193c0d`) a retiré
l'essentiel : behaviours GTA-clone générés, serveur d'écho web de dev,
`AnimGraphParser`, `RenderCapabilities.hpp`. `SAIDA_ENABLE_MCP` est confirmé
comme **capacité V1 assumée** de l'atelier Saida et reste ON par défaut. Son
traitement anti-god-class est terminé : `McpBridge.cpp` ne fait plus que 77
lignes et le catalogue de 45 outils est couvert automatiquement.

## F. Hygiène (mineur)

- **Fins de ligne** : plusieurs fichiers du working tree sont matérialisés en
  CRLF (`src/graphics/Mesh.{cpp,hpp}`, `VulkanDevice.hpp`, `hub/Hub.cpp`,
  `editor/BuildExporter.hpp`, …). `.gitattributes` (`* text=auto eol=lf`) les
  normalise au commit, mais l'incohérence traîne côté disque et pollue les diffs.
- **Dette TODO/FIXME quasi nulle** — un acquis à préserver.

## G. Améliorable pour enrichissement futur (déjà décidé, non-bloquant V1)

Limitations documentées et assumées (PLAN P1/P2, SPEC) — listées pour visibilité,
pas comme surprises :

- point-light cubemap shadows absentes ([LightNode.hpp:55](src/scene/LightNode.hpp)) ;
- retargeting BVH non implémenté ([BVHLoader.hpp:20](src/scene/BVHLoader.hpp)) ;
- pas de tangentes MikkTSpace (normal map désactivée sans tangentes d'auteur) ;
- pas de backend GPU RmlUi (le backend CPU tient le budget) ;
- KTX2/Basis reporté (textures PNG/JPG partout).

## Ce que l'audit ne remet PAS en cause

Architecture two-repo, contrat SaidaOps → snapshot versionné, runtime unique
QuickJS, fail-closed des loaders, corpus de formats figé, matrice de types
vérifiée au boot, saves versionnées/quota-ées : sains et cohérents avec la V1.

## Disposition finale

Le plan exécuté et ses validations sont dans [REFACTOR_V1.md](REFACTOR_V1.md).
Les éléments cochés ci-dessus sont clos. Renderer est transféré avec ses
prérequis dans [TODO.md](TODO.md) ; les autres cases ouvertes restent un backlog
explicite, pas une phase inachevée de REFACTOR_V1.
