# XR / OpenXR — PCVR Quest Link (multiview, auto-détection)

> Plan d'implémentation **validé**. Repris à l'Étape A. Découpage `src/xr/` en
> petits types RAII (pas de god class), style `VulkanDevice`/`Swapchain`/`Pipeline`.

## Context

Objectif : lancer NextEngine en VR via **Meta Quest Link (PCVR)**, **regarder
autour et tourner la tête avec suivi** (head tracking). Choix validés :
**multiview** (1 passe, cible mobile/VR de la roadmap) + **auto-détection** du
casque au démarrage. **Fondation déjà faite + commitée** (`baa01ec`) : OpenXR-SDK
vendu (`third_party/openxr`, release-1.1.60), `openxr_loader` statique buildé +
linké à `ne_engine`. Le runtime actif (Oculus) est trouvé par le loader via le
registre.

**Réalité importante** : le rendu/tracking XR **ne se valide que dans le casque**
(compiler ≠ marcher). On procède donc par **étapes amenées en route avec
retour casque** ; chaque étape compile + commit.

### Contraintes d'architecture (déjà explorées)
- `VulkanDevice` (`src/graphics/VulkanDevice.cpp`) crée lui-même `VkInstance` /
  `VkPhysicalDevice` / `VkDevice`. En XR, **OpenXR doit piloter** cette création
  (`XR_KHR_vulkan_enable2` : `xrCreateVulkanInstanceKHR`,
  `xrGetVulkanGraphicsDevice2KHR`, `xrCreateVulkanDeviceKHR`) et **choisir le GPU**
  du casque. → l'ordre d'init de l'`Engine` change en mode XR.
- `Renderer` (`src/render/Renderer.cpp`) est **mono → swapchain GLFW**, avec
  GI/shadows/HDR/tonemap/ImGui. La présentation est la « couture » XR.
- Boucle : `Engine::run` est cadencée GLFW ; en XR c'est `xrWaitFrame` qui cadence.

---

## Étape A — Session XR + device piloté OpenXR + sanity 1 vue

Objectif : l'app démarre une **session VR** (Quest Link), affiche la scène
(d'abord **une seule vue** suivie par la tête, pour valider toute la chaîne avant
le multiview), sans casser le mode desktop.

### A1. Module `src/xr/` — découpé par responsabilité (PAS de god class)
Même esprit que `VulkanDevice`/`Swapchain`/`Pipeline` : chaque type possède ses
handles (RAII), emprunte ce dont il a besoin, copies interdites. 5 petits fichiers
+ 1 header math, chacun une seule responsabilité :

- **`XrMath.hpp`** (header-only, pur) : `projectionFromFov(XrFovf, near, far)→mat4`
  (frustum asymétrique, flip Y Vulkan + depth 0..1) ; `viewFromPose(XrPosef)→mat4` ;
  `poseToTransform(XrPosef)→{pos,quat}`. Aucune dépendance OpenXR-runtime.

- **`XrInstance.{hpp,cpp}`** — *cycle de vie instance + system, rien d'autre*.
  `static bool headsetPresent()` (probe instance+`xrGetSystem` HMD, détruit, bool).
  Possède `XrInstance` + `XrSystemId`. Charge les `PFN_xr…KHR` (récupérées via
  `xrGetInstanceProcAddr`). Accesseurs `instance()/systemId()/pfn…()`.

- **`XrVulkanBinding.{hpp,cpp}`** — *le pont OpenXR↔Vulkan, en fonctions libres*
  (pas une classe), prenant un `const XrInstance&`. C'est ce que `VulkanDevice`
  appelle en mode XR :
  `createVulkanInstance(xr, VkInstanceCreateInfo)→VkInstance`
  (`xrGetVulkanGraphicsRequirements2KHR` + `xrCreateVulkanInstanceKHR`) ;
  `pickPhysicalDevice(xr, VkInstance)→VkPhysicalDevice`
  (`xrGetVulkanGraphicsDevice2KHR`) ;
  `createVulkanDevice(xr, VkPhysicalDevice, VkDeviceCreateInfo)→VkDevice`.

- **`XrSwapchain.{hpp,cpp}`** — *un swapchain XR couleur* (miroir de
  `graphics/Swapchain`) : `xrCreateSwapchain` + `xrEnumerateSwapchainImages`
  (`VkImage[]`/views), `acquire()/wait()/release()`. Format = intersection de
  `xrEnumerateSwapchainFormats` avec un sRGB voulu. RAII.

- **`XrSession.{hpp,cpp}`** — *session + espace + vues + frame loop*, possède les
  `XrSwapchain`. Emprunte `XrInstance&` + `VulkanDevice&`.
  - ctor : `XrGraphicsBindingVulkan2KHR` + `xrCreateSession` ; espace
    `XR_REFERENCE_SPACE_TYPE_LOCAL` ; vues `PRIMARY_STEREO` (2, rect recommandé) ;
    crée le(s) `XrSwapchain`.
  - `pollEvents()` : `xrPollEvent` → machine d'états (begin/end session sur
    READY/STOPPING, quit sur EXITING). `running()/shouldRender()`.
  - `beginFrame()→FrameViews{ images, view[2], proj[2], headPose }` :
    `xrWaitFrame`+`xrBeginFrame`, si render `xrLocateViews` (via `XrMath`) +
    acquire/wait swapchain.
  - `endFrame()` : release + `xrEndFrame`(`XrCompositionLayerProjection`).

### A2. `VulkanDevice` — init pilotée OpenXR (chemin desktop intact)
- Ajouter un chemin « XR » : reçoit un `XrInstance*`. Si présent, `createInstance`/
  `pickPhysicalDevice`/`createLogicalDevice` délèguent aux **fonctions libres**
  `XrVulkanBinding::*` (fusion des extensions moteur + requises ; pas de surface
  GLFW en XR). Si `nullptr` → chemin desktop actuel **strictement inchangé**.

### A3. `Engine` — auto-détection + ré-ordonnancement init
- Au démarrage : `if (XrInstance::headsetPresent())` → créer `XrInstance`, puis
  `VulkanDevice(&xrInstance)`, puis `XrSession(xrInstance, device)`. Sinon chemin
  desktop actuel (Window + VulkanDevice + Swapchain).
- Membres `std::unique_ptr<XrInstance> xrInstance_` + `std::unique_ptr<XrSession>
  xrSession_` ; flag `xrMode_`.
- **Sanity A** : boucle XR minimale — `xrSession_->pollEvents()` ; si render,
  `auto fv = xrSession_->beginFrame()` → `renderer_->drawFrame(scene, eyeCam, …)`
  dans l'image XR d'**un œil**, `xrSession_->endFrame(fv)`. But : voir la scène
  bouger quand on tourne la tête.

### A4. CMake / divers
- Ajouter les `.cpp` du module `src/xr/` à `ne_engine`. Définir
  `XR_USE_GRAPHICS_API_VULKAN` + `XR_USE_PLATFORM_WIN32` avant
  `<openxr/openxr_platform.h>`.
- Récupérer les `PFN_xr...KHR` via `xrGetInstanceProcAddr` (les fonctions
  d'extension ne sont pas exportées par le loader).

**Test casque (utilisateur)** : Quest Link actif → lancer l'exe → une session VR
démarre, la scène s'affiche dans le casque et **suit la tête**. Rapporter logs +
ce qui s'affiche (noir / une vue / tracking ok).

---

## Étape B — Multiview (stéréo en 1 passe)

Une fois A validé (session + tracking ok en mono) :
- **Cibles 2-layers** : color/depth/HDR du `Renderer` deviennent des images
  `arrayLayers=2` (`VkImageView` 2D_ARRAY) ; `vkCmdBeginRendering` avec
  `viewMask = 0b11` (active multiview) sur une passe ; resolve/tonemap par layer.
- **UBO caméra en tableau** : `view[2]`, `proj[2]` (set 0 binding 0) ; `shader.vert`
  utilise `gl_ViewIndex` (`#extension GL_EXT_multiview`). Idem passes qui
  dépendent de la vue (forward, skybox, transparents). Shadows/GI = vue-
  indépendants → inchangés.
- **Présentation** : copier/rendre les 2 layers vers la swapchain XR
  (swapchain à `arraySize=2` → `imageArrayIndex` par vue dans la layer projection,
  OU 2 swapchains). `xrEndFrame` avec les 2 `ProjectionView`.
- ImGui : overlay 2D → soit désactivé en XR, soit rendu sur un layer/quad
  (hors périmètre « regarder autour » ; le désactiver en XR pour commencer).

**Test casque** : image stéréo correcte (pas de double image, bonne profondeur),
confort (pas de juddering).

---

## Étape C — Finitions « regarder autour »
- `headPose()` → caméra moteur (la fly-cam desktop est désactivée en XR ; la tête
  pilote la vue). Option : déplacement par stick plus tard.
- Recentrage (`xrRequestReferenceSpaceBounds`/recenter) optionnel.
- Hors périmètre (plus tard) : contrôleurs (action sets), hand tracking,
  passthrough, package d'interaction.

---

## Fichiers clés
- Nouveaux (module `src/xr/`, découpé) : `XrMath.hpp`, `XrInstance.{hpp,cpp}`,
  `XrVulkanBinding.{hpp,cpp}`, `XrSwapchain.{hpp,cpp}`, `XrSession.{hpp,cpp}`.
- Modifiés : `src/graphics/VulkanDevice.{hpp,cpp}` (chemin XR via XrVulkanBinding),
  `src/Engine.{hpp,cpp}` (auto-détection, ordre d'init, boucle XR),
  `src/render/Renderer.{hpp,cpp}` + `shaders/shader.vert` (multiview, Étape B),
  `CMakeLists.txt`, `CLAUDE.md`.
- Réutilisés : `core/Camera` (conventions proj/flip Y), pipeline de scène existant.

## Vérification
- Build à chaque étape : `cmake --build build -j 2` (sans warning sur nos TUs).
- **Bring-up casque (indispensable, par l'utilisateur)** : Quest Link branché +
  runtime Oculus = OpenXR par défaut → lancer l'exe → session VR, scène visible,
  tête suivie. Itérer A → B → C avec retour (logs + observation casque) ; je n'ai
  pas le matériel donc tu es « mes yeux » pour la mise au point.

## Risques connus
- Ordre instance/device OpenXR↔Vulkan strict (sinon `xrCreateSession` échoue).
- Format swapchain XR à intersecter (sinon image noire / couleurs fausses).
- Conventions matrices (flip Y Vulkan + depth 0..1) sur la proj asymétrique.
- Multiview à travers GI/HDR/tonemap = le plus gros risque → fait en Étape B
  seulement après que A (mono+tracking) marche.
