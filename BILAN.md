# NextEngine — Bilan & feuille de route

> Document prospectif : où en est le moteur et quoi construire ensuite pour un
> rendu **photoréaliste, extrêmement optimisé et compatible mobile / VR**, sans
> trahir la philosophie **légère et multiplateforme**. Le détail du volet rendu
> avancé (analyse de l'état de l'art, techniques écartées et pourquoi) vit dans
> **`RENDU_AVANCE.md`** ; ce bilan en donne la version intégrée et priorisée.

---

## 0. État actuel (le socle est sain)

RAII et ownership clairs, split `ne_engine` / exe / `ne_editor`, `Renderer`
découpé de l'`Engine`, sérialisation JSON round-trip, undo/redo, `Time`/`Input`,
assets content-addressed (`AssetRegistry` + GUID), frustum culling, MSAA,
pipeline cache.

**Éclairage (fait)** : Directional / Point / Spot unifiées (`GpuLight[16]`),
ombres temps réel 2D-array (`ShadowMap`, PCF), **lightmap baking GPU sans GI**
(`LightBaker`) avec toggle Realtime/Baked. Toute la math d'éclairage est
centralisée dans `shaders/lighting.glsl` (source unique) — couture idéale pour la
suite.

**Le manque structurel** : rendu **forward**, Blinn-Phong, **aucun compute**,
**pas de HDR**, **pas de G-buffer**, **pas de multiview**. C'est ce que la Phase 0
corrige.

---

## 1. Cap rendu : photoréaliste + optimisé + mobile/VR

Décision (cf. `RENDU_AVANCE.md`) : on ne garde de l'état de l'art que les
techniques **compute, déterministes et view-independent** — celles qui tiennent
sur mobile/VR et restent légères. On **écarte le path tracing matériel**
(ReSTIR PT/G-PT, RTXDI) : pas de RT cores exploitables sur Quest, et son denoiser
temporel crée du ghosting rédhibitoire en VR. (Au mieux option desktop ultra,
très long terme — pas le cœur.)

**Colonne vertébrale** : une **matrice de scalabilité** Low→Ultra, combinable à
l'exécution (commutation de `VkDescriptorSet`/pipelines), alignée sur les
capacités GPU interrogées au démarrage. C'est le « regress gracefully » qui colle
à la philosophie : un même moteur, du Quest au desktop.

### Phase 0 — Fondations rendu **(DÈS MAINTENANT)**

Sans ça, rien de la GI avancée n'est possible. C'est l'essentiel du travail réel.

- **PBR metallic-roughness + normal mapping** : remplacer Blinn-Phong (BRDF
  changée à un seul endroit, `lighting.glsl`). *Le* saut visuel vers le moderne.
- **HDR + tonemapping (ACES)** : cible de rendu R16F → passe plein-écran → swapchain
  (`SceneSettings.enablePostProcessing` existe, inutilisé). Indispensable dès que
  l'éclairage dépasse 1.0.
- **Infrastructure compute** : `ComputePipeline`, dispatch, images storage,
  barrières **sync2**. Brique de toute la GI future.
- **Rendu différé / Visibility buffer** : G-buffer (pos monde, normale, albedo,
  rugosité/métal). Entrée obligatoire des cascades et des caches ; réduit l'overdraw
  (bon en mobile TBDR).
- **Multiview (stéréo) dès maintenant** : concevoir **toutes** les nouvelles passes
  multiview-aware (un render, deux vues). Fondation VR — ne pas câbler « une vue mono ».
- **GPU-driven + bindless** : indirect draw/dispatch, `VK_EXT_descriptor_indexing`.
  Moderne, et nécessaire au sampling efficace des caches.
- **Support assets PBR** (compagnon immédiat, sinon le PBR n'a rien à afficher) :
  **chargement glTF** (matériaux PBR, hiérarchies) + **mipmaps/anisotropie** sur
  `Texture` (aujourd'hui `mipLevels=1`).

### Plus tard (validé phase par phase, pas maintenant)

- **Phase 1 — Radiance Cascades 2D** : GI diffuse **sans bruit**, 100 % compute,
  zéro TAA → idéal mobile/VR. Remplace l'ambient constant par un vrai indirect.
- **Phase 2 — World Radiance Cache + spatial hashing** (idTech 8) : ombrage
  **découplé de l'écran** → **ombré une fois, échantillonné par les deux yeux**.
  Le gain VR le plus fort. Async compute, mises à jour entrelacées (~14 Mo).
- **Phase 3 — Volumétrie froxel** : brouillard / lumière volumétrique / SSS,
  alimentés par les cascades.
- *(Écarté du cœur)* réflexions/PT par ray-query : option desktop ultra seulement.

---

## 2. Pipeline d'assets (au-delà de glTF/mipmaps de la Phase 0)

- Animations / skinning (via glTF) quand la couche jeu en aura besoin.

## 3. Couche jeu **(plus tard)**

- **Scripting Lua** (étape 8b décidée) : `ScriptBehaviour : Behaviour` délégant à
  Lua (sol2 vendu, hot-reload file-watcher) — itérer sans recompiler le moteur.
- **Physique légère** (Jolt, ou maison) : collisions / rigidbodies.

## 4. Capstone — XR / OpenXR **(plus tard)**

Objectif final. Réutilise tout le pipeline de scène ; seul le chemin de
présentation diffère. **Le multiview de la Phase 0 est exactement la fondation
qui le prépare** — d'où son inclusion immédiate. À faire après que le rendu soit
moderne (PBR/HDR + GI), pour ne pas figer de choix sur un rendu basique.

---

## 5. Transversal — propreté & Vulkan moderne (au fil de l'eau)

- **Vulkan moderne** : **dynamic rendering** (VK 1.3, supprime le boilerplate
  render-pass/framebuffer de `Swapchain`/`ShadowMap`/`LightBaker`), **sync2**,
  **timeline semaphores** (indispensables pour la synchro graphics↔compute async).
  Moderne *et* plus léger — à introduire pendant la Phase 0.
- **Découpler `Texture` d'ImGui** (violation de couche : une classe graphics ne
  doit pas connaître l'UI).
- **Finir l'éclatement d'`EditorUI`** (god-class) en panels — déjà entamé.

## 6. Polish court terme (limites v1 de l'éclairage)

- Lightmaps **non sérialisées** → exporter (PNG/EXR + GUID). *(La GI Phase 1
  réduira l'intérêt du baking statique, sauf pour le tier Low mobile.)*
- Pas de dilation de seams sur les lightmaps → passe de dilation (~30 lignes).
- Pas d'unwrap auto `.obj` (fallback UV chevauchants) → xatlas si utile.
- Ombres de Point lights absentes (cubemap, hors scope initial).

---

## 7. Priorisation

| # | Chantier | Quand | Pourquoi |
|---|---|---|---|
| 1 | **Phase 0 rendu** (PBR, HDR, compute, deferred, multiview, GPU-driven, glTF) | **Maintenant** | Fondations de tout : moderne, scalable, prêt GI + VR |
| 2 | Dynamic rendering / sync2 / timeline (cleanup) | Pendant Phase 0 | Allège le code, requis pour l'async compute |
| 3 | **Phase 1 — Radiance Cascades 2D** | Plus tard | 1re GI sans bruit, mobile/VR |
| 4 | **Phase 2 — World Radiance Cache** | Plus tard | Découple l'ombrage, clé VR |
| 5 | **Phase 3 — Volumétrie froxel** | Plus tard | Brouillard / volumétrique |
| 6 | Lua scripting | Plus tard | Itération gameplay |
| 7 | Physique | Plus tard | Vrais jeux |
| 8 | **OpenXR / stéréo** | Plus tard | Objectif final, après un rendu moderne |

> Règle d'or : **fondations (Phase 0) avant GI avancée**, **rendu moderne avant
> XR**, **déterministe avant stochastique** (VR). Mener les nettoyages Vulkan au
> fil de l'eau, pas en gros chantier dédié.

---

## Différé volontairement

- **Tests & CI** : écartés pour l'instant (choix assumé). À reconsidérer quand le
  moteur se stabilise et que les régressions deviennent coûteuses.
