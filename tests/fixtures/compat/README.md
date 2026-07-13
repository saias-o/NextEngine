# Corpus de rétro-compatibilité (IMMUABLE)

Ces fichiers sont des documents *figés* dans d'anciens formats du moteur.
Ils sont chargés par `saida_compat_corpus_tests` à chaque build : toute rupture
de chargement fait échouer la CI (cf. `docs/PUBLIC_COMPATIBILITY.md`).

Règles :

- **Ne jamais modifier ni régénérer un fichier de ce dossier.** Un nouveau
  schéma s'ajoute comme *nouveau* fixture (`*_vN.*`), les anciens restent.
- Un chargement ne doit jamais réécrire le fichier source ; le test vérifie
  que les octets sont identiques après chargement.
- Quand le jeu témoin de la V1 sera publié, son package sera gelé ici aussi.

Contenu :

| Fichier | Format | Schéma |
|---|---|---|
| `project_v0.saidaproj` | projet clé=valeur historique | v0 (legacy) |
| `project_v1.saidaproj` | projet JSON | 1 |
| `asset_registry_v0.json` | registre plat sans enveloppe | v0 (legacy) |
| `asset_registry_v1.json` | registre avec enveloppe | 1 |
| `scene_v0.scene` | scène sans champ `schema` | v0 (legacy) |
| `scene_v2.scene` | scène courante | 2 |
| `scenario_v0.saidascenario` | scénario sans `schema` | v0 (legacy) |
| `scenario_v1.saidascenario` | scénario courant | 1 |
| `game_v0.saida` | boot manifest sans `schema` | v0 (legacy) |
| `game_v1.saida` | boot manifest courant | 1 |
