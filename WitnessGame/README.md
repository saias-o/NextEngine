# WitnessGame — le jeu témoin de la V1

Projet d'exemple **et instrument de mesure** du chantier 1 de
[PLAN_V1_ENGINE.md](../PLAN_V1_ENGINE.md) : un petit jeu vertical qui traverse
les sous-systèmes du moteur. Chaque friction rencontrée en le construisant ou
en l'exportant est un bug de V1 — elles sont consignées dans
[docs/WITNESS_GAME.md](../docs/WITNESS_GAME.md).

Le projet couvre beaucoup de sous-systèmes, mais ne prouve pas encore une V1
ship-ready : UI du player web, communication cross-node/autoload JS, séquence
`.sseq`, bouton Build UI et machine vierge restent des gates ouvertes.

## Le jeu

Deux scènes en greybox (cube builtin uniquement) :

- **hub.scene** — salle de départ. Un joueur (`CharacterBody` +
  `Character`, ZQSD/WASD + Espace), une caméra troisième personne
  (`CameraFollow`), un point de sauvegarde (dalle verte) et une porte bleue
  vers l'arène.
- **arena.scene** — 3 reliques dorées à ramasser (Area + `Rotator` +
  particules), des caisses physiques (`RigidBody`) à bousculer, une porte
  orange pour revenir au hub.

L'état (reliques, sauvegardes) vit dans l'autoload JS `GameState`
(`scripts/game_state.mjs`) et persiste via l'API `storage`
(`saves/witness.json`, à côté de l'exe une fois packagé).

## Sous-systèmes traversés

| Sous-système | Où |
|---|---|
| Scènes + changement de scène | hub ⇄ arena via `tree.changeScene` (door.js) |
| Physique | sols/murs StaticBody, caisses RigidBody, joueur CharacterBody, triggers Area |
| Scripts JS | ScriptBehaviours + 1 autoload module `.mjs` + pilote E2E |
| Signaux → JS | `node.on("bodyEntered", …)` sur les Area (pickup/door/savepoint) |
| Particules | `ParticleSystem` sur chaque relique |
| UI | `UICanvasNode` + `UITextNode` (HUD piloté par `node.setText`) |
| Save/load | API `storage` (slot `witness`) |
| Animation | totem riggé skinné (`assets/models/totem.gltf`) + graphe `anim/locomotion.sgraph` (Idle ⇄ Walk par le paramètre `speed`) |
| Audio | `audio.play` sur les alias `.ogg` `pickup`/`save` |

## Régénérer le contenu

```sh
python WitnessGame/gen_witness.py     # scènes (ids stables dérivés des noms)
python WitnessGame/gen_character.py   # assets/models/totem.gltf
```

Modifier les générateurs, jamais les `.scene`/`.gltf` à la main.

## Test E2E sans clavier

```sh
bash tools/witness_e2e.sh   # packe + lance le runtime + attend [E2E] PASS
```

## Lancer

- Éditeur : `./build/bin/SaidaEngine.exe --project WitnessGame/WitnessGame.saidaproj`
  puis Play.
- Packagé : Build Settings → Export Windows, ou le smoke test manuel décrit
  dans docs/WITNESS_GAME.md.
