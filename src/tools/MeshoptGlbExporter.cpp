#include "tools/MeshoptGlbExporter.hpp"
#include "core/Log.hpp"

#include <meshoptimizer.h>
#include <nlohmann/json.hpp>
#include <glm/glm.hpp>

#include <cstddef>  // offsetof
#include <fstream>
#include <limits>

namespace saida {
namespace {

using json = nlohmann::json;

void append(std::vector<uint8_t>& buf, const void* data, size_t size) {
    const uint8_t* p = static_cast<const uint8_t*>(data);
    buf.insert(buf.end(), p, p + size);
}
void padTo4(std::vector<uint8_t>& buf, uint8_t pad) {
    while (buf.size() % 4 != 0) buf.push_back(pad);
}
void writeU32(std::ofstream& os, uint32_t v) {
    const uint8_t b[4] = {uint8_t(v), uint8_t(v >> 8), uint8_t(v >> 16), uint8_t(v >> 24)};
    os.write(reinterpret_cast<const char*>(b), 4);
}

} // namespace

bool exportMeshoptGlb(const std::vector<ExportMesh>& meshes, const std::string& outPath) {
    constexpr size_t kStride = sizeof(Vertex);

    std::vector<uint8_t> bin;  // buffer 0: compressed data (lives in the BIN chunk)
    json bufferViews = json::array();
    json accessors = json::array();
    json jmeshes = json::array();
    json nodes = json::array();
    json sceneNodes = json::array();

    size_t fallbackOffset = 0;  // running offset into the data-less fallback buffer 1

    for (const ExportMesh& m : meshes) {
        if (m.vertices.empty() || m.indices.empty()) {
            Log::warn("MeshoptGlbExporter: skipping mesh '", m.name, "' (no geometry)");
            continue;
        }
        const size_t vertexCount = m.vertices.size();
        const size_t indexCount = m.indices.size();

        // 1. Optimize: vertex-cache reorder of indices, then vertex-fetch reorder
        //    of vertices (rewrites the indices to match the new vertex order).
        std::vector<uint32_t> indices(indexCount);
        meshopt_optimizeVertexCache(indices.data(), m.indices.data(), indexCount, vertexCount);

        std::vector<Vertex> verts(vertexCount);
        const size_t uniqueCount = meshopt_optimizeVertexFetch(
            verts.data(), indices.data(), indexCount, m.vertices.data(), vertexCount, kStride);
        verts.resize(uniqueCount);

        // 2. Entropy-code the vertex + index streams (lossless).
        std::vector<uint8_t> vbuf(meshopt_encodeVertexBufferBound(uniqueCount, kStride));
        vbuf.resize(meshopt_encodeVertexBuffer(vbuf.data(), vbuf.size(), verts.data(), uniqueCount, kStride));

        std::vector<uint8_t> ibuf(meshopt_encodeIndexBufferBound(indexCount, uniqueCount));
        ibuf.resize(meshopt_encodeIndexBuffer(ibuf.data(), ibuf.size(), indices.data(), indexCount));

        // 3. Lay the compressed bytes into the BIN buffer (4-byte aligned).
        const size_t vCompOffset = bin.size();
        append(bin, vbuf.data(), vbuf.size());
        padTo4(bin, 0);
        const size_t iCompOffset = bin.size();
        append(bin, ibuf.data(), ibuf.size());
        padTo4(bin, 0);

        // Fallback (uncompressed) layout in buffer 1 — never read (decode overrides
        // view->data), kept only so the file is spec-consistent.
        const size_t vUncompLen = uniqueCount * kStride;
        const size_t iUncompLen = indexCount * sizeof(uint32_t);
        const size_t vFallbackOffset = fallbackOffset; fallbackOffset += (vUncompLen + 3) & ~size_t(3);
        const size_t iFallbackOffset = fallbackOffset; fallbackOffset += (iUncompLen + 3) & ~size_t(3);

        const int vViewIdx = int(bufferViews.size());
        bufferViews.push_back({
            {"buffer", 1}, {"byteOffset", vFallbackOffset}, {"byteLength", vUncompLen},
            {"byteStride", kStride}, {"target", 34962 /*ARRAY_BUFFER*/},
            {"extensions", {{"EXT_meshopt_compression", {
                {"buffer", 0}, {"byteOffset", vCompOffset}, {"byteLength", vbuf.size()},
                {"byteStride", kStride}, {"count", uniqueCount}, {"mode", "ATTRIBUTES"}}}}},
        });
        const int iViewIdx = int(bufferViews.size());
        bufferViews.push_back({
            {"buffer", 1}, {"byteOffset", iFallbackOffset}, {"byteLength", iUncompLen},
            {"target", 34963 /*ELEMENT_ARRAY_BUFFER*/},
            {"extensions", {{"EXT_meshopt_compression", {
                {"buffer", 0}, {"byteOffset", iCompOffset}, {"byteLength", ibuf.size()},
                {"byteStride", sizeof(uint32_t)}, {"count", indexCount}, {"mode", "TRIANGLES"}}}}},
        });

        // 4. Accessors. POSITION carries min/max (required by glTF).
        glm::vec3 mn(std::numeric_limits<float>::max());
        glm::vec3 mx(-std::numeric_limits<float>::max());
        for (const Vertex& v : verts) { mn = glm::min(mn, v.pos); mx = glm::max(mx, v.pos); }

        auto floatAccessor = [&](size_t byteOffset, const char* type) {
            const int idx = int(accessors.size());
            accessors.push_back({
                {"bufferView", vViewIdx}, {"byteOffset", byteOffset},
                {"componentType", 5126 /*FLOAT*/}, {"count", uniqueCount}, {"type", type}});
            return idx;
        };

        const int aPos = int(accessors.size());
        accessors.push_back({
            {"bufferView", vViewIdx}, {"byteOffset", offsetof(Vertex, pos)},
            {"componentType", 5126}, {"count", uniqueCount}, {"type", "VEC3"},
            {"min", {mn.x, mn.y, mn.z}}, {"max", {mx.x, mx.y, mx.z}}});
        const int aNrm = floatAccessor(offsetof(Vertex, normal), "VEC3");
        const int aUv0 = floatAccessor(offsetof(Vertex, texCoord), "VEC2");
        const int aUv1 = floatAccessor(offsetof(Vertex, lightmapUV), "VEC2");
        const int aTan = floatAccessor(offsetof(Vertex, tangent), "VEC4");

        const int aIdx = int(accessors.size());
        accessors.push_back({
            {"bufferView", iViewIdx}, {"componentType", 5125 /*UNSIGNED_INT*/},
            {"count", indexCount}, {"type", "SCALAR"}});

        // 5. Mesh + node.
        json prim = {
            {"attributes", {
                {"POSITION", aPos}, {"NORMAL", aNrm},
                {"TEXCOORD_0", aUv0}, {"TEXCOORD_1", aUv1}, {"TANGENT", aTan}}},
            {"indices", aIdx}, {"mode", 4 /*TRIANGLES*/}};
        json jm = {{"primitives", json::array({prim})}};
        if (!m.name.empty()) jm["name"] = m.name;
        const int meshIdx = int(jmeshes.size());
        jmeshes.push_back(jm);

        json node = {{"mesh", meshIdx}};
        if (!m.name.empty()) node["name"] = m.name;
        sceneNodes.push_back(int(nodes.size()));
        nodes.push_back(node);
    }

    const size_t meshCount = jmeshes.size();
    if (meshCount == 0) {
        Log::error("MeshoptGlbExporter: nothing to export");
        return false;
    }

    json root;
    root["asset"] = {{"version", "2.0"}, {"generator", "SaidaEngine meshopt exporter"}};
    root["extensionsUsed"] = json::array({"EXT_meshopt_compression"});
    root["extensionsRequired"] = json::array({"EXT_meshopt_compression"});
    root["buffers"] = json::array({
        {{"byteLength", bin.size()}},
        {{"byteLength", fallbackOffset},
         {"extensions", {{"EXT_meshopt_compression", {{"fallback", true}}}}}},
    });
    root["bufferViews"] = std::move(bufferViews);
    root["accessors"] = std::move(accessors);
    root["meshes"] = std::move(jmeshes);
    root["nodes"] = std::move(nodes);
    root["scenes"] = json::array({{{"nodes", std::move(sceneNodes)}}});
    root["scene"] = 0;

    const std::string jsonStr = root.dump();
    std::vector<uint8_t> jsonChunk(jsonStr.begin(), jsonStr.end());
    padTo4(jsonChunk, ' ');
    padTo4(bin, 0);

    const uint32_t total = 12 + 8 + uint32_t(jsonChunk.size()) + 8 + uint32_t(bin.size());

    std::ofstream os(outPath, std::ios::binary);
    if (!os) { Log::error("MeshoptGlbExporter: cannot open ", outPath); return false; }
    writeU32(os, 0x46546C67);  // "glTF" magic
    writeU32(os, 2);           // version
    writeU32(os, total);
    writeU32(os, uint32_t(jsonChunk.size()));
    writeU32(os, 0x4E4F534A);  // "JSON"
    os.write(reinterpret_cast<const char*>(jsonChunk.data()), jsonChunk.size());
    writeU32(os, uint32_t(bin.size()));
    writeU32(os, 0x004E4942);  // "BIN\0"
    os.write(reinterpret_cast<const char*>(bin.data()), bin.size());
    os.flush();
    if (!os) { Log::error("MeshoptGlbExporter: write failed for ", outPath); return false; }

    Log::info("MeshoptGlbExporter: wrote ", outPath, " (", meshCount,
              " meshes, BIN ", bin.size(), " bytes)");
    return true;
}

} // namespace saida
