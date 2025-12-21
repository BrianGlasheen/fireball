#version 450
#extension GL_EXT_nonuniform_qualifier : require

layout (location = 0) in vec3 in_color;
layout (location = 1) in vec2 in_uv;
layout (location = 2) in vec3 in_normal;

layout (location = 3) flat in uint in_albedo;
layout (location = 4) flat in uint in_normal_map;
layout (location = 5) flat in float in_alpha_cutoff;
layout (location = 6) flat in uint in_blending;

layout (location = 7) in vec3 in_world_pos;
layout (location = 8) in mat3 in_TBN;

layout (location = 0) out vec4 outFragColor;

layout(set = 0, binding = 0) uniform sampler2D textures[];

void main() {
    vec3 color;
    float alpha = 1.0;
    
    if (in_albedo < 65535) {
        vec4 texColor = texture(textures[nonuniformEXT(in_albedo)], in_uv);
        
        if (in_blending == 0 && texColor.a < in_alpha_cutoff)
            discard;
        
        color = texColor.rgb * in_color;
        alpha = texColor.a;
    } else {
        color = in_color;
        alpha = 1.0;
    }

    vec3 normal = texture(textures[nonuniformEXT(in_normal_map)], in_uv).rgb;
    normal = normal * 2.0 - 1.0;
    normal = normalize(in_TBN * normal);

    vec3 light_dir = normalize(vec3(0.2, 1.0, 0.0));
    float diff = max(dot(normal, light_dir), 0.0);
    vec3 lighting = vec3(0.3) + vec3(0.7) * diff;
    
    outFragColor = vec4(color * lighting, alpha);
}