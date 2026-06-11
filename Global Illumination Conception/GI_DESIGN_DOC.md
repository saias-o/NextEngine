# Document de conception GI — Moteur Vulkan maison
## Quest standalone first · Scènes médicales semi-dynamiques

**Sources primaires utilisées**
- Sloan, P.-P. (2008). *Stupid Spherical Harmonics Tricks*. GDC 2008.
- Majercik et al. (2021). *Scaling Probe-Based Real-Time Dynamic GI for Production*. JCGT 10(2).
- Majercik et al. (2021b). *Dynamic Diffuse GI Resampling*. Computer Graphics Forum.

---

## Vue d'ensemble

La stack retenue couvre l'indirect diffuse, l'occlusion de contact et les spéculaires. Elle est ordonnée par priorité d'implémentation et calibrée pour Adreno 740 (Quest 3, 13.8 ms @ 72 Hz).

```
Système 1 — SH Probe Grid baked       ~0.05 ms   fondation indirect diffuse
Système 2 — DDGI dynamique            ~0.8  ms   dynamisme objets mobiles
Système 3 — GTAO réduit               ~0.5  ms   occlusion de contact
Système 4 — Reflection Probes baked   ~0.05 ms   spéculaires
─────────────────────────────────────────────────
Budget total GI                        ~1.4  ms   sur 13.8 ms disponibles
```

---

## Système 1 — SH Probe Grid baked

### Principe (source : Sloan 2008, §Irradiance Environment Maps)

On représente la radiance incidente en chaque probe sous forme de Spherical Harmonics ordre 2 (L2). SH L2 = 9 coefficients par canal RGB = 27 floats par probe. C'est suffisant pour capturer l'indirect diffuse basse fréquence (Sloan démontre que la convolution avec un cosinus clampé atténue agressivement les hautes fréquences, rendant L2 très précis pour le diffuse).

**Pourquoi L2 et pas L3 ?** Le coefficient L4 du noyau cosinus clampé est zéro (Sloan, §Normalization). Passer à L3 ajoute 7 coefficients supplémentaires pour un gain visuel négligeable sur du diffuse.

### Fonctions de base exactes (Sloan, Appendice A2)

Les formes polynomiales pour une direction normalisée `(x, y, z)` :

```
Bande 0 (1 terme) :
  y0  =  1 / (2√π)

Bande 1 (3 termes) :
  y1  = -√3 y / (2√π)
  y2  =  √3 z / (2√π)
  y3  = -√3 x / (2√π)

Bande 2 (5 termes) :
  y4  =  √15 yx / (2√π)
  y5  = -√15 yz / (2√π)
  y6  =  √5 (3z²-1) / (4√π)
  y7  = -√15 xz / (2√π)
  y8  =  √15 (x²-y²) / (4√π)
```

Ces constantes sont exactes. Toute autre valeur est fausse.

### Projection (bake offline)

Algorithme de Monte Carlo : pour chaque probe, lancer N rayons uniformément distribués sur la sphère, accumuler la radiance pondérée par les bases SH.

```
Pour chaque direction d (N rayons, distribution de Fibonacci) :
  radiance = trace_scene(probe_pos, d)
  poids    = 4π / N                        // solid angle moyen
  Pour i = 0..8 :
    coeffs[i] += radiance × SH_basis(i, d) × poids
```

**N recommandé :** 512 minimum pour qualité acceptable. Pas de contrainte temps réel.

**Distribution de Fibonacci (sphère) :**
```
Pour indice i sur N points :
  theta = arccos(1 - 2(i+0.5)/N)
  phi   = 2π × i × φ_or          // φ_or = (√5-1)/2 ≈ 0.6180339887
  d     = (sin θ cos φ, cos θ, sin θ sin φ)
```
Cette distribution est plus uniforme qu'un échantillonnage aléatoire et évite les clusters.

### Evaluation runtime (Sloan, Appendice A10)

Sloan donne une forme optimisée qui pré-bake les constantes dans les coefficients SH au moment du bake. Le shader évalue la radiance en une direction N (normale de surface) via :

```
Précalcul CPU (une fois par bake, depuis Sloan A10) :
  cAr = vec4(-fC1·L[3], -fC1·L[1],  fC1·L[2],  fC0·L[0] - fC3·L[6])  // canal R
  cAg = vec4(...)                                                          // canal G
  cAb = vec4(...)                                                          // canal B
  cBr = vec4( fC2·L[4], -fC2·L[5], 3fC3·L[6], -fC2·L[7])
  cBg, cBb idem
  cC  = vec3(fC4·L[8])

  avec fC0=1/(2√π), fC1=√3/(3√π), fC2=√15/(8√π), fC3=√5/(16√π), fC4=√15/(8√π)
```

```
Shader runtime (11 instructions) :
  x1.rgb = dot(cAr, N_4),  dot(cAg, N_4),  dot(cAb, N_4)   // N_4 = (Nx,Ny,Nz,1)
  vB     = N_4.xyzz × N_4.yzzx
  x2.rgb = dot(cBr, vB),   dot(cBg, vB),   dot(cBb, vB)
  vC     = Nx²-Ny²
  x3.rgb = cC × vC
  irradiance = x1 + x2 + x3                                  // en W/m²/sr
```

C'est le code exact de Sloan A10, pas une approximation.

### Stockage GPU

- Format : `rgba16f` 3D texture, 2 couches de `rgba16f` pour couvrir les 9 coefficients
- Couche A : coefficients 0–3 (xyz = coeff 0,1,2; w = coeff 3)
- Couche B : coefficients 4–8 (5 valeurs, dernier canal padding)
- Échantillonnage runtime : `LINEAR + CLAMP_TO_EDGE`, coalesced sur Adreno

### Gestion du ringing SH (Sloan, §Ringing)

Si les sources lumineuses sont très directionnelles et HDR, les coefficients L2 peuvent produire des lobes négatifs (ringing de Gibbs). Solution : windowing de Hanning sur les coefficients avant stockage.

```
Pour bande l, coefficient c_l^m :
  window(l) = (1 + cos(πl/W)) / 2    // Hanning, W = ordre max + 1
  c_l^m_windowed = c_l^m × window(l)
```

Pour scènes médicales avec éclairage doux (plafonniers, lumière ambiante), le ringing est rare. Activer uniquement si artefacts visibles.

### Paramètres recommandés (scènes médicales)

```
probe_spacing     = 0.4 m   (salle 4×4m → grille 10×10×4)
probe_grid_layers = 4       (sol à plafond, espacement 0.5 m)
rays_per_probe    = 512     (bake offline)
SH_order          = L2      (9 coefficients)
```

---

## Système 2 — DDGI dynamique

### Source primaire : Majercik et al. 2021 (JCGT 10(2))

DDGI stocke la même irradiance SH que le système 1, mais la met à jour chaque frame via raycast GPU. Le papier original utilise 256 rays/probe/frame en hardware raytracing. Cette implémentation adapte DDGI pour Adreno (compute DDA) avec throttling.

### Architecture des probes (Majercik 2021, §Configuration)

Chaque probe DDGI stocke deux types de données :
1. **Irradiance** : 8×8 octahedral map (codage octaédrique de la direction → UV 2D)
2. **Visibility** : 16×16 octahedral map (distance moyenne et distance² vers géométrie, utilisé pour le test de Chebyshev)

**Codage octaédrique (encodage directionnel, Majercik 2021) :**
```
Encode direction d vers UV [0,1]² :
  d_proj = d / (|dx| + |dy| + |dz|)          // projection L1
  Si d.z ≥ 0 :
    uv = (d_proj.x, d_proj.y) × 0.5 + 0.5
  Sinon :
    uv = (1 - |d_proj.y|) × sign(d_proj.x),
         (1 - |d_proj.x|) × sign(d_proj.y)   × 0.5 + 0.5
```

C'est le format exact du papier. Avantage sur les cubemaps : pas de discontinuités aux arêtes, accès mémoire uniforme.

### Mise à jour des probes (Majercik 2021, §Update)

Chaque frame, pour chaque probe active :
1. Lancer M rayons depuis le centre de la probe (sphère uniforme)
2. Pour chaque rayon : accumuler radiance + stocker distance au premier hit
3. Blending temporel (hysteresis) avec l'ancienne valeur

```
Algorithme update probe (M rayons) :
  Pour chaque rayon i (direction d_i) :
    (radiance_i, distance_i) = raycast(probe_center, d_i)

  irr_new = SH_project(radiance_i, d_i)   // ou octahedral map
  vis_new  = octahedral_encode(distance_i, distance_i²)

  // Hysteresis temporelle (Majercik 2021 recommande 0.97)
  irr_stored = lerp(irr_new, irr_stored_prev, 0.97)
  vis_stored = lerp(vis_new, vis_stored_prev, 0.97)
```

**M sur Quest :** 64 rays/probe/frame (vs 256 du papier). Acceptable car hysteresis accumule sur ~33 frames.

**Throttling Quest :** 2–4 probes/frame maximum. Probes sélectionnées par proximité caméra + invalidation (dirty flag CPU déclenché quand objet dynamique entre dans la cellule).

**Note importante :** le papier ne décrit pas de "dirty flags adaptatif" — c'est une optimisation au-delà du papier. Le DDGI original rebake toutes les probes actives chaque frame. Le throttling Quest est un écart conscient au papier pour respecter le budget Adreno.

### Pondération des probes à l'évaluation (Majercik 2021, §Probe Weighting)

Lors de l'échantillonnage d'une position `x` dans le volume DDGI, on interpole trilinéairement les 8 probes voisines. Le papier ajoute trois poids multiplicatifs pour éviter les artefacts :

```
Pour chaque voisin probe p parmi les 8 :

  1. Poids directionnel (back-face) :
     w_dir = max(0, dot(normalize(p.center - x), N))²
     → élimine les probes derrière la surface

  2. Poids de visibilité Chebyshev (évite le light leaking) :
     mean_dist   = probe_visibility_map(p, dir_to_p)
     mean_dist²  = probe_visibility_map²(p, dir_to_p)
     variance     = mean_dist² - mean_dist²
     Si dist_to_p ≤ mean_dist :
       w_vis = 1.0
     Sinon :
       w_vis = variance / (variance + (dist_to_p - mean_dist)²)   // Chebyshev

  3. Poids trilinéaire standard :
     w_trilin = produit des (1-t) ou t selon l'axe

  w_total = w_dir × w_vis × w_trilin + epsilon   // epsilon évite division par zéro
```

C'est critique. Omettre la pondération Chebyshev produit du light leaking évident.

### Self-shadow bias (Majercik 2021 Resampling, §3.5)

L'offset de self-shadow standard (combinaison vue + normale) peut pousser le point d'évaluation à travers la géométrie. Majercik 2021b recommande de décaler le long du chemin vue plutôt que de la normale :

```
// Standard (Majercik 2019) — peut leak à travers corners
bias = α × N + β × dir_to_camera

// Amélioré (Majercik 2021b §3.5) — garantit pas de pénétration
bias = reculer d'une distance fixe d le long du chemin,
       d proportionnel à la taille de la probe
```

### DDGI + ReSTIR (Majercik 2021b — optionnel PC)

Le papier de 2021b montre que traiter DDGI comme une source lumineuse dans ReSTIR (en évaluant les probes aux vertices secondaires plutôt que primaires) réduit significativement les artefacts de fuite. Sur Quest : skip. Sur PC avec RT HW : envisageable en couche 4.

---

## Système 3 — GTAO réduit

### Principe (Jimenez et al. 2016, XeGTAO Intel 2021)

GTAO (Ground Truth Ambient Occlusion) est du screen-space AO basé sur l'horizon marching. Pour chaque pixel, on cherche l'angle maximal vers l'horizon dans plusieurs directions écran, puis on intègre l'occlusion sur l'hémisphère.

**Avantage Quest :** lit uniquement le depth buffer, qui est on-chip sur TBDR Adreno → pas de bandwidth externe.

### Algorithme

```
Pour chaque pixel p :
  pos   = reconstruct_view_position(depth(p), proj_inv)
  N     = decode_normal(gbuffer_normal(p))           // view-space

  ao    = 0
  Pour chaque direction angulaire d (N_dirs directions dans écran) :
    h_max = -1.0                                     // horizon maximal

    Pour chaque pas s = 1..N_steps :
      offset   = d × s × step_size / screen_resolution
      s_pos    = reconstruct_view_position(depth(p + offset), proj_inv)
      horizon  = normalize(s_pos - pos)
      h        = dot(horizon, N) - bias              // bias évite self-occlusion
      h_max    = max(h_max, h)

    ao += asin(clamp(h_max, -1, 1))

  ao = 1 - ao / (N_dirs × π/2)                      // normalisation
```

**Pas de dénoiser temporel** — ghosting inacceptable en VR tête libre. Uniquement un filtre bilatéral spatial 3×3 (pondéré par différence de profondeur).

### Paramètres Quest

```
résolution    : demi-résolution de rendu (ex: 920×960 pour Quest 3)
N_dirs        : 4 directions
N_steps       : 4 pas par direction
rayon         : 0.5 m
bias          : 0.05 rad
workgroup     : 8×8 threads
```

### Upscale demi-résolution → pleine résolution

Bilinear upscale avec pondération par différence de profondeur (bilateral). Évite les halos sur les bords de géométrie.

---

## Système 4 — Reflection Probes baked

### Principe

Cubemaps précuites par zone. Blend entre les 2 probes les plus proches à runtime. Pas de SSR (ghosting en VR, trop cher Adreno).

### Parallax correction (McAuley 2015, Far Cry 4)

Sans parallax correction, un cubemap pris depuis sa position de capture donne des réflexions incorrectes si la surface est à une distance différente. La box-projection corrige ça en intersectant le rayon de réflexion avec le volume de la probe.

```
Algorithme box-projection :
  r        = reflect(-view_dir, N)               // direction réflexion
  rbmax    = (box_max - world_pos) / r
  rbmin    = (box_min - world_pos) / r
  rbminmax = (r > 0) ? rbmax : rbmin             // composante par composante
  t        = min(rbminmax.x, rbminmax.y, rbminmax.z)
  intersect = world_pos + r × t                  // point sur box
  r_corrected = intersect - probe_center         // direction corrigée
```

### GGX prefilter pour roughness

Les mipmaps du cubemap ne correspondent pas à une BRDF GGX naïvement. Il faut pré-filtrer chaque niveau de mip avec un kernel GGX correspondant à la roughness cible.

```
Pour chaque niveau mip k (roughness_k = k / N_mips) :
  Pour chaque texel (direction D) :
    Lancer M_k échantillons importance-samplings de GGX(roughness_k)
    Pour chaque échantillon (direction H) :
      L        = reflect(-D, H)
      poids   += max(0, dot(D, H))
      couleur += sample_envmap_hires(L) × max(0, dot(D, H))
    mip[k][D] = couleur / poids
```

M_k = 1024 pour qualité, offline. Runtime = sample mipmap, ~1 instruction.

### Blend entre 2 probes

Pondération par distance inversée au carré, clampée dans la zone d'influence de chaque probe. Au-delà de 2 probes actives : rendement décroissant pour coût constant sur Adreno.

---

## Intégration dans le lighting pass PBR

### Équation finale (après direct PBR)

```
Entrées disponibles :
  albedo       ← G-buffer
  F0, roughness, metallic ← G-buffer
  N            ← G-buffer normals
  world_pos    ← reconstruit depuis depth
  ao           ← système 3, pleine résolution

Calcul indirect :

  // 1. Indirect diffuse depuis SH probes (systèmes 1 + 2)
  irradiance    = evaluate_sh_probe_grid(world_pos, N)     // Sloan A10
  indirect_diff = irradiance × albedo / π

  // 2. Indirect spéculaire depuis reflection probes (système 4)
  r             = reflect(-view_dir, N)
  lod           = roughness × 7.0                          // 8 mips
  indirect_spec = sample_reflection_probe(r, lod, world_pos)

  // 3. Fresnel split-sum (Epic Games 2013 UE4 approximation)
  NdotV  = max(0, dot(N, view_dir))
  F      = F0 + (1 - F0) × (1 - NdotV)^5                 // Schlick
  kS     = F
  kD     = (1 - kS) × (1 - metallic)

  // 4. Combinaison finale avec AO
  ambient = (kD × indirect_diff + kS × indirect_spec) × ao
  color   = direct_lighting + ambient
```

---

## Pipeline GPU par frame

```
Passe                     Type          Dépendances
─────────────────────────────────────────────────────────────
Depth pre-pass            Graphics      —
G-Buffer                  Graphics      Depth
DDGI update (0-4 probes)  Compute       G-Buffer
GTAO demi-res             Compute       Depth
GTAO bilateral denoise    Compute       GTAO raw
Lighting (direct + indir) Graphics/Comp G-Buffer + DDGI + GTAO + probes
Tone mapping              Graphics      Lighting output
```

### Synchronisation Vulkan entre passes

```
Entre DDGI update → Lighting :
  VkImageMemoryBarrier
    srcAccess = SHADER_WRITE
    dstAccess = SHADER_READ
    srcStage  = COMPUTE_SHADER
    dstStage  = FRAGMENT_SHADER | COMPUTE_SHADER
    layouts   = GENERAL → SHADER_READ_ONLY_OPTIMAL

Entre GTAO denoise → Lighting :
  identique, image AO
```

Ne pas utiliser `vkQueueSubmit` séparé pour chaque passe — une seule soumission de command buffer par frame, barrières internes uniquement.

---

## Considérations TBDR Adreno spécifiques

**Accès on-chip (gratuit en bandwidth) :**
- Depth buffer dans la même render pass
- Color attachments en subpass
- GTAO qui lit le depth local → TBDR adore ça

**Ce qui casse le TBDR (éviter absolument) :**
- Lire et écrire la même image dans la même passe
- Resolve explicite inutile (`STORE_OP_DONT_CARE` pour les passes intermédiaires)
- Accès atlas mémoire aléatoire non-coalesced dans les compute shaders

**Format de textures :**
- `rgba16f` partout pour la GI (pas rgba32f)
- `r8` pour l'AO final
- `VK_IMAGE_TILING_OPTIMAL` exclusivement sur device local

**Workgroup size :**
- 8×8 = 64 threads pour tous les compute shaders GI
- Correspond bien à la taille des wavefronts Adreno (64 threads)

---

## Paramètres de qualité scalables

```cpp
// Quest 3 (Adreno 740)
probe_spacing          = 0.4f
ddgi_rays_per_probe    = 64
ddgi_probes_per_frame  = 4
ddgi_hysteresis        = 0.97f
gtao_dirs              = 4
gtao_steps             = 4
gtao_half_res          = true
cubemap_size           = 128
max_active_probes      = 4

// PC desktop
probe_spacing          = 0.3f
ddgi_rays_per_probe    = 128
ddgi_probes_per_frame  = 16
ddgi_hysteresis        = 0.97f
gtao_dirs              = 8
gtao_steps             = 6
gtao_half_res          = false
cubemap_size           = 256
max_active_probes      = 8
```

---

## Ordre d'implémentation recommandé

```
Semaine 1-2 : Système 1 (SH baked)
  → Outil bake offline CPU/compute
  → Stockage 3D texture + évaluation shader (Sloan A10 exact)
  → Intégration dans lighting pass PBR existant

Semaine 3   : Système 4 (Reflection Probes)
  → Bake cubemap offline avec GGX prefilter
  → Box-projection correction runtime
  → Blend 2 probes dans lighting pass

Semaine 4-5 : Système 3 (GTAO)
  → Compute shader demi-res
  → Bilatéral denoise 3×3
  → Upscale + application sur ambient

Semaine 6-8 : Système 2 (DDGI)
  → Raycast DDA sur SDF ou BVH simplifié
  → Octahedral encoding probes
  → Pondération Chebyshev (critique)
  → Throttling Quest
  → Dirty flags CPU

Mois 3+    : Profiling Snapdragon + optimisations TBDR
```

---

## Références

- Sloan, P.-P. (2008). *Stupid Spherical Harmonics (SH) Tricks*. GDC 2008. — Appendices A2 et A10 pour les constantes et le shader exact.
- Majercik, Z. et al. (2019). *Dynamic Diffuse GI with Ray-Traced Irradiance Fields*. JCGT 8(2). — Algorithme DDGI original, octahedral maps, pondération Chebyshev.
- Majercik, Z. et al. (2021). *Scaling Probe-Based Real-Time Dynamic GI for Production*. JCGT 10(2). — Passage à l'échelle, roughness glossy via probes.
- Majercik, Z. et al. (2021b). *Dynamic Diffuse GI Resampling*. CGF. — Self-shadow bias amélioré (§3.5), DDGI + ReSTIR.
- Jimenez, J. et al. (2016). *Practical Realtime Strategies for Accurate Indirect Occlusion*. SIGGRAPH 2016. — GTAO, horizon marching.
- Intel (2021). *XeGTAO*. github.com/GameTechDev/XeGTAO — Implémentation de référence GTAO open source.
- McAuley, S. (2015). *Rendering the World of Far Cry 4*. GDC 2015. — Box-projection parallax correction.
- Arm Ltd. *Mali GPU Best Practices*. developer.arm.com — TBDR, on-chip, bandwidth.
- Qualcomm. *Adreno Vulkan Developer Guide*. developer.qualcomm.com

---

## Points d'attention critiques pour l'implémentation

Cette section documente les 5 endroits où une implémentation LLM-assistée va silencieusement produire quelque chose de faux. Lire avant de coder chaque système.

---

### Piège 1 — DDA pour le raycast DDGI (risque : BUG SILENCIEUX)

Le papier Majercik utilise du hardware BVH. Sans RT HW sur Quest, on fait du DDA (Digital Differential Analyzer) sur une grille de voxels ou du sphere marching sur SDF.

**L'erreur classique :** implémenter un DDA naïf avec des comparaisons de distance flottantes aux bords de cellule. Ça produit des manqués de cellules (un rayon traverse une voxel sans la détecter) ou des doubles détections, selon la direction du rayon.

**L'algorithme correct : Amanatides & Woo 1987.**
C'est la référence standard, ne pas en inventer un autre.

```
Initialisation (depuis position ray_origin, direction ray_dir normalisée) :
  cell       = floor(ray_origin / voxel_size)    // cellule de départ, entiers
  step       = sign(ray_dir)                     // +1 ou -1 par axe
  t_delta    = abs(voxel_size / ray_dir)         // distance entre deux traversées
                                                 // par axe (invariant)
  t_max      = (cell_boundary - ray_origin) / ray_dir   // prochaine traversée
                                                        // par axe depuis origin

Boucle :
  axis = argmin(t_max.x, t_max.y, t_max.z)     // axe traversé en premier
  cell[axis] += step[axis]
  t_max[axis] += t_delta[axis]
  t_current   = t_max[axis] - t_delta[axis]     // t au moment de l'entrée

  if cell hors grille : break
  if voxel[cell] est solide : hit à t_current, normal = -step[axis]
```

**Les deux pièges dans t_max initial :**
- Si `ray_dir[axis] > 0` : `t_max[axis] = (ceil(ray_origin[axis]/voxel_size)*voxel_size - ray_origin[axis]) / ray_dir[axis]`
- Si `ray_dir[axis] < 0` : `t_max[axis] = (floor(ray_origin[axis]/voxel_size)*voxel_size - ray_origin[axis]) / ray_dir[axis]`
- Si `ray_dir[axis] == 0` : `t_max[axis] = +INF` (ne traversera jamais cet axe)

Ne jamais calculer `t_max` comme `(cell + 0.5) * voxel_size` — c'est faux pour les rayons qui partent d'une position non-centrée dans la cellule.

---

### Piège 2 — Packing SH dans rgba16f (risque : RÉSULTAT SILENCIEUSEMENT FAUX)

9 coefficients RGB dans 2 textures rgba16f. Si le bake et le shader d'évaluation utilisent des packing différents, le résultat sera faux sans message d'erreur — juste une image noir ou une couleur incohérente.

**Convention fixée — ne pas dévier :**

```
Texture SH_A (rgba16f 3D) :
  .r = coeff[0].r    (L0, rouge)
  .g = coeff[0].g    (L0, vert)
  .b = coeff[0].b    (L0, bleu)
  .a = coeff[1].r    (L1_y, rouge)

Texture SH_B (rgba16f 3D) :
  .r = coeff[1].g    (L1_y, vert)
  .g = coeff[1].b    (L1_y, bleu)
  .b = coeff[2].r    (L1_z, rouge)
  .a = coeff[2].g    (L1_z, vert)

Texture SH_C (rgba16f 3D) :
  .r = coeff[2].b    (L1_z, bleu)
  .g = coeff[3].r    (L1_x, rouge)
  .b = coeff[3].g    (L1_x, vert)
  .a = coeff[3].b    (L1_x, bleu)

... continuer de la même façon pour les 5 termes L2
```

**Alternative plus simple (recommandée) :** stocker en 3 textures `rgba16f` distinctes par canal RGB, soit 3×3 = 9 textures. Plus coûteux en mémoire mais indexation triviale sans confusion. Décider une convention avant de coder et ne pas changer entre bake et runtime.

**Validation :** placer une probe dans une scène avec une lumière rouge directionnelle connue. Si l'éval SH donne bien du rouge dans la direction de la lumière et du noir dans la direction opposée, le packing est cohérent.

---

### Piège 3 — Pondération Chebyshev DDGI (risque : LIGHT LEAKING ÉVIDENT)

C'est le point le plus souvent omis ou mal implémenté. Sans Chebyshev, une probe derrière un mur va éclairer à travers ce mur.

**Ce qui est facile à oublier :** la visibility map de chaque probe stocke `(mean_distance, mean_distance²)`, pas juste une distance binaire. Ce sont deux valeurs distinctes et les deux doivent être écrites par l'update et lues par l'évaluation.

**Test de Chebyshev (formule exacte, Majercik 2019 §3) :**

```
Données disponibles pour une probe p et un point x :
  dist_actual  = length(x - p.center)         // distance réelle
  mean_d       = probe_vis_map(p, dir_p_to_x).r   // lire depuis octahedral map
  mean_d2      = probe_vis_map(p, dir_p_to_x).g   // distance au carré moyenne
  variance     = max(0, mean_d2 - mean_d * mean_d)

Si dist_actual <= mean_d :
  w_vis = 1.0                // x est probablement visible depuis p
Sinon :
  // Test de Chebyshev : probabilité que x soit visible
  w_vis = variance / (variance + (dist_actual - mean_d)²)
  w_vis = max(0, w_vis)
  w_vis = w_vis³             // puissance 3 pour durcir la transition (Majercik §3)
```

**Ce que l'agent va probablement faire à la place :**
```
// FAUX — simple test distance
w_vis = (dist_actual < mean_d) ? 1.0 : 0.0;
// Résultat : aliasing dur, pas de gradient de visibilité
```

**La puissance 3 finale est importante.** Elle durcit la frontière visible/occlus. Sans elle, le leaking reste visible sur les murs fins.

---

### Piège 4 — Octahedral encoding (risque : ARTEFACTS EN CROIX)

L'encodage octaédrique mappe une direction sphérique sur un carré UV [0,1]². L'erreur classique est de se tromper sur le repliement des quadrants négatifs (z < 0).

**Formule exacte (Cigolle et al. 2014, utilisée par Majercik) :**

```
Encoder (direction d normalisée → uv dans [-1,1]²) :
  p   = d.xy / (|d.x| + |d.y| + |d.z|)        // projection L1 sur plan z=0
  uv  = (d.z >= 0) ? p
                    : (1.0 - abs(p.yx)) * sign(p.xy)   // ← repliement z<0

Puis mapper vers [0,1]² :
  uv_texel = uv * 0.5 + 0.5
```

**Décoder (uv dans [0,1]² → direction normalisée) :**
```
  uv  = uv_texel * 2.0 - 1.0                   // retour dans [-1,1]²
  d   = vec3(uv.x, uv.y, 1.0 - |uv.x| - |uv.y|)
  Si d.z < 0 :
    d.xy = (1.0 - abs(d.yx)) * sign(d.xy)      // ← même repliement qu'à l'encode
  d = normalize(d)
```

**Le repliement `(1.0 - abs(p.yx)) * sign(p.xy)` pour z<0 est contre-intuitif.** L'agent va souvent écrire `(1.0 - abs(p.xy)) * sign(p.yx)` (x et y inversés) ou oublier le `abs`. Le signe de l'artefact : les probes auront une croix visible aux jointures des 4 quadrants de l'octaèdre, visible comme une discontinuité lumineuse dans les directions près de l'équateur.

**Validation rapide :** encoder puis décoder 1000 directions aléatoires. La norme de `decode(encode(d)) - d` doit être < 1e-5 pour toutes les directions. Si ça diverge près de z=0, le repliement est faux.

---

### Piège 5 — Barrières Vulkan GI (risque : STALL GPU TOTAL SUR ADRENO)

Pas un bug de rendu, mais un désastre de performance. L'agent va mettre des barrières larges par sécurité.

**Ce que l'agent va écrire (trop large) :**
```
srcStageMask  = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT
dstStageMask  = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT
srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT
dstAccessMask = VK_ACCESS_MEMORY_READ_BIT
```
Sur Adreno TBDR, `ALL_COMMANDS` force le GPU à vider le pipeline complet, y compris le tile buffer. C'est ~0.5 ms de stall gratuit à chaque barrière.

**Barrières exactes pour chaque transition GI :**

```
DDGI update compute → Lighting fragment (SH grid image) :
  srcStageMask  = COMPUTE_SHADER
  dstStageMask  = FRAGMENT_SHADER
  srcAccessMask = SHADER_WRITE
  dstAccessMask = SHADER_READ
  oldLayout     = GENERAL
  newLayout     = SHADER_READ_ONLY_OPTIMAL

GTAO compute → GTAO denoise compute (AO raw image) :
  srcStageMask  = COMPUTE_SHADER
  dstStageMask  = COMPUTE_SHADER
  srcAccessMask = SHADER_WRITE
  dstAccessMask = SHADER_READ
  oldLayout     = GENERAL
  newLayout     = GENERAL           // reste GENERAL entre deux compute

GTAO denoise → Lighting (AO filtered image) :
  srcStageMask  = COMPUTE_SHADER
  dstStageMask  = FRAGMENT_SHADER
  srcAccessMask = SHADER_WRITE
  dstAccessMask = SHADER_READ
  oldLayout     = GENERAL
  newLayout     = SHADER_READ_ONLY_OPTIMAL
```

**Règle générale :** stage source = le shader qui a écrit. Stage destination = le shader qui va lire. Ne jamais être plus large que nécessaire. `SHADER_READ_ONLY_OPTIMAL` pour les images lues en fragment, `GENERAL` pour les images lues/écrites en compute.

**Initialisation des images GI au démarrage :**
Toutes les images créées avec `VK_IMAGE_LAYOUT_UNDEFINED` doivent être transitionnées avant le premier frame. Oublier cette transition initiale donne un comportement indéfini (souvent noir, parfois crash driver Adreno).

```
Au démarrage, pour chaque image GI (SH grid, AO, DDGI maps) :
  srcStageMask  = TOP_OF_PIPE
  dstStageMask  = COMPUTE_SHADER
  srcAccessMask = 0
  dstAccessMask = SHADER_WRITE
  oldLayout     = UNDEFINED
  newLayout     = GENERAL
```
