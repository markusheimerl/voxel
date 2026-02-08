#version 450

// Vertex attributes
layout(location = 0) in vec3 inPos;
layout(location = 1) in vec2 inUV;
layout(location = 2) in uint inFaceId;

// Instance attributes
layout(location = 3) in vec3 inInstancePos;
layout(location = 4) in uint inBlockType;
layout(location = 5) in uint inPartId;
layout(location = 6) in vec3 inInstanceScale;
layout(location = 7) in float inInstanceRotX;
layout(location = 8) in float inInstanceRotY;

// Outputs
layout(location = 0) out vec2 fragUV;
layout(location = 1) flat out uint fragBlockType;

// Camera matrices
layout(push_constant) uniform PushConstants {
    mat4 view;
    mat4 proj;
} pc;

const uint ITEM_ZOMBIE = 9u;

const vec2 ZOMBIE_TEX_SIZE = vec2(64.0, 32.0);
const vec2 ZOMBIE_HEAD_FRONT_MIN_PX = vec2(8.0, 8.0);
const vec2 ZOMBIE_HEAD_FRONT_SIZE_PX = vec2(8.0, 8.0);
const vec2 ZOMBIE_HEAD_LEFT_MIN_PX = vec2(0.0, 8.0);
const vec2 ZOMBIE_HEAD_LEFT_SIZE_PX = vec2(8.0, 8.0);
const vec2 ZOMBIE_HEAD_RIGHT_MIN_PX = vec2(16.0, 8.0);
const vec2 ZOMBIE_HEAD_RIGHT_SIZE_PX = vec2(8.0, 8.0);
const vec2 ZOMBIE_HEAD_BACK_MIN_PX = vec2(8.0, 0.0);
const vec2 ZOMBIE_HEAD_BACK_SIZE_PX = vec2(8.0, 8.0);
const vec2 ZOMBIE_HEAD_BOTTOM_MIN_PX = vec2(16.0, 0.0);
const vec2 ZOMBIE_HEAD_BOTTOM_SIZE_PX = vec2(8.0, 8.0);
const vec2 ZOMBIE_HEAD_TOP_MIN_PX = vec2(24.0, 8.0);
const vec2 ZOMBIE_HEAD_TOP_SIZE_PX = vec2(8.0, 8.0);
const vec2 ZOMBIE_TORSO_RIGHT_MIN_PX = vec2(16.0, 20.0);
const vec2 ZOMBIE_TORSO_RIGHT_SIZE_PX = vec2(4.0, 12.0);
const vec2 ZOMBIE_TORSO_FRONT_MIN_PX = vec2(20.0, 20.0);
const vec2 ZOMBIE_TORSO_FRONT_SIZE_PX = vec2(8.0, 12.0);
const vec2 ZOMBIE_TORSO_TOP_MIN_PX = vec2(20.0, 16.0);
const vec2 ZOMBIE_TORSO_TOP_SIZE_PX = vec2(8.0, 4.0);
const vec2 ZOMBIE_TORSO_LEFT_MIN_PX = vec2(28.0, 20.0);
const vec2 ZOMBIE_TORSO_LEFT_SIZE_PX = vec2(4.0, 12.0);
const vec2 ZOMBIE_TORSO_BOTTOM_MIN_PX = vec2(28.0, 16.0);
const vec2 ZOMBIE_TORSO_BOTTOM_SIZE_PX = vec2(8.0, 4.0);
const vec2 ZOMBIE_TORSO_BACK_MIN_PX = vec2(32.0, 20.0);
const vec2 ZOMBIE_TORSO_BACK_SIZE_PX = vec2(8.0, 12.0);
const vec2 ZOMBIE_LEG_RIGHT_MIN_PX = vec2(0.0, 20.0);
const vec2 ZOMBIE_LEG_RIGHT_SIZE_PX = vec2(4.0, 12.0);
const vec2 ZOMBIE_LEG_FRONT_MIN_PX = vec2(4.0, 21.0);
const vec2 ZOMBIE_LEG_FRONT_SIZE_PX = vec2(4.0, 12.0);
const vec2 ZOMBIE_LEG_TOP_MIN_PX = vec2(4.0, 16.0);
const vec2 ZOMBIE_LEG_TOP_SIZE_PX = vec2(4.0, 4.0);
const vec2 ZOMBIE_LEG_BOTTOM_MIN_PX = vec2(8.0, 16.0);
const vec2 ZOMBIE_LEG_BOTTOM_SIZE_PX = vec2(4.0, 4.0);
const vec2 ZOMBIE_LEG_LEFT_MIN_PX = vec2(8.0, 20.0);
const vec2 ZOMBIE_LEG_LEFT_SIZE_PX = vec2(4.0, 12.0);
const vec2 ZOMBIE_LEG_BACK_MIN_PX = vec2(12.0, 20.0);
const vec2 ZOMBIE_LEG_BACK_SIZE_PX = vec2(4.0, 12.0);

vec2 rect_uv(vec2 minPx, vec2 sizePx, vec2 baseUV, bool flipV) {
    vec2 minUV = minPx / ZOMBIE_TEX_SIZE;
    vec2 sizeUV = sizePx / ZOMBIE_TEX_SIZE;
    vec2 uv = flipV ? vec2(baseUV.x, 1.0 - baseUV.y) : baseUV;
    return minUV + uv * sizeUV;
}

vec2 rect_uv_rot90(vec2 minPx, vec2 sizePx, vec2 baseUV, bool flipV) {
    vec2 uv = flipV ? vec2(baseUV.x, 1.0 - baseUV.y) : baseUV;
    vec2 rot = vec2(uv.y, 1.0 - uv.x);
    rot.y = 1.0 - rot.y;
    vec2 minUV = minPx / ZOMBIE_TEX_SIZE;
    vec2 sizeUV = sizePx / ZOMBIE_TEX_SIZE;
    return minUV + rot * sizeUV;
}

vec2 rect_uv_rot90_ccw(vec2 minPx, vec2 sizePx, vec2 baseUV, bool flipV) {
    vec2 uv = flipV ? vec2(baseUV.x, 1.0 - baseUV.y) : baseUV;
    vec2 rot = vec2(1.0 - uv.y, uv.x);
    rot.y = 1.0 - rot.y;
    vec2 minUV = minPx / ZOMBIE_TEX_SIZE;
    vec2 sizeUV = sizePx / ZOMBIE_TEX_SIZE;
    return minUV + rot * sizeUV;
}


const uint ZOMBIE_PART_LEGS = 1u;
const uint ZOMBIE_PART_TORSO = 2u;
const uint ZOMBIE_PART_ARMS = 3u;
const uint ZOMBIE_PART_HEAD = 4u;

vec2 zombie_part_origin(uint partId) {
    vec2 partSize = vec2(1.0 / 3.0, 0.5);
    if (partId == ZOMBIE_PART_HEAD) return vec2(0.0, 0.0);
    if (partId == ZOMBIE_PART_LEGS) return vec2(0.0, partSize.y);
    if (partId == ZOMBIE_PART_TORSO) return vec2(partSize.x, partSize.y);
    if (partId == ZOMBIE_PART_ARMS) return vec2(partSize.x * 2.0, partSize.y);
    return vec2(0.0, 0.0);
}

vec2 zombie_face_uv(uint faceId, vec2 baseUV) {
    if (inPartId == ZOMBIE_PART_HEAD) {
        if (faceId == 0u) return rect_uv(ZOMBIE_HEAD_FRONT_MIN_PX, ZOMBIE_HEAD_FRONT_SIZE_PX, baseUV, true);
        if (faceId == 1u) return rect_uv(ZOMBIE_HEAD_BACK_MIN_PX, ZOMBIE_HEAD_BACK_SIZE_PX, baseUV, true);
        if (faceId == 2u) return rect_uv(ZOMBIE_HEAD_TOP_MIN_PX, ZOMBIE_HEAD_TOP_SIZE_PX, baseUV, true);
        if (faceId == 3u) return rect_uv(ZOMBIE_HEAD_BOTTOM_MIN_PX, ZOMBIE_HEAD_BOTTOM_SIZE_PX, baseUV, true);
        if (faceId == 4u) return rect_uv(ZOMBIE_HEAD_RIGHT_MIN_PX, ZOMBIE_HEAD_RIGHT_SIZE_PX, baseUV, true);
        if (faceId == 5u) return rect_uv(ZOMBIE_HEAD_LEFT_MIN_PX, ZOMBIE_HEAD_LEFT_SIZE_PX, baseUV, true);
    }
    if (inPartId == ZOMBIE_PART_TORSO) {
        if (faceId == 0u) return rect_uv(ZOMBIE_TORSO_FRONT_MIN_PX, ZOMBIE_TORSO_FRONT_SIZE_PX, baseUV, true);
        if (faceId == 1u) return rect_uv(ZOMBIE_TORSO_BACK_MIN_PX, ZOMBIE_TORSO_BACK_SIZE_PX, baseUV, true);
        if (faceId == 2u) return rect_uv(ZOMBIE_TORSO_TOP_MIN_PX, ZOMBIE_TORSO_TOP_SIZE_PX, baseUV, true);
        if (faceId == 3u) return rect_uv(ZOMBIE_TORSO_BOTTOM_MIN_PX, ZOMBIE_TORSO_BOTTOM_SIZE_PX, baseUV, true);
        if (faceId == 4u) return rect_uv_rot90_ccw(ZOMBIE_TORSO_RIGHT_MIN_PX, ZOMBIE_TORSO_RIGHT_SIZE_PX, baseUV, false);
        if (faceId == 5u) return rect_uv_rot90(ZOMBIE_TORSO_LEFT_MIN_PX, ZOMBIE_TORSO_LEFT_SIZE_PX, baseUV, false);
    }
    if (inPartId == ZOMBIE_PART_LEGS) {
        if (faceId == 0u) return rect_uv(ZOMBIE_LEG_FRONT_MIN_PX, ZOMBIE_LEG_FRONT_SIZE_PX, baseUV, true);
        if (faceId == 1u) return rect_uv(ZOMBIE_LEG_BACK_MIN_PX, ZOMBIE_LEG_BACK_SIZE_PX, baseUV, true);
        if (faceId == 2u) return rect_uv(ZOMBIE_LEG_TOP_MIN_PX, ZOMBIE_LEG_TOP_SIZE_PX, baseUV, true);
        if (faceId == 3u) return rect_uv(ZOMBIE_LEG_BOTTOM_MIN_PX, ZOMBIE_LEG_BOTTOM_SIZE_PX, baseUV, true);
        if (faceId == 4u) return rect_uv_rot90_ccw(ZOMBIE_LEG_RIGHT_MIN_PX, ZOMBIE_LEG_RIGHT_SIZE_PX, baseUV, false);
        if (faceId == 5u) return rect_uv_rot90(ZOMBIE_LEG_LEFT_MIN_PX, ZOMBIE_LEG_LEFT_SIZE_PX, baseUV, false);
    }
    // Layout inside each part region: 4x3 grid
    // Cols: left, front, right, back on middle row
    // Rows: top, middle, bottom (with top/bottom above/below front)
    uint col = 1u;
    uint row = 1u;
    if (faceId == 0u) { col = 1u; row = 1u; }        // front
    else if (faceId == 1u) { col = 3u; row = 1u; }   // back
    else if (faceId == 2u) { col = 1u; row = 0u; }   // top
    else if (faceId == 3u) { col = 1u; row = 2u; }   // bottom
    else if (faceId == 4u) { col = 2u; row = 1u; }   // right
    else if (faceId == 5u) { col = 0u; row = 1u; }   // left

    vec2 partSize = vec2(1.0 / 3.0, 0.5);
    vec2 cellSize = vec2(partSize.x / 4.0, partSize.y / 3.0);
    vec2 origin = zombie_part_origin(inPartId) + vec2(float(col), float(row)) * cellSize;
    return origin + baseUV * cellSize;
}

void main() {
    if (inBlockType == ITEM_ZOMBIE && inPartId != 0u) {
        fragUV = zombie_face_uv(inFaceId, inUV);
    } else {
        fragUV = inUV;
    }
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