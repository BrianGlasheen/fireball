#include "model_manager.h"

#include "texture_manager.h"

#include "fireball/util/time.h"

#include <assimp/GltfMaterial.h>
#include <meshoptimizer.h>
#include <stb_image.h>

#include <mutex>
#include <thread>

using std::mutex;
using std::thread;

struct Animation {
    std::string name;
    uint32_t base_bone_animation;
    uint32_t bone_animation_count;
    float duration;
};

struct Bone_Animation {
    uint32_t bone_index;
    uint32_t base_position_keyframe;
    uint32_t position_keyframe_count;
    uint32_t base_rotation_keyframe;
    uint32_t rotation_keyframe_count;
    uint32_t base_scale_keyframe;
    uint32_t scale_keyframe_count;
    uint32_t padding;
};

struct Position_Keyframe {
    vec3 position;
    float time;
};

struct Rotation_Keyframe {
    quat rotation;
    float time;
    uint32_t padding[3];
};

struct Scale_Keyframe {
    vec3 scale;
    float time;
};

namespace Model_Manager {
    static std::string base_path;

    static std::vector<Vertex> g_vertices(0);
    static std::vector<uint32_t> g_indices(0);

    static std::vector<Model> g_models(0);
    static std::vector<Animated_Model> g_animated_models(0);

    static std::vector<Bone> g_bones(0);
    // another bone list
    // vertex with bone info for skinning
    static std::vector<Animation> g_animations(0);
    static std::vector<Bone_Animation> bone_animations(0);
    static std::vector<Position_Keyframe> position_keyframes(0);
    static std::vector<Rotation_Keyframe> rotation_keyframes(0);
    static std::vector<Scale_Keyframe> scale_keyframes(0);

    static mutex model_mutex;
    static mutex data_mutex;
    static vector<thread> loading_threads;

    mat4 assimp_to_glm(const aiMatrix4x4& ai_mat) {
        return mat4(
            ai_mat.a1, ai_mat.b1, ai_mat.c1, ai_mat.d1,
            ai_mat.a2, ai_mat.b2, ai_mat.c2, ai_mat.d2,
            ai_mat.a3, ai_mat.b3, ai_mat.c3, ai_mat.d3,
            ai_mat.a4, ai_mat.b4, ai_mat.c4, ai_mat.d4
        );
    }

    void init(const std::string& base, bool headless) {
        base_path = base;
    }

    void cleanup() {

    }

    bool model_loaded(const std::string& full_path, Model_Handle& handle) {
        // TODO check handle for animated and search accordingly
        for (size_t i = 0; i < g_models.size(); i++) {
            if (full_path == g_models[i].name) {
                handle.index = i;
                return true;
            }
        }
        return false;
    }

    Model_Handle load_model(const std::string& path, const Mesh_Opt_Flags mesh_opt_flags, bool append_base_path) {
        const std::string full_path = append_base_path ? base_path + path : path;

        model_mutex.lock();
            printf("[Model] Attempting to load %s\n", full_path.c_str());
            
            Model_Handle handle = {};
            if (model_loaded(path, handle)) {
                model_mutex.unlock();
                return handle;
            }

            handle.index = g_models.size();
            Model model;
            model.name = path;
            model.state = Loading_State::Loading;
            
            g_models.push_back(model);
        model_mutex.unlock();

        loading_threads.emplace_back(load_model_async, full_path, handle, mesh_opt_flags);

        return handle;
    }

    void load_model_async(const std::string& path, Model_Handle handle, const Mesh_Opt_Flags mesh_opt_flags) {
        Time start_time = high_resolution_clock::now();

        Assimp::Importer import;
        const aiScene* scene = import.ReadFile(path, aiProcess_CalcTangentSpace | aiProcess_Triangulate | aiProcess_FlipUVs | aiProcess_GenSmoothNormals | aiProcess_ValidateDataStructure | aiProcess_PopulateArmatureData);

        if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) {
            fprintf(stderr, "[Model] Assimp error: %s\n", import.GetErrorString());
            assert(false);
        }

        const std::string path_without_filename = path.substr(0, path.find_last_of("/") + 1);

        uint32_t num_meshes = scene->mNumMeshes;
        vector<Mesh> meshes(num_meshes);

        vector<Vertex> vertex_buffer; // todo could pre reserve total verts, meh
        vector<uint32_t> index_buffer;

        process_node(scene->mRootNode, scene, vertex_buffer, index_buffer, meshes, path_without_filename, mat4(1.0f), mesh_opt_flags);
        // todo write binary format

        data_mutex.lock();
            size_t begin_vertices = g_vertices.size();
            size_t begin_indices = g_indices.size();

            g_vertices.reserve(g_vertices.size() + vertex_buffer.size());
            g_indices.reserve(g_indices.size() + index_buffer.size());

            g_vertices.insert(g_vertices.end(), vertex_buffer.begin(), vertex_buffer.end());
            g_indices.insert(g_indices.end(), index_buffer.begin(), index_buffer.end());
        data_mutex.unlock();

        // update meshes with each base vertex / index
        for (Mesh& mesh : meshes) {
            mesh.base_vertex += (uint32_t)begin_vertices;

            for (Lod& lod : mesh.lods)
                lod.base_index += (uint32_t)begin_indices;
        }

        model_mutex.lock();
            g_models[handle.index].meshes = meshes;
            g_models[handle.index].state = Loading_State::Loaded;
        model_mutex.unlock();

        double elapsed = duration_cast<milliseconds>(high_resolution_clock::now() - start_time).count();

        printf("[Model] Loaded %s: %zu meshes %zu vertices (%.2f MB) %zu indices (%.2f MB) in %.1f Ms\n", path.c_str(), meshes.size(), vertex_buffer.size(), (vertex_buffer.size() * sizeof(Vertex)) * 1e-6, index_buffer.size(), (index_buffer.size() * sizeof(uint32_t)) * 1e-6, elapsed);
    }

    void process_node(aiNode* node, const aiScene* scene, vector<Vertex>& vertex_buffer, vector<uint32_t>& index_buffer, vector<Mesh>& meshes, const std::string& path, const mat4& parent_transform, const Mesh_Opt_Flags mesh_opt_flags) {
        mat4 current_transform = parent_transform * assimp_to_glm(node->mTransformation);

        for (uint32_t i = 0; i < node->mNumMeshes; i++) {
            aiMesh* ai_mesh = scene->mMeshes[node->mMeshes[i]];

            size_t current_vertices = vertex_buffer.size();
            // size_t current_indices = index_buffer.size();

            uint32_t vertex_count = (uint32_t)ai_mesh->mNumVertices;
            uint32_t index_count = (uint32_t)ai_mesh->mNumFaces * 3;

            vector<Vertex> local_vertex_buffer(vertex_count);
            vector<uint32_t> local_index_buffer(index_count);

            process_mesh(ai_mesh, local_vertex_buffer, local_index_buffer);
            optimize_mesh(local_vertex_buffer, local_index_buffer, mesh_opt_flags);

            vertex_buffer.reserve(current_vertices + vertex_count);
            vertex_buffer.insert(vertex_buffer.end(), local_vertex_buffer.begin(), local_vertex_buffer.end());

            Mesh mesh = {};
            mesh.name = ai_mesh->mName.C_Str();
            mesh.transform = current_transform;
            mesh.base_vertex = (uint32_t)current_vertices;
            mesh.vertex_count = (uint32_t)vertex_count;
            mesh.material = load_material(ai_mesh, scene, path);

            for (uint32_t lod = 0; lod < NUM_LODS; lod++) {
                std::vector<uint32_t> lod_indices = (lod == 0) ? 
                    local_index_buffer : generate_lod(local_vertex_buffer, local_index_buffer, LOD_THRESHOLDS[lod]);

                mesh.lods[lod].base_index = (uint32_t)index_buffer.size();
                mesh.lods[lod].index_count = (uint32_t)lod_indices.size();

                index_buffer.insert(index_buffer.end(), lod_indices.begin(), lod_indices.end());
            }

            meshes.push_back(mesh);
        }

        for (uint32_t i = 0; i < node->mNumChildren; i++) {
            process_node(node->mChildren[i], scene, vertex_buffer, index_buffer, meshes, path, current_transform, mesh_opt_flags);
        }
    }

    void process_mesh(const aiMesh* ai_mesh,  std::vector<Vertex>& vertex_buffer, std::vector<uint32_t>& index_buffer) {
        for (uint32_t i = 0; i < ai_mesh->mNumVertices; i++) {
            Vertex vertex = {};

            vertex.position = vec3(
                ai_mesh->mVertices[i].x,
                ai_mesh->mVertices[i].y,
                ai_mesh->mVertices[i].z
            );

            if (ai_mesh->HasNormals()) {
                vertex.normal = vec3(
                    ai_mesh->mNormals[i].x,
                    ai_mesh->mNormals[i].y,
                    ai_mesh->mNormals[i].z
                );
            }
            else {
                vertex.normal = vec3(0.0f, 0.0f, 1.0f);
            }

            if (ai_mesh->mTextureCoords[0]) {
                vertex.uv_x = ai_mesh->mTextureCoords[0][i].x;
                vertex.uv_y = ai_mesh->mTextureCoords[0][i].y;

                vertex.tangent = vec3(
                    ai_mesh->mTangents[i].x,
                    ai_mesh->mTangents[i].y,
                    ai_mesh->mTangents[i].z
                );
            }
            else {
                vertex.tangent = vec3(1.0f, 0.0f, 0.0f);
            }

            if (ai_mesh->HasVertexColors(0)) {
                vertex.color = vec4(
                    ai_mesh->mColors[0][i].r,
                    ai_mesh->mColors[0][i].g,
                    ai_mesh->mColors[0][i].b,
                    ai_mesh->mColors[0][i].a
                );
            }
            else {
                vertex.color = vec4(1.0f);
            }

            vertex_buffer[i] = vertex;
        }

        uint32_t index = 0;
        for (uint32_t i = 0; i < ai_mesh->mNumFaces; ++i) {
            const aiFace& face = ai_mesh->mFaces[i];
            memcpy(&index_buffer[index], face.mIndices, 3 * sizeof(uint32_t));
            index += 3;

            //assert(face.mNumIndices == 3);
            //index_buffer.push_back(face.mIndices[0]);
            //index_buffer.push_back(face.mIndices[1]);
            //index_buffer.push_back(face.mIndices[2]);
        }
    }

    void optimize_mesh(std::vector<Vertex>& vertex_buffer, std::vector<uint32_t>& index_buffer, const Mesh_Opt_Flags flags) {
        if (flags.index) {
            std::vector<uint32_t> remap(vertex_buffer.size());
            size_t unique_vertices = meshopt_generateVertexRemap(
                remap.data(),
                index_buffer.data(),
                index_buffer.size(),
                vertex_buffer.data(),
                vertex_buffer.size(),
                sizeof(Vertex)
            );

            std::vector<uint32_t> optimized_indices(index_buffer.size());
            meshopt_remapIndexBuffer(
                optimized_indices.data(),
                index_buffer.data(),
                index_buffer.size(),
                remap.data()
            );
            index_buffer = std::move(optimized_indices);

            std::vector<Vertex> optimized_vertices(unique_vertices);
            meshopt_remapVertexBuffer(
                optimized_vertices.data(),
                vertex_buffer.data(),
                vertex_buffer.size(),
                sizeof(Vertex),
                remap.data()
            );
            vertex_buffer = std::move(optimized_vertices);

            //printf("[Optimize] Index optimization: %zu -> %zu vertices\n", remap.size(), unique_vertices);
        }

        if (flags.vertex_cache) {
            meshopt_optimizeVertexCache(
                index_buffer.data(),
                index_buffer.data(),
                index_buffer.size(),
                vertex_buffer.size()
            );
            //printf("[Optimize] Vertex cache optimization applied\n");
        }

        if (flags.overdraw) {
            meshopt_optimizeOverdraw(
                index_buffer.data(),
                index_buffer.data(),
                index_buffer.size(),
                &vertex_buffer[0].position.x,
                vertex_buffer.size(),
                sizeof(Vertex),
                1.05f
            );
            //printf("[Optimize] Overdraw optimization applied\n");
        }

        if (flags.vertex_fetch) {
            std::vector<Vertex> optimized_vertices(vertex_buffer.size());
            meshopt_optimizeVertexFetch(
                optimized_vertices.data(),
                index_buffer.data(),
                index_buffer.size(),
                vertex_buffer.data(),
                vertex_buffer.size(),
                sizeof(Vertex)
            );
            vertex_buffer = std::move(optimized_vertices);
            //printf("[Optimize] Vertex fetch optimization applied\n");
        }

        if (flags.vertex_quantization) {

        }

        if (flags.shadow_indexing) {

        }
    }

    std::vector<uint32_t> generate_lod(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices, float threshold) {
        size_t target_index_count = size_t(indices.size() * threshold);

        std::vector<uint32_t> lod_indices(indices.size());

        size_t final_count = meshopt_simplify(
            lod_indices.data(),
            indices.data(),
            indices.size(),
            &vertices[0].position.x,
            vertices.size(),
            sizeof(Vertex),
            target_index_count,
            0.02f,
            0,
            nullptr
        );

        lod_indices.resize(final_count);
        return lod_indices;
    }

    Material load_material(const aiMesh* mesh, const aiScene* scene, const std::string& path) {
        Material mesh_material = {};

        aiMaterial* material = scene->mMaterials[mesh->mMaterialIndex];

        if (mesh->mMaterialIndex >= 0) {
            if (material->GetTextureCount(aiTextureType_BASE_COLOR)) {
                aiString str;
                material->GetTexture(aiTextureType_BASE_COLOR, 0, &str);
                mesh_material.albedo = Texture_Manager::load(path + str.C_Str());
            }
            else {
                mesh_material.albedo = Texture_Manager::get_by_name("error").bindless_id;
            }

            if (material->GetTextureCount(aiTextureType_NORMALS)) {
                aiString str;
                material->GetTexture(aiTextureType_NORMALS, 0, &str);
                mesh_material.normal = Texture_Manager::load(path + str.C_Str());
            }
            else {
                mesh_material.normal = Texture_Manager::get_by_name("normal").bindless_id;
            }
        }

        float alpha_cutoff = 0.5f;
        mesh_material.blend = false;

        aiString alpha_mode;
        if (AI_SUCCESS == material->Get(AI_MATKEY_GLTF_ALPHAMODE, alpha_mode)) {
            if (strcmp(alpha_mode.C_Str(), "BLEND") == 0) {
                mesh_material.blend = true;
                alpha_cutoff = 0.0f;
            }
            else if (strcmp(alpha_mode.C_Str(), "MASK") == 0) {
                material->Get(AI_MATKEY_GLTF_ALPHACUTOFF, alpha_cutoff);
            }
            else if (strcmp(alpha_mode.C_Str(), "OPAQUE") == 0) {
                material->Get(AI_MATKEY_GLTF_ALPHACUTOFF, alpha_cutoff);
                alpha_cutoff = 0.0f;
            }
        }

        mesh_material.alpha_cutoff = alpha_cutoff;

        return mesh_material;
    }

    void load_bones(const aiScene* scene) {
        std::vector<Bone> bones;
        std::unordered_map<std::string, int> boneMap;
        std::unordered_map<std::string, glm::mat4> boneOffsets;

        for (unsigned int i = 0; i < scene->mNumMeshes; i++) {
            aiMesh* mesh = scene->mMeshes[i];
            for (unsigned int j = 0; j < mesh->mNumBones; j++) {
                aiBone* bone = mesh->mBones[j];
                boneOffsets[bone->mName.C_Str()] = assimp_to_glm(bone->mOffsetMatrix);
            }
        }

        std::function<void(aiNode*, int, mat4, const std::unordered_map<std::string, glm::mat4>&,
            std::vector<Bone>&, std::unordered_map<std::string, int>&)> traverseSkeleton =
            [&](aiNode* node, int parentIdx, mat4 accumulatedTransform,
                const std::unordered_map<std::string, glm::mat4>& boneOffsets,
                std::vector<Bone>& bones,
                std::unordered_map<std::string, int>& boneMap) {

                    mat4 currentTransform = accumulatedTransform * assimp_to_glm(node->mTransformation);

                    if (boneOffsets.find(node->mName.C_Str()) != boneOffsets.end()) {
                        int currentIdx = bones.size();
                        boneMap[node->mName.C_Str()] = currentIdx;

                        Bone bone;
                        bone.name = node->mName.C_Str();
                        bone.parent = parentIdx;
                        
                        bone.inverse_bind = boneOffsets.at(node->mName.C_Str());

                        bones.push_back(bone);

                        parentIdx = currentIdx;
                    }

                    for (unsigned int i = 0; i < node->mNumChildren; i++) {
                        traverseSkeleton(node->mChildren[i], parentIdx, currentTransform, boneOffsets, bones, boneMap);
                    }
            };

        traverseSkeleton(scene->mRootNode, -1, mat4(1.0f), boneOffsets, bones, boneMap);

        int base = g_bones.size();
        g_bones.insert(g_bones.end(), bones.begin(), bones.end());
        int count = g_bones.size() - base;

        print_bone_tree(base, count);
    }

    void print_bone_tree(int base, int count) {
        int endIdx = std::min(base + count, (int)g_bones.size());

        std::function<void(int, const std::string&, bool)> printBone = [&](int idx, const std::string& prefix, bool isLast) {
            const Bone& bone = g_bones[idx];

            printf("%s", prefix.c_str());
            printf("%s", isLast ? "\\- " : "|-- ");
            printf("%s [%d]", bone.name.c_str(), idx);
            printf(" (parent: %d)\n", bone.parent);

            std::string childPrefix = prefix + (isLast ? "    " : "|   ");
            std::vector<int> children;

            for (int i = base; i < endIdx; i++) {
                if (g_bones[i].parent == idx) {
                    children.push_back(i);
                }
            }

            for (size_t i = 0; i < children.size(); i++) {
                bool childIsLast = (i == children.size() - 1);
                printBone(children[i], childPrefix, childIsLast);
            }
        };

        std::vector<int> roots;
        for (int i = base; i < endIdx; i++) {
            if (g_bones[i].parent == -1) {
                roots.push_back(i);
            }
        }

        for (size_t i = 0; i < roots.size(); i++) {
            bool isLast = (i == roots.size() - 1);
            printBone(roots[i], "", isLast);
        }
    }

    void wait_for_all_loads() {
        for (thread& load : loading_threads)
            load.join();

        loading_threads.clear();

        for (const Model& model : g_models)
            assert(model.state == Loading_State::Loaded);
    }

    std::vector<Vertex>& get_vertices() {
        return g_vertices;
    }
    
    std::vector<uint32_t>& get_indices() {
        return g_indices;
    }

    std::vector<Model>& get_models() {
        return g_models;
    }

    std::span<const Bone> get_model_bones(Model_Handle handle) {
        assert(handle.animated);

        Animated_Model& model = g_animated_models[handle.index];

        return std::span<const Bone>(g_bones.begin() + model.base_bone, g_bones.begin() + model.base_bone + model.bone_count);
    }

    Model& get_model(Model_Handle handle) {
        return handle.animated ? g_animated_models[handle.index].model : g_models[handle.index];
    };

    std::string& get_model_name(Model_Handle handle) {
        return g_models[handle.index].name;
    }

    size_t get_num_vertices() {
        return g_vertices.size();
    }

    size_t get_num_vertices(Model_Handle handle) {
        return ~0;
    }

    size_t get_num_indices() {
        return g_indices.size();
    }

    size_t get_num_indices(Model_Handle handle) {
        return ~0;
    }

    size_t get_num_models() {
        return g_models.size();
    }
}
