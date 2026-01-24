#version 450

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec2 inUV;
layout(location = 2) in vec3 inInstancePos;   // per-instance translation
layout(location = 3) in uint inBlockType;     // per-instance block type

layout(location = 0) out vec2 fragUV;
layout(location = 1) flat out uint fragBlockType;

layout(push_constant) uniform PushConstants {
    mat4 view;
    mat4 proj;
} pc;

void main() {
    fragUV         = inUV;
    fragBlockType  = inBlockType;

    vec4 worldPos  = vec4(inPos + inInstancePos, 1.0);
    gl_Position    = pc.proj * pc.view * worldPos;
}