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

// Writes a binary glTF (.glb) at `outPath` containing `meshes`, with geometry
// compressed via EXT_meshopt_compression: each mesh is meshopt vertex-cache /
// vertex-fetch optimized, then entropy-coded (lossless, attributes + triangles
// modes). The resulting file loads back through GLTFLoader (which decodes it via
// decodeMeshoptBuffers). This is the encode half of the web asset pipeline
// (Étape 16) — it produces the compressed GLBs the runtime decode path consumes.
//
// v1 is lossless float (no quantization yet); quantization is a follow-up that
// trades a tiny precision loss for a much smaller download.
//
// Returns true on success. Meshes without indices are skipped with a warning.
bool exportMeshoptGlb(const std::vector<ExportMesh>& meshes, const std::string& outPath);

} // namespace saida
