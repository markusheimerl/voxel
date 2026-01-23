#version 450
#extension GL_EXT_nonuniform_qualifier : enable

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform UBO {
    mat4 model;
    mat4 view;
    mat4 proj;
    uint block_type;
    uint _pad0;
    uint _pad1;
    uint _pad2;
} ubo;

layout(set = 0, binding = 1) uniform sampler2D texSamplers[2];

void main() {
    uint idx = ubo.block_type;
    outColor = texture(texSamplers[nonuniformEXT(idx)], fragUV);
}