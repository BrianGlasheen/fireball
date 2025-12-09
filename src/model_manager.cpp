#pragma once

#include "model_manager.h"

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

    Model_Handle load_model(const std::string& path, bool append_base_path) {
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

        process_node(scene->mRootNode, scene, model, path_without_filename, mat4(1.0f));

        uint32_t actual_meshes = (uint32_t)model.meshes.size();
        assert(num_meshes == actual_meshes);

        handle.index = g_models.size();
        g_models.push_back(model);

        size_t end_vertices = g_vertices.size();
        size_t end_indices = g_indices.size();
        
        // todo can add timer
        printf("[Model] Loaded %llu vertices\n", end_vertices - begin_vertices);
        printf("[Model] Loaded %llu indices\n", end_indices - begin_indices);

        return handle;
    }

    void process_node(aiNode* node, const aiScene* scene, Model& model, const std::string& path, const mat4& parent_transform) {
        mat4 current_transform = parent_transform * assimp_to_glm(node->mTransformation);

        for (uint32_t i = 0; i < node->mNumMeshes; i++) {
            aiMesh* ai_mesh = scene->mMeshes[node->mMeshes[i]];

            Mesh mesh = {};
            mesh.name = ai_mesh->mName.C_Str();
            mesh.transform = current_transform;

            printf("[Model] Processing mesh %s\n", mesh.name.c_str());
            size_t begin_vertices = g_vertices.size();
            size_t begin_indices = g_vertices.size();

            process_mesh(ai_mesh, mesh);

            model.meshes.push_back(mesh);

            size_t end_vertices = g_vertices.size();
            size_t end_indices = g_vertices.size();

            printf("[Model] Mesh has %llu vertices\n", end_vertices - begin_vertices);
            printf("[Model] Mesh has %llu indices\n", end_indices - begin_indices);
        }

        for (uint32_t i = 0; i < node->mNumChildren; i++) {
            process_node(node->mChildren[i], scene, model, path, current_transform);
        }
    }

    void process_mesh(const aiMesh* ai_mesh, Mesh& mesh) {
        mesh.base_vertex = g_vertices.size();
        mesh.vertex_count = ai_mesh->mNumVertices;

        g_vertices.reserve(g_vertices.size() + mesh.vertex_count);
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

            g_vertices.push_back(vertex);
        }

        // add indices
        mesh.base_index = g_indices.size();

        uint32_t num_indices = 0;
        for (uint32_t i = 0; i < ai_mesh->mNumFaces; ++i)
            num_indices += ai_mesh->mFaces[i].mNumIndices;

        mesh.index_count = num_indices;
        
        g_indices.reserve(g_indices.size() + num_indices);
        for (uint32_t i = 0; i < ai_mesh->mNumFaces; ++i) {
            const aiFace& face = ai_mesh->mFaces[i];
            g_indices.insert(g_indices.end(), face.mIndices, face.mIndices + face.mNumIndices);
        }
    }

    //Material load_material(const aiMesh* mesh, const aiScene* scene, const std::string& path);

    //void setup_buffers();
    
    //void setup_ssbos();

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
