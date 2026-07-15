# Positionnement et pistes marketing de SaidaEngine

Mise à jour : 2026-07-15.

Ce document est une stratégie, pas une liste de fonctionnalités livrées. Toute
démonstration publique doit utiliser un artefact reproductible et annoncer les
limites de `docs/V1_KNOWN_LIMITATIONS.md`.

## État des idées historiques

| Idée | État réel |
|---|---|
| Serveur MCP | **Implémenté.** Serveur MCP in-process de l'éditeur, bridge stdio et outils de scène/code/validation existent. Il reste à renforcer permissions, transactions, dry-run/diff et rollback avant de le présenter comme sûr pour un agent autonome. |
| Démo IA autonome | **À produire.** Les briques MCP existent, mais aucune démo ne doit promettre un mini-jeu « complet » tant que le chemin ship, l'UI web, les snapshots et le sandbox ne sont pas fermés. |
| Hot reload | **Partiel.** QuickJS/ScriptBehaviour et certaines surfaces UI ont du hot reload transactionnel. Il n'existe pas de promesse générale de hot reload C++/DLL/Lua ; Lua n'est pas le langage de scripting retenu. |
| Ray tracing / convertisseurs | **Non prioritaire pour V1.** Le moteur possède déjà un renderer riche. L'intégrité des projets, le shipping et le cycle de vie des assets priment sur une nouvelle feature graphique. |
| FOSS / local-first | **Positionnement valide à cadrer.** Le dépôt contient GPL-3.0 et le runtime est local. Finaliser notices, SBOM et revue des licences avant campagne publique. |
| Playground WebAssembly | **Fondations présentes.** Runtime d'authoring et player WebGPU existent. Il n'existe pas encore de playground public isolé permettant de saisir un prompt en sécurité. |

## Positionnement honnête recommandé

À court terme, SaidaEngine peut être présenté comme :

- un moteur C++17/Vulkan/WebGPU expérimental et local-first ;
- un éditeur pilotable par MCP avec opérations structurées ;
- un projet Alpha doté d'un player web, d'un runtime d'authoring et d'un jeu
  témoin, mais pas encore d'une V1 stable.

Ne pas revendiquer pour l'instant :

- une supériorité de performance sans benchmarks reproductibles ;
- une parité desktop/Web complète ;
- un moteur sûr pour exécuter des projets tiers non fiables ;
- une compatibilité V1 garantie ;
- une génération de jeu complet depuis un prompt ;
- un support XR production sans matrice matérielle.

## Démonstration publique à viser

Une démo crédible devrait être reproductible depuis un tag signé et montrer :

1. création/modification d'une scène via MCP avec diff et validation ;
2. ouverture du même projet dans l'éditeur ;
3. export desktop et web via le vrai BuildExporter ;
4. exécution du jeu témoin avec les limitations visibles ;
5. hashes, version moteur et instructions permettant à un tiers de reproduire
   le résultat.

Les objectifs de vues, d'étoiles ou de calendrier ne doivent pas être présentés
comme garantis.
