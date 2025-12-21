#include "model_manager.h"

#include "texture_manager.h"

#include <assimp/GltfMaterial.h>
#include <meshoptimizer.h>
#include <stb_image.h>

namespace Model_Manager {
    static std::string base_path;

    static std::vector<Vertex> g_vertices(0);
    static std::vector<uint32_t> g_indices(0);

    static std::vector<Model> g_models(0);

    mat4 assimp_to_glm(const aiMatrix4x4& ai_mat) {
        return mat4(
            ai_mat.a1, ai_mat.b1, ai_mat.c1, ai_mat.d1,
            ai_mat.a2, ai_mat.b2, ai_mat.c2, ai_mat.d2,
            ai_mat.a3, ai_mat.b3, ai_mat.c3, ai_mat.d3,
            ai_mat.a4, ai_mat.b4, ai_mat.c4, ai_mat.d4
        );
    }

    void init(const std::string& base) {
        base_path = base;
    }

    void cleanup() {

    }

    bool model_loaded(const std::string& full_path, Model_Handle& handle) {
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

        printf("[Model] Attempting to load %s\n", full_path.c_str());

        size_t begin_vertices = g_vertices.size();
        size_t begin_indices = g_indices.size();

        Model_Handle handle = {};
        if (model_loaded(path, handle))
            return handle;

        Assimp::Importer import;
        const aiScene* scene = import.ReadFile(full_path, aiProcess_CalcTangentSpace | aiProcess_Triangulate | aiProcess_FlipUVs | aiProcess_GenSmoothNormals | aiProcess_ValidateDataStructure | aiProcess_PopulateArmatureData);

        if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) {
            fprintf(stderr, "[Model] Assimp error: %s\n", import.GetErrorString());
            assert(false);
        }

        const std::string path_without_filename = full_path.substr(0, full_path.find_last_of("/") + 1);

        Model model;
        model.name = path;

        uint32_t num_meshes = scene->mNumMeshes;
        model.meshes.reserve(scene->mNumMeshes);

        process_node(scene->mRootNode, scene, model, path_without_filename, mat4(1.0f), mesh_opt_flags);

        uint32_t actual_meshes = (uint32_t)model.meshes.size();
        assert(num_meshes == actual_meshes);

        handle.index = g_models.size();
        g_models.push_back(model);

        size_t end_vertices = g_vertices.size();
        size_t end_indices = g_indices.size();
        
        // todo can add timer
        printf("[Model] Loaded %s: %zu meshes %zu vertices\n", path.c_str(), model.meshes.size(), end_vertices - begin_vertices);

        return handle;
    }

    void process_node(aiNode* node, const aiScene* scene, Model& model, const std::string& path, const mat4& parent_transform, const Mesh_Opt_Flags mesh_opt_flags) {
        mat4 current_transform = parent_transform * assimp_to_glm(node->mTransformation);

        for (uint32_t i = 0; i < node->mNumMeshes; i++) {
            size_t begin_vertices = g_vertices.size();
            size_t begin_indices = g_indices.size();

            aiMesh* ai_mesh = scene->mMeshes[node->mMeshes[i]];

            uint32_t vertex_count = (uint32_t)ai_mesh->mNumVertices;
            uint32_t index_count = (uint32_t)ai_mesh->mNumFaces * 3;

            std::vector<Vertex> vertex_buffer(vertex_count);
            std::vector<uint32_t> index_buffer(index_count);
            process_mesh(ai_mesh, vertex_buffer, index_buffer);

            optimize_mesh(vertex_buffer, index_buffer, mesh_opt_flags);

            Mesh mesh = {};
            mesh.name = ai_mesh->mName.C_Str();
            mesh.transform = current_transform;
            mesh.base_vertex = (uint32_t)g_vertices.size();
            mesh.vertex_count = (uint32_t)vertex_buffer.size();
            mesh.material = load_material(ai_mesh, scene, path);

            g_vertices.insert(g_vertices.end(), vertex_buffer.begin(), vertex_buffer.end());

            for (uint32_t lod = 0; lod < NUM_LODS; lod++) {
                printf("lod %d\n", lod);
                std::vector<uint32_t> lod_indices = (lod == 0) ? 
                    index_buffer : generate_lod(vertex_buffer, index_buffer, LOD_THRESHOLDS[lod]);

                mesh.lods[lod].base_index = (uint32_t)g_indices.size();
                mesh.lods[lod].index_count = (uint32_t)lod_indices.size();

                g_indices.insert(g_indices.end(), lod_indices.begin(), lod_indices.end());
            }

            model.meshes.push_back(mesh);

            size_t end_vertices = g_vertices.size();
            size_t end_indices = g_indices.size();
            
            printf("[Model] Loaded mesh %s\n", mesh.name.c_str());
            printf("[Model] %zu vertices\n", end_vertices - begin_vertices);
            for (uint32_t lod = 0; lod < NUM_LODS; lod++) {
                printf("[Model] LOD%d: %u indices (%.1f%%)\n", lod, mesh.lods[lod].index_count, LOD_THRESHOLDS[lod] * 100.0f);
            }
            // timer
        }

        for (uint32_t i = 0; i < node->mNumChildren; i++) {
            process_node(node->mChildren[i], scene, model, path, current_transform, mesh_opt_flags);
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

            printf("[Optimize] Index optimization: %zu -> %zu vertices\n", remap.size(), unique_vertices);
        }

        if (flags.vertex_cache) {
            meshopt_optimizeVertexCache(
                index_buffer.data(),
                index_buffer.data(),
                index_buffer.size(),
                vertex_buffer.size()
            );
            printf("[Optimize] Vertex cache optimization applied\n");
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
            printf("[Optimize] Overdraw optimization applied\n");
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
            printf("[Optimize] Vertex fetch optimization applied\n");
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

    std::vector<Vertex>& get_vertices() {
        return g_vertices;
    }
    
    std::vector<uint32_t>& get_indices() {
        return g_indices;
    }

    std::vector<Model>& get_models() {
        return g_models;
    }

    Model& get_model(Model_Handle handle) {
        return g_models[handle.index];
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
