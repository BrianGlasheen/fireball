#pragma once

#include <string>
#include <vector>
#include <cstdint>

constexpr uint32_t NUM_LODS = 6;
constexpr float LOD_THRESHOLDS[NUM_LODS] = { 1.0f, 0.5f, 0.25f, 0.125f, 0.07f, 0.03f };

struct Lod {
	uint32_t base_index;
	uint32_t index_count;
};

struct Mesh {
	uint32_t base_vertex;
	uint32_t vertex_count;

	Lod lods[NUM_LODS];

	// parent? 
	mat4 transform; // relative to root of model
	//vec4 bounding_sphere;
	//Util::AABB aabb;
	//Material material;
	std::string name;
};

struct Model {
	std::string name;
	std::vector<Mesh> meshes;
	// transform
	// aabb / bs
};
