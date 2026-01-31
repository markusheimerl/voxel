#version 450

// Vertex attributes
layout(location = 0) in vec3 inPos;
layout(location = 1) in vec2 inUV;

// Instance attributes
layout(location = 2) in vec3 inInstancePos;
layout(location = 3) in uint inBlockType;
layout(location = 4) in vec3 inInstanceScale;
layout(location = 5) in float inInstanceRotX;
layout(location = 6) in float inInstanceRotY;

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

    vec3 localPos = inPos * inInstanceScale;

    if (inInstanceRotX != 0.0) {
        localPos.y -= 0.5 * inInstanceScale.y;

        float c = cos(inInstanceRotX);
        float s = sin(inInstanceRotX);
        localPos = vec3(localPos.x, localPos.y * c - localPos.z * s, localPos.y * s + localPos.z * c);

        localPos.y += 0.5 * inInstanceScale.y;
    }

    if (inInstanceRotY != 0.0) {
        float c = cos(inInstanceRotY);
        float s = sin(inInstanceRotY);
        localPos = vec3(localPos.x * c - localPos.z * s, localPos.y, localPos.x * s + localPos.z * c);
    }

    gl_Position = pc.proj * pc.view * vec4(localPos + inInstancePos, 1.0);
}