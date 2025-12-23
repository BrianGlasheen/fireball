const uint NUM_LODS = 6; // todo shared config

struct Lod {
	uint base_index;
	uint index_count;
};

struct Mesh {
	int base_vertex;
	uint vertex_count;

	Lod lods[NUM_LODS];

	//uint32_t enitity;
	uint render_info_index;
	uint flags;

	vec4 bounding_sphere;
};

struct Mesh_Render_Info {
	uint transform_index;
	uint material_index;
};

struct Material {
    uint albedo;
    uint normal;
    float alpha_cutoff;
    uint blending;
};