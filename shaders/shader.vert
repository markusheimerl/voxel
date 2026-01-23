#version 450

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec2 inUV;

layout(location = 0) out vec2 fragUV;

layout(set = 0, binding = 0) uniform UBO {
    mat4 model;
    mat4 view;
    mat4 proj;
    uint block_type;
    uint _pad0;
    uint _pad1;
    uint _pad2;
} ubo;

void main() {
    fragUV = inUV;
    gl_Position = ubo.proj * ubo.view * ubo.model * vec4(inPos, 1.0);
}