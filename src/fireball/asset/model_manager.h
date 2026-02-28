#pragma once

#include "model.h"

#include "fireball/util/math.h"

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include <span>
#include <string>
#include <vector>

using std::vector;

struct Model_Handle {
    uint32_t index;
    bool animated;
};

struct Vertex {
    vec3 position;
    float uv_x;
    vec3 normal;
    float uv_y;
    vec4 color;
    vec3 tangent;
    int padding;
};

struct Mesh_Opt_Flags {
    uint8_t index : 1;
    uint8_t vertex_cache : 1;
    uint8_t overdraw : 1;
    uint8_t vertex_fetch : 1;
    uint8_t vertex_quantization : 1;
    uint8_t shadow_indexing : 1;
    uint8_t : 2; 
};

constexpr Mesh_Opt_Flags Mesh_Opt_Flags_All = {
    .index = 1,
    .vertex_cache = 1,
    .overdraw = 1,
    .vertex_fetch = 1,
    .vertex_quantization = 1,
    .shadow_indexing = 1
};

struct Bone {
    std::string name;
    uint32_t parent;
    mat4 inverse_bind;
};

namespace Model_Manager {
    mat4 assimp_to_glm(const aiMatrix4x4& ai_mat);

    void init(const std::string& base);
    void cleanup();

    bool model_loaded(const std::string& full_path, Model_Handle& model_index);
    
    Model_Handle load_model(const std::string& path, const Mesh_Opt_Flags mesh_opt_flags = {}, bool append_base_path = true);
    void load_model_async(const std::string& path, Model_Handle handle, const Mesh_Opt_Flags mesh_opt_flags);

    void process_node(aiNode* node, const aiScene* scene, vector<Vertex>& vertex_buffer, vector<uint32_t>& index_buffer, vector<Mesh>& meshes, const std::string& path, const mat4& parent_transform, const Mesh_Opt_Flags mesh_opt_flags);
    void process_mesh(const aiMesh* ai_mesh, vector<Vertex>& vertex_buffer, vector<uint32_t>& index_buffer);
    void optimize_mesh(vector<Vertex>& vertex_buffer, vector<uint32_t>& index_buffer, const Mesh_Opt_Flags flags);
    vector<uint32_t> generate_lod(const vector<Vertex>& vertices, const vector<uint32_t>& indices, float threshold);

    Material load_material(const aiMesh* mesh, const aiScene* scene, const std::string& path);

    void load_bones(const aiScene* scene);
    void print_bone_tree(int base, int count);

    void wait_for_all_loads();

    Model& get_model(Model_Handle handle);
    //Animated_Model& get_animated_model(model_handle handle);
    std::string& get_model_name(Model_Handle handle);

    vector<Vertex>& get_vertices();
    vector<uint32_t>& get_indices();
    vector<Model>& get_models();
    std::span<const Bone> get_model_bones(Model_Handle handle);
    size_t get_num_vertices();
    size_t get_num_vertices(Model_Handle handle);
    size_t get_num_indices();
    size_t get_num_indices(Model_Handle handle);
    size_t get_num_models();
}
