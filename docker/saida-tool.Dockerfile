# Build reproductible du binaire headless `saida_tool` pour Linux (P0.1 du
# PLAN_INTEGRATION_SAIDA.md). Ce binaire est la dépendance runtime dure de la
# plateforme (GET /scene, snapshots périodiques, dry-run agent) : les images
# Docker de `apps/api` et `apps/workers` le copient depuis ce stage.
#
#   docker build -f docker/saida-tool.Dockerfile -t saida-tool .
#   docker create --name tmp saida-tool && docker cp tmp:/out/saida_tool . && docker rm tmp
#
# Notes :
# - L'outil est headless : Vulkan n'est requis qu'à la compilation (headers +
#   libvulkan.so pour le link de saida_engine), jamais à l'exécution — aucun
#   GPU/ICD n'est nécessaire dans l'image finale.
# - XR et MCP sont hors périmètre headless : XR n'a pas de sens sans casque et
#   le serveur MCP utilise winsock (Windows uniquement).
# - Base **Debian bookworm** exprès : les images plateforme (`node:24-slim`)
#   sont bookworm (glibc 2.36) — builder sur une distro plus récente produit un
#   binaire qui exige GLIBC_2.38+ et casse au COPY --from (vérifié).

# ── Stage 1 : build ──────────────────────────────────────────────────────────
FROM debian:bookworm-slim AS build

RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
        build-essential cmake ninja-build \
        libvulkan-dev libglfw3-dev libglm-dev glslc \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .

RUN cmake -S . -B build -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DSAIDA_ENABLE_XR=OFF \
        -DSAIDA_ENABLE_MCP=OFF \
    && cmake --build build --target saida_tool

# ── Stage 2 : artefact minimal ───────────────────────────────────────────────
# Image runtime : le binaire + ses libs dynamiques (loader Vulkan et GLFW —
# jamais chargées par les commandes headless, mais liées à l'exe). Les images de
# la plateforme font `COPY --from=saida-tool /out/saida_tool /usr/local/bin/` et
# installent les mêmes paquets runtime.
FROM debian:bookworm-slim
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
        libvulkan1 libglfw3 \
    && rm -rf /var/lib/apt/lists/*
COPY --from=build /src/build/bin/saida_tool /out/saida_tool
# Sanity check à la construction : le binaire démarre et connaît ses commandes.
RUN /out/saida_tool describe-engine >/dev/null
ENTRYPOINT ["/out/saida_tool"]
