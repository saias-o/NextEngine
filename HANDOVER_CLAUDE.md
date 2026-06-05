# Passation NextEngine (De Antigravity à Claude)

Ce document résume l'ensemble des travaux, des modifications d'architecture, et des corrections de bugs critiques qui ont été implémentés dans la dernière session de pair-programming. Le moteur est désormais compilable et parfaitement stable.

## 1. Nouveau Système d'Assets (AssetRegistry & GUIDs)
Le système basé uniquement sur des "String paths" a été remplacé par un système de GUID robuste et déterministe pour être "Git-Friendly" (et éviter les conflits en multi-joueurs/multi-développeurs).

- **`project/AssetRegistry.hpp / .cpp`** : Implémentation d'un registre d'assets (`AssetID` en tant que `uint64_t`). Le hash généré garantit que le même asset aura le même GUID sur n'importe quelle machine ou dépôt Git.
- **`ResourceManager` / `Material`** : Ces classes référencent désormais des `AssetID` (via `AssetRegistry`) au lieu de passer des strings partout.

## 2. Optimisations de la Phase 3 (Data-Oriented Design)
Le cœur du moteur (la gestion des `Node` et du rendu) a subi une transformation majeure pour basculer vers un paradigme Data-Oriented plus cache-friendly.

- **Pools de Descripteurs Vulkan Dynamiques** : 
  - *Avant* : Le moteur plantait lamentablement (hard-crash) s'il y avait plus de 64 `Material` dans la scène car le `VkDescriptorPool` était fixé. 
  - *Après* : Le `ResourceManager` instancie et gère dynamiquement une `std::vector<VkDescriptorPool>`. S'il n'y a plus de place, un nouveau pool est automatiquement créé (allocation infinie).
- **Cache des Matrices de Transformations** :
  - *Avant* : L'arbre (`Node`) calculait récursivement (avec multiplication de matrices `parent * local`) les `worldMatrix` de TOUTE la hiérarchie à chaque frame.
  - *Après* : Ajout de `worldTransform_` et de `lastLocalMatrix_` dans la classe `Node`. La matrice globale n'est recalculée que si la matrice locale (ou celle d'un parent) a été modifiée (`dirty flag`). *Note importante : `lastLocalMatrix_` est initialisé à `0.0f` pour garantir que le système détecte la toute première passe et calcule la matrice lors de la frame d'initialisation.*
- **Flat Arrays (Scene.cpp)** :
  - Un flag statique `g_hierarchyVersion` est incrémenté chaque fois qu'un enfant est ajouté ou supprimé dans la scène.
  - La méthode `Scene::update` surveille cette variable. Lorsqu'elle change, la scène "aplatit" la hiérarchie (`flattenHierarchy()`) en créant trois vecteurs (`std::vector`) continus en mémoire : `meshes_`, `lights_` et `flatBehaviours_`.
- **Renderer "Burst"** : 
  - `Renderer::gatherScene` n'utilise plus la fonction récursive lourde et remplie de `dynamic_cast`. Il boucle maintenant en une seule passe contiguë sur `scene.meshes()` et `scene.lights()`.

## 3. Bugfixes Critiques
- **Bug du Frustum Culling (TRES IMPORTANT)** : 
  - *Le symptôme* : Les objets périphériques/en rotation (les petits cubes autour du globe) disparaissaient sans raison en plein milieu de l'écran. 
  - *La cause* : Le code qui extrayait les plans du frustum de la caméra (`Camera::getFrustum()`) additionnait les *colonnes* de la matrice GLM (ex: `m[3] + m[0]`). GLM étant Column-Major, il faut impérativement extraire les *lignes* (Rows). 
  - *Le fix* : Les lignes de la matrice de projection `row0`, `row1`, `row2`, `row3` sont désormais extraites mathématiquement correctement pour former les 6 plans (gauche, droite, haut, bas, near, far).
- **ImGui Stack Underflow** : Correction d'une boucle `ImGui::PushID`/`PopID` asymétrique dans le `SceneHierarchyPanel` qui déséquilibrait la pile (Assertion Failed).

## 4. UI / Editor
- Ajout d'un bouton de don ("Sponsor NextEngine") de couleur saumon/orange au sein de la fenêtre modale `About NextEngine` (`EditorUI::drawAboutWindow()`). Le bouton ouvre l'URL des sponsors GitHub selon l'OS cible.

## 5. Ce qui ne DOIT PAS être fait
- La requête demandant l'ajout "d'une page de changelogs sur le site web" a été **explicitement annulée** par l'utilisateur. Le site web n'a pas été touché et ce n'est plus à l'ordre du jour.

---
**Statut de Compilation** : 100% OK avec Ninja.
**Statut du Moteur** : Stable, Testé (`NextEngine.exe` tourne parfaitement et le rendu est complet).
