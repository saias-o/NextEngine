#pragma once

#include "graphics/Mesh.hpp"  // Vertex

#include <cstdint>
#include <string>
#include <vector>

namespace saida {

// One mesh to write into the GLB. Geometry only (the web asset pipeline's first
// concern); materials/skins can be layered on later.
struct ExportMesh {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;  // triangle list
    std::string name;
};

struct MeshoptExportOptions {
    // Quantizes the vertex attributes (Étape 16.1) — the real size win:
    //   normals + tangents   -> int8 normalized (KHR_mesh_quantization)
    //   UV channels in [0,1] -> uint16 normalized (core glTF)
    //   positions stay float (exact silhouettes; meshopt entropy-codes them well)
    //   vertex color is dropped (the loader defaults it to white)
    // ~100 -> 28-36 bytes per vertex before entropy coding. GLTFLoader decodes
    // it transparently (cgltf resolves normalized accessors to floats).
    // false = lossless float round-trip (the 16.1 v1 behaviour).
    bool quantize = true;
};

// Writes a binary glTF (.glb) at `outPath` containing `meshes`, with geometry
// compressed via EXT_meshopt_compression: each mesh is meshopt vertex-cache /
// vertex-fetch optimized, optionally quantized (options.quantize), then
// entropy-coded. The resulting file loads back through GLTFLoader (which
// decodes it via decodeMeshoptBuffers). This is the encode half of the web
// asset pipeline (Étape 16).
//
// Returns true on success. Meshes without indices are skipped with a warning.
bool exportMeshoptGlb(const std::vector<ExportMesh>& meshes, const std::string& outPath,
                      const MeshoptExportOptions& options = {});

} // namespace saida
