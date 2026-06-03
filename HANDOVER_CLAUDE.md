# Handover pour Claude (Dernière session UI/Editor)

Salut Claude, voici le résumé des dernières modifications apportées au moteur côté Éditeur / UI. Tu peux t'appuyer là-dessus pour reprendre la suite, notamment sur la partie **Sérialisation**.

## 1. Réorganisation Globale de l'UI
- Le menu principal (`EditorUI::drawMenuBar`) a été nettoyé : `File`, `Edit`, `View`, `Build`, `Settings`, `Help`.
- Le contenu de l'ancien menu "Project" est maintenant dans `File`.
- L'ancienne fenêtre "Project Settings" s'appelle désormais juste `Settings` (onglet Projet + onglet Editeur).
- Le bouton "Build" a été déplacé dans le sous-menu `Build`.

## 2. File Browser & Scene Tree (Context Menus)
- **File Browser** : Ajout d'un menu contextuel (clic-droit) pour `Rename` (inline via InputText) et `Delete`. Le renommage affecte directement les fichiers physiques et met à jour le ResourceManager.
- **Scene Tree** : Ajout d'un menu contextuel pour `Rename` (inline) et `Delete` un `Node` directement depuis l'arbre.

## 3. Propriétés des MeshNodes (Inspector)
- L'Inspector affiche maintenant pour les `MeshNode` deux nouvelles propriétés :
  - `Cast Shadows` (booléen, activé par défaut).
  - `Include to light baking` (booléen, désactivé par défaut).

## 4. Outils de Transformation 3D (Gizmos & Toolbar)
- Ajout d'un système de modes de Gizmo (Translation = 0, Rotation = 1, Scale = 2) pilotables par les touches `T`, `R`, `S`.
- Une nouvelle **Toolbar** verticale transparente a été ajoutée sur le bord gauche de la vue 3D pour sélectionner ces modes à la souris.
- Les gizmos sont dessinés manuellement et projetés en espace d'écran dans `EditorUI::drawGizmo`.

## 5. Navigation dans le Viewport 3D (Bugs corrigés)
- L'overlay des stats (FPS) et la Toolbar sont maintenant parfaitement ancrés à la zone du "Central Node" du DockSpace. J'utilise `ImGui::DockBuilderGetCentralNode(dockspaceId)` pour calculer `viewportPos_` et `viewportSize_`.
- **Bug du zoom molette résolu :** Dans `Engine.cpp`, l'ordre de la boucle a été corrigé. L'appel à `imgui_->beginFrame()` est maintenant exécuté **avant** `processInput(realDt)`, ce qui permet à `io.MouseWheel` d'être correctement peuplé par GLFW avant qu'on ne lise la valeur pour le zoom de la caméra !

## Prochaine étape : Sérialisation (Copy/Paste/Duplicate)
- L'UI de `EditorUI` contient des boutons `Duplicate`, `Copy` et `Paste` dans le menu `Edit` et dans le contexte du Scene Tree.
- Ces boutons n'ont pas encore de logique profonde, car ils attendent ton implémentation de la **Sérialisation de scène** (pour pouvoir cloner ou sauvegarder l'état complet d'un nœud et de ses enfants). À toi de jouer !
