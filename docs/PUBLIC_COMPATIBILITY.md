# Contrat de compatibilité publique V1

Ce document est la source de vérité des formats et API garantis. Un document
persistant écrit par le moteur porte un champ entier `schema`. Pendant la
transition V1, le champ historique `version` est écrit avec la même valeur pour
les consommateurs existants. Un chargeur accepte les schémas anciens qu'il sait
migrer et refuse tout schéma plus récent avec un diagnostic explicite.

## Formats persistants

| Surface | Schéma | Politique |
|---|---:|---|
| `game.saida` | 1 | Stable, migration obligatoire |
| `.saidaproj` | 1 | Stable, migration obligatoire |
| `asset_registry.json` | 1 | Stable, migration obligatoire ; les `AssetID` restent stables |
| `.scene` | 2 | Stable, migration obligatoire |
| `.saidascenario` | 1 | Stable, migration obligatoire |
| `.sclip` | 1 | Stable, migration obligatoire |
| `.sgraph` | 2 | Stable, migration obligatoire |
| `.sretarget` | 2 | Stable, migration obligatoire |
| `.srig` | 1 | Stable, migration obligatoire |
| `.sseq` | 1 | Stable, migration obligatoire |
| `.sanimc` | interne | Cache régénérable, aucune compatibilité garantie |
| `asset_registry.local.json` | interne | Cache local régénérable, aucune compatibilité garantie |
| `pipeline_cache.bin` | interne | Cache GPU régénérable, aucune compatibilité garantie |

Les anciennes formes `.saidaproj` clé-valeur, scènes sans `schema`, registres
sans enveloppe et scénarios sans `schema` sont les migrations V0 de référence.
Elles restent dans le corpus de tests. Une migration ne réécrit jamais le
fichier source pendant un simple chargement.

Le corpus de rétro-compat vit dans `tests/fixtures/compat/` (fichiers figés,
jamais régénérés) et est chargé à chaque build par `saida_compat_corpus_tests` :
toute rupture de chargement d'un ancien schéma fait échouer la CI, et le test
vérifie qu'un chargement ne modifie pas les octets du document source.

## Assets média

Audio : **`.ogg` (Vorbis) est le format recommandé et fortement conseillé**
pour tout contenu de jeu. `.wav` reste accepté en dépannage. FLAC et MP3 ne
sont pas compilés dans le moteur.

## API JavaScript stable

Les globals gameplay suivants sont publics :

- `node`: `getName`, `setName`, `getPosition`, `setPosition`, `translate`,
  `setEnabled`, `queueFree`, groupes, `on`, `emit` ; sur un `UITextNode` :
  `setText`, `getText` ;
- `time`: `delta`, `elapsed`, `wait`, `every`, `tween`, `cancel` ;
- `input`: actions, axes, vecteurs, position et delta souris ; `inject(action,
  strength)` est un outil de test/CI (pilote une action sans périphérique) ;
- `audio`: `play(alias)` — joue un alias audio déclaré par le projet (no-op
  explicite sur les plateformes sans backend audio) ;
- `tree`: `changeScene`, `reloadScene`, `quit`, pause ;
- `assets.load(path, priority)`: retourne immédiatement un handle. Le handle
  expose `state`, `ready`, `failed`, `error`, `size`, `id` et `release` ;
  `assets.stats()` retourne les compteurs du loader (`live`, `queued`,
  `loading`, `ready`, `failed`, `residentBytes`, `budgetBytes`) — diagnostics
  de fuite ;
- `storage`: persistance par slot — `save(slot, jsonString)`, `load(slot)`
  (chaîne ou `null`), `has(slot)`, `remove(slot)`. Le slot respecte
  `[A-Za-z0-9_-]{1,64}` ; les données vivent dans `saves/<slot>.json` sous la
  racine du projet (sur le Web : IndexedDB via IDBFS, persistant entre
  sessions). Le moteur stocke des chaînes opaques : la sérialisation
  (`JSON.stringify`/`parse`) appartient au jeu.

`assets.load` ne promet jamais un chargement bloquant. Les états publics sont
`queued`, `loading`, `ready` et `failed`. Les priorités publiques sont `low`,
`normal`, `high` et `critical`. Le handle libère sa référence au garbage
collection ou par `release()`.

Toute suppression ou modification incompatible d'une API JavaScript passe par
au moins une version publiée de dépréciation. Durant cette version, l'ancien
appel reste fonctionnel et émet un warning une seule fois par contexte.

## API C++ réfléchie

Les noms de types et de propriétés produits par le registre de réflexion sont
des identifiants persistants. Leur renommage exige un alias de migration testé.
L'ABI binaire C++ et les headers internes ne sont pas gelés : les extensions
natives sont recompilées avec la version du moteur correspondante.

## Règle de changement

Toute modification d'une surface stable doit fournir dans le même changement :

1. un incrément de schéma ;
2. une migration de chaque schéma encore supporté ;
3. un fixture ancien immuable ;
4. un test de chargement et de refus du schéma futur ;
5. une entrée dans les notes de version.
