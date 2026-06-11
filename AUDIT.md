# Audit technique NextEngine — « ce qui est faux, bâclé ou non digne de l'ambition affichée »

> Document d'analyse, **aucun code modifié**. Objectif : lister honnêtement les
> fonctionnalités factices (« fake »), les optimisations en trompe-l'œil, les
> problèmes d'architecture, les fuites/bugs, et l'écart entre le discours
> marketing et la réalité du dépôt. Chaque point renvoie au fichier:ligne.
>
> Verdict court : **le socle Vulkan est réel et globalement propre** (RAII,
> dynamic rendering, PBR/HDR, shadow maps, sérialisation, undo/redo). Mais
> plusieurs fonctionnalités **annoncées comme faites sont désactivées, stubbées,
> ou ne font pas ce qu'elles prétendent**, et certaines « optimisations » vantées
> dans les .md sont au mieux neutres, au pire trompeuses. La documentation
> (`CLAUDE.md`, `BILAN.md`, `MARKETING.md`, `animation_audit.md`) **surestime
> systématiquement** l'état réel.

---

## 0. Méthode

Lecture croisée des `.md` (`CLAUDE.md`, `BILAN.md`, `MARKETING.md`,
`HANDOVER_CLAUDE.md`, `animation_audit.md`, `RENDU_AVANCE.md`) et du code
(`src/`). Les affirmations des docs ont été confrontées au code exécuté.

---

## 1. Fonctionnalités « FAKE » : annoncées faites, mais désactivées / stub / inertes

### 1.1 🔴 GPU-driven + bindless : **entièrement désactivé**, code mort
- `BILAN.md` affirme : *« Infrastructure compute : `ComputePipeline`, dispatch,
  GPU culling et async compute en place »* et liste GPU-driven/bindless comme un
  acquis de la Phase 0.
- **Réalité** : un `bool useGpuDriven = false; // TEMPORARILY DISABLED FOR
  DEBUGGING` est codé en dur à **trois endroits** :
  [Renderer.cpp:158](src/render/Renderer.cpp:158),
  [Renderer.cpp:541](src/render/Renderer.cpp:541),
  [Renderer.cpp:964](src/render/Renderer.cpp:964).
- Conséquence : **tout** le pipeline indirect/bindless est du code jamais
  exécuté — `culling.comp`, les shaders `bindless.*`, `createGpuDrivenBuffers()`,
  `createCullingPipeline()`, les buffers instance/draw/count, le dispatch compute
  et le `vkCmdDrawIndexedIndirectCount`. C'est de la dette pure : ça compile,
  ça « a l'air » d'un moteur GPU-driven moderne, ça ne tourne jamais.
- Pire, ce chemin est **incomplet de toute façon** : aveu dans le code que le
  `countBuffer` n'est pas vidé dans la version CPU
  ([Renderer.cpp:603-604](src/render/Renderer.cpp:603)), et
  *« bindless lightmaps aren't implemented yet »*
  ([Renderer.cpp:1141](src/render/Renderer.cpp:1141)).

**Action** : soit le réactiver et le finir, soit l'assumer comme « non terminé »
dans les .md et **arrêter de le présenter comme acquis**. Ne pas laisser un
`// TEMPORARILY DISABLED FOR DEBUGGING` dupliqué 3× dans un moteur dit « propre ».

### 1.2 🔴 Lightmap baking : la lightmap par-nœud n'est **jamais bindée**
- `CLAUDE.md` coche **[x] Lightmap baking GPU**.
- Dans le `gatherScene`, on calcule pourtant bien le set lightmap par nœud et on
  le stocke dans `DrawCmd` (`lightBaker_->lightmapSet(node)`,
  [Renderer.cpp:645](src/render/Renderer.cpp:645)).
- Mais à l'exécution du draw, le code **ignore** ce set et bind toujours le
  fallback : `VkDescriptorSet lmSet = lightBaker_->lightmapSet(nullptr);`
  ([Renderer.cpp:1201](src/render/Renderer.cpp:1201)), avec le commentaire
  *« param.x > 0.5 tells shader to use baked lightmap. Since it's a stub, use
  0.0 »* ([Renderer.cpp:1207](src/render/Renderer.cpp:1207)).
- Donc le résultat baké calculé n'arrive jamais au shader pour le bon objet : la
  feature est **inerte** dans le chemin de rendu réel. C'est l'exemple type de la
  « fonction sournoise qui a l'air bien faite » : toute la plomberie existe, sauf
  la ligne qui la branche.

### 1.3 🔴 Timeline « universelle » : c'est littéralement un *fake track*
- `animation_audit.md` vante : *« La fondation pour piloter des propriétés à la
  volée est prête, sans réflexion (Fake track). »*
- `Timeline.hpp` dit le contraire de ce que l'audit prétend :
  [Timeline.hpp:19](src/scene/animation/Timeline.hpp:19) *« placeholder for a
  future reflection-based property track »* et
  [Timeline.hpp:26-27](src/scene/animation/Timeline.hpp:26) *« TODO(Reflection):
  … Faked for now to keep foundations solid »*. La timeline n'applique aucune
  valeur — elle avance un temps et ne fait rien.

### 1.4 🟠 Parseur de graphe d'anim : *« Fake clips for now »*
- [AnimGraphParser.cpp:113](src/scene/animation/AnimGraphParser.cpp:113) : *« Fake
  clips for now as requested »*. Le graphe d'animation chargé depuis les données
  fabrique des clips bidons.

### 1.5 🟠 Cibles de build multiplateforme : pur décor UI
- L'éditeur affiche des onglets/targets Quest, WebGL, Windows, mais ce sont des
  libellés grisés sans aucune implémentation :
  [EditorUI.cpp:714](src/editor/EditorUI.cpp:714) *« Meta Quest target builds
  require Android NDK… »*, [EditorUI.cpp:751](src/editor/EditorUI.cpp:751)
  *« WebGL platform support requires … Emscripten »*. Idem
  [EditorUI.cpp:1187](src/editor/EditorUI.cpp:1187) : *« these settings will be
  wired to the Vulkan backend later »*. Donne l'illusion d'un pipeline de
  packaging multiplateforme (Étape 15) qui n'existe pas.

### 1.6 🟠 Async compute / sync2 / timeline semaphores : annoncés, absents
- `BILAN.md` : *« async compute en place »* et *« dynamic rendering déjà
  implémenté, reste sync2 et timeline semaphores »*.
- **Réalité** : `drawFrame` fait **un seul `vkQueueSubmit` sur une file unique**
  avec des sémaphores binaires classiques
  ([Renderer.cpp:1286-1297](src/render/Renderer.cpp:1286)). Pas de file compute
  séparée, pas de timeline semaphore. Le seul dispatch compute (culling) est dans
  le chemin… désactivé (cf. 1.1). Donc « async compute en place » est **faux**.

### 1.7 🟠 Multiview / stéréo : fondation annoncée « dès maintenant », non câblée
- `CLAUDE.md`/`BILAN.md` insistent : concevoir **toutes** les passes
  multiview-aware *« dès maintenant »* (fondation XR, objectif final).
- **Réalité** : `renderingInfo.layerCount = 1` partout
  ([Renderer.cpp:1107](src/render/Renderer.cpp:1107),
  [Renderer.cpp:820](src/render/Renderer.cpp:820)), une seule matrice
  view/proj, aucun `VK_KHR_multiview`. Le « pipeline de rendu universel prêt pour
  le XR » repose sur des hypothèses mono câblées en dur. À reconnaître comme
  « à faire », pas comme une propriété déjà respectée.

### 1.8 🟡 Gamepad non implémenté
- [Input.cpp:163](src/core/Input.cpp:163) *« Gamepad axis not implemented yet »*.

---

## 2. Optimisations en trompe-l'œil (« data-oriented » sur le papier)

### 2.1 🔴 `Node::g_hierarchyVersion` : variable **statique globale** mutable
- [Node.hpp:156](src/scene/Node.hpp:156) `static uint32_t g_hierarchyVersion;`,
  incrémentée par *chaque* `addChild`/`removeChild`/`addBehaviour`/`setEnabled`/…
  ([Node.cpp:35](src/scene/Node.cpp:35) et suivantes).
- C'est un compteur **global au programme entier**, partagé par tous les nœuds et
  **toutes les scènes**. `Scene::update` re-flatten dès qu'il change
  ([Scene.cpp:16](src/scene/Scene.cpp:16)).
- Conséquences :
  - Modifier un nœud dans **n'importe quelle** scène (ou un prefab, ou le Hub)
    force le re-flatten de **toutes** les scènes vivantes.
  - **Non thread-safe** (incrément non atomique) — incompatible avec tout futur
    job-system / chargement asynchrone, pourtant central pour un moteur « moderne ».
  - Détruit l'idée d'isolation des scènes / multi-instances.
- C'est l'antithèse de l'« ownership clair » revendiqué dans `CLAUDE.md`. Un
  compteur de version **par scène** (membre de `Scene`) ferait le travail
  proprement.

### 2.2 🟠 Cache de transform « dirty flag » : l'optim coûteuse est faite quand même
- `HANDOVER_CLAUDE.md` présente le `worldTransform_`/`lastLocalMatrix_` comme une
  grosse bascule data-oriented évitant de recalculer tout l'arbre.
- En réalité [Node.cpp:122-134](src/scene/Node.cpp:122) :
  ```
  glm::mat4 currentLocal = localMatrix();              // TRS complet CHAQUE frame
  bool dirty = parentDirty || (currentLocal != lastLocalMatrix_); // compare 16 floats
  ```
  - `localMatrix()` reconstruit la matrice TRS complète (`translate` + `mat4_cast`
    du quaternion + `scale`) **à chaque frame pour chaque nœud**, qu'il soit dirty
    ou non. La partie chère n'est donc **pas** économisée.
  - Le « gain » se limite à éviter **une** multiplication `parentWorld *
    local` — négligeable devant la construction TRS faite de toute façon.
  - La comparaison `currentLocal != lastLocalMatrix_` est une **égalité flottante
    exacte sur 16 floats**, fragile (un même transform peut produire des bits
    légèrement différents selon le chemin de calcul).
- `lastLocalMatrix_{0.0f}` ([Node.hpp:153](src/scene/Node.hpp:153)) initialisé à
  la matrice nulle pour « forcer » le premier dirty : hack documenté comme une
  astuce, mais c'est un sentinel fragile.

### 2.3 🟠 Double calcul du monde : le cache est ignoré là où il compte
- `updateTransforms` remplit `worldTransform_`
  ([Node.cpp:127](src/scene/Node.cpp:127)).
- Mais `traverse()` **recalcule** `parentWorld * localMatrix()` en ignorant le
  cache ([Node.cpp:138](src/scene/Node.cpp:138)), et `Scene::flattenHierarchy()`
  utilise précisément `traverse` ([Scene.cpp:40](src/scene/Scene.cpp:40)). Donc le
  flatten refait le travail que le cache était censé éviter. Le rendu lit
  `worldTransform_`, le flatten en recalcule un autre : deux sources de vérité
  pour la matrice monde, incohérent.

### 2.4 🟠 Frustum culling : sphère englobante **bidon** codée en dur
- [Renderer.cpp:619](src/render/Renderer.cpp:619)
  `float radius = 0.866f * maxScale;` — `0.866 ≈ √3/2`, le rayon d'un **cube
  unité**. Appliqué à **tous** les meshes, l'aveu est explicite
  ([Renderer.cpp:558](src/render/Renderer.cpp:558)) : *« DamagedHelmet doesn't
  report bounds, so we use a standard local radius for the unit cube. »*
- Les `Mesh` n'ont **aucune AABB/bounding-sphere réelle**. Donc :
  - un grand mesh (terrain, perso) est culé à tort dès que son centre sort du
    frustum bien qu'il soit visible ;
  - un petit mesh éloigné garde un rayon surdimensionné.
- `HANDOVER_CLAUDE.md` se vante d'avoir *« fixé le frustum culling »* (extraction
  des plans). Le fix des plans est correct
  ([Camera.cpp:43](src/core/Camera.cpp:43)), mais le **rayon est faux**, donc le
  culling reste incorrect pour tout ce qui n'est pas un cube unité.

### 2.5 🟡 `flattenHierarchy` toujours à base de `dynamic_cast` + récursion
- `HANDOVER_CLAUDE.md` prétend que le gather *« n'utilise plus la fonction
  récursive lourde et remplie de `dynamic_cast` »*. C'est vrai pour le gather…
  parce que les `dynamic_cast` ont juste été **déplacés** dans
  `flattenHierarchy()` ([Scene.cpp:43-48](src/scene/Scene.cpp:43)), réexécuté à
  chaque changement de `g_hierarchyVersion` global (cf. 2.1, donc souvent).

### 2.6 🟡 Skinning : SSBO réécrit en double
- Chaque `MeshNode` partageant un `Animator` (cas normal : plusieurs primitives
  d'un même perso) re-`memcpy` les **mêmes** skinning matrices dans le SSBO global
  ([Renderer.cpp:637-641](src/render/Renderer.cpp:637)). Duplication mémoire et
  bande passante. Devrait être uploadé une fois par Animator.

---

## 3. Bugs / fuites concrètes

### 3.1 🟠 Fuite + dead code dans `WebCanvasNode::init`
- [WebCanvasNode.cpp:45](src/scene/WebCanvasNode.cpp:45)
  `ULConfig config = ulCreateConfig();` — créé, **jamais utilisé, jamais
  détruit** (`ulDestroyConfig` absent). Fuite à chaque création de canvas web +
  ligne morte. Sournois car ça « ressemble » à de l'init légitime.

### 3.2 🟡 UI web (Ultralight) : rendu CPU recopié vers Vulkan chaque frame
- `WebEngine` force le **renderer CPU** d'Ultralight (pas de GPUDriver,
  [WebEngine.cpp:22-23](src/ui/WebEngine.cpp:22)), puis chaque canvas lock le
  bitmap CPU, le copie dans un staging buffer et l'upload en texture
  ([WebCanvasNode.cpp:105-117](src/scene/WebCanvasNode.cpp:105)) **à chaque
  frame**. Fonctionnel, mais c'est l'opposé du « léger/optimisé/mobile-VR »
  revendiqué — coûteux en CPU et en bande passante PCIe. L'Étape 12 est cochée
  faite, mais la perf est naïve.

### 3.3 🟡 Logs de debug laissés dans les chemins chauds
- GLTFLoader logue, **par primitive**, les 10 premiers indices + 3 premiers
  vertices ([GLTFLoader.cpp:234-240](src/scene/GLTFLoader.cpp:234)) — spam massif
  au chargement d'un gros modèle.
- Le rendu logue les 5 premières frames CPU avec détail par-draw
  ([Renderer.cpp:656-666](src/render/Renderer.cpp:656),
  [Renderer.cpp:1187-1190](src/render/Renderer.cpp:1187)). Reliquat de debug à
  retirer / passer derrière un flag verbose.

---

## 4. Problèmes d'architecture & de couches

### 4.1 🟠 La couche `scene` dépend d'un **singleton audio global**
- [Node.cpp:28](src/scene/Node.cpp:28) : `Node::~Node()` appelle
  `AudioManager::get().stopAllOnNode(this)`. Donc **tout** destructeur de nœud
  (et il y en a partout, y compris au teardown global) touche un singleton audio.
  Ordre de destruction fragile (le singleton doit survivre à tous les nœuds), et
  couplage dur scène→audio via état global. `WebEngine::get()` est un autre
  singleton ([WebCanvasNode.cpp:33](src/scene/WebCanvasNode.cpp:33)).

### 4.2 🟠 Violation de couche `Texture` ↔ ImGui (déjà notée, non corrigée)
- `BILAN.md` §5 reconnaît : *« Découpler Texture d'ImGui (violation de couche) »*.
  Toujours présent. Une classe `graphics` bas niveau ne devrait pas connaître l'UI.

### 4.3 🟠 God-classes encore massives
- [Renderer.cpp](src/render/Renderer.cpp) : **1322 lignes**, mélange création de
  pipelines, pools, descriptors, gather, culling, shadow, bake, skybox, tonemap,
  submit/present. Difficile à faire évoluer vers deferred/multiview sans le
  fracturer (passes).
- [EditorUI.cpp](src/editor/EditorUI.cpp) : **1319 lignes** — l'« éclatement en
  panels » est entamé mais l'UI reste une god-class.

### 4.4 🟠 Documentation gravement désynchronisée et auto-contradictoire
- `CLAUDE.md` décrit une architecture (« Renderer dans Engine », pas de
  `editor/`, `hub/`, `audio/`, `ui/`, `project/`) qui ne correspond plus au code.
- `CLAUDE.md`/Étape 10 dit *« parsing de cgltf_skin repoussé »* alors que
  `GLTFLoader` **parse bel et bien** skins, rigs, inverse-bind et animations
  ([GLTFLoader.cpp:256-298](src/scene/GLTFLoader.cpp:256)). La doc sous-estime ici,
  surestime ailleurs : dans les deux cas elle n'est **pas fiable**.
- `animation_audit.md` se félicite d'une absence de « Fake track » alors que le
  code contient explicitement un fake track (cf. 1.3). Les .md se contredisent.

### 4.5 🟡 Hypothèses de résolution codées en dur dans l'UI interactive
- [UIInteractionSystem.cpp:29](src/ui/UIInteractionSystem.cpp:29) suppose
  ~1600x900 / 1:1. Cassera l'interaction UI hors de cette résolution.

---

## 5. Marketing vs réalité (`MARKETING.md`)

`MARKETING.md` promet un positionnement « **premier moteur nativement pilotable
par l'IA** », serveur **MCP**, **Vulkan Raytracing**, **hot-reload C++/Lua/
shaders**, **Web Playground Wasm**, démo « agent IA génère un jeu ».
**Aucune de ces briques n'existe dans le code** :
- Pas de serveur MCP, pas d'API de pilotage IA (Étape 13 = non commencée).
- Pas de raytracing Vulkan (aucun `VK_KHR_ray_*` dans le dépôt).
- Pas de hot-reload (Lua non vendu, décision explicitement *différée* dans
  `CLAUDE.md`).
- Pas de build Wasm/Emscripten (juste un libellé grisé, cf. 1.5).

C'est du **vaporware assumé comme argument de lancement**. À distinguer
clairement de l'état réel pour ne pas se piéger soi-même (ni la communauté).

🟡 **Incohérence de licence à vérifier** : `MARKETING.md` met en avant un
positionnement **GPL/FOSS « 100% local, respectueux »**, mais le moteur dépend
d'**Ultralight** (`third_party/ultralight/`), dont la licence est **propriétaire
et payante au-delà d'un seuil de revenus**. Un moteur qui se vend comme « anti-
Unity/Unreal, FOSS, sans verrou » embarquant une dépendance UI propriétaire au
cœur de l'Étape 12, c'est une contradiction à traiter (licence, ou remplacer par
une solution libre).

🟡 Le `HANDOVER_CLAUDE.md` mentionne l'ajout d'un bouton **« Sponsor
NextEngine »** (don GitHub) dans l'À-propos
([EditorUI.cpp drawAboutWindow]) — solliciter du sponsoring alors que des
features cochées « faites » sont des stubs est, au minimum, prématuré.

---

## 6. Robustesse — ce qui manque pour mériter « robuste »

- 🟠 **Aucun test, aucune CI** (`BILAN.md` §Différé l'assume). Pour un moteur
  revendiqué « robuste » avec sérialisation, undo/redo, graphe de scène et
  Vulkan, l'absence totale de tests de non-régression est le plus gros risque
  silencieux. Au minimum : round-trip de sérialisation, math caméra/frustum,
  hiérarchie de transforms.
- 🟡 **Hack linker `-static`** dans `CMakeLists.txt` pour contourner un `ld`
  MSYS2 cassé (exit 116). C'est de l'environnement, pas du code, mais ça fragilise
  la portabilité « léger/multiplateforme » revendiquée et masque le vrai problème
  de toolchain.
- 🟡 **Magic numbers** disséminés (`0.866f`, tailles SSBO `4MB`/`65536 bones`
  codées en dur [Renderer.cpp:181-182](src/render/Renderer.cpp:181)) sans
  constante nommée ni garde-fou si dépassés.

---

## 7. Synthèse priorisée

| # | Problème | Gravité | Type | Effort |
|---|----------|---------|------|--------|
| 1 | GPU-driven/bindless désactivé en dur, présenté comme acquis | 🔴 | Fake | Élevé (finir) / Faible (re-documenter) |
| 2 | Lightmap par-nœud jamais bindée (set fallback en dur) | 🔴 | Fake/bug | Faible |
| 3 | `g_hierarchyVersion` statique globale (multi-scène, thread) | 🔴 | Archi | Moyen |
| 4 | Frustum culling à rayon bidon (cube unité pour tout) | 🟠 | Optim fausse | Moyen (vraies bounds) |
| 5 | Timeline/AnimGraph « fake » présentés comme fondation prête | 🟠 | Fake | Moyen |
| 6 | Async compute/sync2/multiview annoncés, absents | 🟠 | Fake/archi | Élevé |
| 7 | Dirty-flag transform : TRS recalculé chaque frame (gain ~nul) + double calcul monde | 🟠 | Optim trompeuse | Faible-Moyen |
| 8 | Couplage scène→singletons (AudioManager/WebEngine) | 🟠 | Archi | Moyen |
| 9 | Fuite `ULConfig` + UI web full-CPU/upload par frame | 🟠 | Bug/perf | Faible / Moyen |
| 10 | Docs (.md) désynchronisées et contradictoires | 🟠 | Process | Faible |
| 11 | Cibles build Quest/WebGL/Windows = décor UI | 🟡 | Fake | (re-documenter) |
| 12 | God-classes Renderer/EditorUI | 🟡 | Archi | Moyen |
| 13 | Aucun test / CI | 🟠 | Robustesse | Moyen |
| 14 | Logs debug en chemin chaud, magic numbers, hack `-static` | 🟡 | Propreté | Faible |
| 15 | Marketing (MCP/RT/Wasm/LLM-native) = vaporware ; licence Ultralight vs FOSS | 🟡 | Discours | — |

---

## 8. Recommandation transversale

Le vrai problème n'est pas la qualité ponctuelle du code Vulkan (souvent
correcte), c'est **l'écart entre l'état réel et l'état déclaré**. Deux principes
pour assainir :

1. **Une seule source de vérité d'avancement.** Une feature n'est « faite » que
   si elle s'exécute par défaut et fait ce qu'elle dit. Tout le reste est
   « en cours » ou « prototype désactivé ». Reclasser `CLAUDE.md`/`BILAN.md` en
   conséquence (GPU-driven, lightmaps, multiview, async compute, timeline →
   « partiel/désactivé »).
2. **Pas de chemin mort en dur dans le code.** Un `useGpuDriven=false` dupliqué
   3× n'a pas sa place ; soit feature-flag runtime propre (capabilities + option
   projet), soit branche dédiée non mergée. Le code « qui a l'air fait mais ne
   tourne pas » est exactement ce qui trompe un lecteur (humain ou IA).
