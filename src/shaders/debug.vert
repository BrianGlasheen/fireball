#version 450

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec4 in_color;
        
layout(location = 0) out vec4 frag_color;

layout(push_constant) uniform PushConstants {
    mat4 view_proj;
} pc;
        
void main() {
    gl_Position = pc.view_proj * vec4(in_position, 1.0);
    gl_PointSize = 5.0;
    frag_color = in_color;
}
