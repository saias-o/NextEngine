# Limitations connues de la V1

Mise à jour : 2026-07-15. Le projet est encore **Alpha** ; cette liste décrit
les limites connues du candidat V1, pas une release V1 déjà publiée.

- Le player Web cible WebGPU. Un navigateur sans WebGPU ne peut pas lancer le
  jeu et affiche un diagnostic au démarrage.
- Le player Web n'a pas encore les nœuds UI (`UICanvasNode`/`UITextNode`…) :
  ils se dégradent en Node générique et `node.setText` y est un no-op signalé.
  Jolt, Web Audio, scripts et sauvegardes IndexedDB sont intégrés, mais la
  parité gameplay complète avec desktop n'est pas déclarée tant que le jeu
  témoin entier, UI incluse, ne passe pas sur les artefacts de release.
- L'audio Web démarre coupé tant que le navigateur n'a pas reçu un geste
  utilisateur (politique d'autoplay) ; il se débloque au premier clic/touche.
- Les builds Web doivent être servis par HTTP ; ouvrir `index.html` avec une URL
  `file://` n'est pas supporté.
- Le rendu XR n'a pas encore le MSAA multiview et demande une validation sur le
  casque ciblé.
- Les point lights n'ont pas de cubemap d'ombre.
- Les lightmaps sont régénérées et ne sont pas encore sérialisées dans le
  package.
- Les modèles glTF/GLB corrompus peuvent encore interrompre le player Web.
- Les caches `.sanimc`, `asset_registry.local.json` et `pipeline_cache.bin` ne
  font pas partie du contrat de compatibilité.
- Les axes gamepad sont encore marqués non implémentés dans `Input.cpp` alors
  que certaines capacités desktop annoncent Gamepad. Ne pas considérer cette
  capability comme une preuve de support complet.
- Le touch existe comme flux d'événements, pas encore comme système complet de
  bindings/rebinding multi-plateforme.
- QuickJS a des limites mémoire/stack mais pas encore d'interrupt handler ou de
  deadline d'exécution ; une boucle infinie ou une chaîne de jobs peut figer le
  runtime. La résolution de modules doit être confinée au package avant contenu
  tiers.
- Le snapshot/fold d'authoring ne préserve pas encore tous les types et
  behaviours. Les types non enregistrés peuvent retomber en `Node` ou être
  ignorés ; Mesh requiert un `ResourceManager` absent d'un chemin headless.
- Les registres de types natif, authoring web et loader web ne sont pas encore
  identiques. Un manifest natif ne prouve donc pas qu'un type est applicable et
  rechargeable dans le navigateur.
- L'AssetLoader asynchrone charge désormais les textures et les meshes `.obj`
  de scène (décodage worker, création GPU différée, fallbacks visibles) et
  décharge réellement le GPU au changement de scène (`trimUnused`,
  `gpuResidentBytes` stable sur 16 cycles en E2E). Restent hors intégration :
  budget GPU contraignant en cours de scène (LRU), rigs/animations dans le
  sweep, streaming Web (fetch/IDBFS), et l'identité stable des assets glTF
  mémoire (un Stop après changeScene en éditeur peut perdre un mesh à id
  dynamique dans le snapshot restauré).
- Le chemin export a des harnais CLI et des validations locales historiques,
  mais le clic Build UI et l'exécution sur machine vierge ne sont pas encore une
  gate de release reproductible.
- La licence projet est documentée GPL-3.0 via le fichier racine ; les notices
  projet et l'inventaire des licences dépendances/assets restent à finaliser
  avant distribution stable.

Les limites de plateforme sont annoncées par `PlatformCaps` au boot. Une scène
qui requiert une capacité absente doit échouer explicitement ou utiliser un
fallback visible ; elle ne doit pas perdre silencieusement du contenu.

Cet invariant n'est pas encore respecté partout ; les fallbacks génériques du
snapshot/loader sont précisément des blocages de publication.
