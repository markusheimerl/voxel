#version 450

// Vertex attributes
layout(location = 0) in vec3 inPos;
layout(location = 1) in vec2 inUV;

// Instance attributes
layout(location = 2) in vec3 inInstancePos;
layout(location = 3) in uint inBlockType;

// Outputs
layout(location = 0) out vec2 fragUV;
layout(location = 1) flat out uint fragBlockType;

// Camera matrices
layout(push_constant) uniform PushConstants {
    mat4 view;
    mat4 proj;
} pc;

void main() {
    fragUV = inUV;
    fragBlockType = inBlockType;
    gl_Position = pc.proj * pc.view * vec4(inPos + inInstancePos, 1.0);
}