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
| 7 | ✅ | Pas de binding JS pour muter l'UI | corrigé : `node.setText`/`getText` quand le script est attaché à un `UITextNode` ; le HUD affiche le score à l'écran. |
| 8 | 🟡 | Player web : types de nœuds manquants | **parité gameplay livrée** : Jolt en wasm (job system mono-thread, `SAIDA_NO_PHYSICS` réservé au viewer d'authoring), physique complète + Character/CameraFollow, **audio miniaudio (Web Audio)** avec `AudioSourceBehaviour` enregistré et `audio.play` réel, **saves persistantes** (`saves/` sur IDBFS : syncfs au boot + flush après `storage.save`, vérifié dans IndexedDB après reload), AssetLoader qui draine toute sa file par frame. E2E navigateur `[E2E] PASS` (porte + relique). Reste **l'UI seule** : `UIRenderer` non porté au RHI WebGPU, nœuds UI dégradés en Node générique, `setText` no-op signalé une fois. |
| 9 | ✅ | Animation absente du jeu témoin | corrigé : personnage riggé committé (`assets/models/totem.gltf`, généré par `gen_character.py` — 3 os, clips Idle/Walk, mesh skinné, buffer base64), chargé via `importedFrom` sur le joueur ; graphe `anim/locomotion.sgraph` (schéma 2) appliqué par la propriété `graph` de Character, qui alimente le paramètre `speed` du blackboard au lieu de piloter les clips. Reste : une séquence `.sseq` (cinématique d'intro) non traversée. |
| 10 | ✅ | Audio absent du jeu témoin | corrigé : API JS `audio.play(alias)` + assets **.ogg** committés (`assets/audio/pickup.ogg`, `save.ogg` — .ogg est le format recommandé, cf. PUBLIC_COMPATIBILITY) joués au ramassage et à la sauvegarde. |
| 11 | ✅ | Pas d'injection d'input headless | corrigé : `Input::injectAction(action, strength)` (combinée aux bindings réels, mêmes fronts justPressed/justReleased) + binding JS `input.inject`. `tools/witness_e2e.sh` packe le jeu, ajoute l'autoload `e2e_driver.js` et greppe `[E2E] PASS` : porte traversée + relique ramassée sans clavier. |

## Frictions découvertes au playtest (2026-07-13/14)

| # | État | Friction | Détail / correctif |
|---|---|---|---|
| 12 | ✅ | Les scripts d'un projet chargé étaient introuvables dans l'éditeur | `resolveScriptPath` essayait absolu → cwd → racine du *repo*, jamais la racine du `.saidaproj` chargé (BeachDemo n'a pas de scripts, personne ne l'avait vu). Corrigé : `setActiveProjectRoot()` posée par `Project::load`, consultée en premier. |
| 13 | ✅ | Les triggers `Area` ne voyaient jamais le joueur | le `CharacterVirtual` de Jolt n'existe pas dans la broadphase : sans inner body, capteurs et raycasts l'ignorent. Corrigé : `mInnerBodyShape` + layer MOVING ; l'inner body hérite du userData → le dispatch retrouve le `CharacterBodyNode`. |
| 14 | ✅ | **Tout `changeScene`/`queueFree` crashait la frame même** | `Scene::update` aplatit meshes/lights en caches au début de l'update ; le swap différé détruisait les nœuds après l'update ; `Renderer::gatherScene` consommait ensuite les pointeurs morts (SIGSEGV, repro déterministe en spawnant le joueur dans la porte). Corrigé : `Scene::refreshHierarchy()` appelé en fin de `SceneTree::applyDeferred`. Validé headless : porte → arène → pickup → save, exit 0. |
| 15 | 🟡 | Halos colorés autour du viewport signalés au premier lancement | non reproduits après recompilation (binaire probablement obsolète, cf. piège `.ninja_log`). À surveiller ; rouvrir si ça revient. |
| 16 | ✅ | Visualisation des colliders spatialement fausse en mode éditeur | `drawColliderGizmos` projetait sur la fenêtre entière (`GetMainViewport`) au lieu du rectangle du nœud central du dock (`viewportPos_/viewportSize_`) utilisé par le gizmo de sélection — décalage dépendant de la caméra dès que des panneaux sont dockés. Corrigé : même mapping que la sélection. |

## Frictions découvertes au portage web (2026-07-14)

| # | État | Friction | Détail / correctif |
|---|---|---|---|
| 17 | ✅ | Onglet caché = jeu gelé | la boucle emscripten est pilotée par `requestAnimationFrame`, suspendu quand l'onglet n'est pas visible — un harnais E2E n'avance jamais. Corrigé : `?smoke` dans l'URL bascule la boucle sur timer 30 fps. |
| 18 | ✅ | Tous les scripts JS crashaient au chargement sur wasm | « Maximum call stack size exceeded » : la pile wasm d'emscripten (64 Ko par défaut) est plus petite que la limite QuickJS (1 Mo). Corrigé : `-sSTACK_SIZE=4MB` + limite QuickJS à 256 Ko sur `__EMSCRIPTEN__`. |
| 19 | 🟡 | Dispatch des autoloads dupliqué | `Engine::mountWorld` et `web/player/main.cpp` réimplémentent chacun le tri `.scene`/`.js`/type — le web avait raté le support des scripts. Les deux sont alignés ; à factoriser dans `SceneTree`. |

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
