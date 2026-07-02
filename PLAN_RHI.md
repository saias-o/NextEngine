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

## 7. Design 16.3.d/e/f (le gros morceau)

État des lieux mesuré : ~236 sites `vkCmd*`/`VkCommandBuffer` dans render+graphics+fx,
23 fichiers créant des `VkDescriptorSetLayout`, 17 sites de construction de `Pipeline`.
Non découpable *naïvement* — mais découpable avec la règle d'interop ci-dessous.

### 7.1 La règle d'interop qui rend e découpable

`rhi::vulkan::CommandEncoder` est une **vue non-possédante** sur un
`VkCommandBuffer` (trivialement copiable, zéro état), avec un **escape hatch**
`handle()` réservé à la migration :

```cpp
class CommandEncoder {           // backend vulkan
public:
    explicit CommandEncoder(VkCommandBuffer cmd);
    VkCommandBuffer handle() const;   // escape hatch — usage décroissant, 0 à la fin de e
    ...
};
```

Conséquence : le `Renderer` garde son `VkCommandBuffer` pendant toute la
migration ; chaque sous-système converti prend un `rhi::CommandEncoder`, les
autres continuent en `Vk*`. **Chaque commit reste desktop-vert.** Côté WebGPU
(16.4) la même classe enveloppera `WGPUCommandEncoder` — sans `handle()` Vulkan,
ce qui est correct puisque plus aucun appelant ne l'utilisera.

### 7.2 Encodeurs de passe (formes WebGPU, mapping 1:1 dynamic rendering)

```cpp
auto rp = enc.beginRenderPass(desc);   // vkCmdBeginRendering + viewport/scissor
rp.setPipeline(pipe); rp.setBindGroup(0, set); rp.setPushConstants(...);
rp.setVertexBuffer(...); rp.setIndexBuffer(...);
rp.draw(...); rp.drawIndexed(...); rp.drawIndexedIndirect(...);
rp.end();                              // vkCmdEndRendering

auto cp = enc.beginComputePass();
cp.setPipeline(...); cp.setBindGroup(...); cp.dispatch(x, y, z); cp.end();
```

`RenderPassDesc` : attachements couleur (view, loadOp/clear, resolve), depth
(view, load/store, readOnly), extent, viewMask. Copies (staging) : `enc.copyBufferToBuffer`,
`enc.copyBufferToTexture` + `Device::withSingleTimeEncoder(fn)` remplace
begin/endSingleTimeCommands chez les appelants.

### 7.3 Sync : transitions explicites mais neutres (pas de tracking)

Les barriers restent **explicites** (esprit §6 : pas de tracking automatique),
mais exprimées en états neutres — mapping mécanique 1:1 des barriers manuelles
actuelles (GpuSync.hpp) :

```cpp
enum class ResourceState { Undefined, ShaderRead, ColorAttachment, DepthWrite,
                           DepthRead, StorageReadWrite, CopySrc, CopyDst, Present };
enc.transition(view/texture, oldState, newState, baseLayer, layerCount);
enc.storageBarrier();   // compute→compute (cmdComputeToComputeBarrier)
```

Backend Vulkan : mappe vers layout+stage+access (ce que GpuSync construit à la
main aujourd'hui). Backend WebGPU : **no-op** (le driver trace). L'ancien état
reste fourni par l'appelant (PostProcessor trace déjà le layout de ses targets —
il continue, en `ResourceState`).

### 7.4 `rhi::BindGroupLayout` / `rhi::BindGroup` (16.3.d)

- `BindGroupLayout` : builder neutre `{binding, BindingType {UniformBuffer,
  StorageBuffer, CombinedImageSampler, SampledTexture, Sampler, StorageImage},
  visibility, count}` → enveloppe `vkCreateDescriptorSetLayout`.
- `BindGroup` : créé depuis layout + entrées `{binding, buffer|texture|view+sampler}`.
  Pool Vulkan **caché** dans le backend (pool interne au layout, growable) —
  WebGPU n'a pas de pools. **Immutable** : pour changer un binding, on recrée
  (les re-points actuels — GI atlas, tonemap — sont rares ; recréer est correct
  et simple). Les 23 fichiers se convertissent par lots, build vert à chaque lot
  (la création est indépendante du bind, qui reste `Vk*` jusqu'à e).
- Wrinkle 16.4 connu (noté en 16.2) : les shaders web séparent texture/sampler et
  poussent les push constants en UBO set 3 → les layouts web divergeront. On ne
  le résout pas ici ; le type neutre couvre les deux styles, le backend WebGPU
  décrira ses layouts aux quelques sites globaux concernés.

### 7.5 `rhi::Pipeline` neutre (fin de d)

Constructeur → desc neutre : formats `rhi::Format`, `samples` uint32, enums
neutres `CompareOp`/`CullMode`/`Topology` (+ `BlendMode` déjà neutre), layouts
`rhi::BindGroupLayout*`, viewMask (multiview = desktop/XR-only, caps en 16.4).
`bind()` reste Vulkan jusqu'à e (il devient `rp.setPipeline`). Idem
`ComputePipeline`. Les conversions `VkFormat↔rhi::Format` aux coutures
Swapchain/HDR sont temporaires et disparaissent en f.

### 7.6 Ordre de conversion (lots-commits, desktop-vert à chaque commit)

- **d.a** — types `BindGroupLayout`/`BindGroup` + conversion graphics/ (UIRenderer,
  ResourceManager, tonemap/culling du Renderer côté *création* seulement).
- **d.b** — conversion render/features + GIVolume + ParticleRuntime + PostProcessor.
- **d.c** — desc neutre `Pipeline`/`ComputePipeline` (17 sites).
- **e.a** — `CommandEncoder` + `ResourceState` + pass encoders ; **pilote : ShadowMap**
  (petit : 6 vkCmd, mais complet — barriers, depth-only pass layerée, et sa
  `DrawGeometryFn` traverse la frontière Renderer → bon test d'ergonomie).
  Inclut la conversion du pipeline brut de ShadowMap vers `rhi::Pipeline`.
- **e.b** — copies/staging : Texture, Mesh, uploads Buffer, `withSingleTimeEncoder`.
- **e.c** — features (Skybox, Water, DebugLines, Outline, Particle) + UIRenderer
  (`FrameContext.cmd` devient un encoder).
- **e.d** — PostProcessor (le test de vérité des transitions).
- **e.e** — GIVolume (compute chains, storageBarrier).
- **e.f** — ParticleRuntime (indirect draw).
- **e.g** — Renderer (les ~51 sites : tonemap, culling + fallback
  `drawIndexedIndirectCount` derrière `caps.drawIndirectCount`, ImGui via
  escape hatch — l'éditeur est desktop-only, jamais compilé web).
- **f** — `rhi::Device` (VulkanDevice) + `rhi::Surface` (Swapchain, acquire/present,
  sémaphores/fences cachés). Les conversions Format aux coutures disparaissent.

### 7.7 Design 16.3.f (Device / Surface / render targets)

- **`rhi::Device`** = `VulkanDevice`, aliasé tel quel (fwd-decl, zéro include).
  Sa surface utile aux appelants convertis est déjà neutre :
  `capabilities()`, `withSingleTimeEncoder()`, la construction des types rhi.
  Le backend WebGPU exposera la même surface.
- **`rhi::RenderTexture`** : création neutre des render targets (HDR, MSAA,
  depth-resolve, shadow array, bloom, atlas GI, voxel 3D). Desc = format
  `rhi::Format`, extent (depth>1 = 3D), layers (array), samples,
  `rhi::TextureUsage` (bitmask Sampled/Storage/ColorAttachment/DepthAttachment/
  CopySrc/CopyDst/Transient), catégorie MemoryProfiler optionnelle. Expose la
  vue whole-resource (2D / 2D_ARRAY / 3D auto) et des vues par layer
  (attachements shadow/XR). Remplace `StorageImage` (supprimé) et tous les
  `vmaCreateImage` de render/. `Texture` (assets échantillonnés + mips) reste
  un type distinct.
- **`rhi::Surface`** = `Swapchain`, qui absorbe la sync de présentation :
  fences de frame + sémaphores acquire (déplacés du Renderer), sémaphores
  renderFinished (déjà là). API : `waitFrame(frame)`, `acquire(frame, &image)`
  (false = out-of-date, l'appelant recrée), `submitAndPresent(cmd, frame,
  image)` (true = recreate nécessaire — out-of-date/suboptimal/resize).
  Le Renderer ne touche plus ni VkSemaphore/VkFence ni vkQueueSubmit/Present.
  Côté WebGPU : `acquire` = getCurrentTexture, submit = queue.submit, pas de
  sync exposée (no-op) — même forme d'appel. Côté XR, la présentation reste à
  la session OpenXR (le Renderer XR ne crée pas de Surface), couture inchangée.

### 7.8 Cas particuliers (décidés)

- **GpuProfiler** (timestamps) : outillage desktop — prend l'encoder et utilise
  `handle()` en interne ; exclu du build web, pas d'abstraction.
- **TimelineSemaphore/GpuSync.hpp** : infrastructure dormante (zéro usage hors
  header) — ne pas abstraire ; les helpers barrier sont absorbés par 7.3.
- **ImGuiLayer** : éditeur desktop-only → escape hatch permanent, assumé.
- **XR** : chemins `drawXr` sous `SAIDA_ENABLE_XR`, convertis avec e.g via les
  mêmes encoders (le multiview passe par `RenderPassDesc.viewMask`).
