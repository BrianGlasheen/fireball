#version 450
#extension GL_EXT_nonuniform_qualifier : require

layout (location = 0) in vec3 inColor;
layout (location = 1) in vec2 inUV;
layout (location = 2) flat in uint inTextureID;
layout (location = 3) flat in float inAlphaCutoff;
layout (location = 4) flat in uint inBlendMode;

layout (location = 0) out vec4 outFragColor;

layout(set = 0, binding = 0) uniform sampler2D textures[];

void main() {
    vec3 color;
    float alpha = 1.0;
    
    if (inTextureID < 65535) {
        vec4 texColor = texture(textures[nonuniformEXT(inTextureID)], inUV);
        
        if (inBlendMode == 0 && texColor.a < inAlphaCutoff)
            discard;
        
        color = texColor.rgb * inColor;
        alpha = texColor.a;
    } else {
        color = inColor;
        alpha = 1.0;
    }
    
    outFragColor = vec4(color, alpha);
}