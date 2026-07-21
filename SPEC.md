# SaidaEngine - Spécification canonique

Mise à jour : 2026-07-19. Ce document est la vérité technique du moteur. Il
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
| Gamepad | GLFW standard | Gamepad API, mapping `standard` | non requis |
| Haptique pad | non (GLFW) | `dual-rumble` si exposé par le pad | non requis |
| Touch | non | brut + zones/tap/swipes | hôte |
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
- `opVersion: 2` adresse tous les nœuds par `NodeId` stable encodé en chaîne
  décimale 64 bits, afin de ne perdre aucun bit dans JavaScript;
  `parentId`, `newParentId`, `fromNodeId` et `toNodeId` portent les autres
  références. Un parent omis désigne la racine. Les noms ne sont jamais acceptés
  comme références durables. `create_node` accepte un `nodeId` fourni par le
  client pour qu'un batch déterministe puisse cibler immédiatement le nœud;
  sinon l'applier génère l'ID et le renvoie dans son diff.

### 3.3 Snapshots et registres

Tout format durable écrit `schema` et `version` égaux et délègue leur contrôle
au garde unique `format::schemaEnvelopeError` : `schema`/`version` doivent être
entiers, concorder — une divergence signale un document falsifié ou non
conforme — et ne pas dépasser la version supportée. Snapshot, scène, projet,
registre d'assets et scénario partagent ce refus fail-closed avec un diagnostic
nommant le format; les versions antérieures sont migrées en mémoire. Le fold
headless round-trippe son sous-ensemble
enregistré, dont `Node`, `MeshNode` existant, `Camera`, `Area` et
`ScriptBehaviour`, ainsi que le HUD `UINode`/`UICanvasNode`/`UITextNode`. Les
types `CollisionShape`, `StaticBody`, `RigidBody` et `CharacterBody` conservent
également toutes leurs propriétés durables sans démarrer Jolt. Les références
mesh restent opaques sans GPU.

Créer un `MeshNode` pendant un fold sans `ResourceManager` est refusé. Les types
UI avancés restent hors du registre headless tant qu'ils ne font pas partie du
contrat player V1. Le player et l'authoring Web refusent les types/behaviours
absents avant de passer à `ready`.

`RuntimeTypeMatrix.hpp` est l'inventaire V1 unique des factories des quatre
runtimes. Il distingue `required`, `optional` (XR natif conditionnel) et
`absent`, est publié dans `EngineManifest`, puis comparé aux registres effectifs
au boot/snapshot. Toute factory requise manquante ou non déclarée fait échouer le
runtime avec diagnostic. Les registres ne sont pas encore équivalents : la
matrice rend chaque divergence explicite, elle ne donne pas la permission de
perdre du contenu.

Le test `saida_runtime_type_matrix_tests` construit le corpus headless depuis la
matrice : 17 types de nœuds (joints physiques inclus), 18 behaviours et les 161
propriétés réfléchies reçoivent des valeurs non triviales, puis un cycle
serialize/load/serialize doit rester sémantiquement identique. Il couvre aussi
les données manuscrites du HUD, des corps/colliders, de `Blackboard`, de la FSM
et de `ScriptBehaviour`. L'authoring Web exécute son snapshot contractuel avant
de publier `ready`.

`RuntimeRoundTripContract` construit de la même manière un corpus en mémoire.
Le serializer complet couvre le natif (29 nœuds, 22 behaviours, 161 propriétés
dans le build XR courant) via `SaidaEngine --verify-runtime-contract`, et le
player Web (18/10/130) via le paramètre `verify-runtime-contract`. Le codec
snapshot, désormais sans `ResourceManager`, couvre l'authoring Web (9/0/90) avant
son passage à `ready` et le headless (17/18/161). Tous exigent une identité JSON
sémantique après reconstruction et exposent le verdict `[CONTRACT] PASS`.

`saida_tool verify-manifest` ferme la boucle depuis le binaire réellement livré :
il génère le manifeste, exige que chaque nœud/behaviour annoncé soit une ligne de
la `runtimeTypeMatrix` round-trippée, que le registre headless vivant corresponde
à cette matrice, et exécute le round-trip snapshot headless. Aucun type annoncé
ne peut donc échapper à la preuve de round-trip. La commande est jouée en CI sur
l'artefact `saida_tool` produit.

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

Les fichiers d'animation autonomes `.srig`, `.sclip` et `.sgraph` suivent le
même contrat : lecture et parse JSON sur le worker desktop ou dans `pump()` Web,
payload typé finalisé par `ResourceManager` sur le thread principal, état et
diagnostic consultables sans attente. `CharacterBehaviour` demande son graphe
puis l'applique seulement à `ready`; le panneau Animation conserve l'action
Jouer/Éditer/Appliquer/Inspecter et la termine sur une frame ultérieure. Un
document invalide passe à `failed` sans remplacer le graphe ou la vue vivante.

Les enregistrements mémoire `registerMemoryRig`/`registerMemoryAnimation` ET
`registerMemoryMesh` (saveur à clé `model.gltf#meshN_primM`) sont idempotents :
ré-importer le même glTF conserve les instances existantes et rend les mêmes
AssetID — les Animators/MeshNodes attachés gardent des pointeurs valides et un
snapshot restauré après éviction référence des ids résolubles.

Au `changeScene`, `trimUnused` effectue un mark-and-sweep : textures, meshes,
matériaux, plus les rigs/clips que plus aucun Animator vivant ne détient et
les caches ClipView/AnimGraph (purs caches fichier, rechargés par chemin). La
destruction GPU est différée quatre frames; index bindless et slots matériaux
sont recyclés.

PENDANT une scène, `gpuBudgetBytes` (512 MiB par défaut, `assets.setGpuBudget`)
est appliqué à chaque frame : au-delà, les textures/meshes ni référencés par
la scène vivante (photographie d'usage rafraîchie à chaque changement de
hiérarchie) ni en chargement sont évincés en LRU (`lastUse` par frame),
compteurs `gpuEvictedCount/Bytes` dans `assets.stats()`. Si tout le dépassement
est référencé : warning unique, rien de cassé. `gpuResidentBytes` est exposé à
`assets.stats()` et au profiler; l'E2E desktop et Web vérifie stabilité sur 16
cycles, éviction LRU mi-scène réelle et seuil de hitch (dt max publié dans le
verdict, plafond CI 2 s).

Contenu hostile : `cgltf_validate` après chaque parse (accessor hors limites
refusé avant toute lecture — fatale en wasm), OBJ sans géométrie utilisable
refusé au décodage (`failedTotal` cumulatif dans `assets.stats()`), géométrie
vide refusée à la création GPU. WitnessGame embarque un `corrupt.obj` et un
`corrupt.glb` volontaires traversés par les trois harnais.

Sur Web, `project-files.json` sépare le boot MEMFS des assets streamés :
PNG/JPG, OBJ et `.srig/.sclip/.sgraph` sont servis à la demande par fetch via
l'AssetLoader. Les scènes, scripts, glTF/GLB, `.sseq` et `.sretarget` restent
préchargés tant que leurs consommateurs ne sont pas asynchrones.

### 4.3 Formats média

- Audio : `.ogg` Vorbis recommandé; `.wav` accepté. MP3 et FLAC non compilés.
- Meshes : glTF/GLB et OBJ. Meshopt est décodé à l'import et exporté depuis
  l'UI d'import (« Export meshopt GLB », quantifié + EXT_meshopt_compression).
- Textures : PNG/JPG (stbi) sur toutes les plateformes — décision V1 : pas de
  KTX2/Basis (pas de contenu texturé lourd qui justifie le transcodeur; P2).
- Tangentes : sans tangentes d'auteur dans un glTF, le normal mapping du
  matériau est désactivé explicitement (warning loggé) — jamais d'éclairage
  approximé en silence; MikkTSpace est P1.

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
et raycasts.

**Queries de scène.** `PhysicsWorld::raycast` et `overlapSphere` prennent un
`QueryFilter` : les capteurs (Area) sont exclus par défaut — un rayon
d'occlusion caméra ou un hitscan ne s'arrête pas sur un trigger invisible — et
réadmis via `hitSensors`; un body explicite peut être ignoré (le lanceur).
Exposées en JS par le global `physics` (5.4) avec la même sémantique sur
desktop et player Web.

**Joints (V1 : `FixedJoint`, `PointJoint`, `HingeJoint`).** Nœuds réfléchis
reliant deux corps : `bodyA`/`bodyB` sont des *chemins de nœuds* résolus depuis
le joint (`Node::findByPath` : `..`, noms, `/` = racine) — stables au spawn
multiple là où les ids régénérés par `instantiate` ne le seraient pas. `bodyA`
vide = corps ancêtre le plus proche (forme d'autoring naturelle : joint enfant
du corps A); `bodyB` vide = ancrage au monde (`Body::sFixedToWorld`). Le pivot
(et l'axe du hinge, avec limites angulaires optionnelles) vient du transform
monde du nœud joint. La Scene synchronise les joints après les corps : la
contrainte Jolt se crée quand les deux corps sont vivants et se reconstruit
quand un body référencé a été recréé (`markDirty`). `PhysicsWorld` possède le
registre des contraintes : retirer un body purge d'abord ses contraintes (pas
de `Body*` pendant) et réveille l'autre corps; retirer une contrainte réveille
ses deux corps. Matrice : `{R, R, A, R}` comme les autres nœuds physiques.

Contraintes avancées (slider, cône, moteurs, breakables) et le reste de la
parité C++/JS (animation/graph/sequence/blackboard) ne sont pas encore fermés.

### 5.2 Input

Le système agrège des actions numériques/binaires à partir de bindings et de
contexts empilables. Clavier, souris, delta/position, scroll, texte et touch brut
sont échantillonnés une fois par frame.

Sur desktop, le premier gamepad standard GLFW est détecté avec hotplug. Sur Web,
le backend interroge directement la Gamepad API via Emscripten, accepte le
mapping navigateur `standard`, convertit ses boutons et sticks vers le même
contrat sémantique et remet les triggers `[0, 1]` dans la convention GLFW
`[-1, 1]`. `GamepadInput` n'est annoncé que si `navigator.getGamepads` est
utilisable au boot; une manette absente reste distincte d'un backend absent.

Boutons, sticks et triggers supportent deadzone re-échelonnée, inversion et
sensibilité par `scale`. Les actions par défaut couvrent mouvement, saut,
sprint, tir et visée. Forces, fronts et callbacks sont agrégés entre bindings :
relâcher un périphérique ne termine pas une action maintenue par un autre.

Le rebinding runtime remplace atomiquement les contrôles d'une action/contexte
via C++ ou QuickJS : `input.rebindKey`, `rebindMouse`,
`rebindGamepadButton`, `rebindGamepadAxis` et `rebindTouch`.
`input.exportProfile(name)` produit un JSON schema 1 à contrôles nommés, sans
état transitoire de frame;
`input.applyProfile(json)` valide intégralement le document avant de remplacer
les bindings. Un jeu persiste cette chaîne dans `storage.prefs` puis la
réapplique au boot. Schéma futur, contrôle inconnu, identifiant hors limites,
deadzone hors `[0, 0.99]`, valeur non finie et plus de 2048 entrées sont refusés
sans modifier le profil actif.

Sur Web, quatre callbacks Emscripten attachés au canvas alimentent réellement
start/move/end/cancel; `TouchInput` n'est annoncé que si leur installation
réussit. `Press`, `Tap` et les quatre swipes directionnels se lient à des zones
normalisées `[0, 1]`, indépendantes de la résolution. Le seuil de swipe est
configurable, les gestes sont des impulsions d'une frame et le maintien reste
actif tant que le contact est présent dans sa zone. Ces bindings font partie du
profil sérialisé et sont disponibles en C++ comme via `input.rebindTouch`.

`Input::lastActiveDevice` / `input.lastActiveDevice()` publie `none`,
`keyboard-mouse`, `gamepad` ou `touch`. La récence se fonde sur les transitions,
pas sur un contrôle maintenu; sticks et triggers filtrent le drift au repos.
Cette donnée est prête pour les prompts adaptatifs, qui ne sont pas encore
branchés dans l'UI.

La V1 ne promet ni multi-joueur local ni sélection de périphérique par joueur.
Sur Web, `Input::rumble` / `input.rumble(low, high, durationMs)` utilise
strictement le `GamepadHapticActuator` W3C du pad actif avec `dual-rumble`;
`stopRumble` appelle `reset`. Les magnitudes sont bornées à `[0, 1]`, la durée à
5 secondes, et l'API renvoie `false` si le pad ou l'effet manque. Desktop renvoie
`false`, car GLFW 3.x n'expose aucune haptique standard.
`Input::injectAction` et `input.inject` sont réservés aux tests/CI.

### 5.3 Audio

`AudioManager` utilise miniaudio sur desktop et Web Audio dans le player Web.
Le JS joue un alias projet avec `audio.play(alias)`. Les navigateurs imposent un
geste utilisateur avant de démarrer le son; le player commence donc muet et se
débloque au premier clic/touch.

### 5.4 Stockage joueur

L'API JS `storage.*` persiste des chaînes opaques par slot : le jeu sérialise
lui-même son état (`JSON.stringify`) et le moteur ne stocke que la chaîne. Le
service `PlayerStorage` (pur filesystem, testé headless par
`saida_player_storage_tests`, partagé desktop/runtime/player Web) porte la
durabilité :

- **Deux namespaces séparés.** `storage.save/load/has/remove/info/list` opèrent
  sur la *progression* (`saves/<slot>.json`); `storage.prefs.*` sur les
  *préférences* (`prefs/<slot>.json`). Effacer une sauvegarde ne touche pas les
  réglages et inversement. Le nom respecte `[A-Za-z0-9_-]{1,64}`.
- **Enveloppe versionnée.** Chaque slot est écrit dans une enveloppe JSON
  `{schema, version, __saidaStore, kind, dataVersion, savedAt, bytes, payload}`
  (schéma courant 1). `storage.save(slot, json, dataVersion?)` accepte une
  version applicative optionnelle pour les migrations côté jeu. Une enveloppe de
  schéma futur ou incohérente (`schema`≠`version`) est refusée via le garde
  partagé `format::schemaEnvelopeError` : `load` renvoie `null` et pose
  `storage.lastError()` au lieu de mal relire la donnée.
- **Migration des saves V0.** Une sauvegarde héritée sans enveloppe (chaîne brute
  écrite avant ce contrat) charge verbatim (schéma 0) puis est promue en
  enveloppe à la prochaine écriture, sans perte de contenu.
- **Metadata de slot.** `storage.info(slot)` renvoie `{kind, bytes, savedAt,
  dataVersion, schema}` sans lire le payload; `storage.list()` énumère les slots
  d'un namespace.
- **Quotas et erreurs typées.** Budget par slot (1 MiB), par namespace (16 MiB)
  et nombre de slots (256) sont imposés; un dépassement échoue `false` avec un
  statut consultable (`storage.lastError()` → `{status, message}`, `status`
  parmi `invalid_slot`/`quota_exceeded`/`not_found`/`corrupt`/`io_error`).

`storage.save` écrit un fichier temporaire dans le même répertoire, force les
données sur le stockage puis remplace la destination atomiquement
(`writeFileAtomically`). L'ancien fichier reste intact si l'écriture ou le
remplacement échoue, et les temporaires en échec sont supprimés.

**Emplacement (`core/Paths::userSaveRoot`).** Un jeu packagé n'écrit jamais à
côté de son exe (Program Files en lecture seule, copie portable partagée) : ses
`saves/` et `prefs/` vivent sous le dossier de données utilisateur de l'OS, keyé
par l'identité du jeu (son nom nettoyé en composant de dossier sûr), posée au
boot par le runtime (`setSaveIdentity`). Précédence : (1) `$SAIDA_SAVE_DIR`
(override explicite, CI/tests/saves portables), (2) dir utilisateur OS
(`%APPDATA%\SaidaEngine\Games\<jeu>`, `$XDG_DATA_HOME`/`~/.local/share/...`,
`~/Library/Application Support/...`) keyé par l'identité, (3) repli racine projet
quand aucune identité n'est posée (éditeur/dev) ou irrésolvable. Web utilise
IDBFS/IndexedDB (flush `syncfs` après chaque mutation durable), monté à la racine
projet par le shell — inchangé.

Reste hors de ce contrat : API asynchrone complète nécessaire à IDBFS/cloud save.

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

### 6.2 Politique de permissions des scripts (V1)

Le modèle est *capability-based* : un script n'a aucune autorité ambiante
au-delà des globals que le moteur installe explicitement — `console` et les
capacités `node/time/input/tree/assets/audio/physics/storage` (plus
`exportProperty`/`props` pendant le chargement d'un `ScriptBehaviour`).
Concrètement :

- pas de réseau (aucun `fetch`/socket), pas d'accès OS, processus ou variables
  d'environnement; quickjs-libc (`std`, `os`) n'est pas lié;
- pas de filesystem : la seule persistance est `storage`/`storage.prefs`
  (quotas par slot/namespace, erreurs typées); les imports de modules sont
  résolus uniquement sous la racine canonique du projet;
- budget temps interruptible (deadline 100 ms, 1024 jobs, microtasks
  interrompues) et callbacks protégés — un script hostile peut geler sa frame,
  pas le processus;
- toute nouvelle capacité doit être ajoutée ici ET dans l'allowlist du test
  `saida_js_permission_policy_tests`, qui verrouille la surface globale par
  différence avec un contexte QuickJS nu (une autorité apparue ou disparue
  fait échouer la suite).

### 6.3 API candidate V1

- `node` : nom, position, translation, activation, suppression différée,
  groupes, `on`, `emit`; sur `UITextNode`, `setText/getText`. Gameplay (le
  behaviour est résolu sur le nœud, sinon premier descendant) :
  `playClip(name, loop?, crossfade?)`/`currentClip()` (Animator),
  `setAnimFloat/setAnimBool/setAnimTrigger` (blackboard d'animation → pilote
  un `.sgraph`), `playSequence()/stopSequence()` (SequenceDirector, signaux
  `sequenceEvent`/`sequenceFinished` réfléchis), `setData/getData/hasData`
  (Blackboard gameplay, number/bool/string, signal `changed`); cible sans
  behaviour → false/null, jamais d'exception. `animationEvent` de l'Animator
  est un signal réfléchi abonnable par `on`.
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
- `storage` : slots opaques de progression (`save/load/has/remove/info/list`,
  `save` accepte un `dataVersion` optionnel), sous-objet `storage.prefs` pour les
  préférences et `storage.lastError()` pour le dernier échec typé; décrits en 5.4.
  Contrat de durabilité : la visibilité est synchrone (un `load` après `save`
  rend la valeur), la durabilité est asynchrone — `storage.flush()` retourne
  une Promise résolue `true` quand les écritures en attente (saves ET prefs)
  sont durables, `false` en échec, jamais rejetée. Desktop : écritures
  atomiques durables dès `save`, résolution au prochain drain de microtasks;
  Web : résolution par le callback `FS.syncfs` (IndexedDB); un backend cloud
  futur s'insère derrière la même promesse sans changer l'API.
- `physics` : `available()`;
  `raycast(origin, direction, maxDistance, opts?)` → `null` ou
  `{point, normal, distance, node: NodeRef|null}`;
  `overlapSphere(center, radius, opts?)` → `[NodeRef...]`.
  `opts = {hitSensors?: bool, ignoreSelf?: bool}` — capteurs exclus par défaut,
  corps propre de l'appelant (nœud ou ancêtre) ignoré par défaut. Sans monde
  physique (pas de Play, pas de corps), les queries répondent « rien »
  (`null`/liste vide), jamais une erreur. Même surface desktop et player Web.

Les références cross-node deviennent invalides
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

Les assets `.srig/.sclip/.sgraph` sont chargés par l'AssetLoader sans bloquer la
frame. Le runtime continue avec son état courant pendant `queued/loading`; un
graphe de personnage ne devient propriétaire de la lecture qu'après
chargement, validation et compilation réussis.

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

Le backend CPU RmlUi est prouvé par le corpus headless `saida_ui_corpus_tests`
(sans GPU) : géométrie pleine, alpha blending (couleurs de sommet
prémultipliées), glyphes des fonts par défaut, feuille de style projet chargée
du disque avec propriété web filtrée, image projet décodée, image manquante →
damier magenta (même convention que `ResourceManager::missingTexture`),
`overflow:hidden` réellement scissoré, `transform` CSS, resize et ratio DPI.
Un backend GPU/Vulkan RmlUi reste une optimisation future, pas une condition
si le backend CPU tient la charge V1.

Le HUD texte (`UICanvasNode`/`UITextNode`) est rasterisé par le module partagé
`ui/HudRasterizer` : desktop et player Web construisent le même markup et le
même pixel buffer RGBA8 par le backend CPU, puis chaque plateforme le compose
via son RHI (quad bindless Vulkan, texture+bindgroup WebGPU) — c'est
l'invariant de parité visuelle appliqué au HUD. Sur desktop, les nœuds UI hors
texte (couleur/image/bouton/toggle) restent des quads bindless via
`UIRenderer::traverseUI`; le player Web V1 n'enregistre que `UICanvasNode`/
`UITextNode` et refuse les autres nœuds UI et `WebCanvasNode` tant que leur
backend Web n'est pas porté. `setText/getText` opèrent sur les vrais nœuds;
WitnessGame atteint `[E2E] PASS` avec un HUD réellement rasterisé
(`[HUD] rasterized N visible pixel(s)`) en éditeur, desktop packagé et Web.

Les fonts par défaut du moteur (`ui/RmlUiRuntime` : `kEngineFonts`) sont
résolues par fichier sous `assets/fonts/` (bundle packagé ou racine runtime)
puis le checkout dev; une font requise absente est loggée en erreur explicite
et un échec total de chargement est signalé. Le `BuildExporter` embarque ces
fichiers sous `assets/fonts/` dans les packages desktop et Web (NotoEmoji
volontairement hors bundle web).

L'interaction du HUD écran (`UICanvasNode`/`UIInteractableNode`) a un unique
chemin canonique, `ui/UIInteractionSystem` : hit-test des rectangles de nœuds
(pivot inclus, le plus au-dessus gagne), machine hover/press/click et décision
de capture d'input. Le contrat clé — *un HUD ne vole pas les clics du jeu* —
est explicite : seul un `UIInteractableNode` actif sous le pointeur capture la
souris (`Input::setUiCapture`); un HUD purement texte/décoratif laisse l'input
à la logique de jeu. Prouvé sans GPU par `saida_ui_interaction_tests` (hover et
capture, clic press+release, clic annulé au drag-out, HUD texte non capturant,
bouton désactivé transparent, topmost gagnant, canvas inactif). Décision V1 : le
focus clavier, le scroll et le touch sur les interactables du *canvas* ne sont
pas ajoutés, faute de surface V1 — le HUD du player Web est display-only
(`UICanvasNode`/`UITextNode` seuls, §8.3) et l'UI interactive du canvas est
pilotée à la souris sur desktop. Le clavier, le scroll et le touch riches
existent déjà sur `WebCanvasNode` pour les panneaux desktop.

Le World Space (panneau 3D `WebCanvasNode`) intersecte un rayon avec le plan
local z=0 du panneau, borné par ses dimensions monde, et mappe le point en
espace pixel (origine haut-gauche, y vers le bas). Cette géométrie est isolée
du nœud lié au GPU dans `ui/WorldPanelGeometry` (`raycastWorldPanel`) — la même
fonction sert la souris (`UIInteractionSystem`) et le rayon XR
(`XRRayInteractor`) — et prouvée sans GPU par `saida_ui_worldspace_tests`
(centre, coins, mapping y-bas, rejets hors-bornes/parallèle/derrière/dégénéré,
panneaux translatés et pivotés). Le rendu world-space (compositing GPU) reste
exercé sur desktop, non asserté en pixels.

Le contrat auteur (structure `.rml`/HTML, CSS local, module JS projet) est celui
figé en §8.2 : le sous-ensemble CSS fiable, le bridge DOM ciblé et la séparation
structure/style/comportement. Le corpus `saida_ui_corpus_tests` verrouille le
sous-ensemble de rendu (feuille de style projet, propriété web non supportée
filtrée, layout, clipping) et `saida_ui_interaction_tests` la sémantique
d'interaction; ce sont les tests de non-régression du contrat.

Le bridge DOM/JS (`WebCanvasNode`) est une surface *ciblée et explicitement
énumérée* (`installDocumentBindings` : `document` — sous-ensemble navigateur de
§8.2 — et `tree`), sans API navigateur ambiante : aucun `window`, `fetch`,
`XMLHttpRequest` ni timer global n'est installé, et le contexte tourne sur le
même QuickJS *capability-based* que les scripts (§6.2 : pas de quickjs-libc,
pas de réseau/OS, imports confinés à la racine projet — surface verrouillée par
`saida_js_permission_policy_tests`). Une surface de test dédiée au contexte
WebCanvas reste couplée à son init GPU.

Sérialisation et lifecycle : les documents HUD (`UINode`/`UICanvasNode`/
`UITextNode`) round-trippent sémantiquement dans le codec headless (prouvé par
`saida_runtime_type_matrix_tests`), et WitnessGame prouve le lifecycle
Play/Stop/reload — le HUD est restauré après redémarrage desktop et reload Web.
Le hot reload transactionnel des documents `WebCanvasNode`
(`loadDocumentFromState`, garde l'ancien document si le nouveau échoue) est un
comportement desktop exercé dans l'éditeur.

Assets UI et AssetRegistry — décision V1 : les documents UI référencent fonts,
images et feuilles de style par *chemin relatif au projet* (le modèle naturel de
HTML/CSS/RML), résolus et bornés à la racine par l'interface fichier/texture
RmlUi, avec erreurs visibles et fallback damier magenta prouvés. L'AssetRegistry
(identités `AssetID`) reste le système des assets moteur/mesh, pas de la balise
d'auteur; router l'UI par `AssetID` est reporté (le corpus V1 n'a pas d'assets UI
lourds — conséquence assumée : les textures UI ne passent pas par le budget GPU
LRU de l'`AssetLoader`, comme la décision KTX2).

Backend CPU vs GPU — décision V1 : pas de backend GPU RmlUi. La rasterisation CPU
d'un HUD plein 1080p coûte O(surface du canvas) et n'est payée qu'aux frames où
le contenu change (le rasterizer saute un HUD identique); `saida_ui_corpus_tests`
publie ce coût (mesure Debug) et le hitchMax Release du harnais Witness (~0,05 s
sous charge complète) reste borné. Un backend GPU RmlUi est une optimisation P2,
réévaluée si un HUD doit se re-rasteriser en plein écran à chaque frame.

XR — fallback déclaré (§10, gate P0.3) : l'UI XR n'est pas une surface de
livraison V1; son absence est un repli annoncé, pas un blocage de la gate.

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
doivent devenir des commandes undoables.

Le renommage de projet passe par `renameProjectDirectory`
(`src/project/ProjectRename.*`) : le dossier, le fichier `.saidaproj` (nom et
champ `name`) et l'entrée `hub.json` sont modifiés ensemble, chaque étape
intermédiaire reste chargeable et tout échec restaure l'état antérieur. Le nom
est validé comme composant de chemin sûr, un registre Hub corrompu ou un
document projet legacy/futur refuse l'opération, et `Project::load` accepte
désormais le dossier du projet (résolution du `.saidaproj` unique) — le chemin
que le Hub stocke et passe à `--project`. Le champ « Project Name » des réglages
de l'éditeur est en lecture seule et renvoie au Hub : un renommage cohérent
exige le projet fermé (renommage du dossier tenu ouvert par l'éditeur).

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
E2E complet run + restart sur l'artefact produit. La CI conserve ce chemin exact
sur runner Windows propre avec une fenêtre GLFW cachée et l'ICD logiciel
Mesa/Lavapipe explicitement sélectionné; elle ne dépend donc pas du GPU du
runner.

Le mode Play de l'éditeur est également automatisable avec `SaidaEngine
--project <p> --play`. Il déclenche la même transition différée que le bouton
Play; `tools/witness_editor_play.sh` l'utilise sur une copie vierge de
WitnessGame et exige gameplay, HUD et restauration save+HUD au second lancement.

Les recettes peuvent ajouter `--test-autoload NAME=script.js` sans réécrire le
`.saidaproj` ni l'artefact. Cet autoload reste éphémère, limité à un nom simple,
à un fichier `.js/.mjs` existant et à la racine canonique du projet. Le player
Web reçoit le même argument via le paramètre URL `test-autoload`. Ainsi les
harnais exécutent exactement les octets archivés, et non un package modifié
après export.

`tools/witness_release_candidate.ps1` est la recette P0.1 unique. Depuis un
worktree propre, elle compile/vérifie natif et player Web, appelle le vrai Build
éditeur pour Windows et Web, refuse les saves dans les packages, archive les
sorties et produit `release-manifest.json` : SHA moteur, état dirty, SHA-256 et
taille des archives et de l'installeur, plus l'inventaire hashé de chaque
fichier et le bundle de symboles Windows. Les ZIP sont écrits dans un ordre
ordinal avec un timestamp
unique dérivé du commit; les chemins ambigus, symlinks et reparse points sont
refusés, puis chaque entrée est revérifiée contre le stage. Les scripts
`verify_witness_windows.ps1` et `verify_witness_web.ps1` revérifient l'archive
avant extraction. Le premier exécute gameplay/UI puis save/UI au redémarrage;
le second contrôle COOP/COEP et MIME WASM, lance Chrome ou Edge et collecte un
verdict automatique via le serveur local. Aucun checkout moteur, MSYS2 ou SDK
n'est requis par la preuve Windows sur machine vierge.

`build_witness_installer.ps1` compile le même stage avec NSIS 3.12+ dans un
installeur par utilisateur. Avant signature, sa sortie est byte-reproductible :
payload trié ordinalement, timestamps issus du commit, fermeture DLL et
inventaire SHA-256 exacts. Le désinstalleur supprime chaque fichier inventorié,
les caches régénérables `asset_registry.local.json`/`pipeline_cache.bin`, puis
seulement les dossiers devenus vides; il ne fait pas de suppression récursive
aveugle du dossier choisi. `verify_witness_installer.ps1` vérifie le
SHA de l'installeur, installe silencieusement dans un dossier isolé, compare le
payload exact, peut exécuter gameplay + restart, puis exige une désinstallation
propre. La CI construit deux fois les mêmes octets, compare leur SHA et publie
le bundle sous un nom contenant le commit. La signature Authenticode est
volontairement séparée : elle modifie les octets et requiert la clé de
publication.

Le package Web embarque player, shaders et fichiers de boot sous MEMFS. Les
textures PNG/JPG, OBJ et assets `.srig/.sclip/.sgraph` restent hors du preload
et sont fetchés à la demande; scènes, scripts et glTF/GLB restent au boot.

WitnessGame est le corpus vertical : scènes, scripts, signaux, physique,
animation, audio, UI, save/load et changement de scène. Le harnais desktop
injecte les actions via un autoload et exige `[E2E] PASS`. Le 2026-07-16,
le Play éditeur et le desktop export/runtime sont PASS, redémarrage inclus. Le
package Web charge et rend le HUD RmlUi; son harnais atteint aussi `[E2E] PASS`
sur 16 cycles puis `RESTART PASS` après reload. Les trois parcours vérifient le
texte du HUD avant/après collecte et après restauration de la sauvegarde.

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
process éditeur/desktop sur `saves/`, reload navigateur sur IDBFS), ainsi que
Play éditeur automatisé (`--play`). À surveiller : halos de viewport non
reproduits et dispatch autoload encore dupliqué entre `Engine::mountWorld` et le
player Web.

Une release exige encore la signature Authenticode de l'installeur avec la clé
de publication. L'archive et l'installeur reproductibles, la fermeture DLL, les crash logs avec
symboles, le SBOM, les notices, l'inventaire de contenu, le rollback et les
hashes immuables sont désormais produits ou documentés.

Chaque exécutable desktop installe `core/CrashReporter` avant le boot moteur.
Une exception fatale interceptée écrit un rapport texte et, sous Windows, un
minidump dans le dossier de données utilisateur
`SaidaEngine/CrashReports/<produit>`; `SAIDA_CRASH_DIR` fournit l'override
tests/CI. Le rapport contient produit, timestamp UTC, PID/TID, exécutable,
raison/code/adresse, commit de build, logs récents accessibles sans attendre le
mutex du logger, base/RVA du module pour la symbolisation et le nom de
l'artefact de symboles correspondant.

`tools/package_release_symbols.ps1` extrait avec GNU objcopy les symboles
`.dbg` des quatre exécutables `RelWithDebInfo`, dépouille les copies
distribuables, y inscrit `.gnu_debuglink`, épingle le timestamp PE au commit et
produit un manifeste exact de tailles/SHA-256. Le vérificateur autonome refuse
un hash, un lien, une section `.debug_info` ou un fichier supplémentaire
inattendu. La CI publie le bundle sous `windows-symbols-<SHA>`.

`tools/validate_windows_dependencies.ps1` ferme les dépendances PE de chaque
entry point et de ses DLL locales : format `pei-x86-64` obligatoire, import
système explicitement autorisé ou DLL présente dans le bundle, collision de nom
et dépendance manquante refusées. `libgcc_s_seh-1.dll`, `libstdc++-6.dll` et
`libwinpthread-1.dll` sont interdites car le contrat UCRT64 les lie
statiquement. Le rapport déterministe entre dans le bundle de symboles et dans
l'archive Windows Witness. Le lecteur PE64 interne borne chaque offset/RVA et
parcourt les tables d'imports normale et différée; il couvre ainsi également
l'exécutable dont VERSIONINFO/icône ont été réécrits par l'API Windows, sans
dépendre de l'acceptation de cette réécriture par un désassembleur tiers.

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

`tools/engine_release_manifest.ps1` produit ce release manifest
(`build/release/engine/release-manifest.json`, schéma 1) : le commit moteur, les
versions de formats lues depuis `saida_tool describe-engine` (la section
`formats` en est la source unique), et le SHA-256 de `saida_tool`, du runtime
desktop, du player Web, de l'authoring WASM, du runtime d'authoring et de chaque
fixture immuable. Il inclut aussi l'inventaire exact du bundle de conformité :
SBOM SPDX 2.3, notices GPL/tiers, assets/modèles hashés et manifeste de leurs
sources, ainsi que les exécutables Windows dépouillés et leurs symboles
versionnés. `tools/verify_engine_release.ps1` recalcule chaque hash, refuse tout
fichier ajouté ou manquant dans ces bundles et recompare les versions à
l'outil; il échoue au moindre écart d'octet, d'inventaire ou de version. La
plateforme Saida épingle ce manifeste pour interdire toute divergence entre son
outil Docker, son bundle Web servi, ses diagnostics, ses licences et ses
fixtures.

`tools/generate_release_compliance.ps1` relit les deux entrées revues
`compliance/components.json` et `compliance/assets.json`. Le contrôle est
fail-closed : chaque racine de `third_party` doit être déclarée exactement une
fois, chaque asset suivi doit posséder licence, provenance et décision de
distribution, et aucun asset `NOASSERTION` ne peut être distribué. Les quatre
assets legacy sans provenance sont marqués `distribution: false`.
`assets/models/DamagedHelmet.glb`, sous CC-BY-NC-4.0, reste distribuable
uniquement hors produit commercial.

La matrice GPU/OS/navigateur, les exclusions et la procédure de retrait sont
publiées dans [docs/release-support.md](docs/release-support.md). La promotion
s'effectue par manifeste, SHA de commit et digest immuables; `latest` n'est
jamais une identité de release.

Inventaire immuable actuel : `project_v0/v1.saidaproj`,
`asset_registry_v0/v1.json`, `scene_v0/scene_v2.scene`,
`scenario_v0/v1.saidascenario`, `game_v0/v1.saida` et le jeu témoin gelé
`witness_v1.saidaproj`, `witness_v1_asset_registry.json`,
`witness_v1_hub.scene`, `witness_v1_arena.scene` — copies exactes des artefacts
durables de WitnessGame chargées par leurs vrais loaders (le HUD UI, la physique
et les types V1 des scènes validés headless). Ne jamais modifier ou régénérer
ces fichiers; une version publiée suivante ajoute `witness_v2_*`. Le test
vérifie aussi que le chargement ne change aucun octet source.

Le corpus `fold-determinism` contient `base.json`, `ops.json` et
`expected.json` produit sous Windows. Il couvre set_property, create_node,
set_transform avec floats/quaternion non triviaux, reparent, rename et setting
de scène. Linux doit produire une sortie byte-identique. `expected.json` ne se
régénère qu'avec un bump de format, jamais pour masquer une divergence.

## 14. Limites connues consolidées

- V1 non publiée; aucun badge local ne vaut stabilité publique.
- Player Web : WebGPU obligatoire, HTTP obligatoire, UI limitée au HUD
  `UICanvasNode`/`UITextNode`, WebCanvas absent, gamepads sans mapping navigateur
  `standard` ignorés, touch UI avancé non prouvé, MSAA absent.
- Audio Web soumis au geste utilisateur.
- Un runtime/canvas Emscripten par page; build non modularisé.
- Registres natif/headless/Web explicitement matricés; l'UI avancée reste hors
  de certains folds.
- Les producteurs SaidaOp externes V1 doivent émettre `opVersion: 2` et les
  `NodeId`; les opérations historiques par nom sont volontairement refusées.
- Queries physiques (raycast filtré, overlapSphere), joints V1
  (Fixed/Point/Hinge) et bindings animation/graph/séquences/blackboard livrés
  avec parité JS desktop/Web; contraintes avancées (slider, cône, moteurs) et
  API d'animation étendue (scrub, root motion JS) restent P1.
- Sauvegardes d'un jeu packagé sous le dossier utilisateur de l'OS (keyé par
  l'identité du jeu, override `$SAIDA_SAVE_DIR`); éditeur/dev restent sous la
  racine projet. Enveloppe versionnée, metadata, namespaces
  progression/préférences, quotas et contrat de durabilité asynchrone
  (`storage.flush()` → Promise) en place; seul un backend cloud effectif
  reste futur.
- Budget GPU mi-scène avec LRU mesuré, sweep rigs/anims, identités glTF
  stables et refus du contenu corrompu en place; streaming Web fetch/IDBFS,
  politique tangentes explicite et export meshopt UI en place; le package web
  streame textures/OBJ et `.srig/.sclip/.sgraph` à la demande (manifest schéma
  2, fetch async sur miss MEMFS), scènes/scripts/glTF restant préchargés
  (MikkTSpace P1, KTX2/Basis P2 par décision).
- Point-light shadows cubemap et lightmaps persistantes absentes.
- XR sans MSAA multiview, overlay et matrice hardware validée.
- Signature Authenticode de l'installeur non prouvée; elle requiert la clé de
  publication et une qualification des octets signés.
- Fermeture récursive des imports DLL x64 prouvée; la disponibilité effective
  de Vulkan 1.3 reste un prérequis machine.
- Crash reporter Windows avec minidump et bundle de symboles déterministe lié
  au commit; collecte distante des rapports hors périmètre moteur.
- Licences, notices et SBOM générés en mode fail-closed; quatre assets legacy
  sans provenance sont explicitement exclus des bundles V1 et le Damaged
  Helmet CC-BY-NC reste interdit aux produits commerciaux.

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
./tools/witness_editor_play.sh
./tools/witness_editor_build.sh
./tools/witness_web_stage.sh
.\tools\witness_release_candidate.ps1
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
