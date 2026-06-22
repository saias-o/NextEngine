# Plan de fiabilisation de l'editeur

Ce document est le journal d'execution de l'audit editeur. Un lot ne passe a
`[x]` qu'apres compilation de `NextEngine`, execution des tests applicables et
`git diff --check`.

## Etat des lots

- [x] Lot 0 - Suivi, build de reference et hygiene CMake
- [x] Lot 1 - Securite memoire et lifecycle runtime
- [x] Lot 2 - NodeId, SceneDocument et undo/redo sur
- [x] Lot 3 - Mutations transactionnelles et Play en lecture seule
- [x] Lot 4 - Ressources, materiaux et changements de projet
- [x] Lot 5 - Navigateur d'assets et thumbnails bornes
- [x] Lot 6 - Modularisation et InspectorRegistry
- [x] Lot 7 - Performance, code mort et coherence UX
- [x] Lot 8 - Tests, documentation et validation finale

## Journal de validation

| Lot | Configuration | Build NextEngine | Tests | diff-check | Notes |
|---|---|---|---|---|---|
| 0 | OK (Debug) | OK | 1/1 OK | OK | PATH MSYS2 requis; doublons CMake supprimes |
| 1 | OK (Debug) | OK | 2/2 OK | OK | Scene active recalculee; timers possedes par Behaviour |
| 2 | OK (Debug) | OK | 3/3 OK | OK | Scene v2, NodeId, commandes par ID, historique borne |
| 3 | OK (Debug) | OK | 3/3 OK | OK | Play lecture seule (canEdit + chokepoint execute); gizmo + inspecteur transactionnels (SetPropertyCommand + PropertyEditor) |
| 4 | OK (Debug) | OK | 3/3 OK | OK | Materiaux/assets/SceneSettings undoables (PropertyEditor + push one-shot); reset etat editeur sur Project::version() |
| 5 | OK (Debug) | OK | 3/3 OK | OK | ThumbnailCache borne (LRU + budget/frame + downscale + retire differe RAII); remplace texCache_ non borne |
| 6 | OK (Debug) | OK | 3/3 OK | OK | InspectorRegistry: drawers ImGui sortis des behaviours gameplay (engine sans drawer ImGui) |
| 7 | OK (Debug) | OK | 3/3 OK | OK | FileBrowser: scan disque mis en cache (plus de scan/frame); code mort retire; 0 warning |
| 8 | OK (Debug) | OK | 4/4 OK | OK | EditorDocumentTests (5 cas) : SceneDocument, Rename/Transform/Reparent round-trips, CommandHistory clear/redo |

## Lot 3 — termine

Play en lecture seule :
- `EditorUI::canEdit()` (faux en Play) + `EditorUI::execute()` comme unique point
  de passage des commandes ; toutes les mutations structurelles (scene tree,
  paste/duplicate, drag-drop) y transitent.
- Neutralise/desactive de bout en bout en Play : raccourcis clavier mutables,
  menus File/Edit/Scene, drag-drop du scene tree, inspecteur (`BeginDisabled` +
  bandeau « read-only ») ; les requetes de mutation differees du scene tree sont
  jetees si `!canEdit()`.

Mutations transactionnelles :
- Gizmo : un drag emet un `TransformCommand` (undoable + dirty) au relachement.
- `SetPropertyCommand` (closure-based, type-erase, re-resout le node par `NodeId`
  a chaque execute/undo) + framework `editor/PropertyEditor.hpp` : feedback live
  pendant le drag, capture de la valeur pristine a l'activation du widget, une
  seule commande coalescee au relachement (`IsItemDeactivatedAfterEdit`).
  Fiabilite multi-composants (DragFloat3/ColorEdit4) validee via la propagation
  des status flags par `EndGroup` (ImGui 1.92).
- Converties en commandes undoables : Transform (pos/rot euler/scale), `enabled`,
  Light (color/intensity/direction/range/inner+outer/castShadows), MeshRenderer
  (meshEnabled/castShadows/lightBaking), corps physiques (friction/restitution/
  kinematic/mass/gravity/damping/area.moving/character mass+slope, chaque setter
  rebuild le corps a l'undo aussi), CollisionShape (halfExtents/radius/height/
  axis/offset), UI transform (x/y/w/h/pivot/anchor), UI appearance (UIColor,
  UIText texte/taille/couleur, Interactable, Button/Toggle colors), WebCanvas
  (mode/url/hotReload).
- Test : `tests/EditorCommandTests.cpp` couvre le round-trip execute/undo/redo de
  `SetPropertyCommand` et la coherence dirty.

Volontairement deferre aux lots dedies :
- Materiaux, textures, assignations d'assets, Scene Settings → **Lot 4** (fait).
- Champs internes des behaviours (`Behaviour::onDrawInspector`) → **Lot 6** (fait).

## Lot 4 — termine

Ressources & materiaux (via le meme `PropertyEditor`, donc undoables/coalesces) :
- Materiau : couleur de base, metallic, roughness, emissif, AO, + assignation de
  la base texture. Le setter re-derive le `MaterialDesc` courant, surcharge un
  seul champ et re-internise via `ResourceManager::getMaterial` → round-trip
  d'undo propre meme si plusieurs champs ont change entre-temps.
- Assignations d'assets en un coup (drag-drop) via `PropertyEditor::push` :
  Mesh (MeshNode), texture d'image UI, texture de skybox.
- Scene Settings : tout le bloc (ambient/clear/fog avec preservation de l'alpha,
  GI, IBL, AO, fog, bloom, skybox exposure/rotation, debug) commandise par des
  fabriques get/set generiques sur pointeur-membre ; les radios de mode
  d'eclairage en `push` one-shot. « Generate Bake » reste une action transitoire.

Changements de projet :
- `Project::version()` (incremente a chaque create/load reussi). `EditorUI`
  detecte le changement et **drope l'etat per-projet** (historique undo/redo,
  presse-papier, chemin de scene courant, selection, edition en cours) — sinon
  les commandes pointeraient des nodes de l'ancienne scene.

Hors perimetre (suivi) :
- Liste de LOD : editeur de collection par-element ; marque le document dirty
  mais pas d'undo individuel (commande de liste dediee = follow-up).

## Lot 5 — termine

Navigateur d'assets — thumbnails bornes (`editor/ThumbnailCache.{hpp,cpp}`) :
- Remplace l'ancien `ImGuiTextureCache`/`texCache_` **non borne** : il enregistrait
  une texture pleine resolution (via le `ResourceManager`, donc persistante a vie)
  et un descripteur ImGui par image du dossier, sans plafond ni eviction.
- Nouveau cache dedie, **decouple du `ResourceManager`** : genere des thumbnails
  **downscalees** (box-filter area-average, <= 128 px, aspect conserve, jamais
  d'upscale), possede ses propres `Texture` GPU (RAII).
- **Borne sur tous les axes** : plafond LRU (256 thumbnails), budget de
  generation par frame (4), generation **uniquement pour les items visibles**
  (`ImGui::IsRectVisible`), invalidation sur mtime.
- **Liberation GPU-safe** : les thumbnails evictees passent par une file de
  retirement differe (`kRetireFrames=4 > frames-in-flight=2`) avant
  `RemoveTexture`/destruction de la `Texture` — jamais de descripteur libere
  sous une frame en vol.
- Aucune regression : preview d'images en grille conservee, drag-drop ASSET_ID,
  double-clic, rename/delete/search/zoom inchanges. `ImGuiTextureCache.hpp`
  (mort) supprime.

## Lot 6 — termine

InspectorRegistry (`editor/InspectorRegistry.{hpp,cpp}`) — simple, sans
sur-ingenierie :
- Petite map `typeName -> drawer` (`std::function<void(Behaviour&, EditorUI&)>`),
  drawers builtin enregistres une fois a la 1re utilisation (static local).
- Les 4 drawers ImGui (`AudioSource`, `Spawner`, `Character`, `ScriptBehaviour`)
  sont **deplaces des behaviours vers l'editeur**. `Behaviour::onDrawInspector`
  et tous les overrides sont supprimes ; `imgui.h` retire de ces TUs.
- Resultat : **plus aucun behaviour gameplay ne porte de drawer ImGui** →
  l'invariant « le moteur ne depend pas de drawers ImGui portes par les
  behaviours » est respecte (ne_engine ne tire ImGui que via `ImGuiLayer`, le
  wrapper de backend assume).
- `ScriptBehaviour` : petite API editeur publique ajoutee (`loaded()`,
  `hotReloadEnabled()/set`, `properties()`, `applyProperty()`) au lieu d'exposer
  les internes ; les editions de proprietes marquent le document dirty.
- Modularite obtenue par le registre (drawers enfichables) sans eclater
  l'InspectorPanel en N fichiers — volontairement leger.

## Lot 7 — termine

Performance :
- **Navigateur d'assets** : le scan disque etait fait **a chaque frame**
  (`directory_iterator`, et pire, `recursive_directory_iterator` sur tout le
  projet en mode recherche). Desormais le listing est **cache** (`EditorUI::
  FileListing`, chemins tries) et re-scane seulement si le chemin/la requete
  change, apres une mutation locale (rename/delete), ou toutes les 1 s (capture
  les changements externes). Gros gain en mode recherche.

Code mort retire :
- `SceneDocument::generation()` + membre + increments (jamais lu — la detection
  de changement de projet passe par `Project::version()`).
- `SceneDocument::path()` + `path_` (write-only ; `EditorUI::currentScenePath_`
  est la source de verite) ; `markSaved/markLoaded` simplifies.
- Parametre `ResourceManager*` inutilise de `ModelImporterPanel::draw` (le seul
  warning du build) → **build 0 warning**.
- `ImGuiTextureCache` (deja supprime au Lot 5).

Laisse volontairement (cout acceptable, pas de sur-ingenierie) :
- `SceneDocument::find` retraverse la scene a la resolution de selection (1×/frame
  quand un noeud est selectionne) — negligeable aux tailles de scene visees ;
  mettre en cache introduirait un risque de pointeur perime pour un gain marginal.

## Invariants d'architecture

- Le mode Play est inspectable mais non modifiable depuis l'editeur.
- Les references editoriales de noeuds utilisent un `NodeId`, jamais un pointeur
  brut conserve entre deux frames ou deux reconstructions de scene.
- Toute mutation de document passe par le service de commandes et marque le
  document dirty.
- Les ressources temporaires et workers sont bornes et arretes par RAII.
- Le moteur ne depend pas de drawers ImGui portes par les behaviours gameplay.

## Backlog UI volontairement non implemente

Ces controles restent visibles mais desactives, avec la mention "Non disponible".
Ils ne doivent jamais simuler une action reussie.

| Fonction | Emplacement actuel | Backend manquant | Dependances futures |
|---|---|---|---|
| Build / Build & Run Windows | Build Settings | Packaging executable et assets | Runtime standalone, manifest de build |
| Build Linux | Build Settings | Toolchain/cross-compilation Linux | Runtime standalone, packaging |
| Build Meta Quest | Build Settings | Android/Quest packaging | Android NDK, Gradle, OpenXR mobile |
| Build Web | Build Settings | Backend WebGPU/WebAssembly | Emscripten, presentation non Vulkan desktop |
| Hand tracking Quest | Build Settings | Configuration export OpenXR | Manifest Quest et extensions XR |
| Foveation Quest | Build Settings | Configuration runtime XR | Extensions Meta/OpenXR |
| Multiview Quest configurable | Build Settings | Profil de plateforme | Configuration renderer/export |
| WebGPU fallback | Build Settings | Backend graphique Web | Emscripten/WebGPU |
| Threads WebAssembly | Build Settings | Configuration COOP/COEP | Emscripten et serveur web |
| MSAA projet | Settings/Rendering | Reconfiguration swapchain/pipelines | API qualite renderer |
| VSync projet | Settings/Rendering | Reconfiguration presentation | Politique swapchain |
| Autosave | Settings/Editor | Scheduler et sauvegarde atomique | SceneDocument dirty |
| Taille de gizmo persistante | Settings/Editor | Preferences editeur | Stockage de preferences |
| Resize WebCanvas dynamique | Inspector WebCanvas | Recreation texture/contexte | API `WebCanvasNode::resize` |
| Liste libre des scenes de build | Build Settings | Manifest de build | Asset picker et runtime standalone |

## Lot 8 — termine

Tests, documentation et validation finale du chantier de fiabilisation :

Nouveau fichier `tests/EditorDocumentTests.cpp` (target CMake `ne_editor_document_tests`,
linke `ne_editor`) — 5 cas de test sans GPU ni ImGui :

- **testSceneDocumentState** : `bind`/`find`/`dirty`/`saved`/`loaded`/`select`/`clearSelection`,
  et cas de selection perimee (noeud supprime → re-bind → `selectedId == kNodeInvalid`).
- **testRenameCommandRoundTrip** : execute renomme, undo restaure, redo re-renomme ;
  `dirty` positionne a chaque etape.
- **testTransformCommandRoundTrip** : execute applique la nouvelle position, undo restitue
  (0,0,0), redo re-applique (1,2,3).
- **testReparentCommandRoundTrip** : `child` de `parentA` deplace dans `parentB` apres
  execute, retour dans `parentA` apres undo ; comptage de `children()` verifie.
- **testHistoryClearAndRedoOnNew** : historique lineaire (nouvelle commande apres undo
  efface le redo stack), `clear()` vide les deux piles, `undoName`/`redoName` renvoient
  `""` quand vides.

Couverture intentionnellement exclue (GPU/ImGui/filesystem) :
- `AddNodeCommand`/`DeleteNodeCommand`/`CreateParentCommand` : leur undo/redo necessitent
  `ResourceManager` (serialisation JSON ↔ Vulkan) — validables uniquement avec GPU.
- `ThumbnailCache` : necessite `VulkanDevice`.
- `InspectorRegistry::draw` avec vraie UI : necessite ImGui initialisé.
- `Project::version()` via create/load : necessite filesystem temporaire — valide
  par integration (le mecanisme est simple et couvert par le Lot 4).

Suite complete : **4/4 OK** (core, scene lifecycle, editor commands, editor document).

## Commandes de fin de lot

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build --target NextEngine
ctest --test-dir build --output-on-failure
git diff --check
```
