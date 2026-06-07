#include "scene/GLTFLoader.hpp"
#include "scene/Scene.hpp"
#include "scene/MeshNode.hpp"
#include "scene/Node.hpp"
#include "graphics/Mesh.hpp"
#include "graphics/ResourceManager.hpp"
#include "scene/animation/Rig.hpp"
#include "scene/animation/AnimationClip.hpp"
#include "scene/animation/Animator.hpp"
#include "scene/animation/ClipNode.hpp"
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
                        const std::vector<MaterialDesc>& materials, cgltf_data* data,
                        std::vector<std::pair<MeshNode*, cgltf_skin*>>& skinnedMeshes) {
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
            MeshNode* mNode = neNode->createChild<MeshNode>(primName, resources.getMesh(primitives[i]), mat);
            if (node->skin) {
                skinnedMeshes.push_back({mNode, node->skin});
            }
        }
    }

    for (size_t i = 0; i < node->children_count; ++i) {
        processNode(node->children[i], neNode, resources, meshesPrimitives, materials, data, skinnedMeshes);
    }
}

bool GLTFLoader::load(const std::string& path, Node& rootNode, ResourceManager& resources) {
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
                    } else if (attr.type == cgltf_attribute_type_joints && attr.index == 0) {
                        uint32_t tmp[4] = {0,0,0,0};
                        cgltf_accessor_read_uint(attr.data, v, tmp, 4);
                        vertices[v].boneIndices = glm::ivec4(tmp[0], tmp[1], tmp[2], tmp[3]);
                    } else if (attr.type == cgltf_attribute_type_weights && attr.index == 0) {
                        cgltf_accessor_read_float(attr.data, v, glm::value_ptr(vertices[v].boneWeights), 4);
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
    std::vector<std::pair<MeshNode*, cgltf_skin*>> skinnedMeshes;
    if (data->scene) {
        std::filesystem::path p(path);
        Node* containerNode = rootNode.createChild<Node>(p.stem().string());
        for (size_t i = 0; i < data->scene->nodes_count; ++i) {
            processNode(data->scene->nodes[i], containerNode, resources, meshesPrimitives, materials, data, skinnedMeshes);
        }
    }

    // 4. Process Skins (Rigs)
    std::vector<AssetID> skinRigs(data->skins_count, kAssetInvalid);
    for (size_t i = 0; i < data->skins_count; ++i) {
        cgltf_skin& skin = data->skins[i];
        auto rig = std::make_unique<Rig>();
        
        for (size_t j = 0; j < skin.joints_count; ++j) {
            cgltf_node* joint = skin.joints[j];
            std::string name = joint->name ? joint->name : "Bone_" + std::to_string(j);
            
            int32_t parentIndex = -1;
            if (joint->parent) {
                for (size_t p = 0; p < skin.joints_count; ++p) {
                    if (skin.joints[p] == joint->parent) {
                        parentIndex = static_cast<int32_t>(p);
                        break;
                    }
                }
            }
            
            glm::mat4 invBind{1.0f};
            if (skin.inverse_bind_matrices) {
                cgltf_accessor_read_float(skin.inverse_bind_matrices, j, glm::value_ptr(invBind), 16);
            }
            
            rig->addBone(name, parentIndex, invBind);
        }
        
        std::string rigName = path + "#rig" + std::to_string(i);
        skinRigs[i] = resources.registerMemoryRig(rigName, std::move(rig));
        Log::info("Loaded GLTF Skin: ", skin.joints_count, " joints, rigId=", skinRigs[i]);
    }

    // 5. Attach Animators
    for (const auto& [mNode, skin] : skinnedMeshes) {
        size_t skinIdx = skin - data->skins;
        AssetID rigId = skinRigs[skinIdx];
        Rig* rig = resources.getRig(rigId);
        if (rig) {
            mNode->addBehaviour<Animator>()->setRig(rig);
            Log::info("Attached Animator with Rig to MeshNode ", mNode->name());
        }
    }

    // 6. Process Animations
    std::vector<AssetID> loadedClips;
    for (size_t i = 0; i < data->animations_count; ++i) {
        cgltf_animation& anim = data->animations[i];
        std::string animName = anim.name ? anim.name : "Anim_" + std::to_string(i);
        
        // Find max duration
        float duration = 0.0f;
        for (size_t s = 0; s < anim.samplers_count; ++s) {
            cgltf_accessor* input = anim.samplers[s].input;
            if (input && input->count > 0) {
                float maxTime = 0.0f;
                cgltf_accessor_read_float(input, input->count - 1, &maxTime, 1);
                if (maxTime > duration) duration = maxTime;
            }
        }
        
        auto clip = std::make_unique<AnimationClip>(animName, duration);
        
        for (size_t c = 0; c < anim.channels_count; ++c) {
            cgltf_animation_channel& channel = anim.channels[c];
            if (!channel.target_node) continue;
            
            std::string targetName = channel.target_node->name ? channel.target_node->name : "";
            if (targetName.empty()) continue; // We need names for retargeting
            
            cgltf_animation_sampler* sampler = channel.sampler;
            cgltf_accessor* input = sampler->input;
            cgltf_accessor* output = sampler->output;
            
            size_t count = input->count;
            std::vector<float> timestamps(count);
            for (size_t v = 0; v < count; ++v) {
                cgltf_accessor_read_float(input, v, &timestamps[v], 1);
            }
            
            if (channel.target_path == cgltf_animation_path_type_translation || channel.target_path == cgltf_animation_path_type_scale) {
                auto track = std::make_unique<TypedAnimTrack<glm::vec3>>();
                track->target = (channel.target_path == cgltf_animation_path_type_translation) ? TrackTarget::Translation : TrackTarget::Scale;
                track->timestamps = std::move(timestamps);
                track->values.resize(count);
                for (size_t v = 0; v < count; ++v) {
                    cgltf_accessor_read_float(output, v, glm::value_ptr(track->values[v]), 3);
                }
                clip->addTrack(targetName, std::move(track));
            } else if (channel.target_path == cgltf_animation_path_type_rotation) {
                auto track = std::make_unique<TypedAnimTrack<glm::quat>>();
                track->target = TrackTarget::Rotation;
                track->timestamps = std::move(timestamps);
                track->values.resize(count);
                for (size_t v = 0; v < count; ++v) {
                    float q[4];
                    cgltf_accessor_read_float(output, v, q, 4);
                    track->values[v] = glm::quat(q[3], q[0], q[1], q[2]); // wxyz
                }
                clip->addTrack(targetName, std::move(track));
            }
        }
        
        std::string clipPath = path + "#" + animName;
        AssetID clipId = resources.registerMemoryAnimation(clipPath, std::move(clip));
        loadedClips.push_back(clipId);
        Log::info("Loaded GLTF Animation: ", animName, " duration=", duration, " id=", clipId);
    }

    // 7. Auto-play the first animation on all skinned meshes (for demo purposes)
    if (!loadedClips.empty() && !skinnedMeshes.empty()) {
        const AnimationClip* clip = resources.getAnimation(loadedClips[0]);
        if (clip) {
            for (const auto& pair : skinnedMeshes) {
                MeshNode* mNode = pair.first;
                if (Animator* anim = mNode->getBehaviour<Animator>()) {
                    if (const Rig* rig = anim->rig()) {
                        anim->setRootNode(std::make_unique<ClipNode>(clip, *rig));
                        Log::info("Auto-playing animation on ", mNode->name());
                    }
                }
            }
        }
    }

    cgltf_free(data);
    return true;
}

} // namespace ne
