#version 450
#extension GL_EXT_nonuniform_qualifier : require

layout (location = 0) in vec3 inColor;
layout (location = 1) in vec2 inUV;
layout (location = 2) flat in uint inTextureID;

layout (location = 0) out vec4 outFragColor;

layout(set = 0, binding = 0) uniform sampler2D textures[];

void main() {
    vec3 color;
    
    if (inTextureID < 65535) {
        vec4 texColor = texture(textures[nonuniformEXT(inTextureID)], inUV);
        color = texColor.rgb * inColor;
    } else {
        color = inColor;
    }
    
    outFragColor = vec4(color, 1.0f);
}
