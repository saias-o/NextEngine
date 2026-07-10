# Plan d'architecture du système d'animation

> Statut : architecture cible et plan d'exécution.
>
> Périmètre : animation squelettique, mocap, retargeting, graphes de lecture,
> séquences, aperçu éditeur et authoring par LLM. Le système vise en priorité le
> runtime web WebGPU, Android/Quest et la VR à fréquence élevée.

## 1. Objectifs

Le système d'animation de Saida Engine doit :

- rendre le cas courant trivial : importer, choisir un clip et le jouer ;
- permettre de modifier une animation mocap sans jamais dupliquer ses clés ;
- fournir au LLM une surface déclarative, validable et déterministe ;
- n'effectuer aucune allocation, recherche par chaîne ou parsing dans la boucle
  d'animation runtime ;
- partager la même architecture sur Vulkan, WebGPU, mobile et XR ;
- rester léger : les fonctions avancées sont composables, jamais obligatoires ;
- supporter de grandes scènes grâce au LOD d'animation et au partage de poses ;
- conserver un chemin de migration pour les scènes et appels `Animator::play`
  existants.

Le système ne doit pas devenir un clone de l'Animator Controller de Unity. Un
graphe visuel complexe ne doit jamais être nécessaire pour lire un clip, créer
une locomotion ou assembler une séquence.

## 2. Principes non négociables

### 2.1 Sources immuables, modifications non destructives

Un glTF, GLB ou BVH importé est une source immuable. Une découpe, un changement
de vitesse, un miroir, une correction de courbe ou une extraction de root motion
produit une petite vue qui référence la source.

Une même capture mocap peut ainsi fournir `Idle`, `Walk`, `RunStart` et `Run`
sans copier le flux de clés. Une correction manuelle ne stocke que les clés
modifiées.

### 2.2 Authoring et runtime séparés

Les fichiers lisibles et modifiables par l'éditeur ou un LLM ne sont jamais
évalués directement pendant une frame. Ils sont compilés en données compactes,
indexées et validées.

### 2.3 Une architecture, plusieurs plateformes

Le noyau CPU, le format cuit et le graphe compilé sont communs. Les différences
de backend se limitent à l'upload de palette et au shader de skinning. Il ne doit
pas exister de système d'animation web ou XR parallèle.

### 2.4 Le coût dépend du visible

La mémoire uploadée et le nombre de poses évaluées dépendent des personnages
visibles et de leur LOD, pas d'une capacité maximale globale.

### 2.5 Comportement déterministe

À asset, paramètres, temps et version de moteur identiques, le résultat doit
être reproductible. Le seek, le scrub éditeur, les événements et la compilation
doivent produire un ordre stable.

## 3. État actuel et corrections prioritaires

Les briques existantes à conserver sont :

- import glTF et BVH ;
- `Rig`, `AnimationClip`, `Animator` et poses locales/globales ;
- interpolation linéaire, slerp et cubic spline glTF ;
- blend à deux entrées et machine d'états simple ;
- retargeting initial par nom ;
- skinning GPU et buffer global de matrices ;
- timeline générique et aperçu de modèle.

Avant toute extension, les défauts suivants doivent être corrigés :

1. Le renderer neutralise les palettes squelettiques sous WebGPU.
2. La rest pose glTF n'est pas transmise à l'`Animator`.
3. Le calcul global suppose que les parents précèdent leurs enfants, ce que
   l'ordre des joints glTF ne garantit pas.
4. L'interpolation glTF `STEP` n'est pas représentée.
5. Le BVH ne conserve ni hiérarchie, ni offsets, ni convention d'axes/unité.
6. Le retargeting ne corrige pas les différences de rest pose et de proportions.
7. L'aperçu éditeur ne peut piloter qu'un `ClipNode` direct alors que
   `Animator::play` construit une machine d'états.
8. Les graphes et timelines ne sont pas des assets persistants complets.
9. Le runtime parcourt des objets virtuels, maps et chaînes dans des chemins
   appelés chaque frame.
10. La couverture de tests d'animation est insuffisante.

Ces corrections constituent le jalon de stabilité initial. Elles ne doivent pas
être repoussées derrière l'éditeur ou les fonctions avancées.

## 4. Modèle d'assets

### 4.1 Assets d'authoring

Le modèle cible contient six types :

| Type | Rôle | Stockage proposé |
|---|---|---|
| `AnimationSource` | Données importées et immuables d'un glTF/BVH | Sous-assets de la source |
| `RigAsset` | Hiérarchie, rest pose, inverse bind et sémantiques | Sous-asset ou `.srig` |
| `ClipView` | Vue non destructive d'une animation source | `.sclip` JSON |
| `RetargetProfile` | Correspondances et corrections source vers cible | `.sretarget` JSON |
| `AnimationGraph` | Logique de lecture et de mélange | `.sgraph` JSON |
| `AnimationSequence` | Montage multipiste déterministe | `.sseq` JSON |

Chaque asset possède :

- un `AssetID` stable ;
- une version de schéma ;
- les dépendances par `AssetID`, jamais par pointeur ou chemin absolu ;
- un hash de contenu pour le cache de compilation ;
- des diagnostics sérialisables ;
- un nom d'affichage qui n'est jamais son identité runtime.

### 4.2 `RigAsset`

Un rig contient :

- noms d'os pour l'authoring ;
- indices de parent, sans contrainte sur l'ordre de stockage ;
- ordre topologique précompilé ;
- rest pose locale complète TRS ;
- inverse bind matrices ;
- identifiants sémantiques optionnels (`hips`, `spine`, `head`, `left_hand`...) ;
- hauteur et métriques utiles au retargeting ;
- hash de compatibilité du squelette.

Les indices du rig restent compatibles avec `JOINTS_0`. L'ordre topologique est
séparé afin de ne pas remapper inutilement la géométrie.

### 4.3 `ClipView`

Une vue référence un clip source et décrit seulement ses transformations :

```json
{
  "schema": 1,
  "source": 74219,
  "name": "RunLoop",
  "range": { "start": 1.2, "end": 2.05 },
  "loop": true,
  "speed": 1.0,
  "rootMotion": "extract",
  "mirror": false,
  "retarget": 81502,
  "events": [
    { "time": 0.18, "name": "left_foot_down" },
    { "time": 0.61, "name": "right_foot_down" }
  ]
}
```

Les extensions prévues sont :

- boucle avec plage distincte de la plage de lecture ;
- time warp ;
- pose additive de référence ;
- masque d'os ;
- extraction/suppression du root motion ;
- miroir sémantique gauche/droite ;
- corrections de courbes sparse ;
- événements, marqueurs de phase et métadonnées de contacts ;
- profil de compression spécifique.

### 4.4 Cache cuit

Les données compilées sont placées dans un cache dérivé, par exemple
`.saida/cache/animation/<content-hash>.sanimc`. Elles ne sont pas éditées et ne
doivent pas être commitées comme source de vérité.

Le cache contient des `CookedRig`, `CookedClip` et `AnimationProgram`. Il est
invalidé par le hash des sources, des réglages d'import, du schéma et de la
version du cooker.

## 5. Import et cooker

### 5.1 Import glTF/GLB

L'import doit couvrir :

- rest pose locale de chaque joint ;
- hiérarchie sans supposer l'ordre parent/enfant ;
- inverse bind matrices et correspondance exacte avec `JOINTS_0` ;
- interpolation `STEP`, `LINEAR` et `CUBICSPLINE` ;
- plusieurs skins et plusieurs animations ;
- animations de nœuds non squelettiques ;
- morph targets/weights ;
- identifiants stables pour les sous-assets ;
- validation des temps, valeurs non finies, quaternions et indices ;
- conservation des noms originaux pour le retargeting et le diagnostic.

### 5.2 Import BVH

Le BVH doit produire un rig source complet :

- arbre de joints et offsets ;
- ordre exact des canaux ;
- frame time et plage source ;
- translations locales disponibles ;
- réglages d'up-axis, forward-axis, handedness et unité ;
- transformation source vers espace Saida ;
- prévisualisation possible sans rig glTF cible ;
- rapport de joints non mappés lors d'un retargeting.

Les réglages d'import sont persistants et participent au hash du sous-asset.

### 5.3 Compilation des clips

Le cooker exécute dans cet ordre :

1. validation structurelle ;
2. normalisation des quaternions et des temps ;
3. résolution du rig et du retargeting ;
4. application de la vue non destructive ;
5. extraction du root motion et des événements ;
6. réduction de clés sous une tolérance mesurée en espace objet ;
7. quantification ;
8. regroupement SoA et construction des tables de sampling ;
9. génération des métadonnées de streaming ;
10. écriture atomique du cache et rapport de compilation.

Compression proposée :

- temps quantifiés sur 16 bits par segment ;
- quaternions `smallest-three` sur 48 bits par défaut ;
- translation/scale quantifiées sur 16 bits avec bornes par piste ;
- canaux constants stockés une seule fois ;
- suppression des pistes identiques à la rest pose ;
- pages indépendantes pour les longues séquences.

La tolérance est configurable par tier, avec une valeur conservatrice par
défaut. Le cooker doit pouvoir comparer la pose source et la pose reconstruite.

## 6. Noyau runtime data-oriented

### 6.1 Données compactes

Le runtime n'utilise pas les classes d'authoring. Les structures chaudes sont
des tableaux contigus :

- TRS locales en SoA ;
- descripteurs de pistes par indice ;
- flux de temps et valeurs compressés ;
- table d'os animés ;
- ordre topologique du rig ;
- paramètres typés par offset ;
- programme de graphe sous forme d'opérations compactes.

Les noms, JSON, maps et pointeurs polymorphes restent hors de la boucle chaude.

### 6.2 Sampling

Chaque piste conserve un curseur de clés dans l'instance :

- lecture normale vers l'avant : coût amorti O(1) ;
- seek/scrub : recherche binaire ;
- BVH uniforme : index direct de frame ;
- boucle et lecture inverse gérées sans réallouer ;
- slerp avec chemin court et normalisation contrôlée ;
- kernels SIMD128 WASM et NEON lorsque rentables ;
- fallback scalaire de référence utilisé par les tests.

### 6.3 Mémoire temporaire

Une `AnimationInstance` réserve sa mémoire à la création :

- paramètres ;
- curseurs de pistes ;
- états du graphe ;
- pose locale courante et précédente ;
- slots temporaires nécessaires au programme ;
- événements produits pendant le tick.

Le compilateur calcule le nombre minimal de poses temporaires par analyse de
durée de vie. L'instance n'effectue aucune allocation après son initialisation.

### 6.4 Pipeline d'une frame

Le traitement est séparé en quatre étapes :

1. **Control update** : temps, états, transitions, paramètres, root motion et
   événements. Très peu coûteux et exécuté même à LOD bas.
2. **Pose sampling** : évaluation des clips nécessaires selon le budget et la
   visibilité.
3. **Pose solve** : blend, additive, masques, IK/procédural puis calcul global.
4. **Palette upload** : écriture de la palette uniquement pour les renderables
   visibles.

Cette séparation permet de réduire la fréquence des poses sans perdre les
événements gameplay.

## 7. Graphe d'animation

### 7.1 Deux niveaux d'authoring

Le niveau simple couvre la majorité des projets :

- jouer un clip ;
- locomotion idle/walk/run ;
- one-shot avec retour automatique ;
- blend par vitesse ou direction ;
- couche haut du corps ;
- état piloté par quelques paramètres.

Ces recettes sont compilées en graphe sans obliger l'utilisateur à manipuler
des nœuds.

Le niveau avancé expose un DAG explicite pour les cas particuliers. L'éditeur
visuel est une vue de cet asset, pas un format concurrent.

### 7.2 Nœuds runtime prévus

- `ClipSample`
- `Blend2`
- `Blend1D`
- `Blend2D`
- `StateMachine`
- `LayerBlend`
- `Additive`
- `BoneMask`
- `PoseCache`
- `SequencePlayer`
- `TwoBoneIK`
- `LookAt`
- `TrackedPoseOverlay`

Le programme compilé utilise des opcodes ou un enum avec données contiguës. Il
n'utilise pas un arbre de `virtual update/evaluate` par personnage.

### 7.3 Paramètres et transitions

Les paramètres sont déclarés et typés :

- `float`
- `int`
- `bool`
- `trigger`

Une transition définit explicitement :

- conditions ;
- durée et courbe de blend ;
- exit time éventuel ;
- politique de redémarrage ou de conservation du temps ;
- interruption autorisée ;
- priorité ;
- synchronisation par marqueurs ou phase normalisée.

Les comparaisons flottantes exactes ne sont pas utilisées par défaut. Les
conditions disposent d'un epsilon ou de seuils explicites.

### 7.4 Root motion et événements

Le root motion possède trois modes :

- `Ignore`
- `Extract`
- `ApplyToNode`

L'extraction produit un delta translation/rotation avant le solve de pose. La
consommation par la physique ou le gameplay reste explicite.

Les événements sont traversés correctement en lecture normale, en boucle, en
lecture inverse et lors d'un seek contrôlé. L'éditeur peut désactiver leurs
effets pendant le scrub tout en les affichant.

## 8. Retargeting et procédural

### 8.1 `RetargetProfile`

Le retargeting ne doit plus se limiter aux noms. Un profil contient :

- correspondance sémantique source/cible ;
- rotations de correction de rest pose ;
- règles de translation par chaîne ;
- ratio d'échelle global et par membre si nécessaire ;
- règle de racine et de bassin ;
- miroir gauche/droite ;
- politique pour les os absents ou supplémentaires ;
- diagnostics de couverture.

L'auto-mapping par nom existant devient une suggestion initiale que l'utilisateur
peut valider et corriger.

### 8.2 VR et tracking

Pour la VR, la pose animée sert de base. Une couche procédurale tardive applique :

- tête et mains suivies ;
- IK des bras ;
- orientation du torse ;
- foot placement optionnel ;
- contraintes de confort et limites articulaires.

La pose des périphériques suivis peut être injectée près du rendu sans réévaluer
le graphe complet. Les deux yeux partagent une seule palette : l'animation ne
dépend jamais de la vue.

À 72/90/120 Hz, le graphe peut être évalué à 30 ou 45 Hz et interpolé, tandis
que la couche tête/mains est mise à jour à la fréquence XR.

## 9. Skinning et backends GPU

### 9.1 Palette commune

La palette cible utilise trois `vec4` par os, soit une matrice affine 3x4 de
48 octets. Le format et l'alignement sont identiques dans les shaders Vulkan et
WGSL.

Le renderer :

- collecte les `AnimationInstance` visibles ;
- attribue un offset dans un ring buffer par frame ;
- partage cet offset entre toutes les primitives d'un personnage ;
- copie uniquement les palettes utilisées ;
- agrandit le ring de manière bornée ou dégrade proprement selon le budget ;
- expose le nombre d'os et d'octets uploadés au profiler.

Le chemin WebGPU doit utiliser le storage buffer déjà présent au lieu de
neutraliser les animators.

### 9.2 Stratégie GPU

Le skinning vertex GPU reste le chemin principal. L'évaluation des graphes et la
décompression restent d'abord CPU : c'est plus portable, plus facile à tester et
souvent plus efficace pour les petites/moyennes populations web et VR.

Une évaluation GPU de foules ne sera ajoutée qu'après mesure. Elle devra être un
chemin spécialisé partageant les mêmes assets cuits, pas un second système.

### 9.3 Morph targets

Les morph weights glTF rejoignent le même scheduler, mais leur buffer GPU est
séparé des palettes d'os. Les visages peuvent utiliser un LOD plus agressif que
le corps à distance.

## 10. Scheduling et LOD d'animation

Le scheduler calcule un niveau par instance selon :

- visibilité ;
- distance et taille à l'écran ;
- importance gameplay ;
- rôle local/réseau ;
- budget de frame ;
- priorité XR pour les avatars proches.

Niveaux proposés :

| Niveau | Control update | Pose | Palette |
|---|---:|---:|---:|
| LOD 0 | Chaque tick | 60/45/30 Hz | Chaque pose |
| LOD 1 | Chaque tick | 30 Hz | Interpolée |
| LOD 2 | Chaque tick | 15 Hz | Interpolée |
| LOD 3 | Fréquence réduite | 5 Hz ou pose partagée | Si visible |
| Invisible | Événements essentiels | Gelée ou très basse fréquence | Aucun upload |

Les personnages partageant rig, programme, clip et phase quantifiée peuvent
réutiliser une pose cuite pour les foules. Le partage reste optionnel afin de ne
pas synchroniser artificiellement les personnages proches.

## 11. Éditeur d'animation

### 11.1 Inspecteur d'asset

L'inspecteur affiche :

- clips et sous-assets disponibles ;
- durée, fréquence et plage ;
- rig source et compatibilité ;
- nombre de pistes et de clés ;
- courbes constantes/supprimées ;
- taille source et cuite ;
- erreur maximale de compression ;
- root motion, événements et marqueurs ;
- diagnostics d'import/retargeting.

### 11.2 Preview universelle

Le viewport doit prévisualiser indifféremment :

- un clip source ;
- un `ClipView` ;
- un graphe ;
- un profil de retargeting ;
- une séquence.

Contrôles : play, pause, boucle, vitesse, scrub, frame-step, plage, sélection de
clip et paramètres du graphe.

Superpositions : squelette, noms d'os, contacts, trajectoire de root motion,
événements, os non mappés, état actif, transitions et coût d'évaluation.

La preview possède son propre contexte et restaure l'état de la scène après un
scrub. Elle ne déclenche pas de mutations gameplay irréversibles.

### 11.3 Édition non destructive

L'éditeur de clip manipule uniquement le `ClipView` :

- poignées de début/fin ;
- boucle ;
- root motion ;
- miroir ;
- événements ;
- marqueurs ;
- profil de retargeting ;
- corrections sparse dans un curve editor léger.

Aucune commande « rendre éditable » et aucune duplication de source ne sont
nécessaires.

### 11.4 Séquence

L'éditeur de séquence comporte des pistes :

- animation squelettique ;
- transform/nœud ;
- propriété typée ;
- caméra ;
- audio ;
- événement/signal ;
- séquence imbriquée.

Les clips possèdent trim, blend d'entrée/sortie, vitesse et boucle. Le seek est
déterministe. Les pistes de propriétés sont compilées en accès typés : la
réflexion et le JSON ne sont utilisés que pour l'authoring.

## 12. Contrat d'authoring LLM

### 12.1 Surface minimale

Le manifeste moteur expose les schémas et capacités d'animation. Les outils
doivent permettre :

- `list_animation_assets`
- `inspect_animation_asset`
- `inspect_rig`
- `create_clip_view`
- `update_clip_view`
- `create_locomotion_graph`
- `create_animation_graph`
- `set_graph_parameter`
- `add_animation_state`
- `add_animation_transition`
- `create_animation_sequence`
- `validate_animation_asset`
- `compile_animation_asset`
- `preview_animation_asset`
- `get_animation_diagnostics`

Ces outils produisent des `SaidaOp` versionnées et undoables. Il ne faut pas
introduire un journal concurrent réservé à l'animation.

### 12.2 Simplicité par recettes

Les recettes de haut niveau couvrent :

- locomotion idle/walk/run ;
- personnage avec jump/fall/land ;
- one-shot d'attaque ;
- couche haut du corps ;
- interaction main/objet ;
- avatar VR tête/mains ;
- séquence cinématique.

Le LLM choisit d'abord une recette, puis ne modifie que les paramètres utiles.
Le graphe détaillé reste inspectable et éditable.

### 12.3 Validation

Chaque mutation peut être exécutée en dry-run. Les diagnostics contiennent :

- code stable ;
- sévérité ;
- `AssetID` ;
- chemin JSON ;
- message lisible ;
- suggestion structurée si possible.

La validation détecte notamment les assets absents, cycles de graphe, états
inaccessibles, paramètres inconnus, clips incompatibles, os non mappés,
événements hors plage et budgets dépassés.

## 13. Budgets et benchmarks

Les budgets initiaux sont des critères de conception. Ils seront recalibrés sur
un appareil Android/Quest de référence et Chrome avec WASM SIMD.

### 13.1 Invariants mesurables

- zéro allocation dans `AnimationInstance::update` ;
- zéro lookup par chaîne dans le runtime chaud ;
- zéro appel virtuel par piste ;
- palette de 48 octets par os visible ;
- upload limité aux octets réellement utilisés ;
- compilation et résultat déterministes ;
- aucune différence fonctionnelle entre Vulkan et WebGPU ;
- profiler séparant control, sampling, solve et upload.

### 13.2 Scènes de benchmark

1. 100 personnages, 80 os, un clip en boucle.
2. 50 personnages, 80 os, blend de deux clips et transitions.
3. 20 avatars proches avec layers, masks et IK légère.
4. 500 personnages éloignés avec LOD et pose sharing.
5. Séquence de dix minutes paginée avec seek aléatoire.

Objectifs provisoires :

- Chrome/WASM SIMD : 100 personnages × 80 os, blend 2 voies, sous 1,5 ms p95
  pour le sampling/solve à 30 Hz sur machine de référence ;
- Android/Quest : 50 personnages × 80 os sous 1,5 ms p95 à 30 Hz ;
- couche XR tête/mains d'un avatar local sous 0,15 ms ;
- clip mocap cuit inférieur à 25 % des données float brutes pour une erreur
  visuelle sous le seuil configuré ;
- aucune hausse du nombre d'uploads par œil en multiview.

## 14. Tests et qualité

### 14.1 Tests unitaires

- hiérarchies dont les parents apparaissent après les enfants ;
- rest pose partielle et animation de canaux partiels ;
- STEP, LINEAR, CUBICSPLINE ;
- boucle, seek, lecture inverse et vitesse nulle ;
- quaternions antipodaux et données invalides ;
- transitions, triggers, interruption et synchronisation ;
- masks, additive et root motion ;
- événements traversant une boucle ;
- BVH avec ordres de rotation variés ;
- retargeting avec os absents et proportions différentes ;
- compilation déterministe et migration de schéma.

### 14.2 Golden tests

- poses de référence glTF/BVH à des temps connus ;
- comparaison scalaire/SIMD ;
- comparaison source/cuit selon tolérance ;
- hashes de palettes identiques Vulkan/WebGPU ;
- snapshots d'assets rechargés après reimport.

### 14.3 Robustesse

- fuzzing des importeurs et des schémas JSON ;
- limites explicites sur os, pistes, clés, états et profondeur de graphe ;
- refus des NaN/Inf ;
- erreurs sans mutation partielle ;
- hot reload avec conservation ou réinitialisation explicite des instances.

### 14.4 Performance CI

Un exécutable headless `saida_animation_bench` mesure :

- débit de sampling ;
- coût de blend/solve ;
- allocations ;
- taille cuite ;
- temps de compilation ;
- octets de palette ;
- efficacité du LOD et du pose sharing.

La CI signale les régressions au-delà d'un seuil, sans rendre le build dépendant
des fluctuations mineures de la machine.

## 15. Plan d'exécution

### Étape 0 — Référence et correctness

Livrables :

- [x] corpus minimal glTF/BVH versionné dans les fixtures
      (`tests/fixtures/animation/` : `two_bone.gltf` couvre LINEAR/STEP/
      CUBICSPLINE + skin, `two_joint.bvh` couvre canaux 6/3) ;
- [x] tests de pose (`saida_animation_core_tests` : hiérarchie désordonnée,
      cycle, STEP/LINEAR, golden poses sur les deux fixtures) et benchmark du
      runtime actuel (`saida_animation_bench`, headless : 100 perso × 80 os =
      ~0,72 ms avg / 0,83 ms p95 par frame en natif sur machine de dev) ;
- [x] rest pose correcte (glTF `nodeTransform(joint)` → `Bone::restLocal`,
      `Animator::setRig` initialise la bind pose depuis le rig) ;
- [x] ordre topologique indépendant des indices (`Rig::finalize` + Kahn,
      `GlobalPose::computeFrom` suit `evaluationOrder`) ;
- [x] interpolation STEP (enum `TrackInterpolation`, importeur + sampler) ;
- [x] preview capable de sélectionner un clip dans un `Animator`
      (`Animator::activeClipNode`, combo de clips dans le panneau 3D Importer) ;
- [~] skinning WebGPU activé et testable : `boneOffset` traverse le chemin
      GPU-driven (InstanceData + culling.comp) et plus aucun garde web ne
      neutralise les palettes ; reste à valider visuellement sur un build web.

Import animation découplé du GPU : `GLTFLoader::loadAnimationData` et
`BVHLoader::parse` construisent rigs/clips sans `ResourceManager` — même code
de construction que `load()`, utilisé par les tests et le futur cooker.

Critère de sortie : les mêmes fixtures produisent les mêmes poses attendues sur
desktop et web, sans os traité comme racine par erreur. (Desktop : vérifié par
les golden tests ; web : à exécuter sur le runtime WASM.)

### Étape 1 — Contrat d'assets non destructif

Livrables :

- [~] schémas : `ClipView` (`.sclip` schema 1 : source, name, range, loop,
      speed, events), `AnimationGraph` (`.sgraph` schema 1 : paramètres typés
      float/int/bool — trigger réservé —, clips alias→clé, états play/loop,
      initial, transitions conditionnelles + crossfade ; validation §12.3 :
      doublons, références inconnues, états inaccessibles ; `build()` compile
      vers `AnimStateMachine`, le DSL `AnimGraphParser` devient legacy) et
      `RetargetProfile` (`.sretarget` schema 1 : map os cible→piste source
      au-dessus du `RetargetMap`, couverture avec warnings §8.1,
      `fromAutoMap` = suggestion éditable) ; restent `RigAsset` et
      `AnimationSequence` ;
- [x] identifiants stables des sous-assets glTF/BVH : clé `fichier#clip`
      (l'`AssetRegistry` redonne le même `AssetID` à clé identique après
      reimport) ; `ClipView.source` référence cette clé ;
- [x] sérialisation/migration versionnée : `schema` obligatoire, refus des
      schémas plus récents, hook `migrate()` pour les anciens ; diagnostics
      structurés `AssetDiagnostic {code, severity, jsonPath, message}` (§12.3) ;
- [~] création et validation de `ClipView` : lecture non destructive en place
      (`ClipNode::setRange` boucle/clampe dans la sous-plage du clip partagé,
      `ClipView::instantiate` — aucune duplication de clés) ; validation contre
      la source (plages, événements) ; reste l'UI éditeur de création ;
- [~] premiers outils : `saida_tool inspect-anim` (rigs/clips/durées JSON,
      headless), `saida_tool validate-clipview` et `saida_tool
      validate-animgraph` (résolution des clés sources + diagnostics,
      exit 0/1) ; restent les outils MCP éditeur
      (`list/inspect/create_clip_view/validate` côté McpBridge — reportés
      volontairement après le cœur).

Critère de sortie : plusieurs vues éditables utilisent une seule animation
source et survivent à un reimport sans changement d'identité. (Testé :
`testClipViewsShareSource` — deux vues sur un même clip, poses identiques à
temps absolu égal ; roundtrip + refus de schéma futur ; fixtures `.sclip`
valides/invalides passées à `saida_tool` en ctest.)

### Étape 2 — Cooker et compression

Livrables :

- cache cuit par hash ;
- réduction de clés, quantification et canaux constants ;
- rapport d'erreur et statistiques ;
- chargement des données cuites sur desktop et web ;
- pages pour longues séquences.

Critère de sortie : les golden tests respectent la tolérance et les clips mocap
de référence atteignent l'objectif de taille.

### Étape 3 — Kernel data-oriented

Livrables :

- `AnimationInstance` sans allocation ;
- sampling SoA avec curseurs ;
- pose arena ;
- programme compact ;
- kernels scalaire, WASM SIMD et NEON ;
- paramètres typés ;
- instrumentation détaillée.

Critère de sortie : disparition des maps, chaînes et appels virtuels du chemin
chaud ; benchmarks sous les budgets définis.

### Étape 4 — Renderer web/mobile/XR

Livrables :

- palette affine 3x4 commune ;
- ring buffer dimensionné au visible ;
- upload partagé par personnage ;
- multiview avec une seule palette ;
- scheduling 30/45 Hz avec interpolation ;
- couche tardive tête/mains XR ;
- profils de qualité web, mobile et Quest.

Critère de sortie : scène animée identique Vulkan/WebGPU, aucun upload doublé par
œil et maintien du budget XR sur la scène de référence.

### Étape 5 — Playback production

Livrables :

- graphes compilés ;
- blend spaces 1D/2D ;
- state machine complète ;
- layers, masks et additive ;
- root motion ;
- événements et sync markers ;
- LOD d'animation et pose sharing initial.

Critère de sortie : locomotion, one-shot, upper-body layer et foule distante sont
réalisables sans code spécifique au personnage.

### Étape 6 — Retargeting et procédural

Livrables :

- mapping sémantique ;
- corrections de rest pose et d'échelle ;
- éditeur de profil ;
- auto-mapping avec diagnostics ;
- two-bone IK, look-at et avatar VR tête/mains.

Critère de sortie : un BVH de convention différente fonctionne sur deux rigs de
proportions différentes avec un profil réutilisable et sans modifier la source.

### Étape 7 — Éditeur animation et séquences

Livrables :

- inspecteur d'assets ;
- preview universelle ;
- édition de `ClipView` ;
- retarget preview ;
- vue d'état du graphe ;
- timeline multipiste déterministe ;
- curve patches sparse ;
- profiler animation intégré.

Critère de sortie : un utilisateur importe un mocap, crée plusieurs clips,
retargete, compose une séquence et la prévisualise sans dupliquer l'animation.

### Étape 8 — Authoring LLM complet

Livrables :

- recettes haut niveau ;
- opérations SaidaOp undoables ;
- dry-run et diagnostics structurés ;
- génération/patch de graphes et séquences ;
- preview et validation pilotables ;
- manifest des capacités et limites de plateforme.

Critère de sortie : un LLM peut produire une locomotion, une attaque en couche et
une courte cinématique sans écrire de C++ ni manipuler des offsets de bas niveau.

### Étape 9 — Scalabilité avancée

Livrables conditionnés par les mesures :

- jobification multi-thread avec fallback mono-thread web ;
- pose sharing massif ;
- streaming de séquences ;
- morph targets/facial LOD ;
- chemin GPU spécialisé pour foules si le CPU devient réellement limitant ;
- réplication réseau de l'état d'animation.

Critère de sortie : chaque ajout démontre un gain mesuré sur un cas réel et ne
complexifie pas le chemin standard.

## 16. Migration de l'existant

La migration est incrémentale :

1. Corriger `Rig`, `LocalPose` et le renderer sans changer l'API publique.
2. Faire produire aux importeurs les nouveaux assets en plus des anciens objets.
3. Adapter `Animator::play(name)` à un programme simple compilé.
4. Compiler les `AnimNode` existants vers les nouvelles opérations pendant la
   période de transition.
5. Migrer `TimelinePropertyTrack` vers les séquences typées tout en conservant
   le chargement des anciennes scènes.
6. Déprécier les anciens objets polymorphes uniquement lorsque les tests,
   l'éditeur et les outils d'authoring couvrent leurs usages.

Les scènes existantes doivent continuer à charger. Toute rupture de format doit
disposer d'une migration explicite et testée.

## 17. Anti-objectifs

- Pas de duplication d'un clip pour le rendre éditable.
- Pas de JSON, réflexion ou parsing dans l'update runtime.
- Pas de nom d'os ou d'état utilisé comme lookup chaud.
- Pas d'allocation par frame.
- Pas de graphe visuel obligatoire pour les cas courants.
- Pas de système distinct pour Vulkan, WebGPU, mobile ou XR.
- Pas de compute/GPU animation généralisée avant benchmark.
- Pas d'IK full-body complexe avant un retargeting et un kernel solides.
- Pas de seconde base d'assets parallèle à `AssetRegistry`.
- Pas d'op-log distinct des `SaidaOp` pour l'animation.

## 18. Définition de terminé

Le chantier est considéré abouti lorsque :

- glTF et BVH sont importés avec rest pose, hiérarchie et conventions correctes ;
- une source mocap peut produire plusieurs vues sans duplication de clés ;
- les clips, graphes, profils et séquences sont versionnés et persistants ;
- le runtime chaud est data-oriented, sans allocation ni lookup par chaîne ;
- Vulkan et WebGPU rendent les mêmes poses ;
- le scheduler tient les budgets web/mobile/Quest ;
- le mode XR applique tête/mains tardivement sans recalcul complet ;
- root motion, événements, blends, layers, masks et retargeting sont testés ;
- l'éditeur prévisualise clip, graphe, retargeting et séquence ;
- un LLM peut authorer et valider ces assets par opérations structurées ;
- les benchmarks et golden tests protègent correctness, taille et performance.
