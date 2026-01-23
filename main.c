#include <math.h>
#include <png.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

#define VK_USE_PLATFORM_XLIB_KHR
#include <vulkan/vulkan.h>

/* -------------------------------------------------------------------------- */
/* Macros                                                                     */
/* -------------------------------------------------------------------------- */

#define ARRAY_LENGTH(a) (sizeof(a) / sizeof((a)[0]))

#define VK_CHECK(call)                                                            \
    do {                                                                          \
        VkResult vk_check_result__ = (call);                                      \
        if (vk_check_result__ != VK_SUCCESS) {                                    \
            fprintf(stderr, "%s failed: %s\n", #call, vk_result_to_string(vk_check_result__)); \
            exit(EXIT_FAILURE);                                                   \
        }                                                                         \
    } while (0)

/* -------------------------------------------------------------------------- */
/* Math Types                                                                 */
/* -------------------------------------------------------------------------- */

typedef struct { float x, y, z; } Vec3;
typedef struct { float x, y; } Vec2;
typedef struct { float m[16]; } Mat4;
typedef struct { int x, y, z; } IVec3;

/* -------------------------------------------------------------------------- */
/* Math Helpers                                                               */
/* -------------------------------------------------------------------------- */

static Vec3 vec3(float x, float y, float z) {
    Vec3 v = {x, y, z};
    return v;
}

static Mat4 mat4_identity(void) {
    Mat4 m = {0};
    m.m[0] = 1.0f;  m.m[5] = 1.0f;  m.m[10] = 1.0f; m.m[15] = 1.0f;
    return m;
}

static Mat4 mat4_translate(Vec3 v) {
    Mat4 m = mat4_identity();
    m.m[12] = v.x;
    m.m[13] = v.y;
    m.m[14] = v.z;
    return m;
}

static Mat4 mat4_perspective(float fov_radians, float aspect, float z_near, float z_far) {
    Mat4 m = {0};
    float tan_half_fov = tanf(fov_radians * 0.5f);

    m.m[0] = 1.0f / (aspect * tan_half_fov);
    m.m[5] = -1.0f / tan_half_fov;
    m.m[10] = z_far / (z_near - z_far);
    m.m[11] = -1.0f;
    m.m[14] = -(z_far * z_near) / (z_far - z_near);

    return m;
}

static Vec3 vec3_add(Vec3 a, Vec3 b) {
    return vec3(a.x + b.x, a.y + b.y, a.z + b.z);
}

static Vec3 vec3_sub(Vec3 a, Vec3 b) {
    return vec3(a.x - b.x, a.y - b.y, a.z - b.z);
}

static Vec3 vec3_scale(Vec3 v, float s) {
    return vec3(v.x * s, v.y * s, v.z * s);
}

static float vec3_dot(Vec3 a, Vec3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

static float vec3_length(Vec3 v) {
    return sqrtf(vec3_dot(v, v));
}

static Vec3 vec3_normalize(Vec3 v) {
    float len = vec3_length(v);
    if (len > 0.000001f) {
        return vec3_scale(v, 1.0f / len);
    }
    return v;
}

static Vec3 vec3_cross(Vec3 a, Vec3 b) {
    return vec3(a.y * b.z - a.z * b.y,
                a.z * b.x - a.x * b.z,
                a.x * b.y - a.y * b.x);
}

static Mat4 mat4_look_at(Vec3 eye, Vec3 center, Vec3 up) {
    Vec3 forward = vec3_normalize(vec3_sub(center, eye));
    Vec3 side = vec3_normalize(vec3_cross(forward, up));
    Vec3 up_actual = vec3_cross(side, forward);

    Mat4 m = mat4_identity();
    m.m[0]  = side.x;      m.m[4]  = side.y;      m.m[8]  = side.z;
    m.m[1]  = up_actual.x; m.m[5]  = up_actual.y; m.m[9]  = up_actual.z;
    m.m[2]  = -forward.x;  m.m[6]  = -forward.y;  m.m[10] = -forward.z;

    m.m[12] = -vec3_dot(side, eye);
    m.m[13] = -vec3_dot(up_actual, eye);
    m.m[14] =  vec3_dot(forward, eye);

    return m;
}

static bool ivec3_equal(IVec3 a, IVec3 b) {
    return a.x == b.x && a.y == b.y && a.z == b.z;
}

static IVec3 ivec3_add(IVec3 a, IVec3 b) {
    IVec3 result = {a.x + b.x, a.y + b.y, a.z + b.z};
    return result;
}

static int sign_int(int value) {
    return (value > 0) - (value < 0);
}

static IVec3 world_to_cell(Vec3 p) {
    IVec3 cell = {
        (int)floorf(p.x + 0.5f),
        (int)floorf(p.y + 0.5f),
        (int)floorf(p.z + 0.5f)
    };
    return cell;
}

/* -------------------------------------------------------------------------- */
/* Camera                                                                     */
/* -------------------------------------------------------------------------- */

typedef struct {
    Vec3 position;
    Vec3 front;
    Vec3 up;
    Vec3 right;
    Vec3 world_up;
    float yaw;
    float pitch;
    float movement_speed;
    float mouse_sensitivity;
} Camera;

static void camera_update_axes(Camera *cam) {
    float yaw_rad = cam->yaw   * (float)M_PI / 180.0f;
    float pitch_rad = cam->pitch * (float)M_PI / 180.0f;

    Vec3 front = vec3(cosf(yaw_rad) * cosf(pitch_rad),
                      sinf(pitch_rad),
                      sinf(yaw_rad) * cosf(pitch_rad));
    cam->front = vec3_normalize(front);
    cam->right = vec3_normalize(vec3_cross(cam->front, cam->world_up));
    cam->up    = vec3_normalize(vec3_cross(cam->right, cam->front));
}

static void camera_init(Camera *cam) {
    cam->position         = vec3(0.0f, 0.0f, 3.0f);
    cam->world_up         = vec3(0.0f, 1.0f, 0.0f);
    cam->yaw              = -90.0f;
    cam->pitch            = 0.0f;
    cam->movement_speed   = 5.0f;
    cam->mouse_sensitivity = 0.1f;
    camera_update_axes(cam);
}

static void camera_process_mouse(Camera *cam, float x_offset, float y_offset) {
    x_offset *= cam->mouse_sensitivity;
    y_offset *= cam->mouse_sensitivity;

    cam->yaw   += x_offset;
    cam->pitch += y_offset;

    if (cam->pitch > 89.0f)  cam->pitch = 89.0f;
    if (cam->pitch < -89.0f) cam->pitch = -89.0f;

    camera_update_axes(cam);
}

static Mat4 camera_view_matrix(Camera *cam) {
    Vec3 target = vec3_add(cam->position, cam->front);
    return mat4_look_at(cam->position, target, cam->up);
}

/* -------------------------------------------------------------------------- */
/* Player                                                                     */
/* -------------------------------------------------------------------------- */

typedef struct {
    Vec3 position;    /* feet center */
    float velocity_y;
    bool on_ground;
} Player;

typedef struct {
    Vec3 min;
    Vec3 max;
} AABB;

static void player_compute_aabb(Vec3 pos, AABB *aabb) {
    const float half_width = 0.4f;
    const float height = 2.0f;

    aabb->min = vec3(pos.x - half_width, pos.y, pos.z - half_width);
    aabb->max = vec3(pos.x + half_width, pos.y + height, pos.z + half_width);
}

static bool aabb_intersect(const AABB *a, const AABB *b) {
    return (a->min.x < b->max.x && a->max.x > b->min.x) &&
           (a->min.y < b->max.y && a->max.y > b->min.y) &&
           (a->min.z < b->max.z && a->max.z > b->min.z);
}

/* -------------------------------------------------------------------------- */
/* Blocks                                                                     */
/* -------------------------------------------------------------------------- */

typedef enum
{
    BLOCK_DIRT  = 0,
    BLOCK_STONE = 1
} BlockType;

typedef struct {
    IVec3 pos;
    uint8_t type;
} Block;

static int block_index_of(Block *blocks, int count, IVec3 pos) {
    for (int i = 0; i < count; ++i) {
        if (ivec3_equal(blocks[i].pos, pos)) {
            return i;
        }
    }
    return -1;
}

static bool block_add(Block *blocks, int *count, int max_count, IVec3 pos, uint8_t type) {
    if (*count >= max_count) {
        return false;
    }
    if (block_index_of(blocks, *count, pos) >= 0) {
        return false;
    }

    blocks[*count].pos  = pos;
    blocks[*count].type = type;
    (*count)++;
    return true;
}

static bool block_remove(Block *blocks, int *count, IVec3 pos) {
    int idx = block_index_of(blocks, *count, pos);
    if (idx < 0) {
        return false;
    }

    blocks[idx] = blocks[*count - 1];
    (*count)--;
    return true;
}

static bool block_overlaps_player(const Player *player, IVec3 cell) {
    AABB player_box;
    player_compute_aabb(player->position, &player_box);

    AABB block_box;
    block_box.min = vec3(cell.x - 0.5f, cell.y - 0.5f, cell.z - 0.5f);
    block_box.max = vec3(cell.x + 0.5f, cell.y + 0.5f, cell.z + 0.5f);

    return aabb_intersect(&player_box, &block_box);
}

/* -------------------------------------------------------------------------- */
/* Raycast                                                                    */
/* -------------------------------------------------------------------------- */

typedef struct {
    bool hit;
    IVec3 cell;
    IVec3 normal;
} RayHit;

static RayHit raycast_blocks(Vec3 origin, Vec3 direction, Block *blocks, int block_count, float max_distance) {
    const float step = 0.05f;
    RayHit result = {0};
    Vec3 dir = vec3_normalize(direction);

    IVec3 previous_cell = world_to_cell(origin);

    for (float t = 0.0f; t <= max_distance; t += step) {
        Vec3 point = vec3_add(origin, vec3_scale(dir, t));
        IVec3 cell = world_to_cell(point);

        if (!ivec3_equal(cell, previous_cell)) {
            if (block_index_of(blocks, block_count, cell) >= 0) {
                result.hit = true;
                result.cell = cell;
                result.normal = (IVec3){
                    sign_int(previous_cell.x - cell.x),
                    sign_int(previous_cell.y - cell.y),
                    sign_int(previous_cell.z - cell.z)
                };
                break;
            }
            previous_cell = cell;
        }
    }

    return result;
}

/* -------------------------------------------------------------------------- */
/* Player Collision Helpers                                                   */
/* -------------------------------------------------------------------------- */

static AABB block_aabb(Block block) {
    AABB box;
    box.min = vec3(block.pos.x - 0.5f, block.pos.y - 0.5f, block.pos.z - 0.5f);
    box.max = vec3(block.pos.x + 0.5f, block.pos.y + 0.5f, block.pos.z + 0.5f);
    return box;
}

static void resolve_collision_axis(Vec3 *position, float delta, int axis,
                                   Block *blocks, int block_count) {
    if (delta == 0.0f) {
        return;
    }

    AABB player_box;
    player_compute_aabb(*position, &player_box);

    for (int i = 0; i < block_count; ++i) {
        AABB block_box = block_aabb(blocks[i]);

        if (!aabb_intersect(&player_box, &block_box)) {
            continue;
        }

        switch (axis) {
        case 0: /* X axis */
            if (delta > 0.0f) {
                position->x = block_box.min.x - 0.4f - 0.001f;
            } else {
                position->x = block_box.max.x + 0.4f + 0.001f;
            }
            break;

        case 2: /* Z axis */
            if (delta > 0.0f) {
                position->z = block_box.min.z - 0.4f - 0.001f;
            } else {
                position->z = block_box.max.z + 0.4f + 0.001f;
            }
            break;

        default:
            break;
        }

        player_compute_aabb(*position, &player_box);
    }
}

static void resolve_collision_y(Vec3 *position, float *velocity_y, bool *on_ground,
                                Block *blocks, int block_count) {
    AABB player_box;
    player_compute_aabb(*position, &player_box);
    *on_ground = false;

    for (int i = 0; i < block_count; ++i) {
        AABB block_box = block_aabb(blocks[i]);

        if (!aabb_intersect(&player_box, &block_box)) {
            continue;
        }

        if (*velocity_y < 0.0f) {
            position->y = block_box.max.y;
            *velocity_y = 0.0f;
            *on_ground = true;
        } else if (*velocity_y > 0.0f) {
            position->y = block_box.min.y - 2.0f - 0.001f;
            *velocity_y = 0.0f;
        }

        player_compute_aabb(*position, &player_box);
    }
}

/* -------------------------------------------------------------------------- */
/* Vulkan Error Helpers                                                       */
/* -------------------------------------------------------------------------- */

static const char *vk_result_to_string(VkResult result) {
    switch (result) {
    case VK_SUCCESS:                                 return "VK_SUCCESS";
    case VK_NOT_READY:                               return "VK_NOT_READY";
    case VK_TIMEOUT:                                 return "VK_TIMEOUT";
    case VK_EVENT_SET:                               return "VK_EVENT_SET";
    case VK_EVENT_RESET:                             return "VK_EVENT_RESET";
    case VK_INCOMPLETE:                              return "VK_INCOMPLETE";
    case VK_ERROR_OUT_OF_HOST_MEMORY:                return "VK_ERROR_OUT_OF_HOST_MEMORY";
    case VK_ERROR_OUT_OF_DEVICE_MEMORY:              return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
    case VK_ERROR_INITIALIZATION_FAILED:             return "VK_ERROR_INITIALIZATION_FAILED";
    case VK_ERROR_DEVICE_LOST:                       return "VK_ERROR_DEVICE_LOST";
    case VK_ERROR_MEMORY_MAP_FAILED:                 return "VK_ERROR_MEMORY_MAP_FAILED";
    case VK_ERROR_LAYER_NOT_PRESENT:                 return "VK_ERROR_LAYER_NOT_PRESENT";
    case VK_ERROR_EXTENSION_NOT_PRESENT:             return "VK_ERROR_EXTENSION_NOT_PRESENT";
    case VK_ERROR_FEATURE_NOT_PRESENT:               return "VK_ERROR_FEATURE_NOT_PRESENT";
    case VK_ERROR_INCOMPATIBLE_DRIVER:               return "VK_ERROR_INCOMPATIBLE_DRIVER";
    case VK_ERROR_TOO_MANY_OBJECTS:                  return "VK_ERROR_TOO_MANY_OBJECTS";
    case VK_ERROR_FORMAT_NOT_SUPPORTED:              return "VK_ERROR_FORMAT_NOT_SUPPORTED";
    case VK_ERROR_SURFACE_LOST_KHR:                  return "VK_ERROR_SURFACE_LOST_KHR";
    case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR:          return "VK_ERROR_NATIVE_WINDOW_IN_USE_KHR";
    case VK_SUBOPTIMAL_KHR:                          return "VK_SUBOPTIMAL_KHR";
    case VK_ERROR_OUT_OF_DATE_KHR:                   return "VK_ERROR_OUT_OF_DATE_KHR";
    case VK_ERROR_INCOMPATIBLE_DISPLAY_KHR:          return "VK_ERROR_INCOMPATIBLE_DISPLAY_KHR";
    case VK_ERROR_VALIDATION_FAILED_EXT:             return "VK_ERROR_VALIDATION_FAILED_EXT";
    default:                                         return "UNKNOWN_VULKAN_ERROR";
    }
}

static void die(const char *message) {
    fprintf(stderr, "Error: %s\n", message);
    exit(EXIT_FAILURE);
}

/* -------------------------------------------------------------------------- */
/* Vulkan Buffer / Image Helpers                                              */
/* -------------------------------------------------------------------------- */

static uint32_t find_memory_type(VkPhysicalDevice physical_device,
                                 uint32_t type_filter,
                                 VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memory_properties;
    vkGetPhysicalDeviceMemoryProperties(physical_device, &memory_properties);

    for (uint32_t i = 0; i < memory_properties.memoryTypeCount; ++i) {
        bool supported = (type_filter & (1 << i)) != 0;
        bool matches = (memory_properties.memoryTypes[i].propertyFlags & properties) == properties;

        if (supported && matches) {
            return i;
        }
    }

    die("Failed to find suitable memory type");
    return 0;
}

static void create_buffer(VkDevice device,
                          VkPhysicalDevice physical_device,
                          VkDeviceSize size,
                          VkBufferUsageFlags usage,
                          VkMemoryPropertyFlags properties,
                          VkBuffer *buffer,
                          VkDeviceMemory *memory) {
    VkBufferCreateInfo buffer_info = {0};
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = size;
    buffer_info.usage = usage;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VK_CHECK(vkCreateBuffer(device, &buffer_info, NULL, buffer));

    VkMemoryRequirements requirements;
    vkGetBufferMemoryRequirements(device, *buffer, &requirements);

    VkMemoryAllocateInfo alloc_info = {0};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize = requirements.size;
    alloc_info.memoryTypeIndex = find_memory_type(physical_device,
                                                  requirements.memoryTypeBits,
                                                  properties);

    VK_CHECK(vkAllocateMemory(device, &alloc_info, NULL, memory));
    VK_CHECK(vkBindBufferMemory(device, *buffer, *memory, 0));
}

static void create_image(VkDevice device,
                         VkPhysicalDevice physical_device,
                         uint32_t width,
                         uint32_t height,
                         VkFormat format,
                         VkImageTiling tiling,
                         VkImageUsageFlags usage,
                         VkMemoryPropertyFlags properties,
                         VkImage *image,
                         VkDeviceMemory *memory) {
    VkImageCreateInfo image_info = {0};
    image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.extent.width = width;
    image_info.extent.height = height;
    image_info.extent.depth = 1;
    image_info.mipLevels = 1;
    image_info.arrayLayers = 1;
    image_info.format = format;
    image_info.tiling = tiling;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    image_info.usage = usage;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VK_CHECK(vkCreateImage(device, &image_info, NULL, image));

    VkMemoryRequirements requirements;
    vkGetImageMemoryRequirements(device, *image, &requirements);

    VkMemoryAllocateInfo alloc_info = {0};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize = requirements.size;
    alloc_info.memoryTypeIndex = find_memory_type(physical_device,
                                                  requirements.memoryTypeBits,
                                                  properties);

    VK_CHECK(vkAllocateMemory(device, &alloc_info, NULL, memory));
    VK_CHECK(vkBindImageMemory(device, *image, *memory, 0));
}

static VkCommandBuffer begin_single_use_commands(VkDevice device, VkCommandPool command_pool) {
    VkCommandBufferAllocateInfo alloc_info = {0};
    alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc_info.commandPool = command_pool;
    alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info.commandBufferCount = 1;

    VkCommandBuffer command_buffer;
    VK_CHECK(vkAllocateCommandBuffers(device, &alloc_info, &command_buffer));

    VkCommandBufferBeginInfo begin_info = {0};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    VK_CHECK(vkBeginCommandBuffer(command_buffer, &begin_info));
    return command_buffer;
}

static void end_single_use_commands(VkDevice device, VkCommandPool command_pool,
                                    VkQueue graphics_queue, VkCommandBuffer command_buffer) {
    VK_CHECK(vkEndCommandBuffer(command_buffer));

    VkSubmitInfo submit_info = {0};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &command_buffer;

    VK_CHECK(vkQueueSubmit(graphics_queue, 1, &submit_info, VK_NULL_HANDLE));
    VK_CHECK(vkQueueWaitIdle(graphics_queue));

    vkFreeCommandBuffers(device, command_pool, 1, &command_buffer);
}

static void transition_image_layout(VkDevice device,
                                    VkCommandPool command_pool,
                                    VkQueue graphics_queue,
                                    VkImage image,
                                    VkFormat format,
                                    VkImageLayout old_layout,
                                    VkImageLayout new_layout) {
    (void)format;

    VkCommandBuffer command_buffer = begin_single_use_commands(device, command_pool);

    VkImageMemoryBarrier barrier = {0};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = old_layout;
    barrier.newLayout = new_layout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    VkPipelineStageFlags src_stage = 0;
    VkPipelineStageFlags dst_stage = 0;

    if (old_layout == VK_IMAGE_LAYOUT_UNDEFINED &&
        new_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
    {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        src_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dst_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    }
    else if (old_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
             new_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
    {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        src_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        dst_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }
    else
    {
        die("Unsupported image layout transition");
    }

    vkCmdPipelineBarrier(command_buffer,
                         src_stage, dst_stage,
                         0,
                         0, NULL,
                         0, NULL,
                         1, &barrier);

    end_single_use_commands(device, command_pool, graphics_queue, command_buffer);
}

static void copy_buffer_to_image(VkDevice device,
                                 VkCommandPool command_pool,
                                 VkQueue graphics_queue,
                                 VkBuffer buffer,
                                 VkImage image,
                                 uint32_t width,
                                 uint32_t height) {
    VkCommandBuffer command_buffer = begin_single_use_commands(device, command_pool);

    VkBufferImageCopy region = {0};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageExtent.width = width;
    region.imageExtent.height = height;
    region.imageExtent.depth = 1;

    vkCmdCopyBufferToImage(command_buffer,
                           buffer,
                           image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           1,
                           &region);

    end_single_use_commands(device, command_pool, graphics_queue, command_buffer);
}

/* -------------------------------------------------------------------------- */
/* Texture Handling                                                           */
/* -------------------------------------------------------------------------- */

typedef struct {
    VkImage image;
    VkDeviceMemory memory;
    VkImageView view;
    VkSampler sampler;
    uint32_t width;
    uint32_t height;
} Texture;

typedef struct {
    uint8_t *pixels;
    uint32_t width;
    uint32_t height;
} ImageData;

static bool load_png_rgba(const char *filename, ImageData *out_image) {
    FILE *fp = fopen(filename, "rb");
    if (!fp) {
        return false;
    }

    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png) {
        fclose(fp);
        return false;
    }

    png_infop info = png_create_info_struct(png);
    if (!info) {
        png_destroy_read_struct(&png, NULL, NULL);
        fclose(fp);
        return false;
    }

    if (setjmp(png_jmpbuf(png))) {
        png_destroy_read_struct(&png, &info, NULL);
        fclose(fp);
        return false;
    }

    png_init_io(png, fp);
    png_read_info(png, info);

    png_uint_32 width = png_get_image_width(png, info);
    png_uint_32 height = png_get_image_height(png, info);
    png_byte bit_depth = png_get_bit_depth(png, info);
    png_byte color_type = png_get_color_type(png, info);

    if (bit_depth == 16) {
        png_set_strip_16(png);
    }
    if (color_type == PNG_COLOR_TYPE_PALETTE) {
        png_set_palette_to_rgb(png);
    }
    if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8) {
        png_set_expand_gray_1_2_4_to_8(png);
    }
    if (png_get_valid(png, info, PNG_INFO_tRNS)) {
        png_set_tRNS_to_alpha(png);
    }
    if (color_type == PNG_COLOR_TYPE_GRAY ||
        color_type == PNG_COLOR_TYPE_GRAY_ALPHA)
    {
        png_set_gray_to_rgb(png);
    }
    if (color_type == PNG_COLOR_TYPE_RGB ||
        color_type == PNG_COLOR_TYPE_GRAY ||
        color_type == PNG_COLOR_TYPE_PALETTE)
    {
        png_set_filler(png, 0xFF, PNG_FILLER_AFTER);
    }

    png_read_update_info(png, info);

    size_t row_bytes = png_get_rowbytes(png, info);
    uint8_t *pixels = malloc(row_bytes * height);
    if (!pixels) {
        png_destroy_read_struct(&png, &info, NULL);
        fclose(fp);
        return false;
    }

    png_bytep *row_pointers = malloc(sizeof(png_bytep) * height);
    if (!row_pointers) {
        free(pixels);
        png_destroy_read_struct(&png, &info, NULL);
        fclose(fp);
        return false;
    }

    for (png_uint_32 y = 0; y < height; ++y) {
        row_pointers[y] = pixels + y * row_bytes;
    }

    png_read_image(png, row_pointers);
    png_read_end(png, NULL);

    free(row_pointers);
    png_destroy_read_struct(&png, &info, NULL);
    fclose(fp);

    out_image->pixels = pixels;
    out_image->width = (uint32_t)width;
    out_image->height = (uint32_t)height;
    return true;
}

static void free_image(ImageData *image) {
    if (image->pixels) {
        free(image->pixels);
    }
    image->pixels = NULL;
    image->width = 0;
    image->height = 0;
}

static VkSampler create_nearest_sampler(VkDevice device) {
    VkSamplerCreateInfo sampler_info = {0};
    sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler_info.magFilter = VK_FILTER_NEAREST;
    sampler_info.minFilter = VK_FILTER_NEAREST;
    sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler_info.anisotropyEnable = VK_FALSE;
    sampler_info.maxAnisotropy = 1.0f;
    sampler_info.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    sampler_info.unnormalizedCoordinates = VK_FALSE;
    sampler_info.compareEnable = VK_FALSE;
    sampler_info.compareOp = VK_COMPARE_OP_ALWAYS;
    sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;

    VkSampler sampler;
    VK_CHECK(vkCreateSampler(device, &sampler_info, NULL, &sampler));
    return sampler;
}

static void texture_create_from_pixels(VkDevice device,
                                       VkPhysicalDevice physical_device,
                                       VkCommandPool command_pool,
                                       VkQueue graphics_queue,
                                       const uint8_t *pixels,
                                       uint32_t width,
                                       uint32_t height,
                                       Texture *texture) {
    VkDeviceSize image_size = (VkDeviceSize)width * (VkDeviceSize)height * 4;

    VkBuffer staging_buffer;
    VkDeviceMemory staging_memory;
    create_buffer(device,
                  physical_device,
                  image_size,
                  VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                  &staging_buffer,
                  &staging_memory);

    void *mapped = NULL;
    VK_CHECK(vkMapMemory(device, staging_memory, 0, image_size, 0, &mapped));
    memcpy(mapped, pixels, (size_t)image_size);
    vkUnmapMemory(device, staging_memory);

    create_image(device,
                 physical_device,
                 width,
                 height,
                 VK_FORMAT_R8G8B8A8_SRGB,
                 VK_IMAGE_TILING_OPTIMAL,
                 VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                 &texture->image,
                 &texture->memory);

    transition_image_layout(device,
                            command_pool,
                            graphics_queue,
                            texture->image,
                            VK_FORMAT_R8G8B8A8_SRGB,
                            VK_IMAGE_LAYOUT_UNDEFINED,
                            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    copy_buffer_to_image(device,
                         command_pool,
                         graphics_queue,
                         staging_buffer,
                         texture->image,
                         width,
                         height);

    transition_image_layout(device,
                            command_pool,
                            graphics_queue,
                            texture->image,
                            VK_FORMAT_R8G8B8A8_SRGB,
                            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    vkDestroyBuffer(device, staging_buffer, NULL);
    vkFreeMemory(device, staging_memory, NULL);

    VkImageViewCreateInfo view_info = {0};
    view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image = texture->image;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = VK_FORMAT_R8G8B8A8_SRGB;
    view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    view_info.subresourceRange.levelCount = 1;
    view_info.subresourceRange.layerCount = 1;

    VK_CHECK(vkCreateImageView(device, &view_info, NULL, &texture->view));

    texture->sampler = create_nearest_sampler(device);
    texture->width = width;
    texture->height = height;
}

static void texture_create_from_file(VkDevice device,
                                     VkPhysicalDevice physical_device,
                                     VkCommandPool command_pool,
                                     VkQueue graphics_queue,
                                     const char *filename,
                                     Texture *texture) {
    ImageData image = {0};
    if (!load_png_rgba(filename, &image)) {
        die("Failed to load PNG texture");
    }

    texture_create_from_pixels(device,
                               physical_device,
                               command_pool,
                               graphics_queue,
                               image.pixels,
                               image.width,
                               image.height,
                               texture);

    free_image(&image);
}

static void texture_create_solid(VkDevice device,
                                 VkPhysicalDevice physical_device,
                                 VkCommandPool command_pool,
                                 VkQueue graphics_queue,
                                 uint8_t r,
                                 uint8_t g,
                                 uint8_t b,
                                 uint8_t a,
                                 Texture *texture) {
    uint8_t pixel[4] = {r, g, b, a};
    texture_create_from_pixels(device,
                               physical_device,
                               command_pool,
                               graphics_queue,
                               pixel,
                               1, 1,
                               texture);
}

static void texture_destroy(VkDevice device, Texture *texture) {
    vkDestroySampler(device, texture->sampler, NULL);
    vkDestroyImageView(device, texture->view, NULL);
    vkDestroyImage(device, texture->image, NULL);
    vkFreeMemory(device, texture->memory, NULL);
    *texture = (Texture){0};
}

/* -------------------------------------------------------------------------- */
/* Shader Helpers                                                             */
/* -------------------------------------------------------------------------- */

static char *read_binary_file(const char *filepath, size_t *out_size) {
    FILE *file = fopen(filepath, "rb");
    if (!file) {
        return NULL;
    }

    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    if (file_size <= 0) {
        fclose(file);
        return NULL;
    }

    char *buffer = malloc((size_t)file_size);
    if (!buffer) {
        fclose(file);
        return NULL;
    }

    size_t read = fread(buffer, 1, (size_t)file_size, file);
    fclose(file);

    if (read != (size_t)file_size) {
        free(buffer);
        return NULL;
    }

    *out_size = (size_t)file_size;
    return buffer;
}

static VkShaderModule create_shader_module(VkDevice device, const char *filepath) {
    size_t size = 0;
    char *code = read_binary_file(filepath, &size);
    if (!code) {
        die("Failed to read shader file");
    }

    VkShaderModuleCreateInfo create_info = {0};
    create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    create_info.codeSize = size;
    create_info.pCode = (const uint32_t *)code;

    VkShaderModule module;
    VK_CHECK(vkCreateShaderModule(device, &create_info, NULL, &module));

    free(code);
    return module;
}

/* -------------------------------------------------------------------------- */
/* Vertex Formats                                                             */
/* -------------------------------------------------------------------------- */

typedef struct {
    Vec3 pos;
    Vec2 uv;
} Vertex;

static const Vertex BLOCK_VERTICES[] = {
    /* Front */
    {{-0.5f, -0.5f,  0.5f}, {0.0f, 0.0f}},
    {{ 0.5f, -0.5f,  0.5f}, {1.0f, 0.0f}},
    {{ 0.5f,  0.5f,  0.5f}, {1.0f, 1.0f}},
    {{-0.5f,  0.5f,  0.5f}, {0.0f, 1.0f}},

    /* Back */
    {{-0.5f, -0.5f, -0.5f}, {1.0f, 0.0f}},
    {{ 0.5f, -0.5f, -0.5f}, {0.0f, 0.0f}},
    {{ 0.5f,  0.5f, -0.5f}, {0.0f, 1.0f}},
    {{-0.5f,  0.5f, -0.5f}, {1.0f, 1.0f}},

    /* Top */
    {{-0.5f,  0.5f, -0.5f}, {0.0f, 0.0f}},
    {{ 0.5f,  0.5f, -0.5f}, {1.0f, 0.0f}},
    {{ 0.5f,  0.5f,  0.5f}, {1.0f, 1.0f}},
    {{-0.5f,  0.5f,  0.5f}, {0.0f, 1.0f}},

    /* Bottom */
    {{-0.5f, -0.5f, -0.5f}, {0.0f, 1.0f}},
    {{ 0.5f, -0.5f, -0.5f}, {1.0f, 1.0f}},
    {{ 0.5f, -0.5f,  0.5f}, {1.0f, 0.0f}},
    {{-0.5f, -0.5f,  0.5f}, {0.0f, 0.0f}},

    /* Right */
    {{ 0.5f, -0.5f, -0.5f}, {0.0f, 0.0f}},
    {{ 0.5f,  0.5f, -0.5f}, {1.0f, 0.0f}},
    {{ 0.5f,  0.5f,  0.5f}, {1.0f, 1.0f}},
    {{ 0.5f, -0.5f,  0.5f}, {0.0f, 1.0f}},

    /* Left */
    {{-0.5f, -0.5f, -0.5f}, {1.0f, 0.0f}},
    {{-0.5f,  0.5f, -0.5f}, {0.0f, 0.0f}},
    {{-0.5f,  0.5f,  0.5f}, {0.0f, 1.0f}},
    {{-0.5f, -0.5f,  0.5f}, {1.0f, 1.0f}},
};

static const uint16_t BLOCK_INDICES[] = {
     0,  1,  2,  2,  3,  0, /* Front */
     6,  5,  4,  4,  7,  6, /* Back */
     8, 11, 10, 10,  9,  8, /* Top */
    12, 13, 14, 14, 15, 12, /* Bottom */
    16, 17, 18, 18, 19, 16, /* Right */
    22, 21, 20, 20, 23, 22  /* Left */
};

static const Vertex EDGE_VERTICES[] = {
    {{-0.5f, -0.5f,  0.5f}, {0.0f, 0.0f}}, /* 0 */
    {{ 0.5f, -0.5f,  0.5f}, {0.0f, 0.0f}}, /* 1 */
    {{ 0.5f,  0.5f,  0.5f}, {0.0f, 0.0f}}, /* 2 */
    {{-0.5f,  0.5f,  0.5f}, {0.0f, 0.0f}}, /* 3 */
    {{-0.5f, -0.5f, -0.5f}, {0.0f, 0.0f}}, /* 4 */
    {{ 0.5f, -0.5f, -0.5f}, {0.0f, 0.0f}}, /* 5 */
    {{ 0.5f,  0.5f, -0.5f}, {0.0f, 0.0f}}, /* 6 */
    {{-0.5f,  0.5f, -0.5f}, {0.0f, 0.0f}}, /* 7 */
};

static const uint16_t EDGE_INDICES[] = {
    0, 1,  1, 2,  2, 3,  3, 0, /* Front */
    4, 5,  5, 6,  6, 7,  7, 4, /* Back */
    0, 4,  1, 5,  2, 6,  3, 7  /* Connecting edges */
};

/* -------------------------------------------------------------------------- */
/* Uniform Data                                                               */
/* -------------------------------------------------------------------------- */

typedef struct {
    Mat4 model;
    Mat4 view;
    Mat4 proj;
    uint32_t block_type;
    uint32_t pad[3];
} UniformBufferObject;

/* -------------------------------------------------------------------------- */
/* Vulkan Swapchain Resources                                                 */
/* -------------------------------------------------------------------------- */

typedef struct {
    VkSwapchainKHR swapchain;
    VkImage *images;
    VkImageView *image_views;
    VkFramebuffer *framebuffers;
    VkRenderPass render_pass;
    VkPipeline pipeline_solid;
    VkPipeline pipeline_wireframe;
    VkPipeline pipeline_crosshair;
    VkDescriptorPool descriptor_pool;
    VkDescriptorSet *descriptor_sets_normal;
    VkDescriptorSet *descriptor_sets_highlight;
    VkBuffer *uniform_buffers;
    VkDeviceMemory *uniform_memories;
    VkCommandBuffer *command_buffers;

    VkImage depth_image;
    VkDeviceMemory depth_memory;
    VkImageView depth_view;

    uint32_t image_count;
    VkExtent2D extent;
    VkFormat format;
    VkDeviceSize ubo_stride;
} SwapchainResources;

static void swapchain_resources_reset(SwapchainResources *res) {
    *res = (SwapchainResources){0};
}

static void swapchain_destroy(VkDevice device,
                              VkCommandPool command_pool,
                              SwapchainResources *res) {
    if (res->command_buffers) {
        vkFreeCommandBuffers(device, command_pool, res->image_count, res->command_buffers);
        free(res->command_buffers);
        res->command_buffers = NULL;
    }

    if (res->uniform_buffers) {
        for (uint32_t i = 0; i < res->image_count; ++i) {
            vkDestroyBuffer(device, res->uniform_buffers[i], NULL);
            vkFreeMemory(device, res->uniform_memories[i], NULL);
        }
        free(res->uniform_buffers);
        free(res->uniform_memories);
        res->uniform_buffers = NULL;
        res->uniform_memories = NULL;
    }

    if (res->descriptor_pool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device, res->descriptor_pool, NULL);
        res->descriptor_pool = VK_NULL_HANDLE;
    }

    if (res->framebuffers) {
        for (uint32_t i = 0; i < res->image_count; ++i) {
            vkDestroyFramebuffer(device, res->framebuffers[i], NULL);
        }
        free(res->framebuffers);
        res->framebuffers = NULL;
    }

    if (res->pipeline_crosshair != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, res->pipeline_crosshair, NULL);
        res->pipeline_crosshair = VK_NULL_HANDLE;
    }

    if (res->pipeline_wireframe != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, res->pipeline_wireframe, NULL);
        res->pipeline_wireframe = VK_NULL_HANDLE;
    }

    if (res->pipeline_solid != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, res->pipeline_solid, NULL);
        res->pipeline_solid = VK_NULL_HANDLE;
    }

    if (res->render_pass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(device, res->render_pass, NULL);
        res->render_pass = VK_NULL_HANDLE;
    }

    if (res->depth_view != VK_NULL_HANDLE) {
        vkDestroyImageView(device, res->depth_view, NULL);
        res->depth_view = VK_NULL_HANDLE;
    }
    if (res->depth_image != VK_NULL_HANDLE) {
        vkDestroyImage(device, res->depth_image, NULL);
        res->depth_image = VK_NULL_HANDLE;
    }
    if (res->depth_memory != VK_NULL_HANDLE) {
        vkFreeMemory(device, res->depth_memory, NULL);
        res->depth_memory = VK_NULL_HANDLE;
    }

    if (res->image_views) {
        for (uint32_t i = 0; i < res->image_count; ++i) {
            vkDestroyImageView(device, res->image_views[i], NULL);
        }
        free(res->image_views);
        res->image_views = NULL;
    }

    if (res->images) {
        free(res->images);
        res->images = NULL;
    }

    if (res->swapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(device, res->swapchain, NULL);
        res->swapchain = VK_NULL_HANDLE;
    }

    if (res->descriptor_sets_normal) {
        free(res->descriptor_sets_normal);
        res->descriptor_sets_normal = NULL;
    }
    if (res->descriptor_sets_highlight) {
        free(res->descriptor_sets_highlight);
        res->descriptor_sets_highlight = NULL;
    }

    res->image_count = 0;
    res->extent = (VkExtent2D){0, 0};
    res->format = VK_FORMAT_UNDEFINED;
    res->ubo_stride = 0;
}

static void create_depth_resources(VkDevice device,
                                   VkPhysicalDevice physical_device,
                                   VkExtent2D extent,
                                   VkImage *image,
                                   VkDeviceMemory *memory,
                                   VkImageView *view) {
    VkFormat depth_format = VK_FORMAT_D32_SFLOAT;

    create_image(device,
                 physical_device,
                 extent.width,
                 extent.height,
                 depth_format,
                 VK_IMAGE_TILING_OPTIMAL,
                 VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                 image,
                 memory);

    VkImageViewCreateInfo view_info = {0};
    view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image = *image;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = depth_format;
    view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    view_info.subresourceRange.levelCount = 1;
    view_info.subresourceRange.layerCount = 1;

    VK_CHECK(vkCreateImageView(device, &view_info, NULL, view));
}

/* -------------------------------------------------------------------------- */
/* Swapchain creation (monolithic but tidy)                                   */
/* -------------------------------------------------------------------------- */

typedef struct {
    VkDevice device;
    VkPhysicalDevice physical_device;
    VkSurfaceKHR surface;
    VkQueue graphics_queue;
    VkCommandPool command_pool;
    VkDescriptorSetLayout descriptor_set_layout;
    VkPipelineLayout pipeline_layout;
    VkShaderModule vert_shader;
    VkShaderModule frag_shader;
    const Texture *textures;          /* 0: dirt, 1: stone */
    const Texture *black_texture;
    VkBuffer vertex_buffer;
    VkBuffer index_buffer;
    VkBuffer edge_vertex_buffer;
    VkBuffer edge_index_buffer;
    VkBuffer crosshair_vertex_buffer;
    VkDeviceMemory crosshair_vertex_memory;
    VkBuffer uniform_template_buffer; /* unused, but keep for possible future usage */
} SwapchainContext;

static void update_crosshair_vertices(VkDevice device,
                                      VkDeviceMemory memory,
                                      VkExtent2D extent) {
    const float crosshair_size = 0.03f;
    float aspect_correction = (float)extent.height / (float)extent.width;

    Vertex crosshair_vertices[] = {
        {{-crosshair_size * aspect_correction, 0.0f, 0.0f}, {0.0f, 0.0f}},
        {{ crosshair_size * aspect_correction, 0.0f, 0.0f}, {1.0f, 0.0f}},
        {{ 0.0f, -crosshair_size, 0.0f}, {0.0f, 0.0f}},
        {{ 0.0f,  crosshair_size, 0.0f}, {1.0f, 0.0f}},
    };

    void *mapped = NULL;
    VK_CHECK(vkMapMemory(device, memory, 0, sizeof(crosshair_vertices), 0, &mapped));
    memcpy(mapped, crosshair_vertices, sizeof(crosshair_vertices));
    vkUnmapMemory(device, memory);
}

static void swapchain_create(SwapchainContext *ctx,
                             SwapchainResources *res,
                             uint32_t framebuffer_width,
                             uint32_t framebuffer_height,
                             uint32_t max_blocks,
                             uint32_t extra_ubos) {
    VkPhysicalDevice physical_device = ctx->physical_device;
    VkDevice device = ctx->device;
    VkSurfaceKHR surface = ctx->surface;

    /* Query surface capabilities */
    VkSurfaceCapabilitiesKHR capabilities;
    VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device, surface, &capabilities));

    uint32_t format_count = 0;
    VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &format_count, NULL));
    VkSurfaceFormatKHR *formats = malloc(sizeof(VkSurfaceFormatKHR) * format_count);
    VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &format_count, formats));

    VkSurfaceFormatKHR surface_format = formats[0];
    for (uint32_t i = 0; i < format_count; ++i) {
        if (formats[i].format == VK_FORMAT_B8G8R8A8_SRGB &&
            formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
        {
            surface_format = formats[i];
            break;
        }
    }
    free(formats);

    uint32_t image_count = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount > 0 && image_count > capabilities.maxImageCount) {
        image_count = capabilities.maxImageCount;
    }

    VkExtent2D extent = capabilities.currentExtent;
    if (extent.width == UINT32_MAX) {
        extent.width = framebuffer_width;
        extent.height = framebuffer_height;
    }

    VkSwapchainCreateInfoKHR swapchain_info = {0};
    swapchain_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swapchain_info.surface = surface;
    swapchain_info.minImageCount = image_count;
    swapchain_info.imageFormat = surface_format.format;
    swapchain_info.imageColorSpace = surface_format.colorSpace;
    swapchain_info.imageExtent = extent;
    swapchain_info.imageArrayLayers = 1;
    swapchain_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    swapchain_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    swapchain_info.preTransform = capabilities.currentTransform;
    swapchain_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    swapchain_info.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    swapchain_info.clipped = VK_TRUE;

    VK_CHECK(vkCreateSwapchainKHR(device, &swapchain_info, NULL, &res->swapchain));

    VK_CHECK(vkGetSwapchainImagesKHR(device, res->swapchain, &image_count, NULL));
    res->images = malloc(sizeof(VkImage) * image_count);
    VK_CHECK(vkGetSwapchainImagesKHR(device, res->swapchain, &image_count, res->images));
    res->image_count = image_count;
    res->extent = extent;
    res->format = surface_format.format;

    res->image_views = malloc(sizeof(VkImageView) * image_count);
    for (uint32_t i = 0; i < image_count; ++i) {
        VkImageViewCreateInfo view_info = {0};
        view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view_info.image = res->images[i];
        view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view_info.format = surface_format.format;
        view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        view_info.subresourceRange.levelCount = 1;
        view_info.subresourceRange.layerCount = 1;

        VK_CHECK(vkCreateImageView(device, &view_info, NULL, &res->image_views[i]));
    }

    create_depth_resources(device,
                           physical_device,
                           extent,
                           &res->depth_image,
                           &res->depth_memory,
                           &res->depth_view);

    VkAttachmentDescription attachments[2] = {0};

    attachments[0].format = surface_format.format;
    attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    attachments[1].format = VK_FORMAT_D32_SFLOAT;
    attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference color_ref = {0};
    color_ref.attachment = 0;
    color_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depth_ref = {0};
    depth_ref.attachment = 1;
    depth_ref.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass = {0};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color_ref;
    subpass.pDepthStencilAttachment = &depth_ref;

    VkSubpassDependency dependency = {0};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                              VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                              VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                               VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo render_pass_info = {0};
    render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    render_pass_info.attachmentCount = ARRAY_LENGTH(attachments);
    render_pass_info.pAttachments = attachments;
    render_pass_info.subpassCount = 1;
    render_pass_info.pSubpasses = &subpass;
    render_pass_info.dependencyCount = 1;
    render_pass_info.pDependencies = &dependency;

    VK_CHECK(vkCreateRenderPass(device, &render_pass_info, NULL, &res->render_pass));

    VkPipelineShaderStageCreateInfo shader_stages[2] = {0};
    shader_stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shader_stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    shader_stages[0].module = ctx->vert_shader;
    shader_stages[0].pName = "main";

    shader_stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shader_stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    shader_stages[1].module = ctx->frag_shader;
    shader_stages[1].pName = "main";

    VkVertexInputBindingDescription binding = {0};
    binding.binding = 0;
    binding.stride = sizeof(Vertex);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attributes[2] = {0};
    attributes[0].binding = 0;
    attributes[0].location = 0;
    attributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributes[0].offset = offsetof(Vertex, pos);

    attributes[1].binding = 0;
    attributes[1].location = 1;
    attributes[1].format = VK_FORMAT_R32G32_SFLOAT;
    attributes[1].offset = offsetof(Vertex, uv);

    VkPipelineVertexInputStateCreateInfo vertex_input = {0};
    vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertex_input.vertexBindingDescriptionCount = 1;
    vertex_input.pVertexBindingDescriptions = &binding;
    vertex_input.vertexAttributeDescriptionCount = ARRAY_LENGTH(attributes);
    vertex_input.pVertexAttributeDescriptions = attributes;

    VkPipelineInputAssemblyStateCreateInfo input_assembly_tri = {0};
    input_assembly_tri.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_assembly_tri.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineInputAssemblyStateCreateInfo input_assembly_lines = input_assembly_tri;
    input_assembly_lines.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;

    VkViewport viewport = {0};
    viewport.width = (float)extent.width;
    viewport.height = (float)extent.height;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor = {0};
    scissor.extent = extent;

    VkPipelineViewportStateCreateInfo viewport_state = {0};
    viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state.viewportCount = 1;
    viewport_state.pViewports = &viewport;
    viewport_state.scissorCount = 1;
    viewport_state.pScissors = &scissor;

    VkPipelineRasterizationStateCreateInfo raster_solid = {0};
    raster_solid.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster_solid.polygonMode = VK_POLYGON_MODE_FILL;
    raster_solid.cullMode = VK_CULL_MODE_BACK_BIT;
    raster_solid.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster_solid.lineWidth = 3.0f;

    VkPipelineRasterizationStateCreateInfo raster_wire = raster_solid;
    raster_wire.cullMode = VK_CULL_MODE_NONE;

    VkPipelineRasterizationStateCreateInfo raster_cross = raster_wire;

    VkPipelineMultisampleStateCreateInfo multisample = {0};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depth_solid = {0};
    depth_solid.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth_solid.depthTestEnable = VK_TRUE;
    depth_solid.depthWriteEnable = VK_TRUE;
    depth_solid.depthCompareOp = VK_COMPARE_OP_LESS;

    VkPipelineDepthStencilStateCreateInfo depth_wire = depth_solid;
    depth_wire.depthWriteEnable = VK_FALSE;
    depth_wire.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

    VkPipelineDepthStencilStateCreateInfo depth_cross = {0};
    depth_cross.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth_cross.depthTestEnable = VK_FALSE;
    depth_cross.depthWriteEnable = VK_FALSE;
    depth_cross.depthCompareOp = VK_COMPARE_OP_ALWAYS;

    VkPipelineColorBlendAttachmentState color_blend = {0};
    color_blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
                                 VK_COLOR_COMPONENT_G_BIT |
                                 VK_COLOR_COMPONENT_B_BIT |
                                 VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo color_state = {0};
    color_state.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    color_state.attachmentCount = 1;
    color_state.pAttachments = &color_blend;

    VkGraphicsPipelineCreateInfo pipeline_info = {0};
    pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeline_info.stageCount = ARRAY_LENGTH(shader_stages);
    pipeline_info.pStages = shader_stages;
    pipeline_info.pVertexInputState = &vertex_input;
    pipeline_info.pViewportState = &viewport_state;
    pipeline_info.pMultisampleState = &multisample;
    pipeline_info.pColorBlendState = &color_state;
    pipeline_info.layout = ctx->pipeline_layout;
    pipeline_info.renderPass = res->render_pass;

    pipeline_info.pInputAssemblyState = &input_assembly_tri;
    pipeline_info.pRasterizationState = &raster_solid;
    pipeline_info.pDepthStencilState = &depth_solid;

    VK_CHECK(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipeline_info, NULL, &res->pipeline_solid));

    pipeline_info.pInputAssemblyState = &input_assembly_lines;
    pipeline_info.pRasterizationState = &raster_wire;
    pipeline_info.pDepthStencilState = &depth_wire;

    VK_CHECK(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipeline_info, NULL, &res->pipeline_wireframe));

    pipeline_info.pRasterizationState = &raster_cross;
    pipeline_info.pDepthStencilState = &depth_cross;

    VK_CHECK(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipeline_info, NULL, &res->pipeline_crosshair));

    res->framebuffers = malloc(sizeof(VkFramebuffer) * image_count);
    for (uint32_t i = 0; i < image_count; ++i) {
        VkImageView attachments_views[] = {res->image_views[i], res->depth_view};

        VkFramebufferCreateInfo framebuffer_info = {0};
        framebuffer_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebuffer_info.renderPass = res->render_pass;
        framebuffer_info.attachmentCount = ARRAY_LENGTH(attachments_views);
        framebuffer_info.pAttachments = attachments_views;
        framebuffer_info.width = extent.width;
        framebuffer_info.height = extent.height;
        framebuffer_info.layers = 1;

        VK_CHECK(vkCreateFramebuffer(device, &framebuffer_info, NULL, &res->framebuffers[i]));
    }

    VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties(physical_device, &properties);

    VkDeviceSize ubo_size = sizeof(UniformBufferObject);
    VkDeviceSize min_align = properties.limits.minUniformBufferOffsetAlignment;
    if (min_align > 0) {
        ubo_size = (ubo_size + min_align - 1) & ~(min_align - 1);
    }
    res->ubo_stride = ubo_size;

    VkDeviceSize buffer_size = ubo_size * (max_blocks + extra_ubos);

    res->uniform_buffers = malloc(sizeof(VkBuffer) * image_count);
    res->uniform_memories = malloc(sizeof(VkDeviceMemory) * image_count);

    for (uint32_t i = 0; i < image_count; ++i) {
        create_buffer(device,
                      physical_device,
                      buffer_size,
                      VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      &res->uniform_buffers[i],
                      &res->uniform_memories[i]);
    }

    VkDescriptorPoolSize pool_sizes[2] = {0};
    pool_sizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    pool_sizes[0].descriptorCount = image_count * 2;
    pool_sizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    pool_sizes[1].descriptorCount = image_count * 4;

    VkDescriptorPoolCreateInfo pool_info = {0};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.poolSizeCount = ARRAY_LENGTH(pool_sizes);
    pool_info.pPoolSizes = pool_sizes;
    pool_info.maxSets = image_count * 2;

    VK_CHECK(vkCreateDescriptorPool(device, &pool_info, NULL, &res->descriptor_pool));

    VkDescriptorSetLayout *layouts = malloc(sizeof(VkDescriptorSetLayout) * image_count);
    for (uint32_t i = 0; i < image_count; ++i) {
        layouts[i] = ctx->descriptor_set_layout;
    }

    VkDescriptorSetAllocateInfo alloc_info = {0};
    alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc_info.descriptorPool = res->descriptor_pool;
    alloc_info.descriptorSetCount = image_count;
    alloc_info.pSetLayouts = layouts;

    res->descriptor_sets_normal = malloc(sizeof(VkDescriptorSet) * image_count);
    res->descriptor_sets_highlight = malloc(sizeof(VkDescriptorSet) * image_count);

    VK_CHECK(vkAllocateDescriptorSets(device, &alloc_info, res->descriptor_sets_normal));
    VK_CHECK(vkAllocateDescriptorSets(device, &alloc_info, res->descriptor_sets_highlight));

    free(layouts);

    for (uint32_t i = 0; i < image_count; ++i) {
        VkDescriptorBufferInfo buffer_info = {0};
        buffer_info.buffer = res->uniform_buffers[i];
        buffer_info.range = sizeof(UniformBufferObject);

        VkDescriptorImageInfo normal_images[2] = {0};
        normal_images[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        normal_images[0].imageView = ctx->textures[0].view;
        normal_images[0].sampler = ctx->textures[0].sampler;

        normal_images[1].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        normal_images[1].imageView = ctx->textures[1].view;
        normal_images[1].sampler = ctx->textures[1].sampler;

        VkDescriptorImageInfo highlight_images[2];
        highlight_images[0] = normal_images[0];
        highlight_images[1] = normal_images[1];
        highlight_images[1].imageView = ctx->black_texture->view;
        highlight_images[1].sampler = ctx->black_texture->sampler;

        VkWriteDescriptorSet writes[2] = {0};
        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = res->descriptor_sets_normal[i];
        writes[0].dstBinding = 0;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        writes[0].descriptorCount = 1;
        writes[0].pBufferInfo = &buffer_info;

        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = res->descriptor_sets_normal[i];
        writes[1].dstBinding = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[1].descriptorCount = 2;
        writes[1].pImageInfo = normal_images;

        vkUpdateDescriptorSets(device, ARRAY_LENGTH(writes), writes, 0, NULL);

        writes[0].dstSet = res->descriptor_sets_highlight[i];
        writes[1].dstSet = res->descriptor_sets_highlight[i];
        writes[1].pImageInfo = highlight_images;

        vkUpdateDescriptorSets(device, ARRAY_LENGTH(writes), writes, 0, NULL);
    }

    res->command_buffers = malloc(sizeof(VkCommandBuffer) * image_count);

    VkCommandBufferAllocateInfo command_alloc = {0};
    command_alloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    command_alloc.commandPool = ctx->command_pool;
    command_alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    command_alloc.commandBufferCount = image_count;

    VK_CHECK(vkAllocateCommandBuffers(device, &command_alloc, res->command_buffers));

    update_crosshair_vertices(device, ctx->crosshair_vertex_memory, extent);
}

/* -------------------------------------------------------------------------- */
/* Utility Functions                                                          */
/* -------------------------------------------------------------------------- */

static bool is_key_pressed(const bool *keys, KeySym sym) {
    if (sym < 256) {
        return keys[sym];
    }
    return false;
}

/* -------------------------------------------------------------------------- */
/* Main                                                                       */
/* -------------------------------------------------------------------------- */

int main(void) {
    const uint32_t MAX_BLOCKS = 4096;
    const uint32_t EXTRA_UBOS = 2; /* highlight + crosshair */
    uint32_t window_width = 800;
    uint32_t window_height = 600;

    /* ---------------------------------------------------------------------- */
    /* X11 Setup                                                              */
    /* ---------------------------------------------------------------------- */

    Display *display = XOpenDisplay(NULL);
    if (!display) {
        die("Failed to open X11 display");
    }

    int screen = DefaultScreen(display);
    Window root = RootWindow(display, screen);

    Window window = XCreateSimpleWindow(display, root,
                                        0, 0,
                                        window_width,
                                        window_height,
                                        1,
                                        BlackPixel(display, screen),
                                        WhitePixel(display, screen));

    XStoreName(display, window, "Voxel Engine");
    XSelectInput(display, window,
                 ExposureMask | KeyPressMask | KeyReleaseMask |
                 PointerMotionMask | StructureNotifyMask | ButtonPressMask);

    Atom wm_delete_window = XInternAtom(display, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(display, window, &wm_delete_window, 1);
    XMapWindow(display, window);

    /* Invisible cursor for captured mode */
    char empty_cursor_data[8] = {0};
    Pixmap blank = XCreateBitmapFromData(display, window, empty_cursor_data, 8, 8);
    Colormap colormap = DefaultColormap(display, screen);
    XColor black, dummy;
    XAllocNamedColor(display, colormap, "black", &black, &dummy);
    Cursor invisible_cursor = XCreatePixmapCursor(display, blank, blank, &black, &black, 0, 0);
    XFreePixmap(display, blank);

    /* ---------------------------------------------------------------------- */
    /* Vulkan Instance and Surface                                            */
    /* ---------------------------------------------------------------------- */

    VkApplicationInfo app_info = {0};
    app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName = "Voxel Engine";
    app_info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    app_info.pEngineName = "No Engine";
    app_info.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    app_info.apiVersion = VK_API_VERSION_1_1;

    const char *instance_extensions[] = {
        VK_KHR_SURFACE_EXTENSION_NAME,
        VK_KHR_XLIB_SURFACE_EXTENSION_NAME
    };

    VkInstanceCreateInfo instance_info = {0};
    instance_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instance_info.pApplicationInfo = &app_info;
    instance_info.enabledExtensionCount = ARRAY_LENGTH(instance_extensions);
    instance_info.ppEnabledExtensionNames = instance_extensions;

    VkInstance instance;
    VK_CHECK(vkCreateInstance(&instance_info, NULL, &instance));

    VkXlibSurfaceCreateInfoKHR surface_info = {0};
    surface_info.sType = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR;
    surface_info.dpy = display;
    surface_info.window = window;

    VkSurfaceKHR surface;
    VK_CHECK(vkCreateXlibSurfaceKHR(instance, &surface_info, NULL, &surface));

    /* ---------------------------------------------------------------------- */
    /* Physical Device and Logical Device                                     */
    /* ---------------------------------------------------------------------- */

    uint32_t device_count = 0;
    VK_CHECK(vkEnumeratePhysicalDevices(instance, &device_count, NULL));
    if (device_count == 0) {
        die("No Vulkan-capable GPU found");
    }

    VkPhysicalDevice *physical_devices = malloc(sizeof(VkPhysicalDevice) * device_count);
    VK_CHECK(vkEnumeratePhysicalDevices(instance, &device_count, physical_devices));

    VkPhysicalDevice physical_device = VK_NULL_HANDLE;
    uint32_t graphics_family = UINT32_MAX;

    for (uint32_t i = 0; i < device_count; ++i) {
        uint32_t queue_family_count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(physical_devices[i], &queue_family_count, NULL);

        VkQueueFamilyProperties *families = malloc(sizeof(VkQueueFamilyProperties) * queue_family_count);
        vkGetPhysicalDeviceQueueFamilyProperties(physical_devices[i], &queue_family_count, families);

        for (uint32_t j = 0; j < queue_family_count; ++j) {
            VkBool32 present = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(physical_devices[i], j, surface, &present);

            bool has_graphics = (families[j].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0;
            if (has_graphics && present) {
                physical_device = physical_devices[i];
                graphics_family = j;
                break;
            }
        }

        free(families);

        if (physical_device != VK_NULL_HANDLE) {
            break;
        }
    }

    free(physical_devices);

    if (physical_device == VK_NULL_HANDLE) {
        die("No suitable GPU found");
    }

    float queue_priority = 1.0f;
    VkDeviceQueueCreateInfo queue_info = {0};
    queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue_info.queueFamilyIndex = graphics_family;
    queue_info.queueCount = 1;
    queue_info.pQueuePriorities = &queue_priority;

    const char *device_extensions[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };

    VkPhysicalDeviceFeatures supported_features;
    vkGetPhysicalDeviceFeatures(physical_device, &supported_features);

    VkPhysicalDeviceFeatures enabled_features = {0};
    enabled_features.wideLines = supported_features.wideLines ? VK_TRUE : VK_FALSE;

    VkDeviceCreateInfo device_info = {0};
    device_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    device_info.queueCreateInfoCount = 1;
    device_info.pQueueCreateInfos = &queue_info;
    device_info.enabledExtensionCount = ARRAY_LENGTH(device_extensions);
    device_info.ppEnabledExtensionNames = device_extensions;
    device_info.pEnabledFeatures = &enabled_features;

    VkDevice device;
    VK_CHECK(vkCreateDevice(physical_device, &device_info, NULL, &device));

    VkQueue graphics_queue;
    vkGetDeviceQueue(device, graphics_family, 0, &graphics_queue);

    VkCommandPoolCreateInfo pool_info = {0};
    pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_info.queueFamilyIndex = graphics_family;
    pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    VkCommandPool command_pool;
    VK_CHECK(vkCreateCommandPool(device, &pool_info, NULL, &command_pool));

    /* ---------------------------------------------------------------------- */
    /* Textures                                                                */
    /* ---------------------------------------------------------------------- */

    Texture textures[2];
    texture_create_from_file(device, physical_device, command_pool, graphics_queue, "dirt.png", &textures[0]);
    texture_create_from_file(device, physical_device, command_pool, graphics_queue, "stone.png", &textures[1]);

    Texture black_texture;
    texture_create_solid(device, physical_device, command_pool, graphics_queue, 0, 0, 0, 255, &black_texture);

    /* ---------------------------------------------------------------------- */
    /* Buffers                                                                 */
    /* ---------------------------------------------------------------------- */

    VkBuffer vertex_buffer;
    VkDeviceMemory vertex_memory;
    create_buffer(device,
                  physical_device,
                  sizeof(BLOCK_VERTICES),
                  VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                  &vertex_buffer,
                  &vertex_memory);

    void *mapped = NULL;
    VK_CHECK(vkMapMemory(device, vertex_memory, 0, sizeof(BLOCK_VERTICES), 0, &mapped));
    memcpy(mapped, BLOCK_VERTICES, sizeof(BLOCK_VERTICES));
    vkUnmapMemory(device, vertex_memory);

    VkBuffer index_buffer;
    VkDeviceMemory index_memory;
    create_buffer(device,
                  physical_device,
                  sizeof(BLOCK_INDICES),
                  VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                  &index_buffer,
                  &index_memory);

    VK_CHECK(vkMapMemory(device, index_memory, 0, sizeof(BLOCK_INDICES), 0, &mapped));
    memcpy(mapped, BLOCK_INDICES, sizeof(BLOCK_INDICES));
    vkUnmapMemory(device, index_memory);

    VkBuffer edge_vertex_buffer;
    VkDeviceMemory edge_vertex_memory;
    create_buffer(device,
                  physical_device,
                  sizeof(EDGE_VERTICES),
                  VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                  &edge_vertex_buffer,
                  &edge_vertex_memory);

    VK_CHECK(vkMapMemory(device, edge_vertex_memory, 0, sizeof(EDGE_VERTICES), 0, &mapped));
    memcpy(mapped, EDGE_VERTICES, sizeof(EDGE_VERTICES));
    vkUnmapMemory(device, edge_vertex_memory);

    VkBuffer edge_index_buffer;
    VkDeviceMemory edge_index_memory;
    create_buffer(device,
                  physical_device,
                  sizeof(EDGE_INDICES),
                  VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                  &edge_index_buffer,
                  &edge_index_memory);

    VK_CHECK(vkMapMemory(device, edge_index_memory, 0, sizeof(EDGE_INDICES), 0, &mapped));
    memcpy(mapped, EDGE_INDICES, sizeof(EDGE_INDICES));
    vkUnmapMemory(device, edge_index_memory);

    VkBuffer crosshair_vertex_buffer;
    VkDeviceMemory crosshair_vertex_memory;
    create_buffer(device,
                  physical_device,
                  sizeof(Vertex) * 4,
                  VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                  &crosshair_vertex_buffer,
                  &crosshair_vertex_memory);

    /* ---------------------------------------------------------------------- */
    /* Shaders, Descriptor Layout, Pipeline Layout                            */
    /* ---------------------------------------------------------------------- */

    VkShaderModule vert_shader = create_shader_module(device, "shaders/vert.spv");
    VkShaderModule frag_shader = create_shader_module(device, "shaders/frag.spv");

    VkDescriptorSetLayoutBinding ubo_binding = {0};
    ubo_binding.binding = 0;
    ubo_binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    ubo_binding.descriptorCount = 1;
    ubo_binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding sampler_binding = {0};
    sampler_binding.binding = 1;
    sampler_binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    sampler_binding.descriptorCount = 2;
    sampler_binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding bindings[] = {ubo_binding, sampler_binding};

    VkDescriptorSetLayoutCreateInfo layout_info = {0};
    layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layout_info.bindingCount = ARRAY_LENGTH(bindings);
    layout_info.pBindings = bindings;

    VkDescriptorSetLayout descriptor_layout;
    VK_CHECK(vkCreateDescriptorSetLayout(device, &layout_info, NULL, &descriptor_layout));

    VkPipelineLayoutCreateInfo pipeline_layout_info = {0};
    pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipeline_layout_info.setLayoutCount = 1;
    pipeline_layout_info.pSetLayouts = &descriptor_layout;

    VkPipelineLayout pipeline_layout;
    VK_CHECK(vkCreatePipelineLayout(device, &pipeline_layout_info, NULL, &pipeline_layout));

    /* ---------------------------------------------------------------------- */
    /* Swapchain Resources (lazy init later)                                  */
    /* ---------------------------------------------------------------------- */

    SwapchainResources swapchain;
    swapchain_resources_reset(&swapchain);

    VkSemaphore image_available = VK_NULL_HANDLE;
    VkSemaphore render_finished = VK_NULL_HANDLE;
    VkFence in_flight = VK_NULL_HANDLE;

    VkSemaphoreCreateInfo semaphore_info = {0};
    semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fence_info = {0};
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    VK_CHECK(vkCreateSemaphore(device, &semaphore_info, NULL, &image_available));
    VK_CHECK(vkCreateSemaphore(device, &semaphore_info, NULL, &render_finished));
    VK_CHECK(vkCreateFence(device, &fence_info, NULL, &in_flight));

    bool swapchain_needs_recreate = true;

    SwapchainContext swapchain_ctx = {
        .device = device,
        .physical_device = physical_device,
        .surface = surface,
        .graphics_queue = graphics_queue,
        .command_pool = command_pool,
        .descriptor_set_layout = descriptor_layout,
        .pipeline_layout = pipeline_layout,
        .vert_shader = vert_shader,
        .frag_shader = frag_shader,
        .textures = textures,
        .black_texture = &black_texture,
        .vertex_buffer = vertex_buffer,
        .index_buffer = index_buffer,
        .edge_vertex_buffer = edge_vertex_buffer,
        .edge_index_buffer = edge_index_buffer,
        .crosshair_vertex_buffer = crosshair_vertex_buffer,
        .crosshair_vertex_memory = crosshair_vertex_memory
    };

    /* ---------------------------------------------------------------------- */
    /* Voxel World                                                            */
    /* ---------------------------------------------------------------------- */

    Block *blocks = malloc(sizeof(Block) * MAX_BLOCKS);
    int block_count = 0;

    for (int x = -7; x <= 7; ++x) {
        for (int z = -7; z <= 7; ++z) {
            bool corner = (abs(x) == 7) && (abs(z) == 7);
            block_add(blocks, &block_count, MAX_BLOCKS, (IVec3){x, 0, z}, corner ? BLOCK_STONE : BLOCK_DIRT);
        }
    }

    /* ---------------------------------------------------------------------- */
    /* Player / Camera                                                        */
    /* ---------------------------------------------------------------------- */

    const float GRAVITY = 17.0f;
    const float JUMP_HEIGHT = 1.2f;
    const float JUMP_VELOCITY = sqrtf(2.0f * GRAVITY * JUMP_HEIGHT);
    const float EYE_HEIGHT = 1.6f;

    Player player = {0};
    player.position = vec3(0.0f, 1.5f, 0.0f);

    Camera camera;
    camera_init(&camera);
    camera.position = vec3_add(player.position, vec3(0.0f, EYE_HEIGHT, 0.0f));

    /* Input State */
    bool keys[256] = {0};
    bool mouse_captured = false;
    bool first_mouse = true;
    float last_mouse_x = 0.0f;
    float last_mouse_y = 0.0f;
    uint8_t current_block_type = BLOCK_DIRT;

    struct timespec last_time;
    clock_gettime(CLOCK_MONOTONIC, &last_time);

    /* ---------------------------------------------------------------------- */
    /* Main Loop                                                               */
    /* ---------------------------------------------------------------------- */

    bool running = true;

    while (running) {
        if (swapchain_needs_recreate) {
            vkDeviceWaitIdle(device);

            XWindowAttributes attrs;
            XGetWindowAttributes(display, window, &attrs);
            if (attrs.width == 0 || attrs.height == 0) {
                swapchain_needs_recreate = false;
                continue;
            }

            window_width = attrs.width;
            window_height = attrs.height;

            swapchain_destroy(device, command_pool, &swapchain);
            swapchain_create(&swapchain_ctx,
                             &swapchain,
                             window_width,
                             window_height,
                             MAX_BLOCKS,
                             EXTRA_UBOS);

            swapchain_needs_recreate = false;
        }

        bool mouse_moved = false;
        float mouse_x = 0.0f;
        float mouse_y = 0.0f;
        bool left_click = false;
        bool right_click = false;

        while (XPending(display) > 0) {
            XEvent event;
            XNextEvent(display, &event);

            switch (event.type) {
            case ClientMessage:
                if ((Atom)event.xclient.data.l[0] == wm_delete_window) {
                    running = false;
                }
                break;

            case ConfigureNotify:
                if (event.xconfigure.width != (int)window_width ||
                    event.xconfigure.height != (int)window_height)
                {
                    swapchain_needs_recreate = true;
                }
                break;

            case KeyPress: {
                KeySym sym = XLookupKeysym(&event.xkey, 0);
                if (sym == XK_Escape) {
                    if (mouse_captured) {
                        XUngrabPointer(display, CurrentTime);
                        XUndefineCursor(display, window);
                        mouse_captured = false;
                        first_mouse = true;
                    }
                } else if (sym == XK_1) {
                    current_block_type = BLOCK_DIRT;
                } else if (sym == XK_2) {
                    current_block_type = BLOCK_STONE;
                } else if (sym < 256) {
                    keys[sym] = true;
                }
            } break;

            case KeyRelease: {
                KeySym sym = XLookupKeysym(&event.xkey, 0);
                if (sym < 256) {
                    keys[sym] = false;
                }
            } break;

            case MotionNotify:
                if (mouse_captured) {
                    mouse_x = (float)event.xmotion.x;
                    mouse_y = (float)event.xmotion.y;
                    mouse_moved = true;
                }
                break;

            case ButtonPress:
                if (event.xbutton.button == Button1) {
                    if (!mouse_captured) {
                        XGrabPointer(display, window, True, PointerMotionMask,
                                     GrabModeAsync, GrabModeAsync, window, None, CurrentTime);
                        XDefineCursor(display, window, invisible_cursor);
                        mouse_captured = true;
                        first_mouse = true;
                    } else {
                        left_click = true;
                    }
                } else if (event.xbutton.button == Button3) {
                    if (mouse_captured) {
                        right_click = true;
                    }
                }
                break;

            default:
                break;
            }
        }

        if (mouse_captured && mouse_moved) {
            int center_x = window_width / 2;
            int center_y = window_height / 2;

            if (first_mouse) {
                last_mouse_x = (float)center_x;
                last_mouse_y = (float)center_y;
                first_mouse = false;
            }

            float x_offset = mouse_x - last_mouse_x;
            float y_offset = last_mouse_y - mouse_y;

            camera_process_mouse(&camera, x_offset, y_offset);

            XWarpPointer(display, None, window, 0, 0, 0, 0, center_x, center_y);
            XFlush(display);

            last_mouse_x = (float)center_x;
            last_mouse_y = (float)center_y;
        }

        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);

        float delta_time =
            (now.tv_sec - last_time.tv_sec) +
            (now.tv_nsec - last_time.tv_nsec) / 1000000000.0f;
        last_time = now;

        Vec3 move_delta = vec3(0.0f, 0.0f, 0.0f);
        bool wants_jump = false;

        if (mouse_captured) {
            Vec3 forward = camera.front;
            forward.y = 0.0f;
            forward = vec3_normalize(forward);

            Vec3 right = camera.right;
            right.y = 0.0f;
            right = vec3_normalize(right);

            Vec3 movement_dir = vec3(0.0f, 0.0f, 0.0f);
            if (is_key_pressed(keys, 'w') || is_key_pressed(keys, 'W')) movement_dir = vec3_add(movement_dir, forward);
            if (is_key_pressed(keys, 's') || is_key_pressed(keys, 'S')) movement_dir = vec3_sub(movement_dir, forward);
            if (is_key_pressed(keys, 'a') || is_key_pressed(keys, 'A')) movement_dir = vec3_sub(movement_dir, right);
            if (is_key_pressed(keys, 'd') || is_key_pressed(keys, 'D')) movement_dir = vec3_add(movement_dir, right);

            if (vec3_length(movement_dir) > 0.0f) {
                movement_dir = vec3_normalize(movement_dir);
            }

            move_delta = vec3_scale(movement_dir, camera.movement_speed * delta_time);
            wants_jump = is_key_pressed(keys, ' ') || is_key_pressed(keys, XK_space);
        }

        if (wants_jump && player.on_ground) {
            player.velocity_y = JUMP_VELOCITY;
            player.on_ground = false;
        }

        player.velocity_y -= GRAVITY * delta_time;

        player.position.x += move_delta.x;
        resolve_collision_axis(&player.position, move_delta.x, 0, blocks, block_count);

        player.position.z += move_delta.z;
        resolve_collision_axis(&player.position, move_delta.z, 2, blocks, block_count);

        player.position.y += player.velocity_y * delta_time;
        resolve_collision_y(&player.position, &player.velocity_y, &player.on_ground,
                            blocks, block_count);

        camera.position = vec3_add(player.position, vec3(0.0f, EYE_HEIGHT, 0.0f));

        RayHit ray_hit = raycast_blocks(camera.position, camera.front, blocks, block_count, 6.0f);

        if (mouse_captured && (left_click || right_click)) {
            if (ray_hit.hit) {
                if (left_click) {
                    block_remove(blocks, &block_count, ray_hit.cell);
                }

                if (right_click) {
                    if (!(ray_hit.normal.x == 0 && ray_hit.normal.y == 0 && ray_hit.normal.z == 0)) {
                        IVec3 place = ivec3_add(ray_hit.cell, ray_hit.normal);
                        if (!block_overlaps_player(&player, place)) {
                            block_add(blocks, &block_count, MAX_BLOCKS, place, current_block_type);
                        }
                    }
                }
            }
        }

        if (swapchain_needs_recreate) {
            continue;
        }

        VK_CHECK(vkWaitForFences(device, 1, &in_flight, VK_TRUE, UINT64_MAX));
        VK_CHECK(vkResetFences(device, 1, &in_flight));

        uint32_t image_index = 0;
        VkResult acquire = vkAcquireNextImageKHR(device,
                                                 swapchain.swapchain,
                                                 UINT64_MAX,
                                                 image_available,
                                                 VK_NULL_HANDLE,
                                                 &image_index);

        if (acquire == VK_ERROR_OUT_OF_DATE_KHR) {
            swapchain_needs_recreate = true;
            continue;
        } else if (acquire != VK_SUCCESS && acquire != VK_SUBOPTIMAL_KHR) {
            die("Failed to acquire swapchain image");
        }

        Mat4 view = camera_view_matrix(&camera);
        Mat4 proj = mat4_perspective(55.0f * (float)M_PI / 180.0f,
                                     (float)swapchain.extent.width / (float)swapchain.extent.height,
                                     0.1f,
                                     100.0f);

        int highlight_index = block_count;
        int crosshair_index = block_count + 1;
        size_t mapped_size = (size_t)swapchain.ubo_stride * (size_t)(block_count + EXTRA_UBOS);

        void *ubo_ptr = NULL;
        VK_CHECK(vkMapMemory(device,
                             swapchain.uniform_memories[image_index],
                             0,
                             mapped_size,
                             0,
                             &ubo_ptr));
        char *base = (char *)ubo_ptr;

        for (int i = 0; i < block_count; ++i) {
            UniformBufferObject ubo = {0};
            ubo.model = mat4_translate(vec3((float)blocks[i].pos.x,
                                            (float)blocks[i].pos.y,
                                            (float)blocks[i].pos.z));
            ubo.view = view;
            ubo.proj = proj;
            ubo.block_type = blocks[i].type;

            memcpy(base + (size_t)i * swapchain.ubo_stride, &ubo, sizeof(ubo));
        }

        if (ray_hit.hit) {
            UniformBufferObject ubo = {0};
            ubo.model = mat4_translate(vec3((float)ray_hit.cell.x,
                                            (float)ray_hit.cell.y,
                                            (float)ray_hit.cell.z));
            ubo.view = view;
            ubo.proj = proj;
            ubo.block_type = 1;

            memcpy(base + (size_t)highlight_index * swapchain.ubo_stride, &ubo, sizeof(ubo));
        }

        UniformBufferObject cross_ubo = {0};
        cross_ubo.model = mat4_identity();
        cross_ubo.view = mat4_identity();
        cross_ubo.proj = mat4_identity();
        cross_ubo.block_type = 1;

        memcpy(base + (size_t)crosshair_index * swapchain.ubo_stride, &cross_ubo, sizeof(cross_ubo));
        vkUnmapMemory(device, swapchain.uniform_memories[image_index]);

        VK_CHECK(vkResetCommandBuffer(swapchain.command_buffers[image_index], 0));

        VkCommandBufferBeginInfo begin_info = {0};
        begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        VK_CHECK(vkBeginCommandBuffer(swapchain.command_buffers[image_index], &begin_info));

        VkClearValue clears[2];
        clears[0].color.float32[0] = 0.1f;
        clears[0].color.float32[1] = 0.12f;
        clears[0].color.float32[2] = 0.18f;
        clears[0].color.float32[3] = 1.0f;
        clears[1].depthStencil.depth = 1.0f;

        VkRenderPassBeginInfo rp_begin = {0};
        rp_begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rp_begin.renderPass = swapchain.render_pass;
        rp_begin.framebuffer = swapchain.framebuffers[image_index];
        rp_begin.renderArea.extent = swapchain.extent;
        rp_begin.clearValueCount = ARRAY_LENGTH(clears);
        rp_begin.pClearValues = clears;

        vkCmdBeginRenderPass(swapchain.command_buffers[image_index], &rp_begin, VK_SUBPASS_CONTENTS_INLINE);

        VkDeviceSize offsets = 0;

        vkCmdBindPipeline(swapchain.command_buffers[image_index], VK_PIPELINE_BIND_POINT_GRAPHICS, swapchain.pipeline_solid);
        vkCmdBindVertexBuffers(swapchain.command_buffers[image_index], 0, 1, &vertex_buffer, &offsets);
        vkCmdBindIndexBuffer(swapchain.command_buffers[image_index], index_buffer, 0, VK_INDEX_TYPE_UINT16);

        for (int i = 0; i < block_count; ++i) {
            uint32_t dynamic_offset = (uint32_t)((VkDeviceSize)i * swapchain.ubo_stride);
            vkCmdBindDescriptorSets(swapchain.command_buffers[image_index],
                                    VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    pipeline_layout,
                                    0,
                                    1,
                                    &swapchain.descriptor_sets_normal[image_index],
                                    1,
                                    &dynamic_offset);

            vkCmdDrawIndexed(swapchain.command_buffers[image_index], ARRAY_LENGTH(BLOCK_INDICES), 1, 0, 0, 0);
        }

        if (ray_hit.hit) {
            vkCmdBindPipeline(swapchain.command_buffers[image_index], VK_PIPELINE_BIND_POINT_GRAPHICS, swapchain.pipeline_wireframe);
            vkCmdBindVertexBuffers(swapchain.command_buffers[image_index], 0, 1, &edge_vertex_buffer, &offsets);
            vkCmdBindIndexBuffer(swapchain.command_buffers[image_index], edge_index_buffer, 0, VK_INDEX_TYPE_UINT16);

            uint32_t dynamic_offset = (uint32_t)((VkDeviceSize)highlight_index * swapchain.ubo_stride);

            vkCmdBindDescriptorSets(swapchain.command_buffers[image_index],
                                    VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    pipeline_layout,
                                    0,
                                    1,
                                    &swapchain.descriptor_sets_highlight[image_index],
                                    1,
                                    &dynamic_offset);

            vkCmdDrawIndexed(swapchain.command_buffers[image_index], ARRAY_LENGTH(EDGE_INDICES), 1, 0, 0, 0);
        }

        vkCmdBindPipeline(swapchain.command_buffers[image_index], VK_PIPELINE_BIND_POINT_GRAPHICS, swapchain.pipeline_crosshair);
        vkCmdBindVertexBuffers(swapchain.command_buffers[image_index], 0, 1, &crosshair_vertex_buffer, &offsets);

        uint32_t cross_offset = (uint32_t)((VkDeviceSize)crosshair_index * swapchain.ubo_stride);
        vkCmdBindDescriptorSets(swapchain.command_buffers[image_index],
                                VK_PIPELINE_BIND_POINT_GRAPHICS,
                                pipeline_layout,
                                0,
                                1,
                                &swapchain.descriptor_sets_highlight[image_index],
                                1,
                                &cross_offset);

        vkCmdDraw(swapchain.command_buffers[image_index], 4, 1, 0, 0);
        vkCmdEndRenderPass(swapchain.command_buffers[image_index]);

        VK_CHECK(vkEndCommandBuffer(swapchain.command_buffers[image_index]));

        VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

        VkSubmitInfo submit = {0};
        submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit.waitSemaphoreCount = 1;
        submit.pWaitSemaphores = &image_available;
        submit.pWaitDstStageMask = &wait_stage;
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &swapchain.command_buffers[image_index];
        submit.signalSemaphoreCount = 1;
        submit.pSignalSemaphores = &render_finished;

        VK_CHECK(vkQueueSubmit(graphics_queue, 1, &submit, in_flight));

        VkPresentInfoKHR present = {0};
        present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        present.waitSemaphoreCount = 1;
        present.pWaitSemaphores = &render_finished;
        present.swapchainCount = 1;
        present.pSwapchains = &swapchain.swapchain;
        present.pImageIndices = &image_index;

        VkResult present_result = vkQueuePresentKHR(graphics_queue, &present);
        if (present_result == VK_ERROR_OUT_OF_DATE_KHR || present_result == VK_SUBOPTIMAL_KHR) {
            swapchain_needs_recreate = true;
        } else if (present_result != VK_SUCCESS) {
            die("Failed to present swapchain image");
        }
    }

    /* ---------------------------------------------------------------------- */
    /* Cleanup                                                                 */
    /* ---------------------------------------------------------------------- */

    vkDeviceWaitIdle(device);

    swapchain_destroy(device, command_pool, &swapchain);

    vkDestroyFence(device, in_flight, NULL);
    vkDestroySemaphore(device, render_finished, NULL);
    vkDestroySemaphore(device, image_available, NULL);

    vkDestroyBuffer(device, crosshair_vertex_buffer, NULL);
    vkFreeMemory(device, crosshair_vertex_memory, NULL);

    vkDestroyBuffer(device, edge_vertex_buffer, NULL);
    vkFreeMemory(device, edge_vertex_memory, NULL);
    vkDestroyBuffer(device, edge_index_buffer, NULL);
    vkFreeMemory(device, edge_index_memory, NULL);

    vkDestroyBuffer(device, vertex_buffer, NULL);
    vkFreeMemory(device, vertex_memory, NULL);
    vkDestroyBuffer(device, index_buffer, NULL);
    vkFreeMemory(device, index_memory, NULL);

    texture_destroy(device, &textures[0]);
    texture_destroy(device, &textures[1]);
    texture_destroy(device, &black_texture);

    vkDestroyPipelineLayout(device, pipeline_layout, NULL);
    vkDestroyDescriptorSetLayout(device, descriptor_layout, NULL);

    vkDestroyShaderModule(device, vert_shader, NULL);
    vkDestroyShaderModule(device, frag_shader, NULL);

    vkDestroyCommandPool(device, command_pool, NULL);
    vkDestroyDevice(device, NULL);
    vkDestroySurfaceKHR(instance, surface, NULL);
    vkDestroyInstance(instance, NULL);

    free(blocks);

    XFreeCursor(display, invisible_cursor);
    XDestroyWindow(display, window);
    XCloseDisplay(display);
    
    return 0;
}