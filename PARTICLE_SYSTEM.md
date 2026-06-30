# PARTICLE_SYSTEM.md - Plan SaidaFX Pour SaidaEngine

## Resume

Creer **SaidaFX**, un systeme de particules et FX inspire de Niagara, mais adapte a
SaidaEngine : leger, data-driven, Vulkan-first, compatible XR multiview,
optimise VR/mobile, et facile a piloter par humains comme par LLM.

Le systeme repose sur trois niveaux :

- `ParticleSystemNode` : noeud de scene simple, inspectable, serialisable,
  expose au manifeste LLM.
- `ParticleEffect` / `.saidafx` : asset d'effet compose d'emitters et de modules.
- `ParticleFeature` : render feature autonome qui simule/rend les particules
  sans modifier directement `Renderer`.

Le design suit les patterns existants du moteur : RAII, ownership clair,
reflexion, `RenderFeatureRegistry`, shaders compiles par CMake, chemins
desktop/XR unifies.

## Etape 1 - Fondation Render + Node

Ajouter une premiere version utilisable de particules billboard GPU-rendues,
sans simulation compute avancee.

Changements publics :

- Ajouter `ParticleSystemNode`.
  - Type reflechi : `"ParticleSystem"`.
  - Proprietes v1 :
    - `effectClass`: enum `Simple`, `Fire`, `Magic`, `Rain`, `Snow`, `Smoke`,
      `Explosion`.
    - `maxParticles`: int.
    - `spawnRate`: float.
    - `lifetime`: float.
    - `startSpeed`: float.
    - `startSize`: float.
    - `startColor`: vec4.
    - `endColor`: vec4.
    - `gravity`: vec3.
    - `radius`: float.
    - `emissive`: float.
    - `blendMode`: enum `Alpha`, `Additive`.
    - `looping`: bool.
    - `playing`: bool.
  - Slots reflechis : `play`, `stop`, `burst`.
  - Signal reflechi : `finished`.

- Etendre `Scene`.
  - Ajouter une liste aplatie `particleSystems()`.
  - Collecter les `ParticleSystemNode` actifs dans `flattenHierarchy()`.

- Ajouter `ParticleFeature`.
  - Enregistree dans `RenderFeatureRegistry` avec ordre `250`, apres
    skybox/outline et avant debug lines.
  - Construit ses pipelines via `RenderContext`.
  - Supporte mono desktop et XR multiview avec `MULTIVIEW`.

- Ajouter les shaders :
  - `particle_render.vert`
  - `particle_render.frag`
  - variante CMake `multiview.particle_render.vert`.

- Etendre `Pipeline`.
  - Remplacer le booleen `useBlending` par un enum interne ou une surcharge
    compatible.
  - Support minimum v1 : alpha blend et additive blend.
  - Garder le comportement existant inchange pour les pipelines actuels.

Comportement v1 :

- Simulation CPU simple dans `ParticleFeature::record()` ou petit runtime interne
  temporaire.
- Rendu billboard camera-facing.
- Couleur et taille interpolees selon l'age.
- Sortie HDR lineaire pour profiter du bloom/tonemap existant.
- Pas de tri v1 sauf ordre par emitter ; l'additif est le chemin recommande.
- Pas de collision, ribbons, subemitters, soft particles, compute simulation dans
  l'etape 1.

## Etat Actuel

Fait :

- `ParticleSystemNode` reflechi, serialisable, inspectable.
- Proprietes v1 : classe d'effet, budget, spawn, lifetime, vitesse, taille,
  couleurs, gravite, radius, emissive, blend, looping, playing.
- Slots : `play`, `stop`, `burst`, `applyEffectPreset`, `loadEffect`.
- Signal : `finished`.
- `Scene::particleSystems()` collecte les emitters actifs.
- `ParticleFeature` rend des billboards HDR en alpha/additive.
- Shaders render desktop + multiview XR.
- `Pipeline` supporte alpha/additive sans casser l'ancien chemin.
- `ParticleRuntime` existe avec buffers GPU, descriptor sets, compute pipelines.
- Shaders compute presents : `particle_emit.comp`, `particle_sim.comp`.
- `ParticlePresetLibrary` pour `Simple`, `Fire`, `Magic`, `Rain`, `Snow`,
  `Smoke`, `Explosion`.
- Format `.saidafx` JSON versionne avec emitters + modules Niagara-like.
- Validation `.saidafx` : version, emitters, modules et params requis.
- Compilation `.saidafx -> ParticleSystemNode` pour le chemin v1.
- `effectPath` + drop/load `.saidafx` dans l'inspector.
- Budgets SaidaFX par `QualityTier`.
- Simulation CPU v1 optimisee : compactage en une passe, reserve par emitter,
  cadence reduite pour effets lointains.
- Culling frustum desktop par emitter pour eviter simulation/pack hors camera.
- Runtime GPU avance :
  - freelist template `deadIndices`
  - reset counters par frame
  - upload emitters host-visible
  - dispatch encapsule emit + sim
  - barriers compute internes
- Rotation billboard et stretch billboard shader-side pour pluie/sparks/magie.
- Modules v1 executes sur le chemin CPU :
  - shape `Point/Sphere/Disc/Box/Cone/Ring`
  - burst count
  - drag
  - noise/turbulence
  - attractor
  - size end scale
  - stretch
- Culling frustum desktop + XR stereo par emitter.
- Templates `.saidafx` livres dans `assets/fx/`.
- Warnings editeur simples : budget mobile/XR, overdraw estime, alpha dense.
- Tests non-GUI : reflection + particle effect assets.

## Reste A Faire

### 1. Simulation GPU Reelle

- Brancher l'upload emitters depuis `ParticleFeature`.
- Lire/dessiner depuis les buffers GPU au lieu du buffer CPU-packe.
- Ajouter draw indirect / indirect count quand disponible.
- Ajouter buckets GPU par blend mode (`Alpha`, `Additive`).
- Garder le chemin CPU v1 comme fallback debug/compat.

### 2. Runtime Modules Niagara-like

- Compiler les modules `.saidafx` en structs compactes, pas en JSON runtime.
- Executer vraiment :
  - `SubEmitter`
- Ajouter validation de budget plus fine : cout estime, overdraw, limites XR.

### 3. Rendering Avance

- Texture atlas / flipbook.
- Sorting alpha optionnel ou buckets coarse.
- Soft particles via depth.
- Ribbons/trails.
- Mesh particles.
- Heat distortion.
- Shockwave.
- Decal/splash impacts.
- Light pulses optionnels.

### 4. Editor FX

- Editeur `.saidafx` simple/advanced.
- Liste d'emitters.
- Ajouter/supprimer/reordonner modules.
- Edition des params module par module.
- Preview live.
- Save/load `.saidafx`.
- Templates rapides : Fire, Magic, Rain, Snow, Smoke, Explosion.
- Editeur visuel avance des warnings overdraw/budget.

### 5. Optimisation VR/Mobile

- LOD par distance plus fin que la cadence actuelle.
- Limites globales par scene et par camera XR.
- Demi-resolution optionnelle pour fumee dense.
- Budgets differents desktop / XR / mobile.
- Stats debug : live particles, spawned/frame, killed/frame, overdraw risk,
  GPU buffer usage.

### 6. Ergonomie Humain + LLM

- Manifeste plus riche pour les modules `.saidafx`.
- Commandes MCP/LLM :
  - creer un effet preset
  - ajouter un module
  - modifier un parametre module
  - sauvegarder `.saidafx`
  - appliquer a un `ParticleSystemNode`
- Documentation courte des params par classe d'effet.

## Prochaine Etape Recommandee

Faire le vrai chemin GPU :

1. init GPU des freelists/counters ;
2. upload emitters actifs ;
3. dispatch emit + sim ;
4. barriers compute -> vertex ;
5. draw depuis les buffers GPU ;
6. fallback CPU conserve.

## Plan De Test

Tests de build :

- `cmake --build build`
- Verifier que les nouveaux shaders se compilent.
- Verifier que `saida_engine`, `SaidaEngine`, `SaidaEngineRuntime` linkent.

Tests unitaires / non-GUI :

- Etendre ou ajouter un test de serialisation pour `ParticleSystemNode`.
- Verifier round-trip JSON des proprietes reflechies.
- Verifier que `NodeRegistry` cree `"ParticleSystem"`.

Tests manuels GPU :

- Creer une scene avec un `ParticleSystemNode` `Simple`.
- Verifier en desktop : particules visibles, bloom HDR, alpha/additive corrects.
- Verifier en XR build : shader multiview compile et n'utilise pas d'hypothese
  mono.
- Verifier que desactiver le noeud le retire du rendu.
- Verifier que `play`, `stop`, `burst` fonctionnent depuis inspector/MCP/JS plus
  tard.

## Assumptions Et Defauts

- Le fichier de roadmap est `PARTICLE_SYSTEM.md` a la racine du repo.
- La premiere implementation privilegie la clarte et l'integration moteur plutot
  qu'une simulation GPU complete.
- Les particules v1 utilisent des billboards unlit/emissive dans le HDR existant.
- Les chemins existants `Renderer` et `RenderFeatureRegistry` restent les coutures
  principales ; pas de render graph ajoute.
- Les effets avances `.saidafx` arrivent apres une fondation stable et testee.
