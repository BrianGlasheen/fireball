#include "model_manager.h"

#include "meshoptimizer.h"

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
        printf("[Model] Loaded %lu vertices\n", end_vertices - begin_vertices);
        printf("[Model] Loaded %lu indices\n", end_indices - begin_indices);

        return handle;
    }

    void process_node(aiNode* node, const aiScene* scene, Model& model, const std::string& path, const mat4& parent_transform, const Mesh_Opt_Flags mesh_opt_flags) {
        mat4 current_transform = parent_transform * assimp_to_glm(node->mTransformation);

        for (uint32_t i = 0; i < node->mNumMeshes; i++) {
            size_t begin_vertices = g_vertices.size();
            size_t begin_indices = g_vertices.size();

            aiMesh* ai_mesh = scene->mMeshes[node->mMeshes[i]];

            Mesh mesh = {};
            mesh.name = ai_mesh->mName.C_Str();
            mesh.transform = current_transform;
            mesh.base_vertex = (uint32_t)g_vertices.size();
            mesh.base_index = (uint32_t)g_indices.size();
            mesh.vertex_count = (uint32_t)ai_mesh->mNumVertices;
            mesh.index_count = (uint32_t)ai_mesh->mNumFaces * 3;

            std::vector<Vertex> vertex_buffer(mesh.vertex_count);
            std::vector<uint32_t> index_buffer(mesh.index_count);
            process_mesh(ai_mesh, vertex_buffer, index_buffer);

            // for each lod
            optimize_mesh(vertex_buffer, index_buffer, mesh_opt_flags);

            g_vertices.insert(g_vertices.end(), vertex_buffer.begin(), vertex_buffer.end());
            g_indices.insert(g_indices.end(), index_buffer.begin(), index_buffer.end());

            model.meshes.push_back(mesh);

            size_t end_vertices = g_vertices.size();
            size_t end_indices = g_vertices.size();
            
            printf("[Model] Loaded mesh %s\n", mesh.name.c_str());
            printf("[Model] %lu vertices & %lu indices\n", end_vertices - begin_vertices,  end_indices - begin_indices);
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

            vertex_buffer[i]  = vertex;
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

        }

        if (flags.vertex_cache) {

        }

        if (flags.overdraw) {

        }

        if (flags.vertex_fetch) {

        }

        if (flags.vertex_quantization) {

        }

        if (flags.shadow_indexing) {

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
