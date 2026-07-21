# Support et retrait d'une release Saida Engine

Mise à jour : 2026-07-19. Ce document est le guide opérationnel de release. La
spécification technique reste [SPEC.md](../SPEC.md) et la checklist de fermeture
reste [PLAN_V1.md](../PLAN_V1.md).

## Matrice de support V1

Une plateforme n'est supportée que si le bundle exact d'une release propre,
identifié par son commit et son manifeste, passe les vérifications indiquées.

| Surface | Cible supportée | Prérequis | Preuve bloquante |
|---|---|---|---|
| Éditeur et player desktop | Windows 11 x64 | GPU et pilote Vulkan 1.3; UCRT système et `glfw3.dll` livrée | build UCRT64, CTest complet, corpus compatibilité, Witness exporté puis run + restart, installation et désinstallation exactes |
| Player Web | Chrome et Edge stables récents sur desktop | WebGPU activé, contexte sécurisé HTTP(S), COOP/COEP, MIME `application/wasm`, IndexedDB | build Emscripten, contrat runtime, Witness run + restart dans Chrome CI; recette externe Chrome et Edge |
| Outil headless `saida_tool` | Debian 12 x64, glibc 2.36 | aucune surface GPU requise pour validate/fold/export | build propre conteneur Debian, CTest complet et fold byte-identique Windows/Linux |
| Authoring WASM | navigateurs desktop de la ligne Player Web | WebAssembly, ES modules et hôte conforme au contrat d'authoring | build Emscripten et smoke Node bloquants |

La cible Vulkan publiée est 1.3. Le fait que le renderer puisse négocier
certaines fonctionnalités optionnelles ou une version de loader antérieure ne
constitue pas une promesse de support V1.

Les preuves de référence Windows ont été exécutées sur Windows 11 x64 avec un
GPU Intel Iris Xe. Chrome et Edge ont tous deux validé le bundle Web sur une
machine sans checkout moteur, MSYS2 ni SDK. Les versions exactes du navigateur,
de l'emsdk et le commit doivent être conservés dans l'identité de build de
chaque nouvelle release; une preuve ancienne ne qualifie pas automatiquement
une version future.

## Non supporté ou non qualifié

Les surfaces suivantes ne doivent pas être annoncées comme supportées en V1
tant qu'une preuve dédiée n'a pas fermé leur gate :

- éditeur ou player desktop Linux;
- macOS;
- Firefox, Safari et navigateurs mobiles;
- WebGPU désactivé, `file://`, serveur sans COOP/COEP ou mauvais MIME WASM;
- XR de production, casques, contrôleurs et hand tracking;
- adaptation visuelle des prompts aux manettes physiques Xbox/PlayStation;
- haptique desktop, non exposée par GLFW 3.x.

Un backend absent doit rester visible dans `PlatformCaps` et échouer ou se
dégrader explicitement. Il ne peut pas être reclassé comme supporté à partir
d'une compilation seule.

## Identité et promotion

L'identité d'une release est le fichier
`build/release/engine/release-manifest.json`, pas un nom mutable :

1. partir d'un commit propre et conserver son SHA complet;
2. générer le manifeste moteur et le bundle de conformité;
3. vérifier chaque fichier avec `tools/verify_engine_release.ps1`;
4. vérifier l'archive et l'installeur Witness, puis signer l'installeur avec la
   clé de publication et inventorier le SHA des octets signés;
5. conserver les artefacts CI dont le nom contient ce SHA;
6. promouvoir la plateforme et l'image conteneur par SHA ou digest immuable;
7. n'utiliser `latest` que comme alias pratique, jamais comme preuve ni comme
   unique référence de déploiement.

Une release ne doit jamais être reconstruite sous la même identité. Toute
différence d'octet exige un nouveau commit, un nouveau manifeste et une nouvelle
qualification.

## Retrait et retour à la version précédente

En cas de régression ou d'incident :

1. geler immédiatement la promotion du SHA concerné et relever le manifeste,
   les logs, les symboles et les digests déployés;
2. marquer la release GitHub comme retirée et publier un avis indiquant les
   plateformes, formats et versions touchés;
3. arrêter de servir ou d'installer ce SHA; ne jamais remplacer ses artefacts
   en place;
4. réépingler chaque consommateur sur le manifeste et le digest de la dernière
   release qualifiée;
5. rejouer `tools/verify_engine_release.ps1` ainsi que Witness run + restart sur
   cette version avant de rouvrir le trafic;
6. conserver la release retirée et ses preuves en accès d'audit, mais hors des
   canaux d'installation normaux;
7. corriger en avant avec une nouvelle identité et refaire la qualification
   complète.

Le rollback d'un binaire ne rétrograde pas automatiquement les données. Si une
release a écrit un schéma que la version précédente refuse, restaurer une
sauvegarde compatible ou publier une correction en avant; ne jamais réécrire
silencieusement les snapshots, projets ou saves. La révocation d'un certificat
de signature est une opération distincte, réservée à une compromission de clé
ou de chaîne de confiance.

Sous Windows, joindre le `.crash.log` et le `.dmp` issus de
`%LOCALAPPDATA%\SaidaEngine\CrashReports\<produit>\`. Le champ
`symbolsArtifact` du log désigne le bundle CI immuable à télécharger; vérifier
celui-ci avec `verify_release_symbols.ps1` avant toute analyse.

## Contenu et licences

`tools/generate_release_compliance.ps1` produit le SBOM SPDX 2.3, les notices,
l'inventaire hashé des assets/modèles et leur manifeste. Les entrées
`distribution: false` seraient exclues des bundles V1; depuis la purge
open-source du 2026-07-21 (retrait des projets legacy sans provenance et du
DamagedHelmet CC-BY-NC), le dépôt n'en contient plus aucune — tout asset suivi
est distribuable sous sa licence déclarée.

Le bundle de conformité est inclus dans le manifeste moteur et dans les
archives Witness. Ajouter une racine sous `third_party` ou un asset d'une
extension suivie sans décision explicite fait échouer la génération.
