#pragma once

#include "math.h"
#include "model.h"

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include <string>
#include <vector>

struct Model_Handle {
    uint32_t index;
    // animated
};

struct Vertex {
    vec3 position;
    float uv_x;
    vec3 normal;
    float uv_y;
    vec4 color;
    // tangents
    // maybe bitangents
};

namespace Model_Manager {
    mat4 assimp_to_glm(const aiMatrix4x4& ai_mat);

    void init(const std::string& base);
    void cleanup();

    bool model_loaded(const std::string& full_path, Model_Handle& model_index);
    Model_Handle load_model(const std::string& path, bool append_base_path = true);

    void process_node(aiNode* node, const aiScene* scene, Model& model, const std::string& path, const mat4& parent_transform);
    void process_mesh(const aiMesh* ai_mesh, Mesh& mesh);

    //Material load_material(const aiMesh* mesh, const aiScene* scene, const std::string& path);

    //void setup_buffers();
    //void setup_ssbos();

    Model& get_model(Model_Handle handle);
    //Animated_Model& get_animated_model(model_handle handle);
    std::string& get_model_name(Model_Handle handle);

    std::vector<Vertex>& get_vertices();
    std::vector<uint32_t>& get_indices();
    std::vector<Model>& get_models();
    size_t get_num_vertices();
    size_t get_num_vertices(Model_Handle handle);
    size_t get_num_indices();
    size_t get_num_indices(Model_Handle handle);
    size_t get_num_models();
}
