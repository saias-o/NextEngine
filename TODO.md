# NextEngine — Roadmap des tâches restantes

## Étape 8 — Couche jeu (statut: ~95%)

### À faire
- [ ] **Runtime standalone sans éditeur (Étape 15 interdépendance)**
  - Livrer un jeu sans l'interface éditeur, UI minimale
  - Fait : `NextEngineRuntime` target existe
  - Manque : intégration complète + testing

---

## Étape 9 — Rendu global / GI pragmatique (statut: 100% CORE, futur optionnel)

### Recherche différée (optionnel, hors roadmap active)
- [ ] **Radiance Cascades 2D / World Radiance Cache / froxels volumétriques**
  - Dépendance : un jeu concret prouvant que le pipeline actuel (DDGI + IBL + AO) ne suffit pas
  - Priorité : très basse (sauf perf sur mobile/XR)
  - Notes : recherche spécialisée, dépasse scope moteur léger

---

## Étape 10 — Animation System (statut: 100% CORE)

### Restes hors-périmètre (futurs)
- [ ] **Retargeting proportionnel / rest-pose**
  - Contexte : notre `AnimationClip` n'encode pas la bind pose source
  - Impact : retargeting avancé nécessite architecture changement
  - Priorité : très basse (retargeting par noms suffisant pour jeux simples)

---

## Étape 11 — Simulation Physique (statut: 100% CORE)

### Notes de performance
- **Build Release obligatoire** : build sans `CMAKE_BUILD_TYPE` → Jolt non optimisé
  - Solution : `-DCMAKE_BUILD_TYPE=Release` pour la perf de jeu

---

## Étape 12 — UI 2D (Screen & World Space) (statut: 95%)

### Optimisation future (optionnelle, sans changement d'API)
- [ ] **Backend GPU/Vulkan pour très gros documents RmlUi animés**
  - Contexte : rendu CPU actuel (`RmlUiRenderInterface`) est déterministe et portable
  - Quand : si un jeu démontre besoin de HUD complexe/haute-fps
  - Impact : zéro changement API `WebCanvasNode`, juste implémentation
  - Priorité : très basse

---

## Étape 13 — Intégration LLM Native (statut: 95% M1-M6 complets)

### M1 — Réflexion + Manifeste (partiel)
- [ ] **Inspecteur générique pour behaviours réfléchis**
  - Fait : générateur pour les **nœuds** réfléchis (ex. WaterNode)
  - Manque : étendre aux **behaviours** réfléchis (Health, Vehicle, NpcWander, Gun, StateMachine…)
  - Détail : nécessite commandes d'undo **adressées par behaviour** dans `PropertyEditor`, pas juste par nœud
  - Impact : LLM pourra autorer/modifier comportements directement en éditeur
  - Fichiers clés : `src/editor/InspectorPanel.cpp`, `src/editor/PropertyEditor.{hpp,cpp}`

### Cibles transverses (futurs)
- [ ] **World model** (état du monde pour l'IA)
  - Description : snapshot de scène pour prompt LLM
  - Dépendance : MCP `get_scene` déjà compact, extension nécessaire
  - Priorité : moyenne (avec agents autonomes)

- [ ] **Skills exécutables** (API action pour agents)
  - Description : primitives haut niveau que l'IA peut invoquer (spawn, move, animate…)
  - Dépendance : MCP `configure_behaviour` / signal wiring déjà là
  - Priorité : moyenne (avec agents autonomes)

- [ ] **Agents autonomes** (boucle de reasoning)
  - Description : agents LLM locaux orchestrant gameplay via skills
  - Dépendance : skills + world model
  - Priorité : basse (nice-to-have, hors core)

---

## Étape 14 — XR / OpenXR (statut: 85%)

### Contrôleurs & Actions
- [ ] **Hand tracking (skeletal, `XR_EXT_hand_tracking`)**
  - Statut : optionnel, plus tard
  - Contexte : les action sets (grip/aim/trigger/thumbstick) suffisent v1
  - Dépendance : extension OpenXR, validation au casque
  - Priorité : très basse

### Optimisations performance/qualité
- [ ] **MSAA multiview + resolve par layer**
  - Contexte : v1 = 1 sample en XR, desktop = jusqu'à 4×
  - Impact : qualité visuelle XR, perf PCVR testée
  - Détail : resolve layer-wise après multiview
  - Priorité : moyenne (après validation v1)

- [ ] **Overlay ImGui (quad/layer)**
  - Contexte : ImGui désactivé en XR v1
  - Impact : debug/console en XR
  - Priorité : basse (debug only)

- [ ] **Culling stéréo combiné**
  - Contexte : v1 dessine tout deux fois (pas de culling per-eye)
  - Impact : perf mobile/Quest, moins critique PCVR
  - Priorité : très basse

### Découplage Renderer (DRY)
- [ ] **Extraire boucle commune `recordMeshDraws(VkCommandBuffer, Pipeline&)`**
  - Contexte : dessin de meshes **dupliqué** entre desktop (`recordScenePass`) et XR (`recordXrScenePass`)
  - Fichier : `src/render/Renderer.cpp`
  - Solution : méthode partagée prenant le pipeline en paramètre (mono vs multiview)
  - Avant : valider visuellement au casque
  - Priorité : haute (code maintenance, safe refactor)

### NEXRTK — NextEngine XR Toolkit (statut: 75%, v1 compiled)

#### Phase 1 (fait, compile)
- XRInput, XRController, XRGrabbable, XRTouchable, XRDirectInteractor

#### Phase 2 (fait, compile)
- Locomotion : XROrigin, TeleportArea, XRRayInteractor

#### Phase 3 (fait, compile)
- Anchors & passthrough : XRAnchor, XRAnchors service, XRPassthrough service

#### Restes optionnels (casque)
- [ ] **Backend d'anchors réel**
  - Extensions : `XR_MSFT_spatial_anchor` ou `XR_FB_spatial_entity`
  - Outil : `XRAnchors::setBackend()`
  - Priorité : très basse (v1 = anchors pose-only)

- [ ] **Hand tracking skeletal**
  - Extension : `XR_EXT_hand_tracking`
  - Dépendance : skelettal hand model
  - Priorité : très basse

---

## Étape 15 — Build & Release Windows (statut: 80%)

### Fait
- Pipeline autonome de build/export ✅
- Runtime sans éditeur ✅
- Packaging assets + shaders ✅
- UI Build Settings réelle ✅

### À faire

- [ ] **Gestion des versions, métadonnées de l'exécutable et icône du jeu**
  - Détail : versionning dans `game.ne`, resource PE (icône/version/description)
  - Impact : jeu livrable avec metadata professionnel
  - Priorité : moyenne (avant release public)

- [ ] **Optimisation finale de build**
  - Link-Time Optimization (LTO) optional en Release
  - Suppression traces de debug/ImGui en Release (ifdefs)
  - Impact : taille exe, stripping debug symbols
  - Priorité : basse (perf optionnelle)

---

## Étape 16 — Effets visuels avancés (statut: 0%, nouveau)

### Shader gouttes de pluie (paramétriques, réaliste)
- [ ] **Système de gouttes de pluie paramétrique**
  - Détail : shader surface-agnostic, appliqué via `Material` property
  - Paramètres : densité, taille, vitesse de chute, angle du vent, brille (reflectivity)
  - Optimisation mobile/VR : instancing par microgeometry overlay ou world-space quad par screenspace
  - Implémentation : passe fullscreen post-processing OU overlay procedural sur géométrie (évaluer perf)
  - Réalisme : normal map + roughness variation, parallaxe motion
  - Priorité : moyenne (effectif showstopper pour scènes météo)
  - Notes : validé visuellement desktop + VR + mobile perf-test

### Shader & Node Outline (selection/effect non-destructif)
- [ ] **Système d'outline paramétriques sans duplication mesh**
  - Détail critique : l'outline ne remplace **pas** le material du mesh, c'est un effet optionnel ajouté
  - Architecture : 2 approches à évaluer :
    - **A) Post-processing edge detection** (cheapest, écran full) : Sobel/Roberts sur depth+normal
    - **B) Expanded silhouette** (quality, per-mesh) : amplifier les backfaces via position + thickness UBO
  - Node `OutlineNode` ou flag sur `MeshNode` : `enableOutline` + `outlineColor`/`outlineThickness`/`outlineEmission`
  - Paramètres : épaisseur (pixels/world), couleur, emission, softness blend
  - Optimisation mobile/VR : LOD outline thickness, post-processing cheaper que per-mesh
  - Impact : `Renderer` passe d'outline (avant ou après tonemap)
  - Priorité : moyenne (essentiel pour UI/sélection en jeu)
  - Notes : mesh duplication verboten, perf-test post-proc vs silhouette

### Shader Fresnel (halo, highlight, rim-light)
- [ ] **Système Fresnel paramétrique et composable**
  - Détail : pas un matériau, un **layer d'effet** appliqué sur l'éclairage PBR existant
  - Paramètres : fresnel power, color (halo), intensity, falloff curve
  - Use-cases : rim-light (perso), halo (UI/magie), highlight (armes/special)
  - Implémentation : ajout `FresnelDesc` dans `Material` + flag `enableFresnel`
  - Calcul : `pow(1 - dot(V, N), power) * color * intensity` dans fragment courant, pas pipeline séparé
  - Optimisation mobile/VR : O(1) par pixel, zéro surcharge vs PBR
  - Priorité : moyenne (ajoute perception de "qualité" visuelle cheap)
  - Notes : intégrable dans `shader.frag` via UBO unifié set 1

### Système de Particules révolutionnaire
- [ ] **Particle system GPU-driven, data-oriented, dead simple**
  - Objectif : feu/pluie/magie/flocons/explosion avec une API triviale
  - Architecture GPU-first : SSBO particules, compute shader update, INDIRECT draw
  - Node : `ParticleEmitterNode` (attachable) → emit(count, preset, duration, scale)
  - Presets fournis : `Fire`, `Rain`, `MagicSpark`, `Snowflake`, `Explosion`
  - Paramètres emitter :
    - Shape : point, sphere, cone, box, custom mesh
    - Emission : rate (px/s), burst (instant count), lifetime, velocity (min/max)
    - Physics : gravity, drag, collision (opt), bounce
    - Rendering : mesh/sprite, material, size over lifetime, color gradient
  - Optimisation mobile/VR : 
    - **GPU simulation** (non-GPU fallback optionnel) : update sur compute
    - **GPU instancing** : `vkCmdDrawIndirect` sur count buffer
    - **LOD** : réduction particules en mobile/mobile, simple quad sprite en LOD-far
    - **Pooling** : réutilisation buffers, zéro alloc runtime
  - API simple (script JS) :
    ```js
    let emitter = node.addBehaviour(ParticleEmitterBehaviour);
    emitter.startEmitting("Fire", { scale: 2.0, lifetime: 3.0 });
    ```
  - Édition : inspecteur + preview temps-réel, courbes lifetime (size/color), shape gizmos
  - Priorité : très haute (effet de base manquant, demandé game-dev)
  - Notes : valider perf compute shader desktop/Quest, fallback CPU si pas compute

---

## Résumé par priorité

### Très haute (manque critical, game-dev demand)
- **Système de Particules GPU-driven** (Étape 16) — base effects FX (feu/pluie/explosion), MVP pour tout jeu
- Découplage Renderer multiview `recordMeshDraws` (Étape 14) — code maintainability

### Haute
- Shader Outline (non-destructif, selection UI) (Étape 16)
- Shader Fresnel (rim-light, perception visuelle cheap) (Étape 16)
- Inspecteur générique behaviours (Étape 13 M1)

### Moyenne
- Shader Gouttes de pluie paramétrique (Étape 16) — monde vivant
- Gestion versions + icône (Étape 15)
- MSAA multiview (Étape 14 quality)
- Agents autonomes + world model (Étape 13 futurs)

### Basse
- Hand tracking skeletal (Étape 14 optionnel)
- Culling stéréo (Étape 14 optionnel)
- Overlay ImGui XR (Étape 14 debug)
- LTO build (Étape 15 optionnel)
- Backend anchors réel (NEXRTK optionnel)

### Très basse (recherche future)
- Radiance Cascades / World Cache / froxels (Étape 9 recherche)
- Retargeting proportionnel (Étape 10 futur)
- GPU backend RmlUi (Étape 12 optionnel)
