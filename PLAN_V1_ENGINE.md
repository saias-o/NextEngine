# Plan V1 — ce qu'il faut verrouiller avant de publier le moteur

> Statut resynchronisé le 2026-07-15 : **aucun des trois chantiers n'est encore
> fermé pour une publication publique**. Le jeu témoin et les exports CLI ont
> franchi une grande partie du chemin nominal, les schémas/fixtures existent et
> l'API asynchrone d'assets est posée. **Chantier 3, cœur livré le
> 2026-07-15** : textures et meshes `.obj` transitent par l'`AssetLoader`
> (lecture + décodage stbi/tinyobj sur le worker, création GPU sur le thread
> principal, fallbacks visibles : défauts pendant le chargement, damier magenta
> pour un asset en échec), déchargement GPU réel par mark-and-sweep au
> `changeScene` (graveyard > frames en vol, recyclage des index bindless et des
> slots matériaux), `gpuResidentBytes` exposé dans `assets.stats()` et asserté
> stable sur 16 cycles par l'E2E **desktop + web** (évictions vérifiées dans
> les logs) ; restes chantier 3 : budget GPU contraignant en cours de scène
> (LRU), rigs/animations dans le sweep. Restent néanmoins la preuve sur machine
> vierge et via l'UI, la parité sémantique des snapshots entre runtimes et la
> stabilité publique du contrat. Voir aussi [docs/V1_KNOWN_LIMITATIONS.md](docs/V1_KNOWN_LIMITATIONS.md)
> et l'[audit plateforme](https://github.com/saias-o/saida/blob/main/AUDIT_BEFORE_PROD.md). Complète
> `PLAN_SAIDA_ENGINE_UPDATE.md`
> (qui reste la feuille de route fonctionnelle) : ce document ne liste pas des
> features, mais les trois chantiers qui rendent une publication publique
> *irréversible sans casse*. Principe directeur : on priorise ce qu'on ne peut
> plus corriger une fois que des utilisateurs ont des projets réels.

## 1. Pourquoi ces trois-là

Un moteur devient « public » le jour où des inconnus ont des projets qu'on n'a
pas le droit de casser. À partir de là, trois choses deviennent quasi
impossibles à rattraper :

1. un **chemin d'export** qui n'a jamais été traversé en entier — on le
   découvre en public, au pire moment ;
2. des **formats et une API sans discipline de compatibilité** — la première
   rupture détruit la confiance, et la rétro-compat ne s'ajoute pas après coup ;
3. une **API de chargement synchrone figée publiquement** — le passage à
   l'asynchrone casse ensuite tout le code gameplay des utilisateurs.

Tout le reste (features, perf, plateformes) peut s'améliorer version après
version sans douleur. Ces trois-là, non.

## 2. Chantier 1 — Le chemin « ship » : du bouton Build au jeu autonome

### État réel

Le runtime standalone, le packaging, les métadonnées/icône, `saida_tool
export-game` et des harnais WitnessGame Windows/Web existent. Des exécutions
locales historiques sont consignées dans `docs/WITNESS_GAME.md`.

Le critère public n'est toutefois pas atteint : le bouton Build UI n'est pas
automatisé, aucune preuve sur machine vierge n'est attachée à la release, le
player web ne rend pas l'UI du jeu, une séquence `.sseq` n'est pas traversée et
les ressources de scène ne passent pas toutes encore par l'AssetLoader.

### Méthode : le jeu témoin

Créer un petit jeu vertical (5-10 minutes de jeu) qui traverse *tous* les
sous-systèmes : scènes + prefabs, scripts JS, physique, animation (graphe
.sgraph + une séquence .sseq), audio, UI, save/load, changement de scène.
Ce jeu n'est pas une démo marketing : c'est l'instrument de mesure. Chaque
friction rencontrée en le construisant puis en l'exportant est un bug de V1.

### Livrables

- runtime standalone desktop : exécutable de jeu sans code éditeur, sans
  ImGui, qui boote un projet packagé (pas le dossier source) ;
- export Windows complet depuis l'UI Build Settings : version, métadonnées
  d'exécutable, icône, nom du jeu — testé sur une machine sans MSYS2 ni SDK ;
- export Web du même projet via le pipeline existant (`build_web.sh` /
  `BuildExporter::exportWebBuild`), même gameplay, mêmes saves ;
- le jeu témoin committé comme projet d'exemple (il devient aussi le corpus
  de tests des chantiers 2 et 3) ;
- un document court « limitations connues de la V1 », honnête et mesuré
  (exigé par la définition V1 du plan maître, §22).

### Critères de sortie

- « Build » produit un artefact qui tourne sur une machine vierge (desktop et
  Web), sans éditeur, avec save/load fonctionnels ;
- le même projet, le même code gameplay, tourne dans l'éditeur, en standalone
  desktop et en Web ;
- aucune étape manuelle non documentée entre l'éditeur et l'artefact final.

## 3. Chantier 2 — Le gel de compatibilité : formats et API sous contrat

### Problème

La règle sacrée d'un moteur public : **on ne casse jamais le projet d'un
utilisateur**. Le moteur sérialise beaucoup : scènes JSON, `.neproj`,
`.saidascenario`, boot manifest, scripts de wiring, et les cinq assets
animation (`.sclip`/`.sgraph`/`.sretarget`/`.srig`/`.sseq`). La discipline est
inégale : l'animation a schéma + migration + refus-du-plus-récent testés ;
les autres formats ont au mieux un numéro de version, au pire rien.
`FormatVersionTests.cpp` existe comme embryon.

### État réel

Le champ `schema`, des migrations, `docs/PUBLIC_COMPATIBILITY.md` et un corpus
de fixtures existent. Cette discipline structurelle est une bonne base, mais le
contrat n'est pas encore « gelé » :

- le snapshot headless est fail-closed et préserve son sous-ensemble enregistré,
  Camera incluse, mais UI/physique restent explicitement hors contrat ;
- les refs Mesh existantes round-trippent sans GPU, tandis que l'opération
  `create_node MeshNode` reste explicitement interdite sans `ResourceManager` ;
- l'authoring Web annonce maintenant son registre réel et charge ce sous-ensemble
  de façon atomique/fail-closed ; le player Web publie également son registre,
  valide scènes et autoloads avant `ready` et refuse tout type absent. Les types
  UI/physique restent à implémenter ou aligner avec le natif/headless ;
- les fixtures prouvent surtout la lecture/migration, pas l'équivalence
  sémantique exhaustive après round-trip ;
- l'API JS manque encore des services annoncés et son sandbox/interrupt n'est
  pas prêt pour du contenu public non fiable.

Le document `PUBLIC_COMPATIBILITY.md` est donc un **contrat candidat V1**, pas
encore une garantie de stabilité déjà offerte aux utilisateurs.

### Livrables

- **inventaire des surfaces gelées** : chaque format sérialisé et chaque API
  publique (JS d'abord — c'est elle que les utilisateurs codent contre —,
  puis les behaviours C++ réfléchis) listés dans un document unique, avec leur
  numéro de version et leur politique (stable / évolutif avec migration /
  interne non garanti — le cache `.sanimc` par exemple est régénérable, donc
  hors contrat) ;
- **la même mécanique partout** : `schema` obligatoire, refus explicite d'un
  schéma plus récent, hook de migration testé pour les schémas anciens —
  généraliser ce qui existe déjà côté animation aux scènes, projets et
  scénarios ;
- **corpus de rétro-compat en CI** : des projets/scènes/assets *figés* (dont
  le jeu témoin du chantier 1, gelé à sa version de publication), chargés à
  chaque build ; toute rupture de chargement fait échouer la CI ;
- **politique écrite** dans CLAUDE.md/docs : toute rupture de format passe par
  une migration testée, toute dépréciation d'API JS vit au moins une version
  avec warning avant suppression.

### Critères de sortie

- la CI charge avec succès le corpus d'anciens formats à chaque commit ;
- un projet créé à la publication de la V1 s'ouvre dans toute version
  ultérieure, ou échoue avec un message de migration explicite — jamais de
  corruption silencieuse ;
- la liste de ce qui est garanti (et de ce qui ne l'est pas) est publiée.

## 4. Chantier 3 — Cycle de vie des assets : l'asynchrone AVANT le gel de l'API

### Problème

C'est le P0 §8 du plan maître, mais son urgence est contractuelle plus que
technique : si l'API JS publiée est synchrone (« charge et rends la main »),
le passage ultérieur à l'asynchrone casse les signatures, les ordres
d'initialisation et donc tout le code gameplay existant. C'est le retrofit le
plus douloureux de l'histoire des moteurs — à faire pendant qu'on est seuls
sur le code. Il doit donc aboutir avant que le chantier 2 ne gèle l'API JS.

### État réel

`AssetLoader`, les handles/états/priorités, le budget et `assets.load/stats`
existent. La copie de travail actuelle intègre aussi progressivement textures
et meshes au chargement asynchrone. Ce chantier reste en cours : toutes les
ressources de scène ne sont pas encore comptées/libérées par ce chemin, le Web
ne démontre pas encore le streaming fetch/IDBFS attendu et les modifications
assets locales doivent être commitées, revues et couvertes avant de déclarer le
contrat stable.

### Périmètre minimum (celui du plan maître §8)

- identités d'assets stables + comptage de références (l'`AssetRegistry`
  existant est la base — pas de seconde base d'assets) ;
- chargement asynchrone avec priorités, sans bloquer durablement la frame ;
- déchargement réel et mémoire bornée (pas de croissance sans limite) ;
- API JS/C++ pensée asynchrone dès la signature (handle + état/promesse),
  même si l'implémentation V1 reste simple ;
- fallbacks visibles pour les assets manquants, diagnostics de fuite
  (compteurs d'assets vivants au profiler).

### Critères de sortie

- le jeu témoin change de scène sans hitch mesurable ni fuite (mémoire stable
  sur N cycles charge/décharge, mesurée en CI headless) ;
- l'API de chargement publiée est asynchrone ; aucun point de l'API publique
  ne promet un chargement bloquant ;
- desktop et Web (fetch) partagent le même contrat.

## 5. Ordre et articulation

1. **Chantier 1 d'abord** : il est mesurable, visible, et révèle mécaniquement
   les manques réels des deux autres (c'est en exportant le jeu témoin qu'on
   découvre ce qui doit être gelé et ce qui bloque la frame).
2. **Chantier 3 ensuite** (ou en chevauchement dès que le jeu témoin existe) :
   l'API asynchrone doit exister avant le gel.
3. **Chantier 2 en dernier verrou** : geler formats + API une fois stabilisés
   par les deux premiers, corpus en CI, puis publication.

## 6. Hors périmètre (assumé, conforme au plan maître)

- multiplayer, grands terrains, frameworks de genre (post-V1, §20/§22) ;
- SIMD animation, pose sharing massif, GPU crowds (gated aux mesures) ;
- store d'assets, services en ligne, éditeur web complet (plateforme Saida,
  autre repo) ;
- toute nouvelle feature de rendu : la V1 se juge sur la fiabilité du chemin
  éditeur → jeu livré, pas sur une feature de plus.

## 7. Définition de terminé

La V1 est publiable quand : le jeu témoin s'exporte et tourne sur machine
vierge (desktop + Web) ; le corpus de rétro-compat passe en CI ; l'API de
chargement est asynchrone et gelée ; les limitations restantes sont écrites.
À ce moment-là — et pas avant — on ouvre au public.

**État au 2026-07-15 : non atteint.** Aucun document ou badge de CI ne doit
présenter la V1 comme stable tant que ces trois critères et le round-trip
cross-runtime ne sont pas prouvés sur les artefacts exacts d'une release.
