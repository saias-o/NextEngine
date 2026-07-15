# Limitations connues de la V1

Mise à jour : 2026-07-15. Le projet est encore **Alpha** ; cette liste décrit
les limites connues du candidat V1, pas une release V1 déjà publiée.

- Le player Web cible WebGPU. Un navigateur sans WebGPU ne peut pas lancer le
  jeu et affiche un diagnostic au démarrage.
- Le player Web n'a pas encore les nœuds UI (`UICanvasNode`/`UITextNode`…) :
  une scène qui en contient est désormais refusée explicitement au chargement.
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
- Le snapshot/fold d'authoring est maintenant fail-closed : les types et
  behaviours supportés round-trippent, les refs Mesh restent opaques et un type
  inconnu fait échouer le batch sans publier de snapshot. Le contrat headless ne
  couvre toutefois pas encore UI et tous les nœuds physiques ; ces
  scènes sont refusées explicitement jusqu'à l'alignement des registres.
- Le runtime d'authoring Web charge atomiquement et refuse désormais tout type
  absent de son manifeste (`Node`, `MeshNode`, `Camera`, `LightNode`, `Water`,
  `ParticleSystem`) ainsi que tout behaviour. Les registres natif, authoring Web
  et player Web ne sont cependant pas identiques. Le player publie son registre
  exact dans `saida_player_status`, refuse les types/behaviours absents et ne
  passe à `ready` qu'après la validation des autoloads ; un manifeste natif ne
  prouve toujours pas qu'un type est exécutable sur une autre plateforme.
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

Cet invariant est maintenant respecté aux frontières snapshot headless,
authoring Web et player Web. La couverture incomplète de types (notamment UI)
reste une limitation fonctionnelle et une scène incompatible est bloquée.
