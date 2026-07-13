# Plan V1 — ce qu'il faut verrouiller avant de publier le moteur

> Statut : en cours. Chantier 2 quasi verrouillé (schema partout, corpus de
> rétro-compat en CI, contrat publié dans `docs/PUBLIC_COMPATIBILITY.md`) ;
> chantier 3 fondation posée (`AssetLoader` async + `assets.load` JS) ;
> chantier 1 : jeu témoin créé et joué de bout en bout
> ([WitnessGame/](WitnessGame/README.md)), frictions consignées dans
> [docs/WITNESS_GAME.md](docs/WITNESS_GAME.md). Complète `PLAN_SAIDA_ENGINE_UPDATE.md`
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

### Problème

L'éditeur sait créer, l'export-template pipeline existe (~80 %), mais le test
qui définit un moteur n'a jamais été passé de bout en bout : produire un jeu
qui tourne **sans l'éditeur, sur une machine vierge**. C'est la différence
entre un moteur et une démo d'éditeur. Restes connus : runtime standalone
(Étape 8), versioning/métadonnées/icône (Étape 15), et tout ce que le trajet
complet révélera.

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
