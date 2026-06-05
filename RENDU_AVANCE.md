# NextEngine — Rendu nouvelle génération (analyse & plan)

> Analyse du rapport « Architecture Avancée de Rendu Photoréaliste Temps Réel
> sous Vulkan » (état de l'art juin 2026) et **feuille de route filtrée** pour la
> contrainte cardinale de NextEngine : **photoréaliste + extrêmement optimisé +
> compatible mobile / VR**. À valider avant toute implémentation.

---

## 0. La contrainte cardinale décide de tout

Le rapport décrit le sommet du rendu 2026, mais **80 % de son contenu vise le
ray-tracing matériel desktop** (RTX 4070→5090). Or l'objectif de NextEngine est
**mobile + VR**. Trois faits d'ingénierie incontournables :

1. **Pas de RT matériel exploitable sur cible mobile/VR standalone.** Quest 3 =
   Adreno 740 : pas de cœurs RT utilisables pour du path tracing temps réel. Le
   RT desktop (RTX/RDNA) ne concerne que le PCVR haut de gamme, et encore.
2. **La VR interdit le bruit + denoiser temporel.** Path tracing → bruit
   stochastique → denoiser temporel (TAA/NRD) → **ghosting + artefacts de
   réprojection** : précisément ce que le rapport critique, et c'est *rédhibitoire*
   en VR (deux yeux, head-tracking, 72–120 Hz). En VR on veut du **déterministe**.
3. **Budget VR brutal.** ~2 vues à 72–120 Hz, latence < ~15 ms : le coût d'ombrage
   doit être **découplé de la résolution écran** et idéalement **partagé entre les
   deux yeux**.

**Conséquence** : on garde du rapport tout ce qui est **compute, déterministe et
view-independent** (les tiers Low/Medium), et on **écarte le path tracing
matériel** (tiers High/Ultra) du cœur — au mieux option desktop long terme.

> ⚠️ Tension avec la philosophie « léger » de NextEngine : viser « la pointe »
> tout en restant léger impose de **choisir un sous-ensemble**, pas d'empiler tout
> le rapport. Ce document recommande ce sous-ensemble.

---

## 1. Lecture critique du rapport (tri par compatibilité mobile/VR)

| Technique (rapport) | Ce que c'est | Mobile/VR ? | Verdict |
|---|---|---|---|
| **Radiance Cascades** (2D écran) | GI diffuse **déterministe** (sans Monte-Carlo), compute, hiérarchie d'intervalles de radiance, fusion par interpolation bilinéaire | ✅ Excellent (compute pur, zéro bruit, pas de TAA) | **ADOPTER — cœur GI** |
| **Radiance Cascades 3D / world-space** | Variante volumétrique (encodage octaédrique, atlas), brouillard + GI volumétrique | ✅ mais plus lourd | **ADOPTER + tard** |
| **World Radiance Cache + Spatial Hashing** (idTech 8) | Cache de radiance monde indexé par hachage (pcg32 + Jenkins), shading **découplé de l'écran**, async compute, ~14 Mo | ✅✅ **Idéal VR** (ombré une fois en espace monde → **les 2 yeux échantillonnent le même cache**) | **ADOPTER — clé VR** |
| **Cascaded Irradiance Volumes** (sondes) | Grille de sondes d'irradiance en cascade, mises à jour entrelacées | ✅ Mobile cheap | **ADOPTER** (alternative/complément GI bas tier) |
| **Async Compute / GPU-driven** (`vkCmdDispatchIndirect`) | Calculs GI sur file compute en //, dispatch pilotés GPU (zéro latence CPU) | ✅ Optimisation transversale | **ADOPTER — transversal** |
| **Visibility Buffer** (deferred) | Stocke triangle/instance par pixel, évalue le matériau en différé | ✅ (bande passante, fort polycount, mobile TBDR) | **ADOPTER** (base GI + perf) |
| **Volumétrie Froxel** (Frustum Voxels) | Grille 3D alignée caméra pour brouillard / lumière volumétrique / SSS | ✅ Scalable | **ADOPTER + tard** |
| **Encodage octaédrique** | Direction 3D → UV 2D, faible distorsion | ✅ Utilitaire | **ADOPTER** (brique RC/probes) |
| **ReSTIR DI** (ombres par réservoirs) | Échantillonnage d'importance des lumières directes (ray queries) | ⚠️ Desktop surtout | **OPTIONNEL desktop** |
| **ReSTIR PT Enhanced** (path tracing) | Path tracing temps réel par réservoirs (RcVertex, hybrid shift, RTXDI) | ❌ RT matériel + denoiser → pas mobile/VR | **HORS CŒUR** (option desktop ultra, long terme) |
| **ReSTIR G-PT** (domaine des gradients) | Reconstruction Poisson filtrée des gradients | ❌ Recherche, desktop ultra | **ÉCARTER** (pour l'instant) |
| **NRD / denoiser temporel** | Débruitage du PT | ❌ Ghosting VR | **ÉCARTER** (sauf si chemin PT desktop) |

**Synthèse du tri** : la « pointe » *compatible mobile/VR* = **Radiance Cascades
+ World Radiance Cache (spatial hashing) + PBR/HDR + froxels**, le tout en
**compute + async + view-independent**. Le path tracing matériel est un cul-de-sac
pour la cible — séduisant sur le papier, inadapté à Quest/mobile.

---

## 2. Pré-requis : NextEngine n'est pas encore prêt pour ça

État actuel : **PBR**, **HDR**, **Dynamic Rendering** (Vulkan 1.3) et **Infrastructure Compute** (F1) implémentés,
ombres shadow-map 2D-array, lightmaps GPU (`LightBaker`). Cependant, le rendu est **toujours en Forward** (pas de G-buffer/deferred), **pas de multiview**.
On ne peut pas encore brancher des Radiance Cascades
là-dessus. Fondations indispensables **avant** la GI avancée :

- **(FAIT) F1. Infrastructure compute.** `ComputePipeline`, dispatch, barrières compute, GPU culling fonctionnels.
- **(FAIT) F2. PBR metallic-roughness + normal mapping.**
- **(FAIT) F3. Pipeline HDR + tonemapping (ACES).**
- **(FAIT) Dynamic Rendering (Vulkan 1.3).**
- **F4. Rendu différé / Visibility buffer.** (À FAIRE) G-buffer (pos monde, normale, albedo,
  rugosité/métal) : entrée obligatoire des Radiance Cascades et des caches.
- **F5. Multiview (stéréo) dès maintenant.** Concevoir **toutes** les nouvelles
  passes multiview-aware (un seul render, deux vues) — fondation VR de `CLAUDE.md`.
- **F6. GPU-driven de base + bindless.** Indirect draw/dispatch + descriptor
  indexing (matériaux/textures bindless) : moderne, et nécessaire au sampling
  efficace des caches/cascades.

> Sans F1–F4, l'« advanced GI » est impossible. C'est ~70 % du travail réel.

---

## 3. Feuille de route phasée (recommandée)

**Phase 0 — Fondations (suite du prérequis absolu)**
F4 deferred/visibility buffer →
F5 multiview → F6 GPU-driven/bindless. (F1 Compute, PBR, HDR et Dynamic Rendering sont déjà FAITS).
*Résultat : un renderer moderne, scalable, prêt pour la GI et la VR.*

**Phase 1 — Radiance Cascades 2D (screen-space GI)**
Première GI **sans bruit**, 100 % compute, mobile/VR friendly. Diffuse indirect +
AO. *Le saut « photoréaliste » le plus accessible.*

**Phase 2 — World Radiance Cache + Spatial Hashing**
Découple l'ombrage de l'écran ; **un cache monde échantillonné par les deux yeux**
→ gain VR majeur. Mises à jour async/interleaved (~14 Mo). AO mondiale.

**Phase 3 — Volumétrie Froxel**
Brouillard / lumière volumétrique / atmosphère, alimenté par les cascades.

**Phase 4 — (optionnel, desktop ultra, long terme)**
Réflexions RT via *ray queries* (`VK_KHR_ray_query`) là où le matériel le permet,
voire ReSTIR DI. **Pas de path tracing complet** sauf comme démo desktop. Activé
par détection de capacités, jamais sur le chemin mobile/VR.

**Transversal — Matrice de scalabilité (excellente idée du rapport).**
4 tiers (Low/Medium/High/Ultra) **combinables à l'exécution** par commutation de
`VkDescriptorSet` / pipelines alternatifs, sans refondre le graphe de trame.
S'aligne sur `VkPhysicalDeviceLimits` interrogé au démarrage. C'est *exactement*
le « regress gracefully » qui colle à la philosophie NextEngine :
- **Low (mobile/Quest)** : lightmaps existantes + Cascaded Irradiance Volumes +
  hachage spatial, **zéro RT**, ombres rastérisées.
- **Medium (PCVR / console)** : Radiance Cascades 2D + World Radiance Cache.
- **High (PC desktop)** : RC 3D directionnelles + réflexions ray-query.
- **Ultra (desktop only, optionnel)** : ReSTIR DI/PT — hors cible principale.

---

## 4. Détail par technique adoptée

### 4.1 Radiance Cascades (Phase 1)
- **Quoi** : champ de radiance hiérarchique ; cascade *n+1* a un espacement de
  sondes ×2 et une résolution angulaire ×4 → **mémoire constante par cascade**
  (théorème clé du rapport), total **linéaire**. Fusion descendante par
  interpolation bilinéaire matérielle (TMU) → lissage « presque gratuit ».
- **Pourquoi mobile/VR** : déterministe (zéro bruit, zéro TAA → pas de ghosting
  VR), 100 % compute, coût borné.
- **Mapping NextEngine** : `render/RadianceCascades.{hpp,cpp}` + compute shaders
  (`cascade_gather.comp`, `cascade_merge.comp`), atlas `R16G16B16A16_SFLOAT`,
  encodage octaédrique. Lit le G-buffer (F4), écrit l'irradiance indirecte,
  consommée dans `lighting.glsl` (terme indirect, remplace l'ambient constant).
- **Coût/risque** : implémentation non triviale (indexation d'atlas, merge) ;
  commencer en **2D screen-space** (le plus documenté : Sannikov / PoE2) avant le
  world-space. VRAM ~quelques dizaines de Mo.

### 4.2 World Radiance Cache + Spatial Hashing (Phase 2)
- **Quoi** : cache de radiance en espace monde, indexé par **double hachage**
  (pcg32 + Jenkins) dans un `VkBuffer` 1D ; payload compressé (~32 bits/cellule).
  Mises à jour **interleaved** (une cascade/volume par trame), **async compute**,
  dispatch indirects pilotés GPU.
- **Pourquoi VR** : **view-independent** → ombré **une fois** en monde, **les deux
  yeux lisent le même cache**. Découple le coût de la résolution/du nombre de vues.
  C'est l'argument VR le plus fort du rapport (idTech 8).
- **Mapping NextEngine** : `render/RadianceCache.{hpp,cpp}`, hash map GPU,
  `cache_update.comp` (async queue), « final gather » sans ombrage (« 0 shading »)
  dans la passe deferred.
- **Risque** : collisions de hash, gestion de durée de vie des cellules ; nécessite
  F1 + F6 + une file compute dédiée.

### 4.3 Volumétrie Froxel (Phase 3)
- **Quoi** : grille 3D alignée sur le frustum (ex. 160×90×64) ; scatter de la
  lumière/brouillard + injection de l'indirect des cascades ; intégration le long
  du rayon de vue. SSS hybride pour végétation/peau.
- **Mobile/VR** : standard (Frostbite), scalable par résolution de froxels ;
  attention au coût en VR (froxels par œil ou partagés).
- **Mapping** : `render/VolumetricFog.{hpp,cpp}` + `froxel_scatter.comp` /
  `froxel_integrate.comp`, échantillonné dans la passe de composition.

### 4.4 Briques transversales
- **Encodage octaédrique** : util GLSL partagé (directions sondes/cascades).
- **Async compute** : file compute séparée (`VulkanDevice` expose déjà les
  queues — à étendre), sémaphores timeline pour la synchro graphics↔compute.
- **GPU-driven** : `vkCmdDrawIndexedIndirect` / `DispatchIndirect`, culling GPU.
- **Bindless** : `VK_EXT_descriptor_indexing` — tableau de textures/matériaux,
  simplifie le sampling des caches et le rendu GPU-driven.

---

## 5. Risques & garde-fous (honnêteté d'ingénieur)

- **Ampleur.** C'est un programme de **plusieurs mois**, pas une feature. Fait
  intégralement, il dépasse la taille actuelle du moteur. Tenir la philosophie
  « léger » = livrer **Phase 0 + Phase 1** comme cible « pointe mais réaliste »,
  et traiter Phases 2–4 comme paliers ultérieurs validés un par un.
- **Le rapport est généré par LLM**, ton marketing (« surpasser Unreal »,
  « photoréalisme absolu »), certaines affirmations non sourçables. À traiter comme
  **survey d'orientation**, pas comme spécification. Les vraies références utiles :
  Sannikov (Radiance Cascades), Sousa SIGGRAPH 2025 (idTech 8), NVIDIA ReSTIR/RTXDI.
- **Path tracing = piège pour la cible.** Brillant en démo desktop, incompatible
  Quest/mobile et hostile à la VR (bruit+denoiser). Ne pas en faire le cœur.
- **Budget VR.** Profiler tôt sur cible standalone : RC world-space + froxels
  peuvent être trop lourds sur Quest ; prévoir le repli tier Low (lightmaps +
  probes), déjà à moitié en place (`LightBaker`).
- **Déterministe > stochastique** reste la règle pour la VR.

---

## 6. Recommandation finale

Pour un moteur **à la pointe ET mobile/VR ET fidèle à sa philosophie** :

> **Cœur à viser** : PBR + HDR/tonemapping + **Radiance Cascades** (2D → world)
> + **World Radiance Cache (spatial hashing)**, tout en **compute + async +
> multiview + view-independent**. **Écarter** le path tracing matériel
> (ReSTIR PT/G-PT) du chemin principal — option desktop ultra, long terme.

Ordre conseillé : **Phase 0 (fondations : PBR → HDR → compute → deferred →
multiview) → Phase 1 (Radiance Cascades 2D) → Phase 2 (Radiance Cache) →
Phase 3 (froxels)**, avec la **matrice de scalabilité** comme colonne vertébrale
architecturale dès la Phase 0.

> Ce document remplace, pour le volet « rendu avancé », les pistes PBR/HDR du
> `BILAN.md` (qui reste valable pour glTF, Lua, physique, XR, nettoyages).
