#include "scene/GLTFLoader.hpp"
#include "scene/Scene.hpp"
#include "scene/MeshNode.hpp"
#include "scene/Node.hpp"
#include "graphics/Mesh.hpp"
#include "graphics/ResourceManager.hpp"
#include "core/Log.hpp"
#include "core/Log.hpp"

#include <cgltf.h>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/matrix_decompose.hpp>

#include <filesystem>
#include <vector>
#include <string>

namespace ne {

static glm::vec4 toVec4(const float* f) { return {f[0], f[1], f[2], f[3]}; }
static glm::vec3 toVec3(const float* f) { return {f[0], f[1], f[2]}; }

static AssetID loadGLTFTexture(cgltf_texture* tex, ResourceManager& resources, const std::filesystem::path& basePath, bool srgb = true) {
    if (!tex || !tex->image) return kAssetInvalid;
    
    if (tex->image->buffer_view) {
        // Embedded texture
        const uint8_t* data = static_cast<const uint8_t*>(tex->image->buffer_view->buffer->data) + tex->image->buffer_view->offset;
        return resources.registerMemoryTexture(data, tex->image->buffer_view->size, srgb);
    } else if (tex->image->uri) {
        std::filesystem::path uriPath = tex->image->uri;
        if (uriPath.is_absolute()) return resources.getOrRegister(uriPath.string(), AssetType::Texture, srgb);
        else return resources.getOrRegister((basePath / uriPath).string(), AssetType::Texture, srgb);
    }
    return kAssetInvalid;
}

static void processNode(cgltf_node* node, Node* parent, ResourceManager& resources,
                        const std::vector<std::vector<AssetID>>& meshesPrimitives, 
                        const std::vector<MaterialDesc>& materials, cgltf_data* data) {
    Node* neNode = parent->createChild<Node>(node->name ? node->name : "Node");

    if (node->has_translation) neNode->transform().position = toVec3(node->translation);
    if (node->has_rotation) neNode->transform().rotation = glm::quat(node->rotation[3], node->rotation[0], node->rotation[1], node->rotation[2]);
    if (node->has_scale) neNode->transform().scale = toVec3(node->scale);

    if (node->has_matrix) {
        glm::mat4 m = glm::make_mat4(node->matrix);
        glm::vec3 skew;
        glm::vec4 persp;
        glm::decompose(m, neNode->transform().scale, neNode->transform().rotation, neNode->transform().position, skew, persp);
    }

    if (node->mesh) {
        size_t meshIdx = node->mesh - data->meshes;
        const auto& primitives = meshesPrimitives[meshIdx];
        
        for (size_t i = 0; i < primitives.size(); ++i) {
            cgltf_primitive* prim = &node->mesh->primitives[i];
            Material* mat = nullptr;
            if (prim->material) {
                size_t matIdx = prim->material - data->materials;
                mat = resources.getMaterial(materials[matIdx]);
            } else {
                MaterialDesc defaultDesc;
                mat = resources.getMaterial(defaultDesc);
            }
            
            std::string primName = (node->name ? std::string(node->name) : "Mesh") + "_prim" + std::to_string(i);
            neNode->createChild<MeshNode>(primName, resources.getMesh(primitives[i]), mat);
        }
    }

    for (size_t i = 0; i < node->children_count; ++i) {
        processNode(node->children[i], neNode, resources, meshesPrimitives, materials, data);
    }
}

bool GLTFLoader::load(const std::string& path, Scene& scene, ResourceManager& resources) {
    Log::info("GLTFLoader: loading ", path);
    
    cgltf_options options = {};
    cgltf_data* data = nullptr;
    cgltf_result result = cgltf_parse_file(&options, path.c_str(), &data);
    
    if (result != cgltf_result_success) {
        Log::error("GLTFLoader: Failed to parse ", path, " (error ", result, ")");
        return false;
    }
    
    result = cgltf_load_buffers(&options, data, path.c_str());
    if (result != cgltf_result_success) {
        Log::error("GLTFLoader: Failed to load buffers for ", path);
        cgltf_free(data);
        return false;
    }
    
    std::filesystem::path basePath = std::filesystem::path(path).parent_path();

    // 1. Load Materials
    std::vector<MaterialDesc> materials(data->materials_count);
    for (size_t i = 0; i < data->materials_count; ++i) {
        cgltf_material& mat = data->materials[i];
        MaterialDesc& desc = materials[i];
        
        if (mat.has_pbr_metallic_roughness) {
            desc.baseColor = toVec4(mat.pbr_metallic_roughness.base_color_factor);
            desc.metallic = mat.pbr_metallic_roughness.metallic_factor;
            desc.roughness = mat.pbr_metallic_roughness.roughness_factor;
            desc.albedoId = loadGLTFTexture(mat.pbr_metallic_roughness.base_color_texture.texture, resources, basePath, true);
            desc.metallicRoughnessId = loadGLTFTexture(mat.pbr_metallic_roughness.metallic_roughness_texture.texture, resources, basePath, false);
        }
        
        desc.normalId = loadGLTFTexture(mat.normal_texture.texture, resources, basePath, false);
        desc.emissiveId = loadGLTFTexture(mat.emissive_texture.texture, resources, basePath, true);
        desc.emissiveColor = glm::vec4(toVec3(mat.emissive_factor), 1.0f);
        desc.doubleSided = mat.double_sided;
    }

    // 2. Load Meshes (Primitives)
    std::vector<std::vector<AssetID>> meshesPrimitives(data->meshes_count);
    for (size_t i = 0; i < data->meshes_count; ++i) {
        cgltf_mesh& mesh = data->meshes[i];
        for (size_t j = 0; j < mesh.primitives_count; ++j) {
            cgltf_primitive& prim = mesh.primitives[j];
            
            std::vector<Vertex> vertices;
            std::vector<uint32_t> indices;
            
            // Vertices
            size_t vertexCount = 0;
            for (size_t k = 0; k < prim.attributes_count; ++k) {
                if (prim.attributes[k].type == cgltf_attribute_type_position) {
                    vertexCount = prim.attributes[k].data->count;
                    break;
                }
            }
            vertices.resize(vertexCount);
            
            for (size_t k = 0; k < prim.attributes_count; ++k) {
                cgltf_attribute& attr = prim.attributes[k];
                for (size_t v = 0; v < vertexCount; ++v) {
                    if (attr.type == cgltf_attribute_type_position) {
                        cgltf_accessor_read_float(attr.data, v, glm::value_ptr(vertices[v].pos), 3);
                        vertices[v].color = glm::vec3(1.0f); // default color
                        vertices[v].tangent = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f); // default tangent
                    } else if (attr.type == cgltf_attribute_type_normal) {
                        cgltf_accessor_read_float(attr.data, v, glm::value_ptr(vertices[v].normal), 3);
                    } else if (attr.type == cgltf_attribute_type_texcoord && attr.index == 0) {
                        cgltf_accessor_read_float(attr.data, v, glm::value_ptr(vertices[v].texCoord), 2);
                    } else if (attr.type == cgltf_attribute_type_texcoord && attr.index == 1) {
                        cgltf_accessor_read_float(attr.data, v, glm::value_ptr(vertices[v].lightmapUV), 2);
                    } else if (attr.type == cgltf_attribute_type_tangent) {
                        cgltf_accessor_read_float(attr.data, v, glm::value_ptr(vertices[v].tangent), 4);
                    }
                }
            }
            
            // Fix NaN issues: if tangent was missing, it's (0,0,0,0) which breaks shader TBN. Provide a default.
            for (size_t v = 0; v < vertexCount; ++v) {
                if (glm::length(glm::vec3(vertices[v].tangent)) < 0.01f) {
                    vertices[v].tangent = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
                }
            }
            
            // Indices
            if (prim.indices) {
                if (prim.type == cgltf_primitive_type_triangle_strip) {
                    indices.reserve((prim.indices->count - 2) * 3);
                    for (size_t v = 0; v < prim.indices->count - 2; ++v) {
                        uint32_t i0 = static_cast<uint32_t>(cgltf_accessor_read_index(prim.indices, v));
                        uint32_t i1 = static_cast<uint32_t>(cgltf_accessor_read_index(prim.indices, v + 1));
                        uint32_t i2 = static_cast<uint32_t>(cgltf_accessor_read_index(prim.indices, v + 2));
                        if (i0 == i1 || i1 == i2 || i2 == i0) continue; // skip degenerate triangles
                        if (v % 2 == 0) {
                            indices.push_back(i0); indices.push_back(i1); indices.push_back(i2);
                        } else {
                            indices.push_back(i0); indices.push_back(i2); indices.push_back(i1);
                        }
                    }
                } else if (prim.type == cgltf_primitive_type_triangle_fan) {
                    indices.reserve((prim.indices->count - 2) * 3);
                    uint32_t i0 = static_cast<uint32_t>(cgltf_accessor_read_index(prim.indices, 0));
                    for (size_t v = 1; v < prim.indices->count - 1; ++v) {
                        uint32_t i1 = static_cast<uint32_t>(cgltf_accessor_read_index(prim.indices, v));
                        uint32_t i2 = static_cast<uint32_t>(cgltf_accessor_read_index(prim.indices, v + 1));
                        if (i0 == i1 || i1 == i2 || i2 == i0) continue;
                        indices.push_back(i0); indices.push_back(i1); indices.push_back(i2);
                    }
                } else {
                    indices.resize(prim.indices->count);
                    for (size_t v = 0; v < prim.indices->count; ++v) {
                        indices[v] = static_cast<uint32_t>(cgltf_accessor_read_index(prim.indices, v));
                    }
                }
            } else {
                if (prim.type == cgltf_primitive_type_triangle_strip) {
                    indices.reserve((vertexCount - 2) * 3);
                    for (size_t v = 0; v < vertexCount - 2; ++v) {
                        uint32_t i0 = v, i1 = v + 1, i2 = v + 2;
                        if (v % 2 == 0) {
                            indices.push_back(i0); indices.push_back(i1); indices.push_back(i2);
                        } else {
                            indices.push_back(i0); indices.push_back(i2); indices.push_back(i1);
                        }
                    }
                } else {
                    indices.resize(vertexCount);
                    for (size_t v = 0; v < vertexCount; ++v) indices[v] = static_cast<uint32_t>(v);
                }
            }
            
            // Note: we could compute tangents here if missing, but MikkTSpace is complex.
            // For now, if no tangent, we provide a default one (1,0,0,1) which will result in some normal mapping,
            // but ideally we'd compute it.
            
            AssetID meshId = resources.registerMemoryMesh(vertices, indices);
            Log::info("Loaded GLTF Primitive: ", vertexCount, " vertices, ", indices.size(), " indices, meshId=", meshId, " type=", prim.type);
            for(size_t debugIdx=0; debugIdx < std::min<size_t>(10, indices.size()); ++debugIdx) {
                Log::info("  Idx ", debugIdx, ": ", indices[debugIdx]);
            }
            for(size_t debugV=0; debugV < std::min<size_t>(3, vertices.size()); ++debugV) {
                Log::info("  Vtx ", debugV, " pos=(", vertices[debugV].pos.x, ", ", vertices[debugV].pos.y, ", ", vertices[debugV].pos.z, ")");
            }
            meshesPrimitives[i].push_back(meshId);
        }
    }

    // 3. Process Scene Graph
    if (data->scene) {
        for (size_t i = 0; i < data->scene->nodes_count; ++i) {
            processNode(data->scene->nodes[i], &scene, resources, meshesPrimitives, materials, data);
        }
    }

    cgltf_free(data);
    return true;
}

} // namespace ne
