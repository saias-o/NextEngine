# Spike Web (Étape 16.0)

Validation de la toolchain **Emscripten + WebGPU** avant le gros chantier RHI.
Trois étapes, de la plus simple à la plus complète :

| Fichier | Ce qu'il prouve |
|---|---|
| `hello.cpp` | emcc compile C++ → wasm et ça s'exécute (sous node) |
| `webgpu_probe.cpp` | le port `emdawnwebgpu` fournit `<webgpu/webgpu.h>` et linke |
| `spike.cpp` | GLFW (canvas) + WebGPU + boucle RAF : clear animé à l'écran |

## Prérequis

Emscripten activé dans la session (PowerShell) :

```powershell
. C:\Users\evand\emsdk\emsdk_env.ps1
```

## Build

```powershell
# Toolchain de base
emcc hello.cpp -O2 -o hello.js
node hello.js                       # -> "wasm runtime OK"

# WebGPU + GLFW : produit index.html + index.js + index.wasm
emcc spike.cpp -O2 --use-port=emdawnwebgpu -sUSE_GLFW=3 -o index.html
```

## Lancer (navigateur compatible WebGPU : Chrome / Edge récents)

Le wasm ne se charge pas en `file://` — il faut un serveur HTTP :

```powershell
emrun --browser chrome index.html
# ou
python -m http.server 8080   # puis http://localhost:8080/index.html
```

Attendu : un fond qui **change de couleur en continu** (clear animé), et dans la
console JS : `spike: device ready, surface configured — rendering`.

> Note : `spike.cpp` ne rend pas encore de géométrie (pas de pipeline/WGSL) —
> c'est volontaire. Le chemin shader WGSL sera validé en 16.2 avec les vrais
> shaders du moteur transpilés depuis le GLSL.
