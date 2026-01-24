#version 450
#extension GL_EXT_nonuniform_qualifier : enable

layout(location = 0) in vec2 fragUV;
layout(location = 1) flat in uint fragBlockType;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D texSamplers[5]; // BLOCK_TYPE_COUNT

void main() {
    outColor = texture(texSamplers[nonuniformEXT(fragBlockType)], fragUV);
}