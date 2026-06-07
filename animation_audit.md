# Audit du Système d'Animation (Phase 1, 2, 3)

## 1. Ce qui a été codé et validé

- **Immutabilité des données (Phase 1)** : `AnimationClip` et `Rig` sont des structures pures de données (Data-Oriented). L'évaluation d'un clip se fait sans état interne, par recherche binaire (`std::upper_bound`), ce qui est thread-safe et robuste.
- **Topologie Cache-Friendly (Phase 1)** : Les os sont stockés à plat. Le calcul du Forward Kinematics (Local -> Global) dans `Pose.cpp` se fait en une seule boucle continue, maximisant l'utilisation du cache CPU L1/L2.
- **Graphe d'animation strict (Phase 2)** : `AnimNode` (avec `ClipNode`, `BlendNode` et `AnimStateMachine`) utilise des `unique_ptr`. Cela force un graphe en forme d'Arbre (strict Tree) et non un DAG (Directed Acyclic Graph). Cela simplifie grandement l'ownership et évite le bug classique de la double-évaluation des temps.
- **Timeline universelle (Phase 3)** : Le `PlayableDirector` avance le temps de manière absolue et le passe à une `Timeline` agnostique. La fondation pour piloter des propriétés à la volée est prête, sans réflexion (Fake track).

## 2. Défauts architecturaux identifiés (Code Smells & Manques)

### A. Mélange Update/Evaluate dans le Graphe d'Animation
Dans `AnimNode::evaluate(float deltaTime, ...)`, le passage du `deltaTime` avance les chronomètres internes (ex: `time_ += deltaTime` dans `ClipNode`) ET calcule la pose.
- **Problème** : Si l'on souhaite évaluer la pose actuelle sans avancer le temps (par exemple pour du sub-stepping physique ou du motion blur multi-samples), on ne peut pas sans corrompre l'état du graphe.
- **Solution recommandée** : Séparer en deux fonctions virtuelles : `virtual void update(float deltaTime)` et `virtual void evaluate(const LocalPose& bindPose, LocalPose& outPose)`.

### B. Manque du calcul "Skinning Matrices" (Inverse Bind)
Actuellement, `GlobalPose` calcule la position absolue des os dans l'espace de l'objet (`globalMatrices[i] = parentMat * localMat`).
- **Problème** : La carte graphique (Vertex Shader) ne veut pas la position absolue de l'os. Elle veut la **différence** entre la pose actuelle de l'os et sa "Bind Pose" (pose de repos). La formule est `SkinMatrix = GlobalMatrix * InverseBindMatrix`.
- **Manque** : Il n'y a nulle part de tableau `skinningMatrices` généré. Si on envoie `GlobalPose` tel quel au GPU dans la Phase 4, les modèles seront disloqués.

### C. Le lien vers l'Entité (Animator Component)
- **Manque** : La Phase 2 et 3 fournissent les rouages mathématiques, mais il manque le moteur : le composant `Animator`.
- Il nous faut un `Behaviour` qui détient un `Rig`, détient le nœud racine (ex: `AnimStateMachine`), alloue les buffers de `LocalPose` et `GlobalPose`, et effectue l'appel à chaque frame.

### D. Troncature silencieuse dans BlendNode
Dans `BlendNode::evaluate`, si l'animation A a 10 os et la B en a 5, `std::min` tronque la pose de sortie à 5 os. Les 5 os restants deviennent corrompus.
- **Solution recommandée** : Forcer `outPose` à toujours correspondre à la taille de `bindPose` (la référence stricte) et combler avec `bindPose` si des os manquent dans A ou B.

### E. Oublis Fonctionnels (Hors scope pour l'instant mais à noter)
- **Root Motion** : Aucune extraction de la translation de l'os racine pour déplacer le `Node` automatiquement.
- **Animation Events** : Pas de callbacks pour des sons de pas.

## 3. Conclusion avant la Phase 4

Le code produit est **propre, moderne (C++17) et sans God Class**. L'utilisation de `if constexpr` dans les templates d'interpolation garantit l'absence de Magic Numbers et une compilation stricte.

Cependant, avant de modifier le pipeline GPU (Phase 4), **nous devons corriger l'architecture CPU** (séparation Update/Evaluate, et ajout des matrices de Skinning `SkinMatrix = GlobalPose * InverseBind`). Cela consolidera définitivement le socle.
