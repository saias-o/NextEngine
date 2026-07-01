# Plan RHI — Étape 16.3 (extraction de l'abstraction GPU)

> But : rendre le rendu **indépendant de l'API GPU** pour que le backend WebGPU
> (16.4) puisse se substituer à Vulkan sans toucher au code de rendu — tout en
> gardant le desktop **identique** à chaque étape. Esprit Saida : **léger, propre,
> optimisé**.

## 1. Décision d'architecture : RHI mince, backend au **compile-time**

Un build ne cible **qu'un seul** backend (desktop = Vulkan, web = WebGPU). Donc
**pas de dispatch virtuel runtime** (pas de vtable dans le chemin de frame) :
l'interface est *implicite* (duck-typing C++), et `namespace rhi` **type-alias**
le backend actif :

```cpp
// src/rhi/Rhi.hpp
namespace saida::rhi {
#if defined(SAIDA_RHI_WEBGPU)
    namespace backend = webgpu;
#else
    namespace backend = vulkan;   // défaut desktop / XR
#endif
    using Device        = backend::Device;
    using Buffer        = backend::Buffer;
    using Texture       = backend::Texture;
    using CommandEncoder= backend::CommandEncoder;
    // …
}
```

Le `Renderer` écrit `rhi::Buffer`, `rhi::CommandEncoder` — zéro `Vk*`. Les deux
backends exposent la **même API** (mêmes signatures) ; le compilateur relie le bon.
Coût : nul à l'exécution (pas d'indirection), et le code mort de l'autre backend
n'est jamais compilé.

**Ce qu'on abstrait** : la *création de ressources* et *l'enregistrement de
commandes*. **Ce qu'on n'abstrait pas** : la logique de rendu (`Renderer`,
`GIVolume`, `ShadowMap`, `PostProcessor`, features) — elle reste telle quelle,
elle cesse juste de parler `Vk*`.

## 2. Surface RHI (≈12 types)

| Type `rhi::` | Source Vulkan actuelle | Notes |
|---|---|---|
| `Capabilities` | `RenderCapabilities` | dé-Vulkanisé (voir §3), + flags web |
| `Device` | `VulkanDevice` | device/queues/allocateur/caps |
| `Surface` | `Swapchain` | présentation (couture Desktop/XR/Web) |
| `Buffer` | `Buffer` (+ `MemoryUsage`) | déjà propre |
| `Texture` / `Sampler` | `Texture` / `StorageImage` | image + view + sampler |
| `ShaderModule` | SPIR-V (Vk) / WGSL (Web) | chargé par le backend |
| `Pipeline` / `ComputePipeline` | `Pipeline` / `ComputePipeline` | |
| `BindGroupLayout` / `BindGroup` | `VkDescriptorSetLayout` / `Set` | set 0/1 → bind groups 1:1 |
| `CommandEncoder` | `VkCommandBuffer` | **sync cachée dedans** |
| `RenderPass` / `ComputePass` | dynamic rendering begin/end | encodeurs de passe |

## 3. Première brique : `rhi::Capabilities` (dé-Vulkaniser)

`RenderCapabilities` est déjà presque neutre — un seul champ Vulkan
(`VkSampleCountFlagBits maxSamples`). On le passe en `uint32_t maxSamples`
(nombre d'échantillons), et on ajoute les *capabilities* que le rendu lira pour
brancher desktop/web **sans `#ifdef` dans la logique** (plan §3.3) :

```cpp
bool bindless;          // false sur WebGPU courant → fallback bind-group/texture
bool pushConstants;     // false sur WebGPU → UBO (déjà géré côté shaders 16.2)
bool drawIndirectCount; // false sur WebGPU → draws dégénérés instanceCount=0
```

Le `Renderer` lit `caps.bindless` etc. au lieu de supposer Vulkan. Étape
sûre et isolée : aucun changement de comportement desktop (les valeurs Vulkan
restent les mêmes), juste un type de champ et des drapeaux ajoutés.

## 4. Les 5 pièges (rappel, à honorer dans le design des types)

1. **Push constants** → storage buffer indexé (shaders web déjà en UBO, 16.2).
2. **`DrawIndirectCount`** → compute écrit `instanceCount=0`, boucle indirect simple.
3. **Bindless** → `caps.bindless` + fallback bind-group-par-texture.
4. **Sync (semaphores/fences)** → **cachée** dans `CommandEncoder`/`Surface` (no-op WebGPU).
5. **Boucle bloquante** → `Engine::run()` scindé en `tick()` (piège web, traité en 16.4).

## 5. Phasage (incréments **complets et vérifiés**, desktop identique à chaque commit)

- **16.3.a** — `namespace rhi` + `rhi::Capabilities` dé-Vulkanisé (+ flags web). Backend Vulkan aliasé.
- **16.3.b** — `rhi::Buffer` (le plus simple ; surface minimale, `MemoryUsage` déjà là).
- **16.3.c** — `rhi::Texture` / `Sampler` / `ShaderModule`.
- **16.3.d** — `rhi::Pipeline` / `BindGroup(Layout)`.
- **16.3.e** — `rhi::CommandEncoder` / passes (le gros : `Renderer` cesse de toucher `Vk*`).
- **16.3.f** — `rhi::Device` / `Surface` (couture présentation).

Règle : à chaque incrément, `cmake --build build` vert + `ctest` vert + SPIR-V/
comportement desktop inchangé. On ne commence 16.4 (backend WebGPU) qu'une fois
la couture RHI complète et le desktop prouvé intact.

## 6. Ce qu'on ne fait PAS (rester léger)

- Pas de render-graph AAA, pas de barrier-tracking automatique générique.
- Pas de dispatch virtuel : backend au compile-time.
- Pas d'abstraction de la logique de rendu — seulement des ressources/commandes.
