# NextEngine — Roadmap des tâches restantes

## 🔴 CRITIQUE — Optimisation globale du moteur
- [x] **Optimiser performances** : scène simple (water shader) → 40 fps sur PC portable puissant (terminé)

---

## Étape 8 — Couche jeu
- [ ] Runtime standalone sans éditeur

---

## Étape 9 — Rendu global / GI pragmatique
- [ ] Radiance Cascades 2D / World Radiance Cache / froxels volumétriques (recherche future)

---

## Étape 12 — UI 2D
- [ ] Backend GPU/Vulkan RmlUi pour gros documents animés (optionnel)

---

## Étape 13 — Intégration LLM Native
- [ ] Inspecteur générique pour behaviours réfléchis
- [ ] World model (état du monde pour l'IA)
- [ ] Skills exécutables
- [ ] Agents autonomes

---

## Étape 14 — XR / OpenXR
- [ ] Découplage Renderer : extraire méthode commune `recordMeshDraws()`
- [ ] Hand tracking skeletal (`XR_EXT_hand_tracking`)
- [ ] MSAA multiview + resolve par layer
- [ ] ImGui overlay en XR
- [ ] Culling stéréo combiné
- [ ] Backend d'anchors réel

---

## Étape 15 — Build & Release Windows
- [ ] Gestion versions, métadonnées executable, icône du jeu
- [ ] LTO build optimization

---

## Étape 16 — Effets visuels avancés
- [ ] Système de Particules GPU-driven (feu, pluie, magie, flocons, explosion)
- [ ] Shader Outline (non-destructif, selection/effect)
- [ ] Shader Fresnel (rim-light, halo, highlight)
- [ ] Shader Gouttes de pluie paramétrique (réaliste, mobile/VR optimisé)

---

## Priorité de travail

**TERMINÉ** : Optimisation performances
**TRÈS HAUTE** : Particules, Outline, Fresnel
**HAUTE** : Découplage Renderer, Gouttes de pluie, Inspecteur behaviours
**MOYENNE** : XR handtracking/MSAA/ImGui, Versions, World model, Skills
**BASSE** : Culling stéréo, anchors backend, LTO, GPU RmlUi, Agents
**FUTURE** : Radiance Cascades, recherche avancée
