# SaidaEngine — Refactors différés (risqués)

Ces refactors sont **sortis** de [REFACTOR_V1.md](REFACTOR_V1.md) parce qu'ils ne
sont pas sûrs à faire mécaniquement : leur correction n'est **pas couverte par le
filet de tests automatique**. Les faire à l'arrache créerait du spaghetti. Ce
fichier dit *pourquoi*, et *ce qu'il faut mettre en place avant* pour que
l'extraction soit vraiment propre et tienne à long terme.

## Renderer (`src/render/Renderer.cpp`, 1975 l.) — décomposition différée

C'est le plus gros god class et la décomposition à plus forte valeur (elle
débloque XR, le flag GPU-driven et les lightmaps en les rendant testables
isolément). Mais elle est différée : voir les risques ci-dessous.

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
   passerait le filet et casserait le rendu **en silence**. Refactorer le
   Renderer sans vérif visuelle, c'est voler à l'aveugle.
2. **Branches `#ifdef` par plateforme/feature** (`SAIDA_RHI_WEBGPU`,
   `SAIDA_ENABLE_XR`). Le build natif n'exerce qu'une branche ; le build web
   compile l'autre mais ne l'exécute pas sur cette machine. Une extraction peut
   casser une branche jamais exécutée par le filet.
3. **État profondément intriqué.** GI, shadows, tonemap, GPU-culling et XR
   partagent les descriptor sets globaux et l'orchestration de frame
   (`drawFrame`/`recordCommandBuffer`). Les frontières ne sont pas nettes — couper
   au mauvais endroit crée des dépendances croisées entre passes : **exactement le
   spaghetti qu'on veut éviter.**
4. **Fonctions très longues** à découper en même temps : `gatherScene` (213 l.,
   render/Renderer.cpp:704), `recordCommandBuffer` (164, :1328), `drawFrame`
   (118, :1492), `rebuildGlobalSet` (108, :477).

### Ce qu'il faut faire AVANT — pour que ce soit vraiment propre

1. **Établir un filet de vérif visuelle, d'abord. Prérequis absolu.** Sans lui,
   ne rien refactorer dans le Renderer. Un harnais de rendu déterministe qui
   capture une frame de référence (*golden image*) d'une scène fixe et la compare
   pixel-à-pixel (tolérance bornée), lancé sur Lavapipe en CI **et** sur GPU réel.
   Le HUD a déjà des assertions de pixels calculés (`saida_ui_corpus_tests`) —
   étendre cette idée à une frame de scène complète (mesh + lumière + ombre +
   tonemap).
2. **Extraire une unité à la fois, en ordre feuille-d'abord** (la plus isolée
   avant la plus centrale), un commit + une vérif visuelle par unité :
   `TonemapPass` (frontière nette : entrée = cible HDR, sortie = swapchain) →
   `GpuDrivenCuller` (derrière son flag) → `XrRenderer` (guardé) → `GIRenderer` →
   `PipelineCache`/`FrameDescriptors` → `Renderer` mince en dernier.
3. **Règle anti-spaghetti n°1 : les passes ne s'appellent jamais entre elles.**
   Seul `Renderer::drawFrame` séquence les passes. Chaque unité possède ses objets
   GPU (pipelines, descripteurs, cibles) avec un cycle create/destroy lié aux
   changements de swapchain/format, et expose seulement `record*`/`update*`.
   Aucune unité ne lit les descripteurs d'une autre.
4. **Règle anti-spaghetti n°2 : les variations `#ifdef` restent DANS chaque
   unité.** `TonemapPass` gère Vulkan vs WebGPU en interne ; l'orchestrateur de
   frame reste agnostique. On ne disperse plus les `#ifdef` dans le code de frame.
5. **Vérifier chaque unité** : build natif **+ web** + Witness E2E + **diff
   visuel** (le filet du point 1) + smoke XR si un casque est disponible.

## Rappel : vérifications manuelles encore utiles

Les Phases 4 et 5 de REFACTOR_V1 sont terminées et automatisées autant que
possible. Deux compléments restent utiles, sans rouvrir ces phases :

- **EditorUI** — `witness_editor_play` et `witness_editor_build` couvrent les
  chemins Play/build, mais pas les clics ImGui en mode édition. Vérifier
  manuellement gizmos, panneaux et dialogues au viewport lors d'une prochaine
  session UI.
- **McpBridge** — le catalogue, l'unicité, le dispatch d'erreur et un appel TCP
  réel sont couverts. Ajouter progressivement des tests sémantiques par handler
  de mutation quand ces outils évoluent.
