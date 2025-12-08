#pragma once

#include "mesh.h"

#include <string>
#include <vector>
#include <cstdint>

struct Mesh {
	uint32_t base_vertex;
	uint32_t vertex_count;
	uint32_t base_index;
	uint32_t index_count;

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
