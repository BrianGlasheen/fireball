#version 450
#extension GL_EXT_buffer_reference : require

layout (location = 0) out vec3 outColor;
layout (location = 1) out vec2 outUV;
layout (location = 2) flat out uint outTextureID;
layout (location = 3) flat out float outAlphaCutoff;
layout (location = 4) flat out uint outBlendMode;

struct Vertex {
	vec3 position;
	float uv_x;
	vec3 normal;
	float uv_y;
	vec4 color;
};

struct Material {
    uint albedo;
    float alpha_cutoff;
    uint blending;
    uint padding;
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

layout(push_constant) uniform constants {
    VertexBuffer vertexBuffer;
    TransformBuffer transformBuffer;
    MaterialBuffer materialBuffer;
	int padding;
	int padding2;
	mat4 projection;
	mat4 view;
} PushConstants;

void main() {
	Vertex v = PushConstants.vertexBuffer.vertices[gl_VertexIndex];
	mat4 model = PushConstants.transformBuffer.transforms[gl_InstanceIndex];
	Material material =  PushConstants.materialBuffer.materials[gl_InstanceIndex];

	gl_Position = PushConstants.projection * (PushConstants.view * (model * vec4(v.position, 1.0f)));
	outColor = v.color.xyz;
	outUV.x = v.uv_x;
	outUV.y = v.uv_y;

	outTextureID = material.albedo;
	outAlphaCutoff = material.alpha_cutoff;
	outBlendMode = material.blending;
}