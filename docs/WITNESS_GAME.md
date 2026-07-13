# Jeu témoin — journal des frictions V1

Le jeu témoin ([WitnessGame/](../WitnessGame/README.md)) est l'instrument de
mesure du chantier 1 de PLAN_V1_ENGINE : chaque friction rencontrée en le
construisant, en le packageant ou en le lançant est un bug de V1. Ce document
est le journal de bord. Convention : ✅ corrigé, 🔴 ouvert, 🟡 contournement
assumé.

## Frictions découvertes (session du 2026-07-13)

| # | État | Friction | Détail / correctif |
|---|---|---|---|
| 1 | ✅ | Les signaux d'`Area` n'étaient pas réfléchis | un ScriptBehaviour JS ne pouvait pas réagir à son trigger. Corrigé : `bodyEntered`/`bodyExited` réfléchis (payload = nom du nœud entrant), `node.on("bodyEntered", …)` fonctionne. |
| 2 | ✅ | Aucune API de save/load dans le moteur | le plan exige save/load dans le jeu témoin ; rien n'existait. Corrigé : API JS `storage` (save/load/has/remove par slot, `saves/<slot>.json` sous la racine projet), documentée dans PUBLIC_COMPATIBILITY.md. |
| 3 | ✅ | Les autoloads JS déclarés dans le `.saidaproj` étaient ignorés | `autoloads: {GameState: "scripts/game_state.mjs"}` produisait « unknown behaviour type ». Corrigé : valeur en `.js`/`.mjs` → Node + ScriptBehaviour (`SceneTree::registerAutoloadScript`). |
| 4 | ✅ | L'export Windows copiait les shaders depuis un dossier inexistant | `BuildExporter` lisait `build/bin/shaders` alors qu'ils sont dans `build/shaders` (`SAIDA_SHADER_DIR`) : tout export échouait à l'étape 3. Corrigé + validé par un packaging manuel bouté sur machine locale. |
| 5 | ✅ | Hooks de module `.mjs` silencieusement ignorés s'ils ne sont pas `export` | un `function onReady()` de portée module ne fait rien, sans diagnostic. Contourné (export) ; un warning « script chargé mais aucun hook trouvé » serait un vrai correctif. |
| 6 | 🟡 | Pas de communication inter-nœuds en JS | pas d'accès aux autoloads (`tree.autoload`), pas de requête de groupe, pas de signaux cross-node depuis JS. Le jeu fait transiter l'état par `storage` (fichier) — fonctionnel mais inacceptable pour la V1. L'API JS doit exposer au minimum l'accès aux autoloads. |
| 7 | 🔴 | Pas de binding JS pour muter l'UI | impossible de changer le `text` d'un `UITextNode` depuis un script : le HUD ne peut pas afficher le score (il trace en console). |
| 8 | 🔴 | Player web : types de nœuds manquants | le player web n'enregistre ni physique (StaticBody/RigidBody/CharacterBody/Area/CollisionShape), ni UI, ni Character/CameraFollow : le jeu témoin ne peut pas y tourner. Bloque le critère « même gameplay desktop et Web ». |
| 9 | 🔴 | Animation absente du jeu témoin | aucun modèle riggé dans le repo (hors fixtures de test) ; le jeu ne traverse pas encore `.sgraph`/`.sseq`. Il faut un petit personnage riggé committé. |
| 10 | 🔴 | Audio absent du jeu témoin | aucun asset audio dans le repo ; `AudioSourceBehaviour` existe mais n'est pas exercé. Générer un blip .wav et le brancher. |
| 11 | 🔴 | Pas d'injection d'input headless | impossible de tester pickups/portes sans jouer à la main ; le smoke test s'arrête à « boote, scripts vivants, storage écrit ». Une injection d'actions (`Input::submit…`) rendrait le parcours complet testable en CI. Contournement utilisé : déplacer le spawn du joueur dans le trigger à tester. |

## Frictions découvertes au playtest (2026-07-13/14)

| # | État | Friction | Détail / correctif |
|---|---|---|---|
| 12 | ✅ | Les scripts d'un projet chargé étaient introuvables dans l'éditeur | `resolveScriptPath` essayait absolu → cwd → racine du *repo*, jamais la racine du `.saidaproj` chargé (BeachDemo n'a pas de scripts, personne ne l'avait vu). Corrigé : `setActiveProjectRoot()` posée par `Project::load`, consultée en premier. |
| 13 | ✅ | Les triggers `Area` ne voyaient jamais le joueur | le `CharacterVirtual` de Jolt n'existe pas dans la broadphase : sans inner body, capteurs et raycasts l'ignorent. Corrigé : `mInnerBodyShape` + layer MOVING ; l'inner body hérite du userData → le dispatch retrouve le `CharacterBodyNode`. |
| 14 | ✅ | **Tout `changeScene`/`queueFree` crashait la frame même** | `Scene::update` aplatit meshes/lights en caches au début de l'update ; le swap différé détruisait les nœuds après l'update ; `Renderer::gatherScene` consommait ensuite les pointeurs morts (SIGSEGV, repro déterministe en spawnant le joueur dans la porte). Corrigé : `Scene::refreshHierarchy()` appelé en fin de `SceneTree::applyDeferred`. Validé headless : porte → arène → pickup → save, exit 0. |
| 15 | 🟡 | Halos colorés autour du viewport signalés au premier lancement | non reproduits après recompilation (binaire probablement obsolète, cf. piège `.ninja_log`). À surveiller ; rouvrir si ça revient. |
| 16 | ✅ | Visualisation des colliders spatialement fausse en mode éditeur | `drawColliderGizmos` projetait sur la fenêtre entière (`GetMainViewport`) au lieu du rectangle du nœud central du dock (`viewportPos_/viewportSize_`) utilisé par le gizmo de sélection — décalage dépendant de la caméra dès que des panneaux sont dockés. Corrigé : même mapping que la sélection. |

Playtest complet validé le 2026-07-14 : déplacement, physique (murs, caisses),
triggers, hub ⇄ arène, 3 reliques ramassées, sauvegarde persistée
(`saves/witness.json` : `relics=3`).

## Smoke test packagé (validé)

Packaging manuel identique à `BuildExporter::exportWindowsBuild` (exe runtime
renommé + glfw3.dll + `shaders/` + données projet + `game.saida`), puis
lancement avec un autoload de test qui appelle `tree.quit()` après 8 s :

- exit code 0, aucun warning/erreur JS ;
- l'autoload module `GameState` monte et initialise le slot ;
- le HUD lit `storage` chaque 0,5 s ;
- `saves/witness.json` est écrit à côté de l'exe.

Restes du chemin ship : passer par le **bouton Build de l'UI** (plutôt que le
packaging manuel), tester sur machine vierge, export Web du même projet.
