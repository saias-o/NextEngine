# Points faibles / chantiers a renforcer

État resynchronisé le 2026-07-15. Le moteur a une base large, mais certains
points ci-dessous sont des blocages d'intégrité/sécurité et non de simples
améliorations « pour plus tard ».

## 0. Priorité absolue : préserver les projets

- Aligner les registres natif, authoring web, loader web et headless.
- Faire un round-trip canonique de tous les types/propriétés/behaviours ; aucun
  fallback `Node` ou behaviour ignoré ne doit être silencieux.
- Corriger le fold Mesh sans `ResourceManager` et interdire `skipInvalid` pour
  un snapshot durable.
- Lier runtime web, WASM authoring, `saida_tool` et formats dans un release
  manifest hashé.

## 1. Prouver les performances

- Creer des scenes benchmark reproductibles : 1k / 10k / 100k objets, XR, UI RmlUi, physique, animation.
- Exporter un rapport depuis le profiler CPU/GPU/memoire.
- Comparer objectivement les couts frame CPU/GPU avant de revendiquer une meilleure optimisation que Godot / Unity / Unreal.

## 2. Stabiliser le chemin GPU-driven

- Remplacer les `useGpuDriven = false` temporaires du renderer par un vrai flag runtime/setting.
- Stabiliser bindless materials, indirect draw et compute culling.
- Benchmarker le chemin classique vs GPU-driven sur des scenes lourdes.

## 3. Durcir l'export runtime

- Icône, metadata exe, version et packaging existent. Restent : preuve par le
  bouton Build sur machine vierge, archive/installeur signé, crash log,
  validation des DLL et rollback de release.
- Garantir que l'export jeu ne contient ni code editeur ni MCP.
- Enrichir le manifest `game.saida` si besoin : version format, main scene, options runtime, plateforme cible.

## 3bis. Sandboxing runtime

- Ajouter un interrupt/deadline QuickJS et une borne sur la boucle des pending
  jobs.
- Confiner modules et fichiers au package du projet ; aucun chemin absolu ou
  cwd hôte ne doit être visible à du contenu tiers.
- Isoler le player marketplace de l'origine/processus de confiance.

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

- Les champs `schema`, migrations, refus des versions futures et fixtures
  existent pour les principales surfaces. Le contrat reste **candidat V1**.
- Ajouter des fixtures sémantiques de round-trip entre desktop, player web,
  authoring web et headless ; la simple lecture JSON ne suffit pas.
- Prévoir la migration des snapshots Saida et des noms/types réfléchis avant que
  des agents génèrent beaucoup de contenu.

## 9. Corriger les capacités et l'input

- Implémenter les axes gamepad avant d'annoncer Gamepad dans `PlatformCaps`.
- Terminer bindings touch/rebinding/per-device et tester reconnect/hotplug.
- Transformer les capacités en tests de comportement, pas seulement en bits au
  boot.

## 10. Clarifier la release et les licences

- Le dépôt contient GPL-3.0 ; ajouter notices copyright/SPDX et confirmer les
  obligations des dépendances, assets et modèles.
- Ne pas employer « stable V1 » avant fermeture de `PLAN_V1_ENGINE.md`.
- Produire SBOM, provenance de build, signatures et notes de migration.
