# Plan — Spike « Live-Edit Web » (Phase A.5, GO / NO-GO)

> **Statut : à faire.** C'est le point de décision de toute l'intégration
> SaidaEngine ↔ plateforme Saida. Réf. mandat :
> [ARCHITECTURE_PRODUCTION_CLAUDE.md](ARCHITECTURE_PRODUCTION_CLAUDE.md) §0
> (invariants) et §3.5 (runtime d'édition B). À valider **avant** de construire
> la Collaboration Gateway (Phase C du mandat).

---

## 1. Objectif

Prouver — sur du vrai code moteur, pas une maquette — qu'un **runtime web
WASM/WebGPU peut recevoir une SaidaOp, muter le scene-graph vivant et re-render,
sans recompiler ni re-exporter le moteur**.

C'est l'invariant 0.2 rendu concret. S'il ne tient pas, il n'y a pas d'éditeur
web SaidaEngine : on retomberait sur « re-exporter le jeu à chaque modif », ce
qui est inutilisable pour éditer.

**Question à trancher** : peut-on lier l'*authoring-core* (SaidaOpApplier +
Validator + Manifest, opérant sur `Node`/`Scene` réels avec propriétés
réfléchies) dans le build Emscripten, et **à quel coût de taille wasm** ?

---

## 2. État des lieux du code (ce sur quoi on branche)

Le runtime web existe déjà et rend la vraie scène :
[web/runtime/main.cpp](web/runtime/main.cpp).

Points structurants à connaître :

- entrée Emscripten : `main()` → `rhi::Device::requestAsync(...)` →
  `initRuntime()` → `emscripten_set_main_loop(frame, 0, false)` ;
- objets vivants dans `gApp` : `scene` (`std::unique_ptr<Scene>`),
  `renderer`, `resources`, `camera` ;
- boucle : `frame()` fait `scene->update(dt)` puis
  `renderer->drawFrame(*scene, camera, nullptr)` ;
- nodes gérés : `MeshNode`, `LightNode`, `WaterNode`, `Camera`
  (transform via `n.transform().position/rotation/scale`).

**La tension centrale** : ce runtime charge la scène avec un **loader JSON
réduit** (commentaire d'en-tête du fichier : *« so the web build stays free of
the editor's full SceneSerializer/reflection/GLTF stack »*). Autrement dit, il
**évite délibérément** la couche réflexion. Or `set_property` / `add_behaviour`
d'une SaidaOp ont **besoin** de cette réflexion pour muter un node par nom de
propriété. Le spike doit mesurer le coût de la réintroduire dans le wasm — c'est
tout l'enjeu du GO/NO-GO.

L'authoring-core à extraire vit aujourd'hui dans
[src/mcp/McpBridge.cpp](src/mcp/McpBridge.cpp) (~976 lignes) : c'est la surface
d'édition LLM déjà validée (create/delete/rename/reparent/set_transform/
set_property/add_behaviour/connect_signal/write_script/write_ui/import_asset).

---

## 3. Ce que le build web d'édition inclut / exclut

| Brique | Runtime de jeu (A) | Runtime d'édition (B, ce spike) |
|---|---|---|
| Renderer WebGPU (rhi::webgpu) | ✅ | ✅ |
| Scene / Node / behaviours / QuickJS | ✅ | ✅ |
| **`src/authoring/` (SaidaOpApplier/Validator/Manifest)** | ⛔ | ✅ **inclus** |
| **Réflexion de propriétés** (set_property) | ⛔ (loader réduit) | ✅ **inclus** |
| ImGui / éditeur desktop | ⛔ | ⛔ |
| Serveur MCP (transport stdio) | ⛔ | ⛔ (le transport devient JS/WS) |
| OpenXR | ⛔ | ⛔ |

Le point non négociable : **l'authoring-core est du code partagé** (desktop +
headless + web-B), pas une copie. Le transport seul diffère (MCP stdio en
desktop, binding JS/WebSocket en web).

---

## 4. Surface du binding `applyOp`

Binding minimal, string-in / string-out (JSON), friendly `ccall`/`cwrap` — plus
simple qu'embind pour un spike. Le C++ ne connaît ni WebSocket ni React : il
reçoit une op JSON, l'applique à `gApp.scene`, renvoie un résultat JSON.

### 4.1 Côté C++ (nouveau : `web/runtime/edit_bindings.cpp` ou dans `main.cpp`)

```cpp
#include <emscripten.h>
#include "authoring/SaidaOpApplier.hpp"   // core partagé, pas une copie
#include "authoring/SaidaOpValidator.hpp"
#include "authoring/EngineManifest.hpp"

extern "C" {

// Applique une SaidaOp JSON à la scène vivante.
// Retour JSON : {"ok":true,"diff":{...}} ou {"ok":false,"error":"..."}.
// Le pointeur retourné est un buffer statique/thread-local ; le JS le lit
// immédiatement via UTF8ToString puis n'en garde pas la propriété.
EMSCRIPTEN_KEEPALIVE
const char* saida_apply_op(const char* opJson) {
    static std::string out;
    out = saida::authoring::applyOpJson(
        *gApp.scene, *gApp.resources, opJson);  // valide PUIS applique
    return out.c_str();
}

// Exporte l'EngineManifest (types/behaviours/propriétés/signaux/versions).
EMSCRIPTEN_KEEPALIVE
const char* saida_engine_manifest() {
    static std::string m = saida::authoring::buildEngineManifest().dump();
    return m.c_str();
}

// Sérialise l'état courant de la scène (pour resync / vérif round-trip).
EMSCRIPTEN_KEEPALIVE
const char* saida_scene_snapshot() {
    static std::string s;
    s = saida::authoring::serializeScene(*gApp.scene);  // == SceneSerializer
    return s.c_str();
}

} // extern "C"
```

`applyOpJson` doit, dans cet ordre : (1) parser, (2) valider contre le manifest
+ l'état courant (invariant : refuse type/behaviour/propriété inconnus, cycles,
suppression racine, chemins hors projet), (3) appliquer de façon déterministe,
(4) produire un diff lisible. Aucune mutation si la validation échoue.

### 4.2 Côté JS (glue, dans la page ou `shell.html`)

```js
// wrappers ccall (Module chargé par Emscripten)
const applyOp = (op) =>
  JSON.parse(Module.ccall('saida_apply_op', 'string', ['string'],
                           [JSON.stringify(op)]));
const engineManifest = () =>
  JSON.parse(Module.ccall('saida_engine_manifest', 'string', [], []));

// 1) en dur (palier S1) — bouger un node localement
applyOp({
  opVersion: 1, type: 'set_transform', sceneId: 'main',
  payload: { nodeId: 'Cube', position: [0, 2, 0] }
});
// le prochain frame() re-render la scène mutée — rien d'autre à faire

// 2) via WebSocket (palier S3) — ops acceptées par la gateway
ws.onmessage = (e) => {
  const res = applyOp(JSON.parse(e.data));   // op distante déjà validée serveur
  if (!res.ok) console.error('apply failed', res.error);
};
```

Rien à changer dans `frame()` : la scène est déjà relue chaque frame par
`renderer->drawFrame`. Muter `gApp.scene` suffit à voir le changement.

---

## 5. SaidaOp couvertes par le spike (sous-ensemble)

On ne code pas les ~15 ops. Le spike valide le **mécanisme** sur un panel
représentatif des 3 classes de difficulté :

| Op | Classe | Pourquoi dans le spike |
|---|---|---|
| `set_transform` | trivial (champs directs) | 1er signal de vie ; drive l'optimistic-local |
| `create_node` / `delete_node` | structure (mutation d'arbre) | valide add/removeChild sur scène vivante |
| `set_property` | **réflexion** (le point dur) | valide que la couche réfléchie tient dans le wasm |

Si ces trois classes passent, les autres ops (rename/reparent/add_behaviour/
connect_signal/write_script) sont du même ordre et relèvent de la Phase A, pas du
spike.

---

## 6. Paliers du spike

| Palier | Contenu | Sortie attendue |
|---|---|---|
| **S0 ✅** | Linker `src/authoring/` dans `web/runtime` CMake ; le wasm compile et rend toujours BeachDemo | **FAIT** — build vert, +4,22 % brotli (voir §6.1) |
| **S1 ✅** | `saida_apply_op` + `set_transform` en dur depuis la console JS | **FAIT** — Palm_A glissé en live sur BeachDemo (voir §6.2) |
| **S2 ✅** | `create_node` + `delete_node` + `set_property` (réflexion) | **FAIT** — `set_property` passe par la réflexion (`LightNode`, `Water`, `ParticleSystem`) + tests authoring |
| **S3 ✅** | Ops reçues via WebSocket (echo server local) au lieu de « en dur » | **FAIT** — `web/edit_echo_server.py` + `?edit&ws=1`, testé navigateur |
| **S4 ✅** | `saida_scene_snapshot` == `SceneSerializer` desktop (round-trip) | **FAIT** — comparaison automatique navigateur sur BeachDemo |

S3 utilise un simple echo WebSocket local ; **pas** la vraie gateway (c'est
Phase C). Le but est de prouver le chemin transport→apply, pas d'implémenter la
collaboration.

---

### 6.1 Résultat S0 (mesuré, 2026-07-03)

Authoring-core minimal (`src/authoring/EngineManifest.*` + `SaidaOpApplier.*`)
linké dans `web/runtime`, référencé par les bindings `saida_apply_op` /
`saida_engine_manifest` (`EMSCRIPTEN_KEEPALIVE`, ccall/cwrap exportés).

| Metrique (wasm+js, brotli q11) | Baseline | Avec authoring-core | Δ |
|---|---|---|---|
| Taille | 181 361 o | 189 010 o | **+7 649 o (+4,22 %)** |

- build Emscripten Release : vert (exit 0), seuls les 2 `.cpp` authoring + `main`
  recompilent ;
- symboles `_saida_apply_op` / `_saida_engine_manifest` présents dans `index.js`
  (non strippés → réellement appelables) ;
- chemin de rendu inchangé (`initRuntime`/`frame` non touchés) → BeachDemo rendu
  à l'identique par construction ; smoke-test visuel navigateur = 1 étape manuelle
  restante (`python web/serve.py build-web`).

Réserve honnête : ce +4,22 % couvre `set_transform`/`create`/`delete`/`rename` +
`set_property` **allégé**. Il **ne** contient **pas** la vraie réflexion de
propriétés ni `SceneSerializer` — c'est le coût que mesurera S2, et c'est là que
se joue réellement le GO/NO-GO de taille.

### 6.2 Résultat S1 (mesuré, 2026-07-03)

Harness opt-in `?edit` dans `web/runtime/shell.html` : expose `window.saida`
(`applyOp` / `manifest`) via `Module.ccall`, plus une barre de démo. Testé dans
le navigateur (WebGPU) sur BeachDemo.

- runtime chargé : *« BeachDemo loaded — 52 meshes, 1 waters, 1 lights »* ;
- `window.saida.manifest()` renvoie le manifest → binding vivant dans le wasm ;
- `set_transform{ nodeId:'Palm_A', position:[4,0.6,12] }` → `{ok:true}` : le
  palmier (et ses 9 enfants) **glisse en live** de x=-14 à x=4, re-render immédiat,
  **sans re-export ni reload** ;
- `set_property{ nodeId:'Sun', property:'intensity', value:1.5 }` → `{ok:true}` :
  éclairage de scène modifié en live ; l'état persiste entre ops ;
- garde-fous vérifiés : `unknown node 'Ghost'` et `unsupported property 'wobble'`
  → `{ok:false, error}` **sans muter** la scène.

Conclusion : le mécanisme *transport JS → applyOp → mutation scene-graph vivant →
re-render* est prouvé sur du vrai code moteur. Le pivot de l'invariant 0.2 tient.
S2/S3 solidifient maintenant ce chemin avec la réflexion, le snapshot et le
transport WebSocket local. S4 ferme le spike au sens invariant 0.3.

### 6.3 Résultat S2/S3 (mesuré, 2026-07-03)

- `set_property` utilise maintenant la vraie réflexion pour les nodes web
  reflétés (`LightNode`, `Water`, `ParticleSystem`) avec validation de type avant
  mutation. Les cas invalides (node inconnu, propriété inconnue, mauvais type,
  enum hors borne) sont rejetés sans muter.
- `saida_scene_snapshot()` est exposé au runtime web et `window.saida.snapshot()`
  renvoie un document `.scene` sérialisé depuis la scène vivante. Le binding est
  présent dans `build-web/index.js`.
- S3 est validé avec `web/edit_echo_server.py` : `?edit&ws=1` connecte le
  runtime à `ws://127.0.0.1:8787`, les boutons `WS sun` et `WS palm` envoient des
  SaidaOps sur WebSocket, reçoivent l'écho, puis appliquent l'op via le wasm.
- Test navigateur intégré : `WS sun` retourne `{"ok":true,"applied":"set_property"}`
  avec `nodeType:"LightNode"` ; `WS palm` retourne `{"ok":true,"applied":"set_transform"}`.
- Taille après S2/S3 + snapshot : `index.wasm.br = 160 315 o`,
  `index.js.br = 36 325 o`, `index.data.br = 82 869 o`.

### 6.4 Résultat S4 (mesuré, 2026-07-03)

- Le loader web préserve maintenant les métadonnées communes de scène/nodes :
  `id`, `name`, `enabled`, `groups`, `importedFrom`, transform et hiérarchie.
- `Camera` est chargée comme un vrai `CameraNode` sérialisable, tout en continuant
  d'alimenter la caméra runtime utilisée par le rendu web.
- `Water`, `LightNode` et `MeshNode` conservent les champs nécessaires au
  round-trip canonique (`Sea`, ids, `meshEnabled`, outline defaults, etc.).
- `saida_scene_snapshot()` restaure l'id durable du skybox au lieu de laisser
  fuiter le handle de texture généré en mémoire pour le rendu web.
- `saida_scene_snapshot_compare()` compare automatiquement le snapshot web à la
  scène de référence BeachDemo canonisée comme un reload `SceneSerializer`
  desktop (tolérance float32, defaults sérialisés, champs legacy retirés).
- Test navigateur : `S4 compare` retourne `{"ok":true,"actualChildren":17,
  "expectedChildren":17,"durableSkyboxTexture":4242424242}`.

## 7. Critères de succès (GO / NO-GO)

**GO** si tout est vrai :

1. le wasm d'édition (avec authoring-core + réflexion) **compile et rend
   BeachDemo à l'identique** ;
2. `set_transform` reçu → node déplacé au frame suivant, **sans re-export** ;
3. `create_node` / `delete_node` / `set_property` mutent la scène vivante et
   sont visibles ;
4. une op invalide (type inconnu, propriété non réfléchie) est **rejetée
   proprement** avec `{"ok":false,"error":...}`, sans corrompre la scène ;
5. le chemin WebSocket → `applyOp` → render fonctionne (S3) ;
6. `saida_scene_snapshot()` produit **exactement** ce que `SceneSerializer`
   desktop sérialise (S4) ;
7. **Δ taille wasm acceptable** : cible indicative **< +40 % brotli** vs le
   runtime de jeu (base ~213 Ko). À arbitrer selon la mesure réelle.

**NO-GO / re-design** si :

- la réflexion `set_property` ne peut pas entrer dans le wasm (deps desktop-only,
  RTTI/ImGui accidentels) → il faut d'abord découpler la réflexion de l'éditeur ;
- le coût taille explose (> ~2× le runtime de jeu) → envisager un authoring-core
  « allégé web » (sous-ensemble de propriétés réfléchies) ;
- muter la scène hors `scene->update()` casse le renderer → il faut un point
  d'application des ops synchronisé avec la boucle frame.

Dans les trois cas : **on ne démarre pas la gateway** tant que ce n'est pas levé.

---

## 8. Risques & points ouverts

- **Réflexion couplée à l'éditeur.** Le loader web réduit existe *précisément*
  pour éviter la réflexion. Extraire une réflexion utilisable sans ImGui est
  peut-être un pré-requis Phase A avant même S0.
- **Ordre d'application vs boucle frame.** Appliquer une op en plein
  `drawFrame` est dangereux. Bufferiser les ops et les appliquer en tête de
  `frame()` (avant `scene->update`) — un point d'entrée unique et déterministe.
- **Assets référencés par une op** (`import_asset`, `set_property` texture) : sur
  web, l'asset doit déjà être dans MEMFS/fetch. Le spike se limite à des ops qui
  ne créent pas de nouvel asset (transform/property/structure sur nodes
  existants) ; l'import d'asset web est hors spike.
- **Taille wasm.** Mesurer à chaque palier ; c'est le critère qui peut forcer un
  authoring-core allégé.
- **QuickJS + write_script** hors spike (relève d'une brique déjà existante,
  hot-reload script) — on garde le spike sur la structure de scène.

---

## 9. Hors périmètre du spike (explicite)

- la vraie Collaboration Gateway, l'OperationLog Postgres, les revisions
  (Phase C) ;
- l'UI React (hierarchy/inspector/gizmos) — le spike se pilote en console JS ;
- l'optimistic-local complet (le spike prouve la faisabilité ; l'UX vient en
  Phase D avec l'invariant 0.4) ;
- l'IA / SaidaOps générées (Phase E) ;
- l'import d'asset web et le streaming.

Le spike répond à **une seule** question : *le runtime web peut-il appliquer une
op et re-render en live, à un coût de taille acceptable ?* Tout le reste en
dépend.
