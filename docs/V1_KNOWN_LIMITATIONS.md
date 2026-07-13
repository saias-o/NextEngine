# Limitations connues de la V1

- Le player Web cible WebGPU. Un navigateur sans WebGPU ne peut pas lancer le
  jeu et affiche un diagnostic au démarrage.
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

Les limites de plateforme sont annoncées par `PlatformCaps` au boot. Une scène
qui requiert une capacité absente doit échouer explicitement ou utiliser un
fallback visible ; elle ne doit pas perdre silencieusement du contenu.
