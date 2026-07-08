# Fixture de déterminisme du fold (P0.1/P0.2)

`expected.json` est le snapshot produit par `saida_tool apply-ops base.json
ops.json` **sous Windows (MSYS2/GCC)**, versionné comme référence. La CI Linux
refait le même fold et exige une sortie **byte-identique** — c'est la preuve
que la reconstruction de scène de la plateforme (gateway/API) est indépendante
de l'OS qui a exécuté le fold.

Les ops couvrent : set_property, create_node, set_transform (floats non
triviaux, quaternion à précision maximale), reparent_node, rename_node,
set_scene_setting.

Vérification manuelle :

```sh
./build/bin/saida_tool apply-ops tests/fixtures/fold-determinism/base.json \
  tests/fixtures/fold-determinism/ops.json --out /tmp/out.json
cmp /tmp/out.json tests/fixtures/fold-determinism/expected.json
```

À régénérer uniquement si le format de snapshot change (bump de
`formatVersion`) — jamais pour « faire passer » une divergence, qui est un vrai
bug de déterminisme.
