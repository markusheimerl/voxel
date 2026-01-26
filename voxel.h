#ifndef VOXEL_H
#define VOXEL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <vulkan/vulkan.h>
#include <png.h>

#include "math.h"
#include "world.h"

/* -------------------------------------------------------------------------- */
/* Vulkan Error Helpers                                                       */
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

const char *vk_result_to_string(VkResult result);
void die(const char *message);

/* -------------------------------------------------------------------------- */
/* Vulkan Buffer / Image Helpers                                              */
/* -------------------------------------------------------------------------- */
void create_buffer(VkDevice device,
                   VkPhysicalDevice physical_device,
                   VkDeviceSize size,
                   VkBufferUsageFlags usage,
                   VkMemoryPropertyFlags properties,
                   VkBuffer *buffer,
                   VkDeviceMemory *memory);

void upload_buffer_data(VkDevice device, VkDeviceMemory memory, const void *data, size_t size);

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

void texture_create_from_file(VkDevice device,
                              VkPhysicalDevice physical_device,
                              VkCommandPool command_pool,
                              VkQueue graphics_queue,
                              const char *filename,
                              Texture *texture);

void texture_create_solid(VkDevice device,
                          VkPhysicalDevice physical_device,
                          VkCommandPool command_pool,
                          VkQueue graphics_queue,
                          uint8_t r, uint8_t g, uint8_t b, uint8_t a,
                          Texture *texture);

void texture_destroy(VkDevice device, Texture *texture);

/* -------------------------------------------------------------------------- */
/* Shader Helpers                                                             */
/* -------------------------------------------------------------------------- */

VkShaderModule create_shader_module(VkDevice device, const char *filepath);

/* -------------------------------------------------------------------------- */
/* Vertex Formats                                                             */
/* -------------------------------------------------------------------------- */

typedef struct {
    Vec3 pos;
    Vec2 uv;
} Vertex;

typedef struct {
    float x, y, z;
    uint32_t type;
} InstanceData;

typedef struct {
    Mat4 view;
    Mat4 proj;
} PushConstants;

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
    VkPipeline pipeline_overlay;

    VkDescriptorPool descriptor_pool;
    VkDescriptorSet *descriptor_sets_normal;
    VkDescriptorSet *descriptor_sets_highlight;

    VkCommandBuffer *command_buffers;

    VkImage depth_image;
    VkDeviceMemory depth_memory;
    VkImageView depth_view;

    uint32_t image_count;
    VkExtent2D extent;
    VkFormat format;
} SwapchainResources;

void swapchain_resources_reset(SwapchainResources *res);

void swapchain_destroy(VkDevice device,
                       VkCommandPool command_pool,
                       SwapchainResources *res);

/* -------------------------------------------------------------------------- */
/* Swapchain creation                                                         */
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
    const Texture *textures;
    uint32_t texture_count;
    const Texture *black_texture;
} SwapchainContext;

void swapchain_create(SwapchainContext *ctx,
                      SwapchainResources *res,
                      uint32_t framebuffer_width,
                      uint32_t framebuffer_height);

#endif /* VOXEL_H */