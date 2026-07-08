#!/usr/bin/env bash
# Lance SaidaEngine avec les validation layers Vulkan activées.
#
# Les layers (paquet MSYS2 mingw-w64-ucrt-x86_64-vulkan-validation-layers) ne
# sont pas enregistrées auprès du loader : on pointe VK_LAYER_PATH sur le dossier
# du manifeste, et on s'assure que ucrt64/bin est en tête du PATH pour que les
# DLL dont dépend la layer (libstdc++, SPIRV-Tools) se résolvent correctement.
set -euo pipefail

# ucrt64/bin = dossier de gcc (donc de vulkan-1.dll et du manifeste de layer).
ucrt_bin="$(dirname "$(command -v gcc)")"
export PATH="$ucrt_bin:$PATH"
export VK_LAYER_PATH="$(cygpath -w "$ucrt_bin")"

cd "$(dirname "$0")/build/bin"
exec ./SaidaEngine.exe "$@"
