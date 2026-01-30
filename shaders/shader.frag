#version 450
#extension GL_EXT_nonuniform_qualifier : enable

layout(location = 0) in vec2 fragUV;
layout(location = 1) flat in uint fragBlockType;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D texSamplers[9]; // ITEM_TYPE_COUNT

const uint ITEM_TYPE_COUNT = 9u;
const uint CROSSHAIR_INDEX = ITEM_TYPE_COUNT;
const uint INVENTORY_SELECTION_INDEX = ITEM_TYPE_COUNT + 1u;
const uint INVENTORY_BG_INDEX = ITEM_TYPE_COUNT + 2u;
const uint HIGHLIGHT_INDEX = ITEM_TYPE_COUNT + 3u;

void main() {
    if (fragBlockType == CROSSHAIR_INDEX) {
        outColor = vec4(0.0, 0.0, 0.0, 1.0);
    } else if (fragBlockType == INVENTORY_SELECTION_INDEX) {
        outColor = vec4(1.0, 1.0, 0.0, 1.0);
    } else if (fragBlockType == INVENTORY_BG_INDEX) {
        outColor = vec4(0.35, 0.35, 0.35, 1.0);
    } else if (fragBlockType == HIGHLIGHT_INDEX) {
        outColor = vec4(0.0, 0.0, 0.0, 1.0);
    } else {
        outColor = texture(texSamplers[nonuniformEXT(fragBlockType)], fragUV);
    }
}