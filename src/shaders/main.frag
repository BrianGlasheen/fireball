#version 450

#extension GL_EXT_buffer_reference : require
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_GOOGLE_include_directive: require

#include "light.h"

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

layout(buffer_reference, std430) readonly buffer Light_Buffer {
	Light lights[];
};

layout(push_constant) uniform constants {
    vec2 buffers[3];
    Light_Buffer lights_buffer;
	mat4 projection;
	mat4 view;
    uint max_lights;
    uint padding[3];
} PushConstants;

const float PI = 3.14159265359;

float saturate(float x) {
    return clamp(x, 0.0, 1.0);
}

vec3 fresnel_schlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}

float distribution_ggx(vec3 N, vec3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;

    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    return a2 / (PI * denom * denom);
}

float geometry_schlick_ggx(float NdotV, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

float geometry_smith(vec3 N, vec3 V, vec3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx1 = geometry_schlick_ggx(NdotV, roughness);
    float ggx2 = geometry_schlick_ggx(NdotL, roughness);
    return ggx1 * ggx2;
}

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
    // float diff = max(dot(normal, light_dir), 0.0);
    // vec3 lighting = vec3(0.5) + vec3(0.7) * diff;
    // outFragColor = vec4(color * lighting, alpha);

    float metallic = 0.0;
    float roughness = 0.5;
    vec3 albedo = color;

    vec3 cam_pos = inverse(PushConstants.view)[3].xyz;
    vec3 V = normalize(cam_pos - in_world_pos);
    vec3 N = normalize(normal);

    vec3 F0 = mix(vec3(0.04), albedo, metallic);

    vec3 Lo = vec3(0.0);

    for (uint i = 0; i < PushConstants.max_lights; i++) {
        Light light = PushConstants.lights_buffer.lights[i];

        if (light.params.w < 0.5)
            continue;

        vec3 L;
        float attenuation = 1.0;

        vec3 light_pos = light.position_radius.xyz;
        float radius = light.position_radius.w;

        vec3 to_light = light_pos - in_world_pos;
        float distance = length(to_light);
        if (distance > radius)
            continue;

        L = normalize(to_light);

        float falloff = saturate(1.0 - distance / radius);
        attenuation = falloff * falloff;

        if (uint(light.direction_type.w) == 1u) {
            vec3 spot_dir = normalize(light.direction_type.xyz);
            float cos_theta = dot(L, -spot_dir);

            float inner = light.params.x;
            float outer = light.params.y;
            float spot = saturate((cos_theta - outer) / (inner - outer));

            attenuation *= spot;
        }

        vec3 radiance = light.color_strength.rgb * light.color_strength.a * attenuation;

        vec3 H = normalize(V + L);
        float NDF = distribution_ggx(N, H, roughness);
        float G = geometry_smith(N, V, L, roughness);
        vec3 F = fresnel_schlick(max(dot(H, V), 0.0), F0);

        vec3 numerator = NDF * G * F;
        float denom = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.001;
        vec3 specular = numerator / denom;

        vec3 kS = F;
        vec3 kD = vec3(1.0) - kS;
        kD *= (1.0 - metallic);

        float NdotL = max(dot(N, L), 0.0);
        Lo += (kD * albedo / PI + specular) * radiance * NdotL;
    }

    vec3 ambient = vec3(0.03) * albedo;

    vec3 final_color = ambient + Lo;

    outFragColor = vec4(final_color, alpha);
}