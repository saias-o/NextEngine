# SaidaEngine — Plan vers la V1

Mise à jour : 2026-07-21.

**Verdict : NO-GO pour une V1 publique.** Ce fichier ne liste que ce qui reste
**ouvert**. Les gates P0.1 à P0.6 sont fermées ; leurs preuves vivent dans
l'historique Git et les contrats correspondants dans [SPEC.md](SPEC.md). La
dette de qualité connue est dans [AUDIT_V1.md](AUDIT_V1.md).

Il ne reste qu'**une intervention P0** : la signature de l'installeur (clé de
publication). Aucune gate n'est cochée sans le run, le commit ou le corpus exact
qui la prouve.

## P0.6 — Manette physique (fermée)

- [x] Tester le backend desktop avec des manettes physiques Xbox/PlayStation
  reconnues par GLFW. **Validé le 2026-07-21** : une manette physique en mode
  XInput (`isGamepad=YES`, nom `XInput Gamepad (GLFW)`) exercée par
  [`tools/gamepad_probe.cpp`](tools/gamepad_probe.cpp) — qui appelle exactement
  le chemin GLFW du moteur (`glfwJoystickIsGamepad` → `glfwGetGamepadState` →
  `glfwGetGamepadName` de `src/core/Input.cpp`, ordre de mapping = miroir 1:1 de
  `GamepadButton`/`GamepadAxis`) — a fait remonter au bon label standard les 14
  boutons mappables et les 6 axes (sticks pleine amplitude −1..+1, gâchettes
  analogiques). Guide non exposé par XInput (attendu, non bloquant).

Le reste de l'input était déjà fermé : backend navigateur Gamepad API, mapping
standard normalisé (dont triggers) et hotplug, rebinding runtime et profils JSON
validés/appliqués atomiquement, mono-joueur assumé, touch en zones/gestes,
détection du dernier périphérique actif avec prompts UI adaptatifs, haptique Web
standard.

## P0.7 — Signature de l'installeur

- [ ] Produire l'installeur Windows **signé Authenticode** avec la clé de
  publication.

Le reste de la chaîne release est fermé : CI obligatoire (build natif, 50+ tests,
corpus V1, fold déterministe, Witness desktop et Web) ; `saida_tool`, player Web
et authoring WASM publiés comme artefacts pinnés ; preuve byte-identique
Windows/Linux sur les fixtures de fold ; archives et installeur NSIS
byte-reproductibles **avant signature**, validation récursive des imports DLL,
désinstallation inventoriée, rollback immuable documenté ; crash logs et symboles
liés à la version ; SBOM SPDX + notices GPL/tiers + inventaire licences/assets ;
support GPU/OS/navigateur et procédure de retrait dans
[docs/release-support.md](docs/release-support.md). La signature Authenticode
est une opération de publication séparée qui requiert un certificat de confiance
publique (voir [AUDIT_V1.md](AUDIT_V1.md) pour les options OSS).

## P1 — Qualité des sous-systèmes

Post-V1 sauf changement explicite de périmètre.

- [ ] XR : valider casques/runtimes ciblés, contrôleurs et hand tracking.
- [ ] XR : MSAA multiview/resolve, overlay ImGui et backend d'anchors réel.
- [ ] Physique : compléter queries, contraintes et diagnostics.
- [ ] Stabiliser le flag GPU-driven et benchmarker chemin classique, bindless,
  indirect draw et compute culling sur un corpus reproductible.
- [ ] Rendu : point-light cubemap shadows et persistance des lightmaps si
  incluses dans la promesse V1.
- [ ] Mesurer et optimiser LTO seulement après stabilité.

La dette structurelle (god classes, duplication du registre de types, magic
numbers) est cataloguée et priorisée dans [AUDIT_V1.md](AUDIT_V1.md).

## P2 — Hors V1, conservé comme décision

Ces éléments ne retardent pas la V1 sauf changement explicite de promesse :

- multiplayer réseau, grands terrains et frameworks de genre ;
- SIMD animation généralisé, pose sharing massif et GPU crowds ;
- Radiance Cascades et recherches GI avancées ;
- backend GPU RmlUi si le backend CPU respecte les budgets ;
- graph SaidaFX complet, trails/ribbons et collisions particules avancées ;
- world model, skills et agents autonomes ;
- store d'assets et services en ligne, portés par la plateforme Saida.

## Définition de V1

La V1 est atteinte uniquement si toutes les gates P0 sont cochées, depuis un
commit propre, avec artefacts exacts de release. Le même WitnessGame doit tourner
en éditeur, desktop autonome et Web ; les anciens projets doivent migrer ou être
refusés sans corruption ; la mémoire doit rester bornée ; les limitations
publiées doivent correspondre au comportement observé.
