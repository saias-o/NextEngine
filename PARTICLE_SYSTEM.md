# PARTICLE_SYSTEM.md - Plan NEFX Pour NextEngine

## Resume

Creer **NEFX**, un systeme de particules et FX inspire de Niagara, mais adapte a
NextEngine : leger, data-driven, Vulkan-first, compatible XR multiview,
optimise VR/mobile, et facile a piloter par humains comme par LLM.

Le systeme repose sur trois niveaux :

- `ParticleSystemNode` : noeud de scene simple, inspectable, serialisable,
  expose au manifeste LLM.
- `ParticleEffect` / `.nefx` : asset d'effet compose d'emitters et de modules.
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

## Etapes Suivantes

Etape 2 - Runtime GPU :

- Ajouter `src/fx/ParticleRuntime`.
- Ajouter buffers GPU : particles, alive indices, dead indices, emit requests,
  indirect draw.
- Ajouter `particle_emit.comp` et `particle_sim.comp`.
- Passer a une simulation GPU-first avec budgets par qualite.

Etape 3 - Classes D'effets :

- Ajouter `ParticlePresetLibrary`.
- Compiler les classes `Fire`, `Magic`, `Rain`, `Snow`, `Smoke`, `Explosion`
  vers des parametres internes.
- Garder une interface simple dans l'inspecteur et le manifeste LLM.

Etape 4 - Modules Niagara-like :

- Ajouter stack de modules `.nefx` :
  - Spawn rate / burst
  - Shape sphere/cone/box/ring
  - Initial velocity
  - Gravity / drag
  - Noise / turbulence
  - Color over life
  - Size over life
  - Attractor
  - Sub-emitter
- Serialiser en JSON.
- Ajouter un mode editeur simple/advanced.

Etape 5 - VR/Mobile :

- Budgets globaux par `QualityTier`.
- LOD par distance.
- Update cadence reduite pour effets lointains.
- Demi-resolution optionnelle pour fumee dense.
- Overdraw warnings dans l'editeur.

Etape 6 - FX Avances :

- Ribbons/trails.
- Mesh particles.
- Soft particles via depth.
- Heat distortion.
- Shockwave.
- Decal/splash impacts.
- Light pulses optionnels.

## Plan De Test

Tests de build :

- `cmake --build build`
- Verifier que les nouveaux shaders se compilent.
- Verifier que `ne_engine`, `NextEngine`, `NextEngineRuntime` linkent.

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
- Les effets avances `.nefx` arrivent apres une fondation stable et testee.
