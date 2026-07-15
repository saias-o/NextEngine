# Plan de mise a niveau de Saida Engine

État resynchronisé : 2026-07-15. Ce plan décrit une cible. Une rubrique n'est
« terminée » que si ses critères de sortie sont prouvés sur desktop et Web avec
les artefacts de release ; la présence des classes ou d'un test local ne suffit
pas. Le projet reste Alpha.

## 1. Objectif

Ce document definit la feuille de route runtime necessaire pour faire de Saida
Engine un moteur 3D reellement utilisable par des developpeurs independants.

Le perimetre concerne les fonctionnalites de jeu et les API runtime. Les outils
d'edition ne sont inclus que lorsqu'ils sont indispensables pour valider ou
exploiter une fonctionnalite runtime.

L'objectif est qu'un meme jeu puisse etre developpe en C++ ou en JavaScript,
fonctionner avec plusieurs peripheriques, sauvegarder sa progression et etre
exporte sur desktop, Web et VR sans reimplementation specifique du gameplay.

Les priorites sont classees selon quatre niveaux :

- P0 : bloque la creation ou l'export d'un jeu complet ;
- P1 : necessaire pour une premiere version publique credible ;
- P2 : important pour elargir les types de jeux supportes ;
- P3 : extension optionnelle a engager lorsque le socle est stabilise.

Résumé actuel :

| Chantier | État |
|---|---|
| Player Web | Partiel : renderer, scènes, scripts, Jolt, audio et storage existent ; UI, input complet et robustesse contenu restent ouverts. |
| Input | Partiel : clavier/souris/actions/touch brut existent ; axes gamepad, rebinding et multi-device ne sont pas complets. |
| Storage | MVP : slots JS et IDBFS existent ; atomicité, migrations, préférences, métadonnées et emplacements plateforme restent ouverts. |
| Physique | Large base Jolt desktop/web ; API de queries/contraintes et parité JS restent incomplètes. |
| JavaScript | Large base QuickJS ; accès cross-node/autoload, services complets, interrupt et sandbox restent ouverts. |
| Assets | AssetLoader async/handles/budgets existent ; intégration de toutes les ressources et streaming web sont en cours. |
| Animation | Implémentation large ; preuve WitnessGame `.sseq` et bindings JS complets restent ouverts. |
| UI | Desktop partiel/avancé ; player WebGPU sans rendu UI. |

## 2. Principes directeurs

### 2.1 Un seul moteur de jeu

Desktop, Web, mobile et VR doivent executer le meme modele de scene, le meme
cycle de vie, les memes comportements et les memes API de gameplay. Une
plateforme peut utiliser un backend different ou reduire une qualite visuelle,
mais elle ne doit pas devenir un runtime fonctionnellement distinct.

### 2.2 Parite C++ et JavaScript

Une fonctionnalite runtime n'est consideree comme terminee que lorsqu'elle est
accessible proprement depuis les deux langages, sauf justification explicite.
Cette parite est indispensable pour que les developpeurs et les LLM puissent
produire du gameplay sans dependances implicites a du code natif prive.

### 2.3 Leger par defaut

Chaque systeme doit proposer un chemin simple, performant et peu couteux en
memoire. Les fonctionnalites avancees doivent rester activables a la demande.
Cette contrainte est particulierement importante pour le Web, le mobile et les
casques VR autonomes.

### 2.4 API stables et composables

Le moteur doit fournir des primitives generales : input, sauvegarde, queries,
navigation, audio, animation et chargement. Les systemes propres a un genre de
jeu, comme les inventaires ou les quetes, doivent etre construits au-dessus de
ces primitives plutot qu'imposes par le coeur du moteur.

### 2.5 Degradation explicite

Lorsqu'une capacite n'est pas disponible sur une plateforme, le moteur doit le
signaler clairement. Une fonction critique ne doit pas echouer silencieusement
ou retourner une valeur neutre donnant l'impression qu'elle est supportee.

## 3. P0 - Runtime Web de jeu complet

### Probleme

Le problème initial a été réduit : un player web distinct de l'authoring démarre
maintenant le SceneTree, QuickJS, Jolt, Web Audio et le storage communs. La
parité reste toutefois incomplète : UI non rendue, input multi-périphérique
partiel, capabilities trop optimistes et contenu corrompu pouvant interrompre
le player.

Un jeu desktop ne peut donc pas encore être présenté comme exportable tel quel
vers le navigateur dans tous les cas.

### Perimetre

- player Web autonome et distinct du runtime d'authoring ;
- chargement normal du projet et de la scene principale ;
- SceneTree, cycle de vie des nodes et behaviours, signaux et autoloads ;
- changement de scene, instanciation de prefabs, timers et suppression differee ;
- scripts de gameplay ;
- input clavier, souris, touch et manette lorsque le navigateur le permet ;
- audio Web ;
- physique de jeu ou solution de compatibilite officiellement supportee ;
- UI de jeu ;
- meme format de scene et memes identifiants d'assets que le player desktop ;
- erreurs de capacite et diagnostics de demarrage exploitables.

### Criteres de sortie

- un projet gameplay de reference tourne sur desktop et dans un navigateur sans
  branche specifique dans ses scripts ;
- les scenes, prefabs, autoloads, signaux et timers ont le meme comportement ;
- aucun loader specialise pour une scene de demonstration n'est necessaire ;
- les differences restantes sont documentees comme limites de plateforme ;
- le runtime d'authoring et le player Web ne sont plus confondus.

### Dependances

Ce chantier doit commencer avant les autres P0. Les API suivantes doivent etre
concuees de maniere a etre compatibles avec le player Web des leur introduction.

### Etat (juillet 2026)

`web/player` est distinct du runtime d'authoring et boote `game.saida` via le
BootManifest/SceneSerializer commun. WebGPU, SceneTree, autoloads, timers,
opérations différées, QuickJS, Jolt mono-thread, Web Audio et storage IDBFS sont
intégrés. Des scénarios BeachDemo/WitnessGame ont été consignés comme validations
locales historiques.

Le chantier n'est pas terminé : les nœuds UI se dégradent en Node générique,
gamepad/touch ne couvrent pas le contrat multi-périphérique, des contenus glTF
corrompus peuvent interrompre le player, et la parité complète du jeu témoin
n'est pas une gate de release. `PlatformCaps` doit refléter le comportement
réel ; aujourd'hui certains caps desktop, notamment Gamepad, sont trop larges.

## 4. P0 - Input multi-peripherique complet

### Probleme

Le systeme d'actions prend en charge le clavier et la souris, mais les bindings
gamepad sont incomplets et le touch reste une entree brute. Un jeu ne peut pas
encore definir une intention de gameplay unique utilisable sur desktop, mobile,
Web et VR.

### État actuel

Actions, contexts, clavier/souris, boutons gamepad, événements touch et input XR
existent. `Input.cpp` laisse encore les axes gamepad non implémentés ; rebinding,
profils persistants, multi-joueur local, hotplug et touch-as-bindings restent
ouverts. Le bit `GamepadInput` desktop ne doit pas être lu comme support complet.

### Perimetre

- actions numeriques et binaires ;
- boutons et axes de manette ;
- clavier et souris ;
- gestes et zones tactiles utilisables comme bindings ;
- prise en charge de plusieurs peripheriques et joueurs locaux ;
- deadzones, inversion, sensibilite et compositions d'axes ;
- contextes d'input empilables pour gameplay, menus et UI ;
- rebinding runtime ;
- serialisation des profils utilisateur ;
- detection du peripherique actif ;
- haptique standard lorsque la plateforme le permet ;
- integration coherente avec l'input OpenXR ;
- API equivalente en C++ et JavaScript.

### Criteres de sortie

- un personnage et les menus d'un projet de reference sont jouables au clavier,
  a la manette et au touch sans modifier le code gameplay ;
- deux joueurs locaux peuvent utiliser des peripheriques distincts ;
- les bindings peuvent etre modifies et sauvegardes par le jeu ;
- la perte ou la reconnexion d'un peripherique est geree proprement ;
- une action indisponible ne donne pas silencieusement une valeur trompeuse.

## 5. P0 - Stockage utilisateur et sauvegardes

### Probleme

La serialisation de scene actuelle decrit le contenu du projet. Elle ne fournit
pas un systeme de sauvegarde pour la progression, les preferences ou les donnees
propres a un joueur.

### État actuel

L'API JS `storage.save/load/has/remove` par slot existe sur desktop et utilise
IDBFS dans le player web. Elle a couvert le WitnessGame. Elle ne satisfait pas
encore le périmètre complet : écritures atomiques/rollback, schéma+migrations de
sauvegarde, préférences séparées, metadata de slots, quotas et emplacement
utilisateur OS au lieu du dossier projet/package.

### Perimetre

- emplacement utilisateur adapte a chaque plateforme ;
- API de lecture et d'ecriture de donnees de jeu ;
- preferences separees des sauvegardes de progression ;
- slots de sauvegarde ;
- formats versionnes et migrations ;
- ecriture robuste face aux interruptions ;
- metadonnees de slot pour les menus de chargement ;
- quotas et erreurs de stockage explicites ;
- persistance navigateur ;
- API asynchrone lorsque la plateforme l'impose ;
- API C++ et JavaScript ;
- points d'extension futurs pour cloud save sans rendre celui-ci obligatoire.

### Criteres de sortie

- un projet peut sauvegarder et restaurer sa progression apres redemarrage ;
- une ancienne version de sauvegarde peut etre migree ou rejetee proprement ;
- une ecriture interrompue ne detruit pas la derniere sauvegarde valide ;
- le meme code de jeu fonctionne sur desktop et Web ;
- les donnees utilisateur ne sont pas ecrites dans le repertoire d'installation.

## 6. P0 - Physique de gameplay unifiee

### Probleme

Jolt fournit un bon coeur physique, mais l'API exposee au gameplay reste limitee.
Les characters ne participent pas aux memes queries que les rigid bodies et des
recherches lineaires dans l'arbre de scene compensent cette difference.

### État actuel

Jolt, bodies, CharacterBody, Area/triggers, inner body du character et player
web existent. Les queries communes, filtres complets, shape casts/overlaps,
joints, matériaux, CCD et parité des bindings JS ne couvrent pas encore les
critères ci-dessous.

### Perimetre

- physique disponible sur toutes les plateformes runtime visees ;
- collision layers et masks configurables ;
- raycasts filtres, single-hit et multi-hit ;
- shape casts pour les formes principales ;
- overlaps physiques ;
- queries communes aux rigid bodies, characters et areas ;
- forces, impulses, torques et controle des vitesses ;
- verrouillage d'axes et parametres de sommeil ;
- collision continue configurable ;
- joints et contraintes fondamentales ;
- informations de contact exploitables par le gameplay ;
- materiaux physiques reutilisables ;
- comportement coherent des objets kinematic et des plateformes mobiles ;
- API C++ et JavaScript.

### Criteres de sortie

- les weapons, triggers, characters et objets dynamiques utilisent la meme
  famille de queries ;
- aucune approximation par sphere de tous les nodes n'est requise pour compenser
  une absence du monde physique ;
- les filtres de collision sont configurables par un jeu ;
- les cas usuels de doors, ropes simples, projectiles rapides et plateformes
  mobiles sont couverts ;
- les performances des queries sont mesurees sur desktop, Web et VR.

## 7. P0 - API JavaScript gameplay complete

### Probleme

QuickJS, les modules et le hot reload forment une bonne base, mais les bindings
actuels ne donnent acces qu'a une petite partie du moteur. Un gameplay complet
necessite encore du C++ pour des operations fondamentales.

### État actuel

Node, tree, time, input, signals locaux, audio, storage et assets couvrent une
partie significative du gameplay. L'accès aux autoloads et la communication
cross-node restent insuffisants, comme le montre le contournement storage du
WitnessGame. Physique/animation/UI/camera/XR ne sont pas exposés au niveau
annoncé. QuickJS manque aussi d'interrupt/deadline et la résolution de modules
doit être confinée avant exécution de contenu tiers.

### Perimetre

- navigation dans la hierarchie de nodes ;
- recherche par groupe, identifiant et relations parent/enfant ;
- instanciation de prefabs et suppression differee ;
- timers et tweens ;
- autoloads et services runtime ;
- proprietes et appels exposes par la reflection ;
- signaux et connexions avec duree de vie sure ;
- input complet ;
- queries physiques et controle des bodies ;
- audio ;
- animation ;
- UI de jeu ;
- sauvegardes ;
- chargement d'assets ;
- cameras ;
- XR lorsque disponible ;
- erreurs JavaScript avec contexte et traces exploitables ;
- documentation machine-readable de l'API pour les outils et les LLM.

### Criteres de sortie

- un petit jeu complet peut etre ecrit sans composant C++ personnalise ;
- les fonctions refletees eligibles ne demandent pas un binding manuel repetitif ;
- l'API JavaScript suit les memes concepts et noms que l'API C++ ;
- les handles de nodes detruits ne provoquent pas d'acces invalide ;
- les erreurs de script identifient le fichier et la source du probleme ;
- les exemples et tests couvrent l'ensemble des services de jeu essentiels.

## 8. P0 - Cycle de vie et chargement des assets

### Probleme

Les ressources sont principalement chargees de facon synchrone et conservees
jusqu'a la destruction du ResourceManager. Ce fonctionnement est simple mais ne
permet pas de controler les temps de chargement ni la memoire sur Web, mobile et
VR.

### État actuel

Cette description initiale est partiellement dépassée : un `AssetLoader`
asynchrone avec handles, états, priorités, budget, garbage collection et API JS
existe. L'intégration texture/mesh au `ResourceManager` est en cours dans la
copie de travail actuelle. Toutes les ressources de scène ne passent pas encore
par ce cycle, le graphe de dépendances/annulation/streaming web n'est pas fermé
et la stabilité mémoire doit être mesurée sur des scènes représentatives.

### Perimetre

- handles d'assets stables ;
- etats de chargement observables ;
- chargement asynchrone ;
- prechargement explicite ;
- graphe de dependances ;
- priorites et annulation ;
- liberation des ressources inutilisees ;
- budgets CPU et GPU ;
- diagnostics d'assets manquants ou incompatibles ;
- chargement de scene non bloquant ;
- preparation au streaming de textures, audio et contenu de niveau ;
- comportement defini lorsqu'un asset n'est pas encore pret ;
- parite desktop, Web et mobile.

### Criteres de sortie

- un changement de niveau important ne bloque pas toute la frame pendant son
  chargement ;
- les ressources d'un niveau quitte peuvent etre liberees ;
- un projet peut precharger les assets necessaires a une sequence ;
- la memoire reste bornee pendant une session longue avec plusieurs niveaux ;
- les erreurs et placeholders ne masquent pas un echec definitif de chargement.

## 9. P1 - Navigation et IA spatiale

### Probleme

Le blackboard et la machine a etats couvrent la logique decisionnelle simple,
mais les NPC ne disposent pas d'un service general pour se deplacer dans un
niveau ou eviter les autres agents.

### Perimetre

- donnees de navigation chargeables avec une scene ;
- calcul de chemin ;
- agents avec rayon, hauteur et vitesse ;
- requetes de position navigable ;
- liens speciaux pour sauts, portes et teleports ;
- evitement local leger ;
- mise a jour controlee pour les obstacles dynamiques ;
- plusieurs zones ou types d'agents ;
- API C++ et JavaScript ;
- integration avec CharacterBody et les machines a etats.

### Criteres de sortie

- plusieurs dizaines de NPC peuvent rejoindre une cible dans un niveau complexe ;
- un agent gere portes, passages et obstacles sans logique propre au niveau ;
- le cout peut etre budgete par frame ;
- le systeme peut etre totalement omis d'un jeu qui n'en a pas besoin.

## 10. P1 - Audio de jeu complet

### Probleme

L'audio actuel permet de lire, spatialiser et arreter des sons, mais il ne fournit
pas encore le controle attendu pour mixer un jeu complet.

### Perimetre

- bus Master, Music, SFX, UI, Ambience et Voice configurables ;
- volume, mute et pause par bus ;
- listener lie a la camera ou a un node choisi ;
- lecture, pause, reprise, seek, pitch et fades ;
- streaming adapte aux musiques et dialogues longs ;
- reutilisation efficace des sons courts frequents ;
- priorites et limites de voix ;
- spatialisation et attenuation configurables ;
- zones de reverb simples ;
- occlusion optionnelle et peu couteuse ;
- API C++ et JavaScript ;
- comportement coherent lors d'une pause de jeu ou perte de focus.

### Criteres de sortie

- un jeu peut mixer musique, ambiance, UI et effets separement ;
- les reglages audio sont sauvegardables ;
- les musiques longues ne sont pas chargees integralement en memoire ;
- les sons spatiaux suivent correctement la camera et leurs emetteurs ;
- un grand nombre d'effets courts ne provoque pas d'allocations excessives.

## 11. P1 - Systeme d'animation de production — implémentation large, validation partielle

Implémentation livrée en grande partie (voir CLAUDE.md, Etape 10) : assets versionnes (.sclip/.sgraph/
.sretarget/.srig/.sseq) avec diagnostics structures, cooker + cache derive par
hash, kernel data-oriented sans allocation, palette GPU 3x4 commune
Vulkan/WGSL, blend spaces, layers/masks/additif, root motion, events, LOD de
pose, retargeting semantique avec corrections de rest pose, two-bone IK et
look-at, sequences multipistes deterministes, panneau editeur et outils MCP
d'authoring LLM. `TimelinePropertyTrack` n'est plus un no-op : il utilise la
réflexion et l'interpolation. Les critères produit ne sont pas tous fermés : le
WitnessGame ne traverse pas encore une séquence `.sseq` exportée, les bindings
JS animation ne sont pas complets et les budgets/parités plateforme restent à
prouver. Les optimisations SIMD/NEON, pose sharing et GPU crowds restent gated
par les mesures.

### Perimetre minimum attendu

- clips persistants sans duplication des sources mocap ;
- graphes d'animation persistants ;
- transitions interruptibles et parametres complets ;
- blend spaces ;
- layers, bone masks et animation additive ;
- root motion ;
- events et markers ;
- retargeting avec differences de rest pose et de proportions ;
- IK legere et look-at ;
- sequences multipistes ;
- niveaux de qualite et frequence d'update adaptes aux foules ;
- API C++ et JavaScript ;
- chemins optimises Web, mobile et VR.

### Criteres de sortie

- locomotion, attaque one-shot et upper-body layer fonctionnent ensemble ;
- une animation mocap peut etre adaptee sans dupliquer le fichier source ;
- root motion et events sont deterministes ;
- les avatars VR peuvent piloter tete et mains avec une IK legere ;
- les budgets definis dans le plan animation sont respectes.

## 12. P1 - UI de jeu multi-peripherique et localisation

### Probleme

La UI accepte deja souris, clavier et touch, mais il manque les services communs
necessaires aux menus de jeux commercialisables et aux differentes tailles
d'ecran.

### Perimetre

- navigation directionnelle au clavier et a la manette ;
- focus visible et controlable ;
- ordre de navigation previsible ;
- adaptation aux safe areas ;
- echelles et densites d'ecran ;
- changement de resolution et de ratio ;
- localisation par tables ;
- pluriels, substitutions et fallback de langue ;
- changement de langue runtime ;
- support correct des textes longs et directions d'ecriture pertinentes ;
- bases d'accessibilite : taille de texte, contraste, reduction des mouvements et
  remapping ;
- API commune aux UI natives et WebCanvas lorsque cela est pertinent.

### Criteres de sortie

- tous les menus d'un projet de reference sont utilisables sans souris ;
- la UI reste lisible sur desktop, mobile et casque VR ;
- une langue peut etre ajoutee sans modifier le code gameplay ;
- les changements de langue et de peripherique mettent la UI a jour a chaud.

## 13. P1 - Scenes additives, streaming et pooling

### Probleme

Le SceneTree gere bien une scene courante, les autoloads et les prefabs, mais les
jeux plus longs ont besoin de charger progressivement du contenu et de reutiliser
les objets frequemment crees.

### Perimetre

- scenes additives ;
- chargement et dechargement asynchrones de zones ;
- ownership clair des nodes charges ;
- conservation explicite des services persistants ;
- activation differee lorsque les dependances sont pretes ;
- object pools pour nodes et prefabs ;
- remise a zero controlee des objets reutilises ;
- integration avec physique, audio, particules, scripts et animation ;
- limites et statistiques de pools ;
- compatibilite avec les sauvegardes et changements de niveau.

### Criteres de sortie

- un niveau peut etre decoupe en zones chargees sans pause longue ;
- projectiles, ennemis et effets peuvent etre reutilises sans references mortes ;
- les ressources quittant une zone deviennent liberables ;
- les autoloads ne sont pas affectes par le dechargement d'une zone.

## 14. P1 - Cameras de gameplay

### Probleme

Le moteur possede deja des cameras a priorite, des blends et une camera third
person avec avoidance. Il manque surtout les briques de feedback et de cadrage
utilisees quotidiennement dans les jeux.

### Perimetre

- impulses et camera shake ;
- profils de blend ;
- zones de confinement ;
- cameras sur rails et points d'interet ;
- composition et offsets dynamiques ;
- zoom et modifications temporaires de lens ;
- integration avec pause et temps non scale ;
- API C++ et JavaScript ;
- split-screen local en P2 si le besoin est confirme.

### Criteres de sortie

- un jeu peut produire des impacts et transitions de camera sans script ad hoc ;
- la camera reste dans les limites d'une zone et evite correctement les obstacles ;
- les effets temporaires se composent sans modifier directement la camera active.

## 15. P1 - Plateforme mobile et casque autonome

### Probleme

Le depot contient des dependances compatibles mobile, mais pas encore de player
Saida Android ou iOS complet. Le support OpenXR desktop ne suffit pas pour livrer
un jeu sur un casque autonome.

### Perimetre

- player Android officiel ;
- cycle de vie pause, reprise, perte de surface et rotation ;
- touch, clavier virtuel, gamepad et haptique ;
- stockage utilisateur ;
- audio mobile ;
- gestion des permissions ;
- budgets memoire et thermique ;
- integration OpenXR Android pour les casques compatibles ;
- profils de qualite runtime ;
- iOS dans une etape ulterieure selon la demande reelle.

### Criteres de sortie

- le projet de reference peut etre installe et joue sur Android ;
- pause et reprise ne perdent ni scene ni sauvegarde ;
- un build Quest autonome utilise le meme gameplay que le build desktop VR ;
- les limites thermiques et memoire sont mesurees sur appareil reel.

## 16. P2 - Determinisme, replay et tests gameplay

### Perimetre

- generateurs aleatoires controlables ;
- enregistrement et rejeu des actions d'input ;
- horloge de simulation clairement separee du rendu ;
- tests headless des scenes et comportements ;
- scenarios de non-regression gameplay ;
- captures de performance reproductibles ;
- diagnostics des allocations et temps par systeme ;
- validation des sauvegardes et migrations ;
- tests de parite entre desktop et Web.

### Criteres de sortie

- un bug gameplay peut etre reproduit a partir d'un flux d'input enregistre ;
- les principaux systemes runtime peuvent etre testes sans renderer ;
- les ecarts de comportement entre plateformes sont detectes automatiquement ;
- les budgets de frame et de memoire ont des tests de non-regression.

## 17. P2 - Services de plateforme

Ces services doivent passer par des interfaces optionnelles et ne jamais devenir
des dependances du coeur du moteur.

### Perimetre

- achievements ;
- cloud save ;
- presence et invitations ;
- ouverture de liens et partage ;
- achats integres pour les plateformes concernees ;
- gestion du consentement et des permissions ;
- implementations nulles propres pour les plateformes non configurees.

## 18. P2 - Fonctionnalites de monde 3D

Ces fonctions elargissent les genres de jeux supportes, mais ne doivent pas
retarder le socle gameplay.

### Perimetre possible

- terrain leger ;
- vegetation instanciee ;
- decals ;
- zones d'occlusion ou de visibilite ;
- origine flottante pour mondes etendus ;
- streaming spatial plus avance ;
- destruction ou modification de decor selon les besoins valides.

Chaque fonctionnalite doit rester optionnelle et conserver un chemin adapte au
Web et au mobile.

## 19. P3 - Reseau et multiplayer

Le multiplayer ne doit commencer qu'apres stabilisation de l'input, du cycle de
scene, des sauvegardes, de la physique et des API gameplay.

### Perimetre initial envisageable

- transport interchangeable ;
- connexion, deconnexion et erreurs ;
- replication d'entites ;
- RPC controles ;
- ownership et autorite ;
- snapshots et interpolation ;
- prediction uniquement pour les cas qui le necessitent ;
- API Web compatible ;
- outils de simulation de latence et perte de paquets.

Il n'est pas necessaire de viser immediatement une infrastructure competitive ou
massivement multiplayer.

## 20. Fonctionnalites a garder hors du coeur

Les elements suivants sont utiles, mais doivent etre fournis sous forme de
modules, exemples ou templates plutot que de complexifier le runtime central :

- inventaire ;
- quetes ;
- dialogues ;
- crafting ;
- statistiques RPG ;
- combat propre a un genre ;
- behavior trees complexes ;
- generation procedurale specifique ;
- systeme de vehicules realistes ;
- visual scripting generaliste.

Saida doit fournir les primitives permettant de les construire proprement :
sauvegarde, reflection, assets, signaux, UI, navigation, animation et scripts.

## 21. Ordre d'execution recommande

### Phase A - Unification du runtime

1. Player Web de jeu complet.
2. Contrats communs de plateforme et diagnostics de capacite.
3. Tests de parite du cycle de scene desktop/Web.

### Phase B - Services indispensables

1. Input multi-peripherique.
2. Stockage utilisateur et sauvegardes.
3. Physique de gameplay unifiee.
4. API JavaScript complete.

Ces chantiers peuvent se chevaucher, mais leurs API doivent etre validees
ensemble afin d'eviter des variantes incompatibles par langage ou plateforme.

### Phase C - Performance et contenu dynamique

1. Cycle de vie des assets.
2. Chargement asynchrone.
3. Scenes additives et streaming.
4. Pooling.

### Phase D - Capacites de jeu 3D

1. Navigation et IA spatiale.
2. Audio complet.
3. Animation de production.
4. UI multi-peripherique et localisation.
5. Cameras de gameplay.

### Phase E - Plateformes et robustesse

1. Android et casque autonome.
2. Determinisme, replay et tests headless.
3. Services de plateforme optionnels.

### Phase F - Extensions

1. Fonctionnalites de monde 3D.
2. Multiplayer si la demande produit le justifie.
3. Modules de gameplay reutilisables hors du coeur.

## 22. Definition d'une premiere version prete pour les developpeurs indes

**État au 2026-07-15 : définition non atteinte.** Les points ci-dessous restent
des gates, pas une description de l'état actuel.

Saida Engine pourra etre considere comme un moteur de jeu indé complet lorsque :

- le meme projet fonctionne sur desktop et Web avec le meme code gameplay ;
- le desktop VR et un casque autonome partagent le meme modele de jeu ;
- clavier, souris, manette, touch et XR passent par un systeme d'actions coherent ;
- le jeu peut sauvegarder progression et preferences ;
- la physique offre des queries et filtres utilisables par tous les objets ;
- un jeu complet peut etre ecrit en C++ ou JavaScript ;
- les ressources peuvent etre chargees et liberees sans bloquer durablement la
  frame ni faire croitre la memoire sans limite ;
- les NPC peuvent naviguer dans un niveau ;
- audio, animation, UI et cameras couvrent les besoins usuels d'un jeu 3D ;
- les principales fonctions possedent des tests runtime et de parite plateforme ;
- les limitations restantes sont explicites, mesurees et documentees.

Ce seuil doit etre atteint avant d'investir fortement dans le multiplayer, les
grands terrains ou des frameworks de gameplay propres a un genre.
