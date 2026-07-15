#pragma once

#include <glm/glm.hpp>

#include <array>
#include <memory>
#include <string>
#include <vector>

#include "graphics/GeometryRegistry.hpp"
#include "rhi/Rhi.hpp"

#ifndef SAIDA_RHI_WEBGPU
#include <vulkan/vulkan.h>
#endif

namespace saida {

class VulkanDevice;
class GeometryRegistry;

struct Vertex {
    glm::vec3 pos;
    glm::vec3 normal;
    glm::vec3 color;
    glm::vec2 texCoord;
    glm::vec2 lightmapUV;  // secondary UV set, mirrored from texture UV by default
    glm::vec4 tangent;
    glm::ivec4 boneIndices{-1, -1, -1, -1};
    glm::vec4 boneWeights{0.0f, 0.0f, 0.0f, 0.0f};

#ifndef SAIDA_RHI_WEBGPU
    static VkVertexInputBindingDescription bindingDescription();
    static std::array<VkVertexInputAttributeDescription, 8> attributeDescriptions();
#endif
};

// Local-space axis-aligned bounding box, in the mesh's own coordinate space
// (before any node transform). Computed once at construction; the physics layer
// uses it to auto-detect collider shapes without keeping a CPU vertex copy.
struct Aabb {
    glm::vec3 min{0.0f};
    glm::vec3 max{0.0f};

    glm::vec3 center() const { return (min + max) * 0.5f; }
    glm::vec3 extent() const { return max - min; }  // full size along each axis
};

// Géométrie CPU issue d'un parse (.obj sur le worker de l'AssetLoader) —
// prête à être uploadée dans un Mesh sur le thread principal.
struct MeshData {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
};

// Owns device-local vertex and index buffers and knows how to bind/draw itself.
class Mesh {
public:
    Mesh(GeometryRegistry& registry, const std::vector<Vertex>& vertices,
         const std::vector<uint32_t>& indices);
    // Proxy vide (chantier 3) : pointeur stable rendu immédiatement pendant un
    // chargement asynchrone. draw() est un no-op tant que upload() n'a pas
    // rempli la géométrie ; bounds() et collisionVertices() sont vides.
    explicit Mesh(GeometryRegistry& registry);
    ~Mesh();
    Mesh(const Mesh&) = delete;
    Mesh& operator=(const Mesh&) = delete;

    // Remplit un proxy (alloue + uploade la géométrie, calcule bounds et
    // données de collision). Appelé sur le thread principal une fois le parse
    // asynchrone terminé.
    void upload(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices);

    // Faux tant qu'un proxy n'a pas reçu sa géométrie.
    bool loaded() const { return allocation_.indexCount != 0; }

    // Octets GPU de la géométrie (comptabilité assets, chantier 3).
    uint64_t gpuBytes() const { return gpuBytes_; }

    // Parse un .obj depuis la mémoire — pur CPU, sûr hors du thread principal.
    // Ne résout pas les .mtl (les matériaux du moteur viennent des scènes).
    static bool parseObjBytes(const uint8_t* data, size_t size, MeshData& out, std::string& error);

    // Loads an .obj. generateLightmapUVs is retained for compatibility and is
    // currently ignored; the secondary UV set mirrors the texture UVs.
    static std::unique_ptr<Mesh> fromObjFile(GeometryRegistry& registry, const std::string& path,
                                             bool generateLightmapUVs = false);

    void bind(rhi::RenderPassEncoder& rp) const;
    void draw(rhi::RenderPassEncoder& rp) const;

    GeometryAllocation geometryAllocation() const { return allocation_; }
    const GeometryAllocation& allocation() const { return allocation_; }

    const Aabb& bounds() const { return bounds_; }

    // Lightweight CPU copy of the geometry (positions + indices only) kept for the
    // physics layer to build convex-hull / triangle-mesh colliders. (~12 B/vertex
    // + 4 B/index; could be made opt-in for shipping/mobile.)
    const std::vector<glm::vec3>& collisionVertices() const { return collisionVertices_; }
    const std::vector<uint32_t>& collisionIndices() const { return collisionIndices_; }

private:
    GeometryRegistry& registry_;
    GeometryAllocation allocation_;
    Aabb bounds_;
    uint64_t gpuBytes_ = 0;
    std::vector<glm::vec3> collisionVertices_;
    std::vector<uint32_t> collisionIndices_;
};

} // namespace saida
