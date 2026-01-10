#version 450

#extension GL_EXT_buffer_reference : require
#extension GL_GOOGLE_include_directive: require

#include "light.h"
#include "mesh.h"

layout (location = 0) out vec3 out_color;
layout (location = 1) out vec2 out_uv;
layout (location = 2) out vec3 out_normal;

layout (location = 3) flat out uint out_albedo;
layout (location = 4) flat out uint out_normal_map;
layout (location = 5) flat out float out_alpha_cutoff;
layout (location = 6) flat out uint out_blending;

layout (location = 7) out vec3 out_world_pos;
layout (location = 8) out mat3 out_TBN;

struct Vertex {
	vec3 position;
	float uv_x;
	vec3 normal;
	float uv_y;
	vec4 color;
	vec3 tangent;
	int padding;
};

layout(buffer_reference, std430) readonly buffer VertexBuffer { 
	Vertex vertices[];
};

layout(buffer_reference, std430) readonly buffer TransformBuffer {
    mat4 transforms[];
};

layout(buffer_reference, std430) readonly buffer MaterialBuffer {
	Material materials[];
};

layout(buffer_reference, std430) readonly buffer Light_Buffer {
	Light lights[];
};

layout(push_constant) uniform constants {
    VertexBuffer vertexBuffer;
    TransformBuffer transformBuffer;
    MaterialBuffer materialBuffer;
    Light_Buffer lights_buffer;
	mat4 projection;
	mat4 view;
    uint max_lights;
    uint padding[3];
} PushConstants;

/*
layout (set = 0, binding = 4) buffer Mesh_Render_Infos {
	Mesh_Render_Info mesh_render_info[];
};

layout(set = 0, binding = 5) buffer Transforms {
	mat4 transforms[];
};

layout(set = 0, binding = 6) buffer Materials {
	Material materials[];
};
*/

void main() {
	Vertex v = PushConstants.vertexBuffer.vertices[gl_VertexIndex];
	mat4 model = PushConstants.transformBuffer.transforms[gl_InstanceIndex];
	Material material =  PushConstants.materialBuffer.materials[gl_InstanceIndex];

	vec4 world_pos = model * vec4(v.position, 1.0f);
    gl_Position = PushConstants.projection * (PushConstants.view * world_pos);

	mat3 normalMatrix = transpose(inverse(mat3(model)));
    vec3 T = normalize(normalMatrix * v.tangent.xyz);
    vec3 N = normalize(normalMatrix * v.normal);
    T = normalize(T - dot(T, N) * N);
    vec3 B = cross(N, T);
    
    out_TBN = mat3(T, B, N);

	out_world_pos = world_pos.xyz;
	out_color = v.color.xyz;
	out_uv.x = v.uv_x;
	out_uv.y = v.uv_y;
	out_normal = v.normal;
	
	out_albedo = material.albedo;
	out_normal_map = material.normal;
	out_alpha_cutoff = material.alpha_cutoff;
	out_blending = material.blending;
}