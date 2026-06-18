# AutoLOD

Générateur de LOD **hors-ligne** pour meshs statiques `glTF`/`GLB` (rochers, bâtiments, props).
Tu donnes un LOD0, il te rend le même asset enrichi de LOD1..N.

Code MIT. Dépend de [meshoptimizer](https://github.com/zeux/meshoptimizer) (MIT) et
[tinygltf](https://github.com/syoyo/tinygltf) (MIT), récupérés automatiquement par CMake.

## Build (Windows / MSYS2 UCRT64)

Le `bin` d'UCRT64 **doit** être dans le PATH, sinon le linker plante (`ld returned 116`)
car ses DLL runtime ne se résolvent pas.

Bash :
```sh
export PATH="/c/msys64/ucrt64/bin:$PATH"
cmake -S . -B build -G Ninja
cmake --build build
```

PowerShell :
```powershell
$env:PATH = "C:\msys64\ucrt64\bin;$env:PATH"
cmake -S . -B build -G Ninja
cmake --build build
```

## Usage

```sh
# génère sphere_lod.glb à côté de l'entrée
./build/autolod.exe rocher.glb

# sortie explicite
./build/autolod.exe rocher.glb rocher_lod.glb

# ratios/erreurs custom (4 LOD ici)
./build/autolod.exe batiment.glb out.glb --ratios 0.7,0.4,0.2,0.05 --errors 0.005,0.02,0.05,0.1

# assets modulaires emboîtables : ne bouge pas les bords ouverts
./build/autolod.exe mur_modulaire.glb out.glb --lock-border

# BAKE de normal map (qualité AAA) : re-projette le détail haute-poly sur les LOD
./build/autolod.exe casque.glb casque_lod.glb --bake --bake-res 1024

# LOD séparés autonomes + bake (workflow Unity LODGroup)
./build/autolod.exe casque.glb casque_lod.glb --split --bake --bake-res 2048

# PROXY : LOD lointain ultra-léger (re-dépliage + atlas complet)
./build/autolod.exe casque.glb casque_lod.glb --proxy --ratios 0.6,0.3,0.1,0.03

# asset de test pour valider l'install (sphère ~8k tris)
./build/autolod.exe --gen-test sphere.glb

# debug : extraire toutes les textures d'un GLB
./build/autolod.exe --dump-images casque_lod.glb prefix_
```

Options : `--ratios`, `--errors`, `--lock-border`, `--uv-weight(s)`, `--normal-weight`,
`--bake`, `--bake-res`, `--bake-cage`, `--split`.

### Choisir l'agressivité

Le contrôle principal est `--ratios` : la fraction de triangles conservée par niveau.

```sh
# plus agressif (moins de triangles)
./build/autolod.exe casque.glb out.glb --bake --ratios 0.5,0.2,0.05

# plus conservateur (plus de triangles, plus fidèle)
./build/autolod.exe casque.glb out.glb --bake --ratios 0.75,0.5,0.25
```

Note importante sur le **poids UV** :
- **sans `--bake`**, le défaut protège fortement les UV (poids 1 / 3 / 10) → les LOD
  agressifs gardent *plus* de triangles que le ratio demandé, pour éviter la distorsion
  de texture.
- **avec `--bake`**, le détail est restauré par la normal map, donc le poids UV passe
  automatiquement à 1 : les ratios sont respectés à la lettre (LOD3 = vraiment ~9% par
  défaut). C'est pourquoi le bake donne à la fois *peu de triangles* **et** un beau rendu.

Tu peux forcer le poids UV dans les deux cas avec `--uv-weights a,b,c`.

### Les proxy LOD (`--proxy`) — LOD lointains ultra-légers

Sous ~0.15 de ratio, la décimation classique ne peut plus réduire sans étirer ou
déchirer les UV d'origine. Le mode `--proxy` change complètement d'approche pour ces
niveaux très agressifs (ratio < 0.15) :

1. Soudure par **position seule** (connecte le maillage à travers les coutures) →
   décimation géométrique très profonde possible (quelques centaines de triangles).
2. Normales lisses recalculées sur le maillage décimé.
3. **Re-dépliage UV propre** via `xatlas` (nouvel atlas `[0,1]`).
4. **Bake de TOUTES les textures** du matériau (albédo + normal + metallic-roughness +
   occlusion + emissive) depuis le LOD0 vers ce nouvel atlas, par raycasting.
5. Sortie : un mesh ultra-léger + un seul matériau atlasé autonome.

Résultat : 200–600 triangles mais qui **ressemblent** au LOD0 de loin, car albédo ET
normales sont bakés par texel. Rendu en glTF/Unity standard (pas de shader custom,
contrairement aux impostors). `--proxy` implique `--bake`.

Note : `--proxy` ne s'applique qu'aux niveaux de ratio < 0.15 ; tes LOD1/2/3 plus denses
gardent le bake normal classique (UV d'origine préservées).

### Le bake de normal map (`--bake`)

C'est la technique qui permet à un LOD3 bas-poly d'**avoir l'air** détaillé sous
l'éclairage. Pour chaque LOD ciblé (par défaut ratio ≤ 0.40, donc LOD2/LOD3) :

1. Rastérisation du LOD dans son layout UV (préservé depuis le LOD0).
2. Pour chaque texel, raycasting vers la surface haute-poly (BVH + Möller-Trumbore).
3. **Composite** : la normal map d'origine est échantillonnée au point touché, son
   détail (rivets, rayures...) est recombiné avec la géométrie haute-poly, puis
   re-exprimé dans la base tangente du LOD → la normal map bakée reproduit les
   normales monde du LOD0 + son détail, sur une géométrie pauvre.
4. Tangentes (`TANGENT`) écrites sur le LOD pour que le moteur lise la map dans la
   même base que celle du bake.
5. Padding/dilatation des bords d'îlots UV pour éviter les coutures.

Sans normal map source sur le matériau, le bake retombe sur les normales
géométriques seules. Chaque LOD baké reçoit son propre matériau + sa propre map.

## Ce que fait l'outil

1. Charge le glTF/GLB (matériaux, textures, hiérarchie **préservés** : on modifie le
   modèle en place, les textures passent en pass-through).
2. Par primitive : **weld** des sommets (`meshopt_generateVertexRemap`) — indispensable,
   les exports dupliquent les sommets par triangle et bloquent la décimation.
3. Décimation **QEM préservant les attributs** (`meshopt_simplifyWithAttributes`) :
   positions + normales + UV, avec poids (UV poussés pour éviter le stretching de texture).
4. Fallback **sloppy** (`meshopt_simplifySloppy`) pour les LOD lointains que le QEM ne
   réduit pas assez.
5. `meshopt_optimizeVertexCache` sur chaque index buffer.
6. Écriture des LOD via l'extension **`MSFT_lod`** + `MSFT_screencoverage`, plus des
   meshs/nodes nommés `*_LOD1/2/3`. LOD0 reste le mesh d'origine intact.

L'erreur réelle (en unités monde) est reportée par LOD pour chaque mesh.

## Limites connues (volontaires, scope "petit outil")

- LOD0 conserve son vertex buffer d'origine ; les LOD1+ ont leur propre vertex buffer
  soudé (léger surcoût mémoire, simplicité maximale).
- Meshs **skinnés** ignorés (pas de pondération des joints).
- Le bake suppose un dépliage UV en îlots dans `[0,1]` (tuiles gérées par wrap). Pas de
  re-dépliage : on réutilise les UV d'origine (valable car la décimation les préserve).
- Pas encore de **proxy mesh** / remesh enveloppant ni de **billboard/imposteur** pour
  les LOD très lointains (voir pistes ci-dessous).
- Index 32 bits en sortie (pas de downcast 16 bits).
- Bake mono-thread (suffisant : ~0,3 s pour un casque 15k tris en 1024²).

## Pistes pour aller plus loin

- **Bake AO / courbure** en plus des normales (même infrastructure de raycasting).
- **Atlas multi-matériaux** : fusionner via `xatlas` pour un LOD3 à 1 seul draw call.
- **Imposteurs** : octahedral impostors pour la végétation/objets très lointains.
- **Clusters façon Nanite** : `nv_cluster_lod_builder` si le moteur cible une géométrie
  virtualisée GPU.
