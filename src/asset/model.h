#pragma once

#include <string>
#include <vector>
#include <cstdint>

constexpr uint32_t NUM_LODS = 6;
constexpr float LOD_THRESHOLDS[NUM_LODS] = { 1.0f, 0.5f, 0.25f, 0.125f, 0.07f, 0.03f };

struct Material {
	//vec4 base_color;
	uint32_t albedo;
	uint32_t normal;
	//uint32_t met_rough;
	//uint32_t emissive;
	//uint32_t amb_occ;
	//vec4 emissive_factor;
	//float metallic_factor;
	//float roughness_factor;
	float alpha_cutoff;
	bool blend;
};

struct GPU_Material {
	uint32_t albedo;
	uint32_t normal;
	float alpha_cutoff;
	uint32_t blending;
};

struct Lod {
	uint32_t base_index;
	uint32_t index_count;
};

struct Mesh {
	uint32_t base_vertex;
	uint32_t vertex_count;

	Lod lods[NUM_LODS];

	Material material;

	mat4 transform; // relative to root of model
	//vec4 bounding_sphere;
	//Util::AABB aabb;
	//Material material;
	std::string name;
};

//uint32_t    indexCount;
//uint32_t    instanceCount;
//uint32_t    firstIndex;
//int32_t     vertexOffset;
//uint32_t    firstInstance;

struct GPU_Mesh {
	//uint32_t base_index;
	//uint32_t index_count;
	//uint32_t base_vertex;

	//uint32_t instance_count;
	//uint32_t first_instance;

	uint32_t transform_index;
	uint32_t material_index;
};

struct Model {
	std::string name;
	std::vector<Mesh> meshes;
	// transform
	// aabb / bs
};
