# Points faibles / chantiers a renforcer

Notes pour plus tard. Le moteur a deja une base solide ; ces points concernent surtout la stabilisation produit, la preuve de performance et la differenciation publique.

## 1. Prouver les performances

- Creer des scenes benchmark reproductibles : 1k / 10k / 100k objets, XR, UI RmlUi, physique, animation.
- Exporter un rapport depuis le profiler CPU/GPU/memoire.
- Comparer objectivement les couts frame CPU/GPU avant de revendiquer une meilleure optimisation que Godot / Unity / Unreal.

## 2. Stabiliser le chemin GPU-driven

- Remplacer les `useGpuDriven = false` temporaires du renderer par un vrai flag runtime/setting.
- Stabiliser bindless materials, indirect draw et compute culling.
- Benchmarker le chemin classique vs GPU-driven sur des scenes lourdes.

## 3. Durcir l'export runtime

- Ajouter une vraie couche "Shipping" : icone, metadata exe, version, zip portable, crash log, validation des DLL.
- Garantir que l'export jeu ne contient ni code editeur ni MCP.
- Enrichir le manifest `game.saida` si besoin : version format, main scene, options runtime, plateforme cible.

## 4. Rendre le MCP plus sur pour les agents

- Ajouter des transactions groupees et un mode dry-run.
- Produire un diff de scene avant application.
- Snapshot automatique avant action agent et rollback global.
- Permissions par outil : ecriture fichier, ecriture C++, build, import assets, mutation scene.

## 5. Faire une demo IA forte

- Montrer un agent qui cree une scene jouable complete via MCP : nodes, behaviours, UI, signaux, scenario, export runtime.
- Preparer une video courte et reproductible.
- Le positionnement fort n'est pas "un moteur de plus", mais "un moteur que l'IA sait piloter proprement".

## 6. Solidifier l'asset pipeline

- Renforcer import/reimport, dependances assets, cache, materiaux glTF, textures et packaging.
- Ajouter migrations/validation pour l'asset registry.
- Limiter les cas ou un asset casse silencieusement une scene.

## 7. Monter XR en credibilite produit

- Ajouter culling stereo combine.
- Ajouter overlay/debug XR utile.
- Definir profils de performance Quest / PCVR.
- Tester confort, interactions et budgets 72/90/120 Hz.

## 8. Versionner les formats

- Versionner `.scene`, `.saidaproj`, scenarios et `asset_registry.json`.
- Ajouter migrations automatiques.
- Prevoir compatibilite arriere avant que des agents generent beaucoup de contenu.
