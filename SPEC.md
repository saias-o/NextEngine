# SaidaEngine - Spécification canonique

Mise à jour : 2026-07-16. Ce document est la vérité technique du moteur. Il
décrit ce qui existe réellement, les contrats candidats V1 et les limites. Les
travaux à effectuer vivent uniquement dans [PLAN_V1.md](PLAN_V1.md).

## 1. Produit et invariants

SaidaEngine permet de créer des jeux 3D desktop, Web et XR avec un éditeur
desktop, un runtime autonome et une surface d'authoring pilotable par Saida.

Invariants :

1. Le modèle de scène, les behaviours et les API gameplay sont communs aux
   plateformes. Un backend peut manquer, pas inventer un second moteur.
2. Vulkan et WebGPU utilisent le même `Renderer` via le RHI; OpenXR remplace la
   présentation sans dupliquer le pipeline de rendu.
3. À contenu, caméra et réglages identiques, le résultat visuel doit être
   identique sur toutes les plateformes. Le lighting, les matériaux, les ombres,
   la colorimétrie et le post-processing ne peuvent pas avoir de comportement ou
   de rendu propre à une plateforme; toute divergence est une régression à
   corriger, pas une variante admise.
4. Le moteur conserve autant que possible un seul code et un seul chemin
   d'exécution partagés entre toutes les plateformes. Une implémentation
   spécifique n'est admise que lorsqu'une contrainte de plateforme l'impose
   réellement, notamment au niveau du RHI, de la présentation ou des API
   système; elle ne doit pas dupliquer la logique moteur ni modifier le rendu.
5. QuickJS est l'unique runtime JavaScript. RmlUi est l'unique système UI
   HTML/CSS; Ultralight/JavaScriptCore ne font plus partie du produit.
6. Les mutations durables passent par des SaidaOps validées puis un snapshot.
7. Une capacité absente est visible via `PlatformCaps` et doit échouer ou se
   dégrader explicitement.
8. Les chemins de projet, scripts et imports restent sous la racine canonique
   du projet.
9. Toute donnée publique est versionnée et migrée. Un type inconnu ou un schéma
   futur est refusé, jamais transformé silencieusement.
10. Le moteur est conçu en priorité pour les cibles Web et mobiles. Les choix
   d'architecture et d'implémentation doivent donc minimiser le temps de
   démarrage, le coût CPU/GPU, les allocations et l'empreinte mémoire.
11. Dans les chemins critiques, privilégier une complexité `O(1)` chaque fois
   qu'elle est raisonnablement possible. À défaut, retenir l'algorithme et les
   structures de données les plus efficaces, avec une attention particulière
   au démarrage et au code exécuté à chaque frame.

Le moteur privilégie RAII, ownership explicite, composition de nœuds et
abstractions petites. Pas d'ECS lourd, render graph généraliste ou framework de
genre sans besoin mesuré.

Contrat gameplay, humain ou IA :

1. Toute logique est un petit `Behaviour`; un nœud compose données et behaviours.
2. Composer plusieurs responsabilités focalisées, pas créer de god-controller.
3. Call down, signal up : appels directs vers descendants, signaux typés vers le
   parent ou à distance.
4. Aucun singleton gameplay. Seuls services moteur et autoloads sont globaux;
   l'état persistant vit dans un autoload.
5. Trouver par groupe ou requête scopée, pas par nom global.

`listen()` porte la durée de vie des connexions. Les mutations d'arbre utilisent
`changeScene`/`queueFree` différés. Un bus d'événements transversal est un signal
typé porté par un autoload, pas une nouvelle globale.

## 2. Architecture d'exécution

### 2.1 Processus et bibliothèques

- `saida_engine` : bibliothèque statique contenant runtime, rendu, scène,
  physique, scripting, UI et assets.
- `saida_editor` : outils ImGui et Hub, jamais linkés au player exporté.
- `SaidaEngine` : éditeur desktop. Sans `--project`, il ouvre une scène vide.
- `SaidaEngineHub` : création, sélection et ouverture des projets.
- `SaidaEngineRuntime` : template de jeu desktop autonome. Il lit `game.saida`,
  monte le projet packagé et lance la scène principale.
- `saida_tool` : validation, fold, inspection et export headless.
- `saida_player` : player de jeu WebAssembly/WebGPU.
- `saida_authoring` : authoring-core WASM headless chargé par la plateforme.
- `saida_web` : runtime visuel WASM/WebGPU embarqué dans Saida Online.

### 2.2 Ownership et boucle

`Engine` possède les sous-systèmes et orchestre une frame : input, temps,
updates différées, physique, scène, audio, UI, rendu et présentation. Les
ressources Vulkan sont RAII, non copiables et empruntent le device. L'ordre de
déclaration doit garantir que le device survit à ses ressources.

VMA v3.1.0 est vendu sous `third_party/vma`; les headers publics utilisent les
forward declarations et une seule TU définit `VMA_IMPLEMENTATION`. Même règle
pour stb/tinyobj. Les descriptor sets sont organisés par fréquence : set 0
global par frame, set 1 par matériau, matrice model en push constant. Le code
applicatif ne manipule pas directement les types VMA.

`SceneTree` porte la scène active, le `World` persistant, les autoloads, les
timers et les changements de scène différés. Une mutation destructive ne peut
pas invalider les caches de rendu de la frame courante; `refreshHierarchy()` est
appelé après les opérations différées.

Les chemins passent par `core/Paths`. En édition, la racine vient du projet
chargé. En runtime packagé, elle vient du dossier de l'exécutable. Les chemins
relatifs sont normalisés et les symlinks qui sortent de la racine sont refusés.

### 2.3 Capacités par runtime

| Capacité | Desktop | Player Web | Authoring Web/headless |
|---|---|---|---|
| Rendering | Vulkan | WebGPU | WebGPU pour le viewer, aucun GPU pour le fold |
| Physics | Jolt | Jolt mono-thread | hors viewer/fold |
| Audio | miniaudio | Web Audio | non requis |
| JavaScript | QuickJS | QuickJS | validation ciblée |
| Game UI | RmlUi | HUD RmlUi `UICanvasNode`/`UITextNode`; WebCanvas absent | authoring partiel |
| Keyboard/mouse | oui | oui | navigateur/UI hôte |
| Gamepad | GLFW standard | `NO` | non requis |
| Touch | brut/non complet | événements bruts | hôte |
| Storage joueur | fichiers | IDBFS | non requis |
| XR | OpenXR | non | non |

Le rapport `PlatformCaps` est loggé au boot. Le desktop annonce tout sauf touch.
Le player Web n'annonce que les backends réellement linkés.

## 3. Projet, scène et authoring

### 3.1 Projet

Un `.saidaproj` décrit notamment le nom, la scène principale, les autoloads,
les aliases audio et les fichiers du projet. L'AssetRegistry associe un AssetID
stable à un chemin relatif. Les caches locaux ne sont pas une source de vérité.

Les autoloads peuvent être des scènes, des behaviours natifs ou des scripts
`.js`/`.mjs`. Le `World` persiste entre les changements de scène. Les nœuds
supportent hiérarchie, groupes, signaux, transforms, activation et suppression
différée.

### 3.2 SaidaOps

Le contrat d'authoring couvre les opérations validées de création/suppression,
transform, propriété, reparenting et autres mutations déclarées par
`EngineManifest`. Le même code sert au desktop, à `saida_tool`, au gateway WASM
et au runtime d'authoring lorsque le type est enregistré.

Règles :

- l'UI ne fabrique pas de JSON arbitraire hors contrat;
- le manifeste est l'autorité sur types, propriétés, enums et bornes;
- une opération invalide n'est pas persistée;
- un batch durable est atomique par défaut;
- `--skip-invalid` est réservé au diagnostic et ne publie pas un snapshot
  durable;
- les nœuds sont encore adressés par nom mutable. Le passage à un NodeId stable
  est un changement de contrat à migrer.

### 3.3 Snapshots et registres

`SceneSnapshot` écrit `schema` et `version`, refuse les futurs schémas et les
valeurs contradictoires. Le fold headless round-trippe son sous-ensemble
enregistré, dont `Node`, `MeshNode` existant, `Camera`, `Area` et
`ScriptBehaviour`. Les références mesh restent opaques sans GPU.

Créer un `MeshNode` pendant un fold sans `ResourceManager` est refusé. Les types
UI et plusieurs nœuds physiques desktop ne sont pas encore dans le registre
headless. Le player et l'authoring Web publient leur registre effectif et
refusent les types/behaviours absents avant de passer à `ready`.

Les registres natif, headless, authoring Web et player Web ne sont donc pas
encore équivalents. Cette divergence est une limite fonctionnelle explicite,
pas une permission de perdre du contenu.

## 4. Rendu et ressources GPU

### 4.1 Renderer

Le renderer supporte Vulkan 1.3, Dynamic Rendering, VMA, depth, MSAA desktop,
PBR metallic-roughness, matériaux lit/unlit, IBL, ACES, lumières directionnelles,
points et spots, shadow mapping PCF, SSAO, bloom, brouillard, lightmap baking
xatlas et DDGI. Les point lights n'ont pas encore de cubemap d'ombre. Les
lightmaps sont régénérées et ne font pas encore partie du package durable.

Le RHI compile vers Vulkan ou WebGPU. Les shaders GLSL sont compilés en SPIR-V,
puis transpilés en WGSL avec naga pour le Web. Le player Web n'a pas de MSAA.
Le rendu XR utilise le même renderer avec vues stéréo/multiview.

Le chemin GPU-driven possède bindless materials, indirect draw, compute culling
et contrats de bindings testés. Il n'est pas le chemin universel actif : des
`useGpuDriven=false` subsistent. Son activation doit devenir un setting/cap
explicite et être benchmarkée contre le chemin classique sur scènes lourdes.
Les revendications de performance exigent scènes reproductibles, GPU/driver,
résolution, nombre de lights/draws/particules et frame CPU/GPU publiés.

### 4.2 AssetRegistry et AssetLoader

`AssetRegistry` est la seule base d'identités. `AssetLoader` expose des handles
asynchrones avec états `queued`, `loading`, `ready`, `failed`, priorités `low`,
`normal`, `high`, `critical`, erreur, taille, id et `release()`.

Textures et meshes `.obj` suivent ce chemin : lecture/décodage worker sur
desktop, `pump()` sur Web, puis création GPU sur le thread principal. Pendant
le chargement, un fallback est visible; un échec utilise un damier magenta.
Les proxies mesh restent stables et la physique reconstruit le body quand le
mesh devient disponible.

Les enregistrements mémoire `registerMemoryRig`/`registerMemoryAnimation` sont
idempotents : ré-importer le même glTF conserve les instances existantes, les
Animators déjà attachés gardent donc des pointeurs valides.

Au `changeScene`, `trimUnused` effectue un mark-and-sweep. La destruction GPU
est différée quatre frames; index bindless et slots matériaux sont recyclés.
`gpuResidentBytes` est exposé à `assets.stats()` et au profiler. Un E2E desktop
et Web a vérifié sa stabilité sur 16 cycles hub/arène.

Limites : pas encore de LRU contraignant pendant une scène, rigs/animations
hors sweep, streaming Web fetch/IDBFS absent et identités de meshes glTF mémoire
encore fragiles lors d'un Stop après changement de scène.

### 4.3 Formats média

- Audio : `.ogg` Vorbis recommandé; `.wav` accepté. MP3 et FLAC non compilés.
- Meshes : glTF/GLB et OBJ. Meshopt est décodé; KTX2/Basis reste absent.
- Un glTF/GLB corrompu peut encore interrompre le player Web.
- `GLTFLoader` utilise une tangente de secours `(1,0,0,1)`; MikkTSpace ou une
  désactivation propre du normal mapping reste nécessaire.

### 4.4 AutoLOD

AutoLOD est un outil séparé basé sur meshoptimizer/xatlas. Il génère des LOD
GLB, accepte ratios et seuils d'erreur, conserve les bords ouverts pour les
assets modulaires, peut produire des LOD autonomes, baker une normal map depuis
le high-poly et créer un proxy lointain avec nouvel unwrap/atlas.

```sh
./build/autolod.exe rocher.glb
./build/autolod.exe batiment.glb out.glb --ratios 0.7,0.4,0.2,0.05 --errors 0.005,0.02,0.05,0.1
./build/autolod.exe mur.glb out.glb --lock-border
./build/autolod.exe casque.glb out.glb --bake --bake-res 1024
./build/autolod.exe casque.glb out.glb --split --bake --bake-res 2048
./build/autolod.exe casque.glb out.glb --proxy --ratios 0.6,0.3,0.1,0.03
./build/autolod.exe --gen-test sphere.glb
./build/autolod.exe --dump-images out.glb prefix_
```

Options : `--ratios`, `--errors`, `--lock-border`, `--uv-weight(s)`,
`--normal-weight`, `--bake`, `--bake-res`, `--bake-cage`, `--split`, `--proxy`.
Sans bake, des poids UV élevés peuvent empêcher d'atteindre le ratio demandé;
avec bake, le poids retombe à 1 et la normal map restitue le détail. Sous 0,15,
`--proxy` soude par position, redécime, recalcule les normales, refait un atlas
xatlas et bake albedo, normal, metallic-roughness, occlusion et emissive dans un
matériau autonome. Il implique `--bake`.

Le bake raycast le LOD0 via BVH/Möller-Trumbore, recompose la normal map source
dans la base tangente du LOD et dilate les îlots. L'outil préserve matériaux,
textures et hiérarchie, weld les sommets, utilise QEM avec attributs puis le
fallback sloppy, optimise le vertex cache et écrit `MSFT_lod` avec
`MSFT_screencoverage`. LOD0 reste intact.

Limites : meshes skinnés ignorés, indices 32 bits, bake mono-thread, UV source
attendus en îlots `[0,1]` hors proxy, surcoût d'un vertex buffer soudé par LOD.
AO/courbure, atlas multi-matériaux, imposteurs et clusters virtualisés restent
des extensions. Les réglages agressifs doivent être validés visuellement.

## 5. Gameplay

### 5.1 Physique

Jolt fournit rigid bodies, collision shapes, character body, areas/triggers,
layers et intégration scène. Le player Web utilise un job system mono-thread.
Le character possède un inner body pour être visible dans broadphase, capteurs
et raycasts. Queries, contraintes avancées et parité complète C++/JS ne sont pas
encore fermées.

### 5.2 Input

Le système agrège des actions numériques/binaires à partir de bindings et de
contexts empilables. Clavier, souris, delta/position, scroll, texte et touch brut
sont échantillonnés une fois par frame.

Sur desktop, le premier gamepad standard GLFW est détecté avec hotplug. Boutons,
sticks et triggers supportent deadzone re-échelonnée, inversion et sensibilité
par `scale`. Les actions par défaut couvrent mouvement, saut, sprint, tir et
visée. Forces, fronts et callbacks sont agrégés entre bindings : relâcher un
périphérique ne termine pas une action maintenue par un autre.

Le player Web garde `gamepad=NO` car le port GLFW Emscripten ne linke pas cette
API. Multi-joueur local, choix de périphérique, profils persistants, rebinding,
haptique et touch-as-bindings ne sont pas livrés. `Input::injectAction` et
`input.inject` sont réservés aux tests/CI.

### 5.3 Audio

`AudioManager` utilise miniaudio sur desktop et Web Audio dans le player Web.
Le JS joue un alias projet avec `audio.play(alias)`. Les navigateurs imposent un
geste utilisateur avant de démarrer le son; le player commence donc muet et se
débloque au premier clic/touch.

### 5.4 Stockage joueur

L'API JS `storage.save/load/has/remove` persiste des chaînes opaques par slot.
Le nom respecte `[A-Za-z0-9_-]{1,64}`. Desktop écrit sous `saves/` dans la
racine runtime; Web utilise IDBFS/IndexedDB.

`storage.save` écrit un fichier temporaire dans le même répertoire, force les
données sur le stockage puis remplace la destination atomiquement. L'ancien
fichier reste intact si l'écriture ou le remplacement échoue, et les temporaires
en échec sont supprimés.

Ce MVP ne fournit pas encore schéma et migrations, préférences séparées,
metadata de slots, quotas, emplacement OS utilisateur ni API asynchrone
complète.

## 6. JavaScript QuickJS

### 6.1 Modules et cycle de vie

Un `ScriptBehaviour` charge `.js` ou `.mjs`. Les hooks exportés sont appelés par
le cycle de vie. Ils sont inspectés une fois après chargement : un hook de type
incorrect est signalé et un module sans hook reconnu produit un warning unique.
Les autoloads scripts partagent le même runtime.

Chaque entrée JS a une deadline de 100 ms. Un drain est limité à 1024 jobs et
les chaînes de microtasks récursives sont interrompues. Les callbacks C++
convertissent leurs exceptions en erreurs QuickJS.

Le script principal et tous les imports restent dans la racine canonique du
projet. Seuls `.js` et `.mjs` sont acceptés; absolus hors projet, traversées et
symlinks sortants sont refusés.

Les scripts/modules et WebCanvas hot-reloadent transactionnellement : une
nouvelle version invalide ne remplace pas le contexte/document vivant. Les
imports relatifs et dépendances RmlUi réellement ouvertes participent au suivi.
Il n'existe pas de hot reload DLL C++ : le linkage dynamique libstdc++ est
fragile sur la toolchain UCRT64 actuelle et le build est statique. C#/.NET n'est
pas retenu afin d'éviter runtime, marshalling et second écosystème de bindings.

### 6.2 API candidate V1

- `node` : nom, position, translation, activation, suppression différée,
  groupes, `on`, `emit`; sur `UITextNode`, `setText/getText`.
- `time` : `delta`, `elapsed`, `wait`, `every`, `tween`, `cancel`.
- `input` : actions, forces, axes, vecteurs, souris et injection de test.
- `audio` : `play(alias)`.
- `tree` : changement/reload de scène, quit, pause, `autoload`, `firstInGroup`,
  `nodesInGroup` et `nodeById`.
- `NodeRef` : référence faible résolue par NodeId, opérations nœud usuelles,
  signaux `on/emit` cross-node et `call(exportName, ...args)` JSON-compatible
  vers un `ScriptBehaviour` dans un autre contexte QuickJS.
- `assets` : `load(path, priority)` et `stats()`; jamais de promesse de
  chargement bloquant.
- `storage` : slots opaques décrits plus haut.

Il manque encore davantage de physique/gameplay et les bindings complets
animation/séquences/blackboard. Les références cross-node deviennent invalides
explicitement lorsque leur NodeId disparaît; aucun pointeur JS ne survit à la
destruction d'une scène.

Une API stable supprimée doit vivre au moins une version en dépréciation avec
warning avant retrait.

## 7. Animation

Le moteur supporte glTF/BVH, rigs, clips, interpolation cubic spline,
retargeting, GPU skinning, animation graph/state machine, blend nodes,
blackboard, clip views, timelines et séquences `.sseq`. Les propriétés de
timeline utilisent la réflexion et interpolent float, int, vec3, vec4 et quat.

Formats : `.sclip`, `.sgraph`, `.sretarget`, `.srig`, `.sseq`; le cache `.sanimc`
est interne. Le behaviour réfléchi `SequenceDirector` joue un `.sseq` au
runtime : les cibles sont résolues par nom dans la scène du nœud porteur
(piste d'animation vers l'Animator du nœud visé ou d'un descendant, piste de
propriété `Nœud.propriete` vers une propriété réfléchie du nœud ou d'un de ses
behaviours), la piste d'événements est relayée par le signal `sequenceEvent`
puis `sequenceFinished` en fin de lecture. La liaison est fail-closed : une
séquence invalide ou une cible toujours absente après le délai de résolution
désactive la lecture avec diagnostics loggés, sans émettre aucun signal.
WitnessGame traverse un personnage riggé avec Idle/Walk, un graphe locomotion
et la séquence `anim/intro.sseq` (clips du totem, événement `intro_beat`,
intensité du Sun) en desktop et Web.

SIMD généralisé, pose sharing massif et GPU crowds sont différés jusqu'à des
mesures qui les justifient.

## 8. UI et WebCanvas

### 8.1 Modèle

RmlUi rend des documents HTML/CSS légers en Screen Space ou World Space.
`UICanvasNode` porte le document et le mode; les nœuds texte et contrôles
interagissent avec `UIInteractionSystem`. `WebCanvasNode` fournit un DOM/JS
ciblé pour UI de jeu, pas un navigateur généraliste.

Un document doit garder structure, style et comportement séparés : `.rml` ou
HTML pour le DOM, CSS local et module JS projet. Les layouts utilisent flex,
tailles explicites et unités simples; éviter les fonctions CSS non supportées,
les dépendances réseau et les hypothèses de navigateur complet.

### 8.2 Surface auteur attendue

- chargement de document et feuilles de style depuis le projet;
- texte, images, classes, attributs, événements click/hover/focus;
- mutation DOM ciblée, liaison de données et appels vers QuickJS;
- hit-test cohérent avec viewport, DPI, resize et capture input;
- Screen Space pour HUD/menus, World Space pour panneaux 3D;
- sérialisation des chemins/modes et lifecycle Play/Stop/reload.

Le bridge desktop expose actuellement le sous-ensemble navigateur suivant :
`getElementById`, `querySelector(All)`, `body`, `documentElement`, sélecteurs sur
éléments, `textContent`, `innerHTML/innerRML`, `id`, `classList`,
`style.setProperty/removeProperty`, `add/removeEventListener`, `click`, `focus`,
`blur`, `getBoundingClientRect`, offsets et tailles client.

CSS fiable : block, inline-block, flex, direction/align/justify/gap, margin,
padding, tailles px/%, position absolute, hover/active, classes, couleurs,
bordures, backgrounds, fonts et line-height. `text-shadow` est filtré; propriétés
vendor, transforms complexes, masks, filters et compositing avancé ne sont pas
garantis. Une classe `.hidden` peut perdre face à un sélecteur plus spécifique;
utiliser par exemple `#panel.hidden`.

Un HUD transparent ne doit pas voler les clics du jeu. Seuls contrôles natifs
`button/input/select/textarea` et éléments `.ui-hit` capturent le pointeur.
Screen Space utilise le rectangle réel du viewport docké, pas toute la
swapchain; World Space raycast le plan et les rayons XR peuvent l'interroger.
Les chemins scripts/images/styles sont relatifs au document.

Le JavaScript UI doit rester modulaire et utiliser les APIs moteur, sans accès
réseau ou système implicite. Le contenu auteur ne doit pas dépendre d'ImGui.

### 8.3 État réel

Le desktop possède RmlUi CPU fonctionnel mais la complétude du rendu, des fonts,
du world-space, de l'interaction et des outils auteur reste à prouver sur le
corpus UI. Un backend GPU/Vulkan RmlUi est une optimisation future, pas une
condition si le backend CPU tient la charge V1.

Le player Web enregistre `UICanvasNode`/`UITextNode`, rasterise leur HUD avec le
backend CPU RmlUi puis compose la texture dans le pass WebGPU. `setText/getText`
utilisent les vrais nœuds et WitnessGame atteint `[E2E] PASS`. Les autres nœuds
UI et `WebCanvasNode` restent refusés tant que leur backend Web n'est pas porté.

Le niveau V1 exige : fonts/assets robustes, screen-space, world-space,
clipping/scissor, resize/DPI, input clavier/souris/touch, fallback XR, bridge
DOM/QuickJS, lifecycle, sérialisation, inspector, picking et scènes de test.

Qualité d'un exemple : HTML/CSS/JS naturel, état JS séparé du DOM, layout normal,
zones transparentes non capturantes, `click` et mutation DOM démontrés, zéro
warning, fonctionnement éditeur/runtime et hot reload transactionnel qui garde
l'ancienne UI si la nouvelle échoue. Pour diagnostiquer : `lastError`, logs
RmlUi/QuickJS, chemins relatifs, pixels/bbox rendus, cascade CSS et rectangle
`EditorUI::viewportPosition/Size`.

## 9. Particules SaidaFX

SaidaFX possède trois niveaux : `ParticleSystemNode`, asset `.saidafx` composé
d'emitters/modules et `ParticleFeature` enregistré dans le renderer. Le nœud
réfléchi expose classe `Simple/Fire/Magic/Rain/Snow/Smoke/Explosion`, budget,
spawn rate, lifetime, vitesse/taille initiales, couleurs start/end, gravity,
radius, emissive, blend `Alpha/Additive`, looping/playing et `effectPath`. Slots :
`play`, `stop`, `burst`, `applyEffectPreset`, `loadEffect`; signal `finished`.

Le chemin CPU V1 rend des billboards HDR, rotation/stretch, alpha/additif,
compacte en une passe, réserve par emitter, réduit la cadence à distance et
cull frustum desktop/stéréo. Les modules exécutés couvrent shapes
Point/Sphere/Disc/Box/Cone/Ring, burst, drag, noise/turbulence, attractor,
size-end et stretch. Les templates vivent sous `assets/fx`; les budgets
`QualityTier` et warnings overdraw/mobile/XR sont exposés.

Le runtime GPU a buffers, descriptors, freelist `deadIndices`, counters reset,
upload host-visible, dispatch emit/sim et barrières compute; les shaders
`particle_emit.comp`, `particle_sim.comp` et render desktop/multiview existent.
Le dessin utilise encore le buffer CPU packé : upload depuis `ParticleFeature`,
draw indirect, buckets par blend et exécution réelle GPU restent à brancher.

La V1 ne dépend pas d'un éditeur de graph complet. Restent comme extensions :
compilation des modules JSON en structs, `SubEmitter`, atlas/flipbook, sorting
alpha, soft particles, ribbons/trails, mesh particles, heat distortion,
shockwave, decals, light pulses, éditeur emitters/modules avec preview, LOD plus
fin, demi-résolution fumée, limites globales caméra/XR et stats détaillées.
L'ergonomie LLM cible création preset, ajout/module, modification paramètre,
sauvegarde et application. Toute évolution mesure CPU/GPU, mémoire, overdraw et
déterminisme et conserve le fallback CPU.

## 10. XR

OpenXR gère session, swapchains, actions, multiview et SaidaXRTK : grab,
téléportation, anchors abstraits, passthrough selon extension et hand tracking
`XR_EXT_hand_tracking`. Les mains procédurales servent de fallback sans asset.

L'aperçu XR est un processus séparé `--xr` car OpenXR doit créer le device
Vulkan dès le démarrage. La scène de test est `MyGame/scenes/XRSetup.scene`.
Quest Link et le runtime Meta/Oculus doivent être actifs pour un test Quest.

Limites : MSAA multiview/resolve, overlay ImGui XR, backend d'anchors réel et
matrice reproductible casques/runtimes non fermés. Les logs indiquent support du
hand tracking et transitions active/lost; une compilation ne remplace pas le
test matériel.

## 11. Éditeur, MCP et IA

L'éditeur fournit arbre de scène, inspector réfléchi, file browser, gizmos,
Play/Stop et undo/redo. Certaines mutations restent seulement dirty, notamment
scripts WebCanvas et changements de CollisionShape avec `resetAuto`; elles
doivent devenir des commandes undoables. Le renommage de projet doit conserver
dossier, Hub et `.saidaproj` cohérents.

Le MCP natif expose des outils aux agents. Le contrat cible exige permissions
par outil, validation, dry-run/diff, transactions groupées, snapshot/rollback et
audit. World model, skills et agents autonomes restent hors V1 tant que ces
garde-fous ne sont pas fermés.

`write_cpp_behaviour` écrit les behaviours LLM sous `src/generated/`. CMake les
globe dans `saida_engine` et leur enregistrement passe par
`scene/ReflectedTypes.cpp`. Ce chemin est une écriture C++ privilégiée : il doit
rester derrière permissions, diff, validation, build et rollback.

## 12. Export et packaging

`BuildExporter` et `saida_tool export-game` produisent un package desktop avec
runtime sans éditeur, projet, scène, assets, shaders et `game.saida`. Les champs
version, société, nom et icône patchent VERSIONINFO et RT_GROUP_ICON. La copie
Windows parcourt explicitement les arbres, écrase les fichiers réguliers et
refuse symlinks/fichiers spéciaux.

Le clic Build de l'éditeur est automatisable : `SaidaEngine --project <p>
--build <out> [--build-platform web]` exécute exactement le code du bouton du
dialogue Build (`EditorUI::executeBuild`, mêmes défauts d'état que l'ouverture
du dialogue — la scène principale par défaut est la `mainScene` du projet),
logge `[BUILD] PASS/FAIL` et retourne le verdict en code de sortie.
`tools/witness_editor_build.sh` construit WitnessGame par ce chemin et exige le
E2E complet sur l'artefact produit.

Le package Web embarque player, projet et shaders sous MEMFS. Les gros jeux
nécessitent encore fetch/IDBFS streaming et compression/manifest de release.

WitnessGame est le corpus vertical : scènes, scripts, signaux, physique,
animation, audio, UI, save/load et changement de scène. Le harnais desktop
injecte les actions via un autoload et exige `[E2E] PASS`. Le 2026-07-16,
desktop export/runtime est PASS. Le package Web charge et rend le HUD RmlUi;
son harnais atteint aussi `[E2E] PASS` sur 16 cycles.

Le projet contient `hub.scene` (joueur CharacterBody, CameraFollow, savepoint,
porte et totem `SeqStatue` piloté par un `SequenceDirector` qui joue
`anim/intro.sseq` en autoplay) et `arena.scene` (trois reliques
Area/Rotator/particules, caisses RigidBody et porte retour). `GameState` possède l'état vivant et persiste
`saves/witness.json`; pickups, HUD, savepoint et harnais l'appellent par
`tree.autoload`/`NodeRef.call`, sans utiliser le fichier comme bus. Le totem
glTF a trois os, clips Idle/Walk et graphe `anim/locomotion.sgraph`. Les sons
`pickup.ogg` et `save.ogg` sont des aliases projet. Les scènes se régénèrent par
`gen_witness.py`, le personnage par `gen_character.py`; modifier les générateurs,
pas leurs sorties.

Régressions désormais couvertes : signaux Area réfléchis, storage JS, autoloads
scripts, appels JSON inter-contextes, groupes/NodeId/signaux cross-node, chemin
shaders export, `setText/getText`, animation/audio, injection
headless, résolution scripts depuis la racine projet, inner body Character pour
triggers, refresh des caches après `changeScene/queueFree`, gizmos colliders sur
le viewport docké, timer `?smoke` pour onglet caché, pile WASM 4 MiB/QuickJS
256 KiB, copie Windows, sandbox QuickJS, traversée de séquence `.sseq`
(événement + fin, desktop et Web), ré-import d'un même glTF dans une scène
sans invalider rigs/clips des Animators déjà attachés, clic Build éditeur
automatisé (`--build`) et progression restaurée après redémarrage (second
process desktop sur `saves/`, reload navigateur sur IDBFS). À surveiller : halos de viewport non
reproduits et dispatch autoload encore dupliqué entre `Engine::mountWorld` et le
player Web.

Une release exige encore une machine vierge sans MSYS2/SDK, archive ou
installateur signé, DLL vérifiées, crash logs, rollback, SBOM et hashes de
tous les artefacts.

## 13. Compatibilité persistante

| Surface | Schéma | Politique |
|---|---:|---|
| `game.saida` | 1 | candidat V1, migration obligatoire |
| `.saidaproj` | 1 | candidat V1, migration obligatoire |
| `asset_registry.json` | 1 | candidat V1, AssetID stable visé |
| `.scene` | 2 | candidat V1, migration obligatoire |
| `.saidascenario` | 1 | candidat V1, migration obligatoire |
| `.sclip` | 1 | candidat V1, migration obligatoire |
| `.sgraph` | 2 | candidat V1, migration obligatoire |
| `.sretarget` | 2 | candidat V1, migration obligatoire |
| `.srig` | 1 | candidat V1, migration obligatoire |
| `.sseq` | 1 | candidat V1, migration obligatoire |
| `.sanimc` | interne | cache régénérable |
| `asset_registry.local.json` | interne | cache local régénérable |
| `pipeline_cache.bin` | interne | cache GPU régénérable |

Les formes historiques sans enveloppe ou sans `schema` sont les V0 de
référence. Un simple chargement ne réécrit jamais le source. Les fixtures sous
`tests/fixtures/compat` sont immuables et chargées par
`saida_compat_corpus_tests`. Le fixture `fold-determinism` prouve un fold
byte-identique Windows/Linux sur son corpus, pas l'équivalence exhaustive de
toutes les scènes.

Tout changement stable fournit : incrément de schéma, migrations supportées,
fixture ancien, test de chargement/refus futur et note de version. La stabilité
publique exige aussi un corpus de round-trip cross-runtime et un release
manifest liant hashes du player Web, authoring WASM, binaire headless et formats.

Inventaire immuable actuel : `project_v0/v1.saidaproj`,
`asset_registry_v0/v1.json`, `scene_v0/scene_v2.scene`,
`scenario_v0/v1.saidascenario` et `game_v0/v1.saida`. Ne jamais modifier ou
régénérer ces fichiers; ajouter `*_vN`. Le test vérifie aussi que le chargement
ne change aucun octet source. WitnessGame publié sera ajouté comme nouveau
fixture gelé.

Le corpus `fold-determinism` contient `base.json`, `ops.json` et
`expected.json` produit sous Windows. Il couvre set_property, create_node,
set_transform avec floats/quaternion non triviaux, reparent, rename et setting
de scène. Linux doit produire une sortie byte-identique. `expected.json` ne se
régénère qu'avec un bump de format, jamais pour masquer une divergence.

## 14. Limites connues consolidées

- V1 non publiée; aucun badge local ne vaut stabilité publique.
- Player Web : WebGPU obligatoire, HTTP obligatoire, UI limitée au HUD
  `UICanvasNode`/`UITextNode`, WebCanvas absent, gamepad absent, touch incomplet,
  MSAA absent et contenu glTF corrompu encore dangereux.
- Audio Web soumis au geste utilisateur.
- Un runtime/canvas Emscripten par page; build non modularisé.
- Registres natif/headless/Web divergents; UI/physique hors de certains folds.
- Nœuds adressés par noms mutables, pas NodeId stable.
- Bindings physique, animation, séquences et blackboard encore incomplets.
- Sauvegardes encore locales au projet et sans migrations/metadata/quota.
- Asset LRU en cours de scène, streaming Web et sweep rigs/anims absents.
- Point-light shadows cubemap et lightmaps persistantes absentes.
- XR sans MSAA multiview, overlay et matrice hardware validée.
- Build UI/machine vierge, signature, crash reporting et rollback non prouvés.
- Inventaire des licences, notices et SBOM non finalisés.

## 15. Positionnement public

Positionnement honnête : moteur C++17/Vulkan/WebGPU expérimental, local-first,
éditeur pilotable par MCP structuré, player Web et jeu témoin en Alpha. Le
serveur MCP in-process et son bridge stdio existent; hot reload QuickJS/UI est
partiel; il n'existe ni promesse Lua ni hot reload C++ général.

Ne pas revendiquer avant preuves : supériorité de performance, parité Web,
sandbox de projets tiers, compatibilité stable, génération complète depuis un
prompt ou support XR production. Une démo publique doit partir d'un tag signé,
montrer diff/validation MCP, le même projet dans l'éditeur puis les vrais exports
desktop/Web, publier limites et hashes, et être reproductible par un tiers. Les
objectifs de calendrier, vues ou étoiles ne sont pas des garanties produit.

## 16. Vérification de référence

```sh
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/bin/saida_tool.exe describe-engine
./tools/witness_e2e.sh
./tools/witness_editor_build.sh
./tools/witness_web_stage.sh
```

`witness_e2e.sh` lance l'artefact deux fois : le second lancement doit
produire `[E2E] RESTART PASS` (progression restaurée depuis `saves/`). Côté
Web, recharger la page après un `[E2E] PASS` doit produire le même verdict
depuis IDBFS.

Après modification de snapshot, SaidaOp, manifeste, registre, scripting ou
input partagé : reconstruire également `build-authoring-wasm` et
`build-web-player`. Une preuve release est obtenue depuis un commit propre, avec
artefacts versionnés; un résultat local reste une preuve de développement.

Les sondes historiques sous `web/spike` restent utiles pour isoler la toolchain :
`hello.cpp` prouve emcc+Node, `webgpu_probe.cpp` le link emdawnwebgpu et
`spike.cpp` GLFW+canvas+WebGPU+rAF. Exemple :

```sh
emcc hello.cpp -O2 -o hello.js && node hello.js
emcc spike.cpp -O2 --use-port=emdawnwebgpu -sUSE_GLFW=3 -o index.html
python -m http.server 8080
```

Le résultat attendu du spike est un clear animé et le log device/surface ready;
il ne valide volontairement ni géométrie ni shaders du moteur.
