# Plan — Character & Camera System

> Chantier : déplacement personnage (ZQSD/WASD/flèches + saut + physique) et un
> vrai système de caméra **simple**, inspiré de **Cinemachine 3** (pas CM2).
> Respecte le contrat moteur : **nodes + behaviours + signaux + services**, pas
> de manager de gameplay, pas de singleton gameplay.

## Inspiration Cinemachine 3 → naming NextEngine

Cinemachine 3 a simplifié le vocabulaire de CM2. On reprend ses concepts avec un
naming **clair et court**, aligné sur les conventions du moteur (`MeshNode`,
`LightNode`, `…Behaviour`).

| Concept Cinemachine 3            | NextEngine                  | Rôle |
|----------------------------------|-----------------------------|------|
| `CinemachineCamera` (la vcam)    | **`CameraNode`**            | Une caméra placée dans la scène (pose + lens + priorité). |
| `CinemachineBrain`               | **`CameraDirector`**        | Choisit la caméra active (priorité) et **blende** entre les caméras. |
| Position Control (`ThirdPersonFollow` + Deoccluder) | **`CameraFollowBehaviour`** | Suit une cible en 3ᵉ personne, orbite souris, **gère les murs** (raycast). |
| Tracking / Follow Target         | groupe `"player"`           | La cible suivie (trouvée par groupe, jamais par nom). |
| Priority                         | `CameraNode::priority`      | La plus haute priorité = caméra « live ». |
| Default Blend (Ease + time)      | `CameraDirector` blend      | Transition smooth (easing + durée). |

Principe CM3 conservé : **chaque caméra calcule sa propre pose** (un behaviour la
positionne) ; le **director ne fait que choisir + blender**. Une caméra statique
n'a besoin d'aucun behaviour — juste une pose et une priorité.

---

## État actuel (constat)

- `CharacterBehaviour` : saut + physique OK (`CharacterBodyNode` + Jolt). Mais
  déplacement en **axes monde** (pas relatif caméra), pas de flèches, pas
  d'orientation du perso vers sa direction.
- `Camera` : une seule, yaw/pitch, pilotée en fly-cam par `EditorApp`. En Play,
  fly-cam gelée, **rien ne suit le perso**. **FOV codée en dur (45°)** dans le
  Renderer (`Renderer.cpp` ~1301).
- Aucune notion de caméra dans la scène.
- Input additif (`getActionStrength` = max) → plusieurs touches/action OK. Codes
  GLFW = positions physiques QWERTY → `W/A/S/D` couvrent déjà **ZQSD sur AZERTY**.
  Il ne manque que les **flèches**.
- `PhysicsWorld::raycast(origin, dir, maxDist) → RaycastHit{hit, point, normal}`
  via `scene->physics()` → utilisé pour la gestion des murs.

---

## Pièces à créer / modifier

### (A) `Camera` — extension minimale  *(core/Camera.hpp/.cpp)*
- Ajouter `float fovDegrees = 45.0f;`.
- Ajouter `void lookAt(const glm::vec3& target);` (yaw/pitch depuis une direction).
- `Renderer` utilise `camera.fovDegrees` au lieu du 45° codé en dur.
- Modèle yaw/pitch conservé (pas de roll) — simple, non invasif.

### (B) `CameraNode` — la caméra  *(scene/CameraNode.{hpp,cpp})*
- Dérive de `Node`. Champs : `fovDegrees`, `nearZ`, `farZ`, `int priority`,
  `bool active`.
- Transform monde = pose de vue. `typeName() "Camera"`, serialize/deserialize,
  enregistré dans `NodeRegistry`.
- Rejoint le groupe `"camera"` (le perso trouve la cam active sans find-by-name).

### (C) `CameraDirector` — le brain  *(render/CameraDirector.{hpp,cpp})*
- **Membre de `Engine`** (pas un global). État : `CameraState {pos, rot(quat), fov}`
  courant + blend en cours (durée, easing, t).
- `update(Scene&, Camera& out, float dt)` :
  1. traverse, collecte les `CameraNode` actifs, prend la **priorité max** ;
  2. état cible = transform monde + fov de la vcam ;
  3. si la live-vcam **change** → démarre un blend depuis l'état courant ;
  4. avance le blend : **lerp position, slerp rotation, lerp fov** ;
  5. écrit dans `out` (position, `lookAt`, `fovDegrees`).
  - Aucune vcam → ne touche pas la caméra (fly-cam éditeur conservée).

### (D) `CameraFollowBehaviour` — suivi 3ᵉ personne + murs  *(scene/CameraFollowBehaviour.{hpp,cpp})*
- Attaché à un `CameraNode`. Cible via groupe (`targetGroup`, défaut `"player"`).
- Params : `distance`, `height`, `shoulderOffset`, `yawSensitivity`,
  `pitchMin/Max`, `positionDamping`, `rotationDamping`, `collisionRadius`,
  `collisionMargin`.
- `onUpdate` : souris → orbite yaw/pitch ; `pivot = cible + height` ;
  `desired = pivot − dir*distance` ; **raycast(pivot → desired)** ; si collision,
  rapproche la caméra (`hitDist − margin`) ; amortit ; oriente le node vers le pivot.
- Enregistré : `BehaviourRegistry` (`"CameraFollow"`) + drawer `InspectorRegistry`.

### (E) `CharacterBehaviour` — déplacement relatif caméra  *(scene/CharacterBehaviour.cpp)*
- Binder les **flèches** (additif) ; `Sprint` (Shift) optionnel.
- Base de déplacement = forward/right de la cam active (`tree()->firstInGroup("camera")`,
  forward projeté sur XZ) ; fallback axes monde.
- `velocity.xz = (right*in.x + forward*in.y) * moveSpeed`. Saut/gravité inchangés.
- Orientation du perso : slerp de la rotation du node vers la direction de
  déplacement quand il bouge.

### Intégration `Engine`
- `Engine` possède `CameraDirector director_;`.
- Dans `runDesktop`, après `activeScene->update(...)` : si `sceneTree_->mounted()`
  (= Play) → `director_.update(*activeScene, camera_, Time::delta())`.
- À l'arrêt, la fly-cam éditeur reprend (déjà gérée par `EditorApp`).
- **XR non touché** (pose tête pilote `camera_`).

### Éditeur
- Entrée menu **« Camera »** dans le create-menu (`SceneHierarchyPanel`).
- Champs `CameraNode` dans l'inspecteur de node (fov, near/far, priority, active).
- Drawer `CameraFollowBehaviour` (target group, distance, height, sensibilité,
  damping, collision).
- *(Optionnel, repoussable)* gizmo frustum via pipeline `debug_line`.

---

## Étapes (compile à chaque étape)

- [x] **1.** `Camera.fovDegrees` + `lookAt` + Renderer utilise la fov.
- [x] **2.** `CameraNode` + `NodeRegistry` + serialize + menu create + inspecteur.
- [x] **3.** `CameraDirector` + intégration `Engine` (priorité + blend).
- [x] **4.** `CameraFollowBehaviour` (orbite + raycast murs) + registries.
- [x] **5.** `CharacterBehaviour` relatif-caméra + flèches + facing.
      Bonus : `CharacterBodyNode::syncToPhysics` fait suivre la rotation gameplay
      du node au character (capsule symétrique → sans effet physique), sinon le
      facing était écrasé par `syncFromPhysics`.
- [x] **6.** Scène de test livrée : `MyGame/scenes/CharacterTest.scene` (sol, 4 murs,
      obstacles, `Player` CharacterBody groupe `player`, `Follow Camera`). Test
      visuel par l'utilisateur (GUI bloquante).
      Bonus requis : **sérialisation des groupes** ajoutée à `Node` (les groupes
      n'étaient pas persistés ; nécessaire pour authorer le groupe `player`).

## Notes d'usage (montage de scène)

- **Perso** : un `CharacterBody` (menu Physics) avec un `CollisionShape` Auto +
  un mesh enfant ; ajouter le component **`Character`** ; mettre le node dans le
  groupe **`player`** (sinon la caméra de suivi ne le trouve pas).
- **Caméra de suivi** : une **`Camera`** (top-level) + component **`CameraFollow`**
  (target group = `player`). Priorité par défaut 0.
- **Caméras fixes** : des `Camera` placées ; la plus haute **priorité active**
  devient « live ». Changer `priority`/`active` (ou via script) déclenche un
  **blend** smooth.
- Le perso et la `CameraFollow` supposent un node **top-level** (parent identité)
  car ils écrivent une pose monde dans la transform locale.

## Décisions

- Orbite 3ᵉ personne à la souris (pitch clampé).
- Le perso pivote pour faire face à sa direction de déplacement.
- Caméras yaw/pitch sans roll dans les blends (simple, cohérent avec `Camera`).
- Switch de caméra par **priorité** (façon CM3), pas par appel manuel.
- Le perso trouve la cam active via le groupe `"camera"`.
- Signal « in-play » côté moteur = `sceneTree_->mounted()`.
