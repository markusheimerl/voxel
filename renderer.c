#define VK_USE_PLATFORM_XLIB_KHR
#include <vulkan/vulkan.h>

#include <math.h>
#include <png.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "renderer.h"
#include "player.h"
#include "camera.h"

/* -------------------------------------------------------------------------- */
/* Constants and Types                                                        */
/* -------------------------------------------------------------------------- */

typedef struct {
    Mat4 view;
    Mat4 proj;
} PushConstants;

typedef struct {
    VkBuffer buffer;
    VkDeviceMemory memory;
} BufferObject;

/* -------------------------------------------------------------------------- */
/* Block Geometry Data                                                        */
/* -------------------------------------------------------------------------- */

static const Vertex BLOCK_VERTICES[] = {
    {{-0.5f, -0.5f,  0.5f}, {0.0f, 0.0f}}, {{ 0.5f, -0.5f,  0.5f}, {1.0f, 0.0f}},
    {{ 0.5f,  0.5f,  0.5f}, {1.0f, 1.0f}}, {{-0.5f,  0.5f,  0.5f}, {0.0f, 1.0f}},
    {{-0.5f, -0.5f, -0.5f}, {1.0f, 0.0f}}, {{ 0.5f, -0.5f, -0.5f}, {0.0f, 0.0f}},
    {{ 0.5f,  0.5f, -0.5f}, {0.0f, 1.0f}}, {{-0.5f,  0.5f, -0.5f}, {1.0f, 1.0f}},
    {{-0.5f,  0.5f, -0.5f}, {0.0f, 0.0f}}, {{ 0.5f,  0.5f, -0.5f}, {1.0f, 0.0f}},
    {{ 0.5f,  0.5f,  0.5f}, {1.0f, 1.0f}}, {{-0.5f,  0.5f,  0.5f}, {0.0f, 1.0f}},
    {{-0.5f, -0.5f, -0.5f}, {0.0f, 1.0f}}, {{ 0.5f, -0.5f, -0.5f}, {1.0f, 1.0f}},
    {{ 0.5f, -0.5f,  0.5f}, {1.0f, 0.0f}}, {{-0.5f, -0.5f,  0.5f}, {0.0f, 0.0f}},
    {{ 0.5f, -0.5f, -0.5f}, {0.0f, 0.0f}}, {{ 0.5f,  0.5f, -0.5f}, {1.0f, 0.0f}},
    {{ 0.5f,  0.5f,  0.5f}, {1.0f, 1.0f}}, {{ 0.5f, -0.5f,  0.5f}, {0.0f, 1.0f}},
    {{-0.5f, -0.5f, -0.5f}, {1.0f, 0.0f}}, {{-0.5f,  0.5f, -0.5f}, {0.0f, 0.0f}},
    {{-0.5f,  0.5f,  0.5f}, {0.0f, 1.0f}}, {{-0.5f, -0.5f,  0.5f}, {1.0f, 1.0f}},
};

static const uint16_t BLOCK_INDICES[] = {
     0,  1,  2,  2,  3,  0,  6,  5,  4,  4,  7,  6,
     8, 11, 10, 10,  9,  8, 12, 13, 14, 14, 15, 12,
    16, 17, 18, 18, 19, 16, 22, 21, 20, 20, 23, 22
};

static const Vertex EDGE_VERTICES[] = {
    {{-0.5f, -0.5f,  0.5f}, {0.0f, 0.0f}}, {{ 0.5f, -0.5f,  0.5f}, {0.0f, 0.0f}},
    {{ 0.5f,  0.5f,  0.5f}, {0.0f, 0.0f}}, {{-0.5f,  0.5f,  0.5f}, {0.0f, 0.0f}},
    {{-0.5f, -0.5f, -0.5f}, {0.0f, 0.0f}}, {{ 0.5f, -0.5f, -0.5f}, {0.0f, 0.0f}},
    {{ 0.5f,  0.5f, -0.5f}, {0.0f, 0.0f}}, {{-0.5f,  0.5f, -0.5f}, {0.0f, 0.0f}},
};

static const uint16_t EDGE_INDICES[] = {
    0, 1,  1, 2,  2, 3,  3, 0,
    4, 5,  5, 6,  6, 7,  7, 4,
    0, 4,  1, 5,  2, 6,  3, 7
};

static const char *TEXTURE_PATHS[ITEM_TYPE_COUNT] = {
    "textures/dirt.png", "textures/stone.png", "textures/grass.png",
    "textures/sand.png", "textures/water.png", "textures/wood.png",
    "textures/leaves.png", "textures/planks.png", "textures/stick.png"
};

/* -------------------------------------------------------------------------- */
/* Renderer Structure                                                         */
/* -------------------------------------------------------------------------- */

struct Renderer {
    VkInstance instance;
    VkSurfaceKHR surface;
    VkPhysicalDevice physical_device;
    VkDevice device;
    VkQueue graphics_queue;
    uint32_t graphics_family;
    VkCommandPool command_pool;

    VkImage textures[ITEM_TYPE_COUNT];
    VkDeviceMemory textures_memory[ITEM_TYPE_COUNT];
    VkImageView textures_view[ITEM_TYPE_COUNT];
    VkSampler textures_sampler[ITEM_TYPE_COUNT];

    BufferObject block_vertex, block_index;
    BufferObject edge_vertex, edge_index;
    BufferObject crosshair, inventory_grid, inventory_icon;
    BufferObject inventory_count, inventory_selection, inventory_bg;
    BufferObject crafting_grid, crafting_arrow, crafting_result;
    BufferObject instance_buf;
    BufferObject health_bar_bg, health_bar_border;
    uint32_t instance_capacity;

    VkDescriptorSetLayout descriptor_layout;
    VkPipelineLayout pipeline_layout;
    VkPipeline pipeline_solid, pipeline_wireframe, pipeline_crosshair, pipeline_overlay;

    VkSwapchainKHR swapchain;
    VkImageView *swapchain_views;
    VkFramebuffer *swapchain_framebuffers;
    VkRenderPass render_pass;
    uint32_t image_count;
    VkExtent2D extent;
    VkFormat surface_format;

    VkImage depth_image;
    VkDeviceMemory depth_memory;
    VkImageView depth_view;

    VkDescriptorPool descriptor_pool;
    VkDescriptorSet *descriptor_sets_normal;
    VkDescriptorSet *descriptor_sets_highlight;

    VkCommandBuffer *command_buffers;

    VkSemaphore image_available, render_finished;
    VkFence in_flight;
};

/* -------------------------------------------------------------------------- */
/* Error Handling                                                             */
/* -------------------------------------------------------------------------- */

_Noreturn static void die(const char *message) {
    fprintf(stderr, "Error: %s\n", message);
    exit(EXIT_FAILURE);
}

static const char *vk_result_string(VkResult result) {
    switch (result) {
    case VK_SUCCESS: return "VK_SUCCESS";
    case VK_ERROR_OUT_OF_HOST_MEMORY: return "VK_ERROR_OUT_OF_HOST_MEMORY";
    case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
    case VK_ERROR_DEVICE_LOST: return "VK_ERROR_DEVICE_LOST";
    case VK_ERROR_SURFACE_LOST_KHR: return "VK_ERROR_SURFACE_LOST_KHR";
    case VK_ERROR_OUT_OF_DATE_KHR: return "VK_ERROR_OUT_OF_DATE_KHR";
    case VK_SUBOPTIMAL_KHR: return "VK_SUBOPTIMAL_KHR";
    default: return "UNKNOWN_VULKAN_ERROR";
    }
}

#define VK_CHECK(call) do { \
    VkResult result__ = (call); \
    if (result__ != VK_SUCCESS) { \
        fprintf(stderr, "%s failed: %s\n", #call, vk_result_string(result__)); \
        exit(EXIT_FAILURE); \
    } \
} while (0)

/* -------------------------------------------------------------------------- */
/* Memory and Buffer Helpers                                                  */
/* -------------------------------------------------------------------------- */

static uint32_t find_memory_type(VkPhysicalDevice pd, uint32_t filter, VkMemoryPropertyFlags props) {
    VkPhysicalDeviceMemoryProperties mem_props;
    vkGetPhysicalDeviceMemoryProperties(pd, &mem_props);
    
    for (uint32_t i = 0; i < mem_props.memoryTypeCount; i++) {
        if ((filter & (1 << i)) && (mem_props.memoryTypes[i].propertyFlags & props) == props) {
            return i;
        }
    }
    die("Failed to find suitable memory type");
}

static void create_buffer(Renderer *r, VkDeviceSize size, VkBufferUsageFlags usage,
                          VkMemoryPropertyFlags props, VkBuffer *buf, VkDeviceMemory *mem) {
    VkBufferCreateInfo buf_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE
    };
    VK_CHECK(vkCreateBuffer(r->device, &buf_info, NULL, buf));

    VkMemoryRequirements mem_reqs;
    vkGetBufferMemoryRequirements(r->device, *buf, &mem_reqs);

    VkMemoryAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = mem_reqs.size,
        .memoryTypeIndex = find_memory_type(r->physical_device, mem_reqs.memoryTypeBits, props)
    };
    VK_CHECK(vkAllocateMemory(r->device, &alloc_info, NULL, mem));
    VK_CHECK(vkBindBufferMemory(r->device, *buf, *mem, 0));
}

static void upload_buffer_data(VkDevice dev, VkDeviceMemory mem, const void *data, VkDeviceSize size) {
    void *mapped;
    VK_CHECK(vkMapMemory(dev, mem, 0, size, 0, &mapped));
    memcpy(mapped, data, size);
    vkUnmapMemory(dev, mem);
}

static void create_and_upload_buffer(Renderer *r, BufferObject *obj, const void *data,
                                     VkDeviceSize size, VkBufferUsageFlags usage) {
    VkMemoryPropertyFlags props = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    create_buffer(r, size, usage, props, &obj->buffer, &obj->memory);
    if (data) upload_buffer_data(r->device, obj->memory, data, size);
}

static void destroy_buffer_object(VkDevice dev, BufferObject *obj) {
    if (obj->buffer) vkDestroyBuffer(dev, obj->buffer, NULL);
    if (obj->memory) vkFreeMemory(dev, obj->memory, NULL);
}

static void create_image(Renderer *r, uint32_t w, uint32_t h, VkFormat fmt, VkImageTiling tiling,
                         VkImageUsageFlags usage, VkMemoryPropertyFlags props,
                         VkImage *img, VkDeviceMemory *mem) {
    VkImageCreateInfo img_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .extent = {w, h, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .format = fmt,
        .tiling = tiling,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .usage = usage,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE
    };
    VK_CHECK(vkCreateImage(r->device, &img_info, NULL, img));

    VkMemoryRequirements mem_reqs;
    vkGetImageMemoryRequirements(r->device, *img, &mem_reqs);

    VkMemoryAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = mem_reqs.size,
        .memoryTypeIndex = find_memory_type(r->physical_device, mem_reqs.memoryTypeBits, props)
    };
    VK_CHECK(vkAllocateMemory(r->device, &alloc_info, NULL, mem));
    VK_CHECK(vkBindImageMemory(r->device, *img, *mem, 0));
}

/* -------------------------------------------------------------------------- */
/* Command Buffer Helpers                                                     */
/* -------------------------------------------------------------------------- */

static VkCommandBuffer begin_single_time_commands(Renderer *r) {
    VkCommandBufferAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = r->command_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1
    };
    
    VkCommandBuffer cmd;
    VK_CHECK(vkAllocateCommandBuffers(r->device, &alloc_info, &cmd));
    
    VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
    };
    VK_CHECK(vkBeginCommandBuffer(cmd, &begin_info));
    return cmd;
}

static void end_single_time_commands(Renderer *r, VkCommandBuffer cmd) {
    VK_CHECK(vkEndCommandBuffer(cmd));
    
    VkSubmitInfo submit_info = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &cmd
    };
    VK_CHECK(vkQueueSubmit(r->graphics_queue, 1, &submit_info, VK_NULL_HANDLE));
    VK_CHECK(vkQueueWaitIdle(r->graphics_queue));
    
    vkFreeCommandBuffers(r->device, r->command_pool, 1, &cmd);
}

static void transition_image_layout(Renderer *r, VkImage img, VkImageLayout old_layout, VkImageLayout new_layout) {
    VkCommandBuffer cmd = begin_single_time_commands(r);
    
    VkImageMemoryBarrier barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .oldLayout = old_layout,
        .newLayout = new_layout,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = img,
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}
    };
    
    VkPipelineStageFlags src_stage, dst_stage;
    
    if (old_layout == VK_IMAGE_LAYOUT_UNDEFINED && new_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        src_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dst_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (old_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && new_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        src_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        dst_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else {
        die("Unsupported layout transition");
    }
    
    vkCmdPipelineBarrier(cmd, src_stage, dst_stage, 0, 0, NULL, 0, NULL, 1, &barrier);
    end_single_time_commands(r, cmd);
}

static void copy_buffer_to_image(Renderer *r, VkBuffer buf, VkImage img, uint32_t w, uint32_t h) {
    VkCommandBuffer cmd = begin_single_time_commands(r);
    
    VkBufferImageCopy region = {
        .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
        .imageExtent = {w, h, 1}
    };
    vkCmdCopyBufferToImage(cmd, buf, img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    
    end_single_time_commands(r, cmd);
}

/* -------------------------------------------------------------------------- */
/* Texture Loading                                                            */
/* -------------------------------------------------------------------------- */

static void load_png_data(const char *filename, uint8_t **pixels, uint32_t *w, uint32_t *h) {
    FILE *fp = fopen(filename, "rb");
    if (!fp) die("Failed to open texture file");
    
    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png) { fclose(fp); die("PNG error"); }
    
    png_infop info = png_create_info_struct(png);
    if (!info) { png_destroy_read_struct(&png, NULL, NULL); fclose(fp); die("PNG error"); }
    
    if (setjmp(png_jmpbuf(png))) {
        png_destroy_read_struct(&png, &info, NULL);
        fclose(fp);
        die("PNG read error");
    }
    
    png_init_io(png, fp);
    png_read_info(png, info);
    
    *w = png_get_image_width(png, info);
    *h = png_get_image_height(png, info);
    
    if (png_get_bit_depth(png, info) == 16) png_set_strip_16(png);
    if (png_get_color_type(png, info) == PNG_COLOR_TYPE_PALETTE) png_set_palette_to_rgb(png);
    if (png_get_color_type(png, info) == PNG_COLOR_TYPE_GRAY && png_get_bit_depth(png, info) < 8) 
        png_set_expand_gray_1_2_4_to_8(png);
    if (png_get_valid(png, info, PNG_INFO_tRNS)) png_set_tRNS_to_alpha(png);
    if (png_get_color_type(png, info) == PNG_COLOR_TYPE_RGB ||
        png_get_color_type(png, info) == PNG_COLOR_TYPE_GRAY ||
        png_get_color_type(png, info) == PNG_COLOR_TYPE_PALETTE) {
        png_set_filler(png, 0xFF, PNG_FILLER_AFTER);
    }
    if (png_get_color_type(png, info) == PNG_COLOR_TYPE_GRAY || 
        png_get_color_type(png, info) == PNG_COLOR_TYPE_GRAY_ALPHA) {
        png_set_gray_to_rgb(png);
    }
    
    png_read_update_info(png, info);
    
    size_t row_bytes = png_get_rowbytes(png, info);
    *pixels = malloc(row_bytes * (*h));
    png_bytep *row_ptrs = malloc(sizeof(png_bytep) * (*h));
    
    for (uint32_t y = 0; y < *h; y++) row_ptrs[y] = (*pixels) + y * row_bytes;
    
    png_read_image(png, row_ptrs);
    png_read_end(png, NULL);
    
    free(row_ptrs);
    png_destroy_read_struct(&png, &info, NULL);
    fclose(fp);
}

static void load_texture(Renderer *r, const char *filename, VkImage *img, VkDeviceMemory *mem,
                         VkImageView *view, VkSampler *sampler) {
    uint8_t *pixels;
    uint32_t w, h;
    load_png_data(filename, &pixels, &w, &h);
    
    VkDeviceSize img_size = w * h * 4;
    
    VkBuffer staging_buf;
    VkDeviceMemory staging_mem;
    create_buffer(r, img_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                  &staging_buf, &staging_mem);
    
    upload_buffer_data(r->device, staging_mem, pixels, img_size);
    free(pixels);
    
    create_image(r, w, h, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_TILING_OPTIMAL,
                 VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, img, mem);
    
    transition_image_layout(r, *img, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    copy_buffer_to_image(r, staging_buf, *img, w, h);
    transition_image_layout(r, *img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    
    vkDestroyBuffer(r->device, staging_buf, NULL);
    vkFreeMemory(r->device, staging_mem, NULL);
    
    VkImageViewCreateInfo view_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = *img,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_SRGB,
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}
    };
    VK_CHECK(vkCreateImageView(r->device, &view_info, NULL, view));
    
    VkSamplerCreateInfo sampler_info = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_NEAREST,
        .minFilter = VK_FILTER_NEAREST,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST
    };
    VK_CHECK(vkCreateSampler(r->device, &sampler_info, NULL, sampler));
}

/* -------------------------------------------------------------------------- */
/* Shader Loading                                                             */
/* -------------------------------------------------------------------------- */

static VkShaderModule load_shader(VkDevice dev, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) die("Failed to open shader");
    
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    char *code = malloc(size);
    if (fread(code, 1, size, f) != (size_t)size) {
        free(code);
        fclose(f);
        die("Failed to read shader");
    }
    fclose(f);
    
    VkShaderModuleCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = size,
        .pCode = (const uint32_t *)code
    };
    
    VkShaderModule module;
    VK_CHECK(vkCreateShaderModule(dev, &create_info, NULL, &module));
    free(code);
    return module;
}

/* -------------------------------------------------------------------------- */
/* Pipeline Creation                                                          */
/* -------------------------------------------------------------------------- */

static VkPipeline create_graphics_pipeline(Renderer *r, VkShaderModule vert, VkShaderModule frag,
                                           VkPrimitiveTopology topology, VkPolygonMode polygon_mode,
                                           VkCullModeFlags cull, bool depth_test, bool depth_write,
                                           bool enable_blend) {
    VkPipelineShaderStageCreateInfo stages[2] = {
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = vert, .pName = "main"},
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = frag, .pName = "main"}
    };
    
    VkVertexInputBindingDescription bindings[2] = {
        {.binding = 0, .stride = sizeof(Vertex), .inputRate = VK_VERTEX_INPUT_RATE_VERTEX},
        {.binding = 1, .stride = sizeof(InstanceData), .inputRate = VK_VERTEX_INPUT_RATE_INSTANCE}
    };
    
    VkVertexInputAttributeDescription attrs[4] = {
        {.binding = 0, .location = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(Vertex, pos)},
        {.binding = 0, .location = 1, .format = VK_FORMAT_R32G32_SFLOAT, .offset = offsetof(Vertex, uv)},
        {.binding = 1, .location = 2, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(InstanceData, x)},
        {.binding = 1, .location = 3, .format = VK_FORMAT_R32_UINT, .offset = offsetof(InstanceData, type)}
    };
    
    VkPipelineVertexInputStateCreateInfo vertex_input = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = 2, .pVertexBindingDescriptions = bindings,
        .vertexAttributeDescriptionCount = 4, .pVertexAttributeDescriptions = attrs
    };
    
    VkPipelineInputAssemblyStateCreateInfo input_assembly = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = topology
    };
    
    VkViewport viewport = {.width = (float)r->extent.width, .height = (float)r->extent.height, .maxDepth = 1.0f};
    VkRect2D scissor = {.extent = r->extent};
    
    VkPipelineViewportStateCreateInfo viewport_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1, .pViewports = &viewport,
        .scissorCount = 1, .pScissors = &scissor
    };
    
    VkPipelineRasterizationStateCreateInfo rasterizer = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = polygon_mode,
        .cullMode = cull,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .lineWidth = 3.0f
    };
    
    VkPipelineMultisampleStateCreateInfo multisampling = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT
    };
    
    VkPipelineDepthStencilStateCreateInfo depth_stencil = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = depth_test,
        .depthWriteEnable = depth_write,
        .depthCompareOp = depth_write ? VK_COMPARE_OP_LESS : VK_COMPARE_OP_LESS_OR_EQUAL
    };
    
    VkPipelineColorBlendAttachmentState blend_attach = {
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
        .blendEnable = enable_blend,
        .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .colorBlendOp = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
        .alphaBlendOp = VK_BLEND_OP_ADD
    };
    
    VkPipelineColorBlendStateCreateInfo color_blending = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &blend_attach
    };
    
    VkGraphicsPipelineCreateInfo pipeline_info = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = 2, .pStages = stages,
        .pVertexInputState = &vertex_input,
        .pInputAssemblyState = &input_assembly,
        .pViewportState = &viewport_state,
        .pRasterizationState = &rasterizer,
        .pMultisampleState = &multisampling,
        .pDepthStencilState = &depth_stencil,
        .pColorBlendState = &color_blending,
        .layout = r->pipeline_layout,
        .renderPass = r->render_pass
    };
    
    VkPipeline pipeline;
    VK_CHECK(vkCreateGraphicsPipelines(r->device, VK_NULL_HANDLE, 1, &pipeline_info, NULL, &pipeline));
    return pipeline;
}

/* -------------------------------------------------------------------------- */
/* Initialization Helpers                                                     */
/* -------------------------------------------------------------------------- */

static void init_instance_and_surface(Renderer *r, void *display, unsigned long window) {
    const char *inst_exts[] = {VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_XLIB_SURFACE_EXTENSION_NAME};
    VkApplicationInfo app_info = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "Voxel Engine",
        .apiVersion = VK_API_VERSION_1_1
    };
    VkInstanceCreateInfo inst_info = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &app_info,
        .enabledExtensionCount = 2,
        .ppEnabledExtensionNames = inst_exts
    };
    VK_CHECK(vkCreateInstance(&inst_info, NULL, &r->instance));
    
    VkXlibSurfaceCreateInfoKHR surf_info = {
        .sType = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR,
        .dpy = (Display *)display,
        .window = (Window)window
    };
    VK_CHECK(vkCreateXlibSurfaceKHR(r->instance, &surf_info, NULL, &r->surface));
}

static void init_physical_device(Renderer *r) {
    uint32_t dev_count = 0;
    VK_CHECK(vkEnumeratePhysicalDevices(r->instance, &dev_count, NULL));
    if (dev_count == 0) die("No Vulkan devices");
    
    VkPhysicalDevice *devs = malloc(sizeof(VkPhysicalDevice) * dev_count);
    VK_CHECK(vkEnumeratePhysicalDevices(r->instance, &dev_count, devs));
    
    r->graphics_family = UINT32_MAX;
    for (uint32_t i = 0; i < dev_count; i++) {
        uint32_t q_count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(devs[i], &q_count, NULL);
        
        VkQueueFamilyProperties *queues = malloc(sizeof(VkQueueFamilyProperties) * q_count);
        vkGetPhysicalDeviceQueueFamilyProperties(devs[i], &q_count, queues);
        
        for (uint32_t j = 0; j < q_count; j++) {
            VkBool32 present = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(devs[i], j, r->surface, &present);
            
            if ((queues[j].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present) {
                r->physical_device = devs[i];
                r->graphics_family = j;
                break;
            }
        }
        free(queues);
        if (r->physical_device != VK_NULL_HANDLE) break;
    }
    free(devs);
    if (r->physical_device == VK_NULL_HANDLE) die("No suitable GPU");
}

static void init_device_and_queue(Renderer *r) {
    float queue_priority = 1.0f;
    VkDeviceQueueCreateInfo queue_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = r->graphics_family,
        .queueCount = 1,
        .pQueuePriorities = &queue_priority
    };
    
    const char *dev_exts[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    
    VkPhysicalDeviceFeatures supported_feats, enabled_feats = {0};
    vkGetPhysicalDeviceFeatures(r->physical_device, &supported_feats);
    enabled_feats.wideLines = supported_feats.wideLines;
    
    VkDeviceCreateInfo dev_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = 1, .pQueueCreateInfos = &queue_info,
        .enabledExtensionCount = 1, .ppEnabledExtensionNames = dev_exts,
        .pEnabledFeatures = &enabled_feats
    };
    VK_CHECK(vkCreateDevice(r->physical_device, &dev_info, NULL, &r->device));
    
    vkGetDeviceQueue(r->device, r->graphics_family, 0, &r->graphics_queue);
    
    VkCommandPoolCreateInfo pool_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .queueFamilyIndex = r->graphics_family,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT
    };
    VK_CHECK(vkCreateCommandPool(r->device, &pool_info, NULL, &r->command_pool));
}

static void init_textures(Renderer *r) {
    for (uint32_t i = 0; i < ITEM_TYPE_COUNT; i++) {
        load_texture(r, TEXTURE_PATHS[i], &r->textures[i], &r->textures_memory[i],
                     &r->textures_view[i], &r->textures_sampler[i]);
    }
}

static void init_static_buffers(Renderer *r) {
    create_and_upload_buffer(r, &r->block_vertex, BLOCK_VERTICES, sizeof(BLOCK_VERTICES), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    create_and_upload_buffer(r, &r->block_index, BLOCK_INDICES, sizeof(BLOCK_INDICES), VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
    create_and_upload_buffer(r, &r->edge_vertex, EDGE_VERTICES, sizeof(EDGE_VERTICES), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    create_and_upload_buffer(r, &r->edge_index, EDGE_INDICES, sizeof(EDGE_INDICES), VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
}

static void init_ui_buffers(Renderer *r, float aspect) {
    create_and_upload_buffer(r, &r->crosshair, NULL, sizeof(Vertex) * 4, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    create_and_upload_buffer(r, &r->inventory_grid, NULL, sizeof(Vertex) * 32, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    create_and_upload_buffer(r, &r->inventory_icon, NULL, sizeof(Vertex) * 6, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    create_and_upload_buffer(r, &r->inventory_count, NULL, sizeof(Vertex) * 1500, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    create_and_upload_buffer(r, &r->inventory_selection, NULL, sizeof(Vertex) * 8, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    create_and_upload_buffer(r, &r->inventory_bg, NULL, sizeof(Vertex) * 18, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    create_and_upload_buffer(r, &r->crafting_grid, NULL, sizeof(Vertex) * 32, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    create_and_upload_buffer(r, &r->crafting_arrow, NULL, sizeof(Vertex) * 16, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    create_and_upload_buffer(r, &r->crafting_result, NULL, sizeof(Vertex) * 16, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    create_and_upload_buffer(r, &r->health_bar_bg, NULL, sizeof(Vertex) * 60, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    create_and_upload_buffer(r, &r->health_bar_border, NULL, sizeof(Vertex) * 80, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    
    /* Initialize crosshair */
    float ch_size = 0.03f;
    Vertex ch_verts[4] = {
        {{-ch_size * aspect, 0, 0}, {0, 0}}, {{ ch_size * aspect, 0, 0}, {1, 0}},
        {{0, -ch_size, 0}, {0, 0}}, {{0,  ch_size, 0}, {1, 0}}
    };
    upload_buffer_data(r->device, r->crosshair.memory, ch_verts, sizeof(ch_verts));
    
    /* Initialize inventory grid */
    float h_step, v_step;
    Vertex grid_verts[32];
    uint32_t grid_count;
    player_inventory_grid_vertices(aspect, grid_verts, 32, &grid_count, &h_step, &v_step);
    upload_buffer_data(r->device, r->inventory_grid.memory, grid_verts, grid_count * sizeof(Vertex));
    
    /* Initialize crafting UI */
    Vertex verts[32];
    uint32_t count;
    player_crafting_grid_vertices(aspect, verts, 32, &count);
    upload_buffer_data(r->device, r->crafting_grid.memory, verts, count * sizeof(Vertex));
    
    player_crafting_arrow_vertices(aspect, verts, 16, &count);
    upload_buffer_data(r->device, r->crafting_arrow.memory, verts, count * sizeof(Vertex));
    
    player_crafting_result_slot_vertices(aspect, verts, 16, &count);
    upload_buffer_data(r->device, r->crafting_result.memory, verts, count * sizeof(Vertex));

    /* Initialize health bar border (static) */
    Vertex health_border[80];
    uint32_t health_border_count;
    player_health_bar_border_vertices(aspect, health_border, 80, &health_border_count);
    upload_buffer_data(r->device, r->health_bar_border.memory, health_border,
                       health_border_count * sizeof(Vertex));

    /* Initialize health bar background */
    Player temp_player = {.health = 10};
    Vertex health_bg[60];
    uint32_t health_bg_count;
    player_health_bar_background_vertices(&temp_player, aspect, health_bg, 60, &health_bg_count);
    upload_buffer_data(r->device, r->health_bar_bg.memory, health_bg,
                       health_bg_count * sizeof(Vertex));
    
    /* Initialize inventory icon quad */
    Vertex icon_verts[6];
    uint32_t icon_count;
    player_inventory_icon_vertices(h_step, v_step, icon_verts, 6, &icon_count);
    upload_buffer_data(r->device, r->inventory_icon.memory, icon_verts, icon_count * sizeof(Vertex));
}

static void init_instance_buffer(Renderer *r) {
    r->instance_capacity = INITIAL_INSTANCE_CAPACITY;
    create_and_upload_buffer(r, &r->instance_buf, NULL, r->instance_capacity * sizeof(InstanceData), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
}

static void init_descriptor_layout(Renderer *r) {
    VkDescriptorSetLayoutBinding sampler_binding = {
        .binding = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = ITEM_TYPE_COUNT,
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT
    };
    
    VkDescriptorSetLayoutCreateInfo layout_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1,
        .pBindings = &sampler_binding
    };
    VK_CHECK(vkCreateDescriptorSetLayout(r->device, &layout_info, NULL, &r->descriptor_layout));
}

static void init_pipeline_layout(Renderer *r) {
    VkPushConstantRange push_range = {
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
        .offset = 0,
        .size = sizeof(PushConstants)
    };
    
    VkPipelineLayoutCreateInfo pipe_layout_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1, .pSetLayouts = &r->descriptor_layout,
        .pushConstantRangeCount = 1, .pPushConstantRanges = &push_range
    };
    VK_CHECK(vkCreatePipelineLayout(r->device, &pipe_layout_info, NULL, &r->pipeline_layout));
}

static void init_swapchain(Renderer *r, uint32_t fb_w, uint32_t fb_h) {
    VkSurfaceCapabilitiesKHR caps;
    VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(r->physical_device, r->surface, &caps));
    
    uint32_t fmt_count;
    VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(r->physical_device, r->surface, &fmt_count, NULL));
    VkSurfaceFormatKHR *fmts = malloc(sizeof(VkSurfaceFormatKHR) * fmt_count);
    VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(r->physical_device, r->surface, &fmt_count, fmts));
    
    r->surface_format = VK_FORMAT_B8G8R8A8_SRGB;
    for (uint32_t i = 0; i < fmt_count; i++) {
        if (fmts[i].format == VK_FORMAT_B8G8R8A8_SRGB &&
            fmts[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) break;
    }
    free(fmts);
    
    r->image_count = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && r->image_count > caps.maxImageCount) {
        r->image_count = caps.maxImageCount;
    }
    
    r->extent = caps.currentExtent;
    if (r->extent.width == UINT32_MAX) {
        r->extent.width = fb_w;
        r->extent.height = fb_h;
    }
    
    VkSwapchainCreateInfoKHR swap_info = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = r->surface,
        .minImageCount = r->image_count,
        .imageFormat = r->surface_format,
        .imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
        .imageExtent = r->extent,
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .preTransform = caps.currentTransform,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = VK_PRESENT_MODE_FIFO_KHR,
        .clipped = VK_TRUE
    };
    VK_CHECK(vkCreateSwapchainKHR(r->device, &swap_info, NULL, &r->swapchain));
    
    VK_CHECK(vkGetSwapchainImagesKHR(r->device, r->swapchain, &r->image_count, NULL));
    VkImage *imgs = malloc(sizeof(VkImage) * r->image_count);
    VK_CHECK(vkGetSwapchainImagesKHR(r->device, r->swapchain, &r->image_count, imgs));
    
    r->swapchain_views = malloc(sizeof(VkImageView) * r->image_count);
    for (uint32_t i = 0; i < r->image_count; i++) {
        VkImageViewCreateInfo view_info = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = imgs[i],
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = r->surface_format,
            .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}
        };
        VK_CHECK(vkCreateImageView(r->device, &view_info, NULL, &r->swapchain_views[i]));
    }
    free(imgs);
}

static void init_depth_buffer(Renderer *r) {
    create_image(r, r->extent.width, r->extent.height, VK_FORMAT_D32_SFLOAT,
                 VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &r->depth_image, &r->depth_memory);
    
    VkImageViewCreateInfo depth_view_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = r->depth_image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = VK_FORMAT_D32_SFLOAT,
        .subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1}
    };
    VK_CHECK(vkCreateImageView(r->device, &depth_view_info, NULL, &r->depth_view));
}

static void init_render_pass(Renderer *r) {
    VkAttachmentDescription attachments[2] = {
        {
            .format = r->surface_format, .samples = VK_SAMPLE_COUNT_1_BIT,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR, .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE, .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED, .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
        },
        {
            .format = VK_FORMAT_D32_SFLOAT, .samples = VK_SAMPLE_COUNT_1_BIT,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR, .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE, .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED, .finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
        }
    };
    
    VkAttachmentReference color_ref = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference depth_ref = {1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    
    VkSubpassDescription subpass = {
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = 1, .pColorAttachments = &color_ref,
        .pDepthStencilAttachment = &depth_ref
    };
    
    VkSubpassDependency dependency = {
        .srcSubpass = VK_SUBPASS_EXTERNAL, .dstSubpass = 0,
        .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
        .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT
    };
    
    VkRenderPassCreateInfo rp_info = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = 2, .pAttachments = attachments,
        .subpassCount = 1, .pSubpasses = &subpass,
        .dependencyCount = 1, .pDependencies = &dependency
    };
    VK_CHECK(vkCreateRenderPass(r->device, &rp_info, NULL, &r->render_pass));
}

static void init_pipelines(Renderer *r) {
    VkShaderModule vert = load_shader(r->device, "shaders/vert.spv");
    VkShaderModule frag = load_shader(r->device, "shaders/frag.spv");
    
    r->pipeline_solid = create_graphics_pipeline(r, vert, frag, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
                                                  VK_POLYGON_MODE_FILL, VK_CULL_MODE_BACK_BIT, true, true, false);
    
    r->pipeline_wireframe = create_graphics_pipeline(r, vert, frag, VK_PRIMITIVE_TOPOLOGY_LINE_LIST,
                                                      VK_POLYGON_MODE_LINE, VK_CULL_MODE_NONE, true, false, false);
    
    r->pipeline_crosshair = create_graphics_pipeline(r, vert, frag, VK_PRIMITIVE_TOPOLOGY_LINE_LIST,
                                                      VK_POLYGON_MODE_LINE, VK_CULL_MODE_NONE, false, false, false);
    
    r->pipeline_overlay = create_graphics_pipeline(r, vert, frag, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
                                                    VK_POLYGON_MODE_FILL, VK_CULL_MODE_BACK_BIT, false, false, true);
    
    vkDestroyShaderModule(r->device, vert, NULL);
    vkDestroyShaderModule(r->device, frag, NULL);
}

static void init_framebuffers(Renderer *r) {
    r->swapchain_framebuffers = malloc(sizeof(VkFramebuffer) * r->image_count);
    for (uint32_t i = 0; i < r->image_count; i++) {
        VkImageView atts[] = {r->swapchain_views[i], r->depth_view};
        
        VkFramebufferCreateInfo fb_info = {
            .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            .renderPass = r->render_pass,
            .attachmentCount = 2, .pAttachments = atts,
            .width = r->extent.width, .height = r->extent.height,
            .layers = 1
        };
        VK_CHECK(vkCreateFramebuffer(r->device, &fb_info, NULL, &r->swapchain_framebuffers[i]));
    }
}

static void init_descriptor_sets(Renderer *r) {
    VkDescriptorPoolSize pool_size = {
        .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = r->image_count * ITEM_TYPE_COUNT * 2
    };
    
    VkDescriptorPoolCreateInfo desc_pool_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .poolSizeCount = 1, .pPoolSizes = &pool_size,
        .maxSets = r->image_count * 2
    };
    VK_CHECK(vkCreateDescriptorPool(r->device, &desc_pool_info, NULL, &r->descriptor_pool));
    
    VkDescriptorSetLayout *layouts = malloc(sizeof(VkDescriptorSetLayout) * r->image_count);
    for (uint32_t i = 0; i < r->image_count; i++) layouts[i] = r->descriptor_layout;
    
    VkDescriptorSetAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = r->descriptor_pool,
        .descriptorSetCount = r->image_count,
        .pSetLayouts = layouts
    };
    
    r->descriptor_sets_normal = malloc(sizeof(VkDescriptorSet) * r->image_count);
    r->descriptor_sets_highlight = malloc(sizeof(VkDescriptorSet) * r->image_count);
    
    VK_CHECK(vkAllocateDescriptorSets(r->device, &alloc_info, r->descriptor_sets_normal));
    VK_CHECK(vkAllocateDescriptorSets(r->device, &alloc_info, r->descriptor_sets_highlight));
    
    free(layouts);
    
    for (uint32_t i = 0; i < r->image_count; i++) {
        VkDescriptorImageInfo img_infos[ITEM_TYPE_COUNT];
        
        for (uint32_t j = 0; j < ITEM_TYPE_COUNT; j++) {
            img_infos[j].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            img_infos[j].imageView = r->textures_view[j];
            img_infos[j].sampler = r->textures_sampler[j];
        }
        
        VkWriteDescriptorSet write = {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstBinding = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = ITEM_TYPE_COUNT,
            .pImageInfo = img_infos
        };
        
        write.dstSet = r->descriptor_sets_normal[i];
        vkUpdateDescriptorSets(r->device, 1, &write, 0, NULL);
        
        write.dstSet = r->descriptor_sets_highlight[i];
        vkUpdateDescriptorSets(r->device, 1, &write, 0, NULL);
    }
}

static void init_command_buffers(Renderer *r) {
    r->command_buffers = malloc(sizeof(VkCommandBuffer) * r->image_count);
    VkCommandBufferAllocateInfo cmd_alloc = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = r->command_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = r->image_count
    };
    VK_CHECK(vkAllocateCommandBuffers(r->device, &cmd_alloc, r->command_buffers));
}

static void init_sync_objects(Renderer *r) {
    VkSemaphoreCreateInfo sem_info = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VkFenceCreateInfo fence_info = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, .flags = VK_FENCE_CREATE_SIGNALED_BIT};
    
    VK_CHECK(vkCreateSemaphore(r->device, &sem_info, NULL, &r->image_available));
    VK_CHECK(vkCreateSemaphore(r->device, &sem_info, NULL, &r->render_finished));
    VK_CHECK(vkCreateFence(r->device, &fence_info, NULL, &r->in_flight));
}

/* -------------------------------------------------------------------------- */
/* Renderer Creation                                                          */
/* -------------------------------------------------------------------------- */

Renderer *renderer_create(void *display, unsigned long window, uint32_t width, uint32_t height) {
    Renderer *r = calloc(1, sizeof(*r));
    if (!r) die("Failed to allocate renderer");
    
    float aspect = (float)height / (float)width;
    
    init_instance_and_surface(r, display, window);
    init_physical_device(r);
    init_device_and_queue(r);
    init_textures(r);
    init_static_buffers(r);
    init_ui_buffers(r, aspect);
    init_instance_buffer(r);
    init_descriptor_layout(r);
    init_pipeline_layout(r);
    init_swapchain(r, width, height);
    init_depth_buffer(r);
    init_render_pass(r);
    init_pipelines(r);
    init_framebuffers(r);
    init_descriptor_sets(r);
    init_command_buffers(r);
    init_sync_objects(r);
    
    return r;
}

/* -------------------------------------------------------------------------- */
/* Renderer Destruction                                                       */
/* -------------------------------------------------------------------------- */

void renderer_destroy(Renderer *r) {
    if (!r) return;
    
    vkDeviceWaitIdle(r->device);
    
    vkDestroyFence(r->device, r->in_flight, NULL);
    vkDestroySemaphore(r->device, r->render_finished, NULL);
    vkDestroySemaphore(r->device, r->image_available, NULL);
    
    vkFreeCommandBuffers(r->device, r->command_pool, r->image_count, r->command_buffers);
    free(r->command_buffers);
    
    vkDestroyDescriptorPool(r->device, r->descriptor_pool, NULL);
    free(r->descriptor_sets_normal);
    free(r->descriptor_sets_highlight);
    
    for (uint32_t i = 0; i < r->image_count; i++) {
        vkDestroyFramebuffer(r->device, r->swapchain_framebuffers[i], NULL);
    }
    free(r->swapchain_framebuffers);
    
    vkDestroyPipeline(r->device, r->pipeline_overlay, NULL);
    vkDestroyPipeline(r->device, r->pipeline_crosshair, NULL);
    vkDestroyPipeline(r->device, r->pipeline_wireframe, NULL);
    vkDestroyPipeline(r->device, r->pipeline_solid, NULL);
    
    vkDestroyRenderPass(r->device, r->render_pass, NULL);
    
    vkDestroyImageView(r->device, r->depth_view, NULL);
    vkDestroyImage(r->device, r->depth_image, NULL);
    vkFreeMemory(r->device, r->depth_memory, NULL);
    
    for (uint32_t i = 0; i < r->image_count; i++) {
        vkDestroyImageView(r->device, r->swapchain_views[i], NULL);
    }
    free(r->swapchain_views);
    
    vkDestroySwapchainKHR(r->device, r->swapchain, NULL);
    
    vkDestroyPipelineLayout(r->device, r->pipeline_layout, NULL);
    vkDestroyDescriptorSetLayout(r->device, r->descriptor_layout, NULL);
    
    destroy_buffer_object(r->device, &r->instance_buf);
    destroy_buffer_object(r->device, &r->health_bar_border);
    destroy_buffer_object(r->device, &r->health_bar_bg);
    destroy_buffer_object(r->device, &r->crafting_result);
    destroy_buffer_object(r->device, &r->crafting_arrow);
    destroy_buffer_object(r->device, &r->crafting_grid);
    destroy_buffer_object(r->device, &r->inventory_bg);
    destroy_buffer_object(r->device, &r->inventory_selection);
    destroy_buffer_object(r->device, &r->inventory_count);
    destroy_buffer_object(r->device, &r->inventory_icon);
    destroy_buffer_object(r->device, &r->inventory_grid);
    destroy_buffer_object(r->device, &r->crosshair);
    destroy_buffer_object(r->device, &r->edge_index);
    destroy_buffer_object(r->device, &r->edge_vertex);
    destroy_buffer_object(r->device, &r->block_index);
    destroy_buffer_object(r->device, &r->block_vertex);
    
    for (uint32_t i = 0; i < ITEM_TYPE_COUNT; i++) {
        vkDestroySampler(r->device, r->textures_sampler[i], NULL);
        vkDestroyImageView(r->device, r->textures_view[i], NULL);
        vkDestroyImage(r->device, r->textures[i], NULL);
        vkFreeMemory(r->device, r->textures_memory[i], NULL);
    }
    
    vkDestroyCommandPool(r->device, r->command_pool, NULL);
    vkDestroyDevice(r->device, NULL);
    vkDestroySurfaceKHR(r->instance, r->surface, NULL);
    vkDestroyInstance(r->instance, NULL);
    
    free(r);
}

/* -------------------------------------------------------------------------- */
/* Frame Rendering Helpers                                                    */
/* -------------------------------------------------------------------------- */

static void ensure_instance_capacity(Renderer *r, uint32_t required) {
    if (required <= r->instance_capacity) return;
    
    uint32_t new_cap = r->instance_capacity;
    while (new_cap < required) new_cap *= 2;
    if (new_cap > MAX_INSTANCE_CAPACITY) die("Instance buffer overflow");
    
    vkDeviceWaitIdle(r->device);
    destroy_buffer_object(r->device, &r->instance_buf);
    
    r->instance_capacity = new_cap;
    create_and_upload_buffer(r, &r->instance_buf, NULL, r->instance_capacity * sizeof(InstanceData),
                              VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
}

static uint32_t fill_instance_buffer(Renderer *r, World *world, const Player *player, float aspect,
                                     bool highlight, IVec3 highlight_cell,
                                     uint32_t *out_highlight_idx, uint32_t *out_crosshair_idx,
                                     uint32_t *out_inventory_idx, uint32_t *out_selection_idx,
                                     uint32_t *out_bg_idx, uint32_t *out_health_bg_idx,
                                     uint32_t *out_health_border_idx,
                                     uint32_t *out_icons_start) {
    int block_count = world_total_render_blocks(world);
    uint32_t icon_count = player_inventory_icon_instances(player, aspect, NULL, 0);
    uint32_t total = block_count + 7 + icon_count;
    
    ensure_instance_capacity(r, total);
    
    InstanceData *instances;
    VK_CHECK(vkMapMemory(r->device, r->instance_buf.memory, 0, total * sizeof(InstanceData), 0, (void **)&instances));
    
    uint32_t idx = 0;
    
    for (int i = 0; i < world->chunk_count; i++) {
        Chunk *chunk = world->chunks[i];
        for (int j = 0; j < chunk->block_count; j++) {
            Block b = chunk->blocks[j];
            instances[idx++] = (InstanceData){b.pos.x, b.pos.y, b.pos.z, b.type};
        }
    }
    
    *out_highlight_idx = idx++;
    *out_crosshair_idx = idx++;
    *out_inventory_idx = idx++;
    *out_selection_idx = idx++;
    *out_bg_idx = idx++;
    *out_health_bg_idx = idx++;
    *out_health_border_idx = idx++;
    *out_icons_start = idx;
    
    instances[*out_highlight_idx] = (InstanceData){
        highlight ? (float)highlight_cell.x : 0, highlight ? (float)highlight_cell.y : 0,
        highlight ? (float)highlight_cell.z : 0, HIGHLIGHT_TEXTURE_INDEX
    };
    instances[*out_crosshair_idx] = (InstanceData){0, 0, 0, CROSSHAIR_TEXTURE_INDEX};
    instances[*out_inventory_idx] = (InstanceData){0, 0, 0, HIGHLIGHT_TEXTURE_INDEX};
    instances[*out_selection_idx] = (InstanceData){0, 0, 0, INVENTORY_SELECTION_TEXTURE_INDEX};
    instances[*out_bg_idx] = (InstanceData){0, 0, 0, INVENTORY_BG_TEXTURE_INDEX};
    instances[*out_health_bg_idx] = (InstanceData){0, 0, 0, HEALTH_BAR_INDEX};
    instances[*out_health_border_idx] = (InstanceData){0, 0, 0, HIGHLIGHT_TEXTURE_INDEX};
    
    if (icon_count > 0) {
        player_inventory_icon_instances(player, aspect, &instances[*out_icons_start], icon_count);
    }
    
    vkUnmapMemory(r->device, r->instance_buf.memory);
    return icon_count;
}

static void update_ui_buffers(Renderer *r, const Player *player, float aspect) {
    /* Update health bar background */
    Vertex health_verts[60];
    uint32_t health_count;
    player_health_bar_background_vertices(player, aspect, health_verts, 60, &health_count);
    if (health_count > 0) {
        upload_buffer_data(r->device, r->health_bar_bg.memory, health_verts, health_count * sizeof(Vertex));
    }

    if (!player->inventory_open) return;
    
    Vertex sel_verts[8];
    uint32_t sel_count = 0;
    player_inventory_selection_vertices((int)player->selected_slot, aspect, sel_verts, 8, &sel_count);
    if (sel_count > 0) {
        upload_buffer_data(r->device, r->inventory_selection.memory, sel_verts, sel_count * sizeof(Vertex));
    }
    
    Vertex bg_verts[18];
    uint32_t bg_count = 0;
    player_inventory_background_vertices(aspect, bg_verts, 18, &bg_count);
    if (bg_count > 0) {
        upload_buffer_data(r->device, r->inventory_bg.memory, bg_verts, bg_count * sizeof(Vertex));
    }
    
    Vertex count_verts[1500];
    uint32_t count_count = player_inventory_count_vertices(player, aspect, count_verts, 1500);
    if (count_count > 0) {
        upload_buffer_data(r->device, r->inventory_count.memory, count_verts, count_count * sizeof(Vertex));
    }
}

static void record_world_rendering(VkCommandBuffer cmd, Renderer *r, uint32_t img_idx,
                                    int block_count, uint32_t highlight_idx, bool highlight,
                                    const PushConstants *pc) {
    VkBuffer bufs[2];
    VkDeviceSize offsets[2] = {0, 0};
    
    if (block_count > 0) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r->pipeline_solid);
        vkCmdPushConstants(cmd, r->pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(*pc), pc);
        
        bufs[0] = r->block_vertex.buffer;
        bufs[1] = r->instance_buf.buffer;
        vkCmdBindVertexBuffers(cmd, 0, 2, bufs, offsets);
        vkCmdBindIndexBuffer(cmd, r->block_index.buffer, 0, VK_INDEX_TYPE_UINT16);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r->pipeline_layout, 0, 1,
                                &r->descriptor_sets_normal[img_idx], 0, NULL);
        
        vkCmdDrawIndexed(cmd, (sizeof(BLOCK_INDICES) / sizeof((BLOCK_INDICES)[0])), block_count, 0, 0, 0);
    }
    
    if (highlight) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r->pipeline_wireframe);
        vkCmdPushConstants(cmd, r->pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(*pc), pc);
        
        bufs[0] = r->edge_vertex.buffer;
        bufs[1] = r->instance_buf.buffer;
        vkCmdBindVertexBuffers(cmd, 0, 2, bufs, offsets);
        vkCmdBindIndexBuffer(cmd, r->edge_index.buffer, 0, VK_INDEX_TYPE_UINT16);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r->pipeline_layout, 0, 1,
                                &r->descriptor_sets_highlight[img_idx], 0, NULL);
        
        vkCmdDrawIndexed(cmd, (sizeof(EDGE_INDICES) / sizeof((EDGE_INDICES)[0])), 1, 0, 0, highlight_idx);
    }
}

static void record_crosshair_rendering(VkCommandBuffer cmd, Renderer *r, uint32_t img_idx,
                                       const Player *player, uint32_t crosshair_idx,
                                       uint32_t health_bg_idx, uint32_t health_border_idx,
                                       const PushConstants *pc_overlay) {
    VkBuffer bufs[2];
    VkDeviceSize offsets[2] = {0, 0};
    
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r->pipeline_crosshair);
    vkCmdPushConstants(cmd, r->pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(*pc_overlay), pc_overlay);
    
    bufs[0] = r->crosshair.buffer;
    bufs[1] = r->instance_buf.buffer;
    vkCmdBindVertexBuffers(cmd, 0, 2, bufs, offsets);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r->pipeline_layout, 0, 1,
                            &r->descriptor_sets_highlight[img_idx], 0, NULL);
    
    vkCmdDraw(cmd, 4, 1, 0, crosshair_idx);

    /* Render health bar */
    uint32_t health_count = player->health > 10 ? 10 : player->health;
    if (health_count > 0) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r->pipeline_overlay);
        vkCmdPushConstants(cmd, r->pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(*pc_overlay), pc_overlay);
        
        bufs[0] = r->health_bar_bg.buffer;
        bufs[1] = r->instance_buf.buffer;
        vkCmdBindVertexBuffers(cmd, 0, 2, bufs, offsets);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r->pipeline_layout, 0, 1,
                                &r->descriptor_sets_highlight[img_idx], 0, NULL);
        vkCmdDraw(cmd, health_count * 6, 1, 0, health_bg_idx);
    }
    
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r->pipeline_crosshair);
    vkCmdPushConstants(cmd, r->pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(*pc_overlay), pc_overlay);
    
    bufs[0] = r->health_bar_border.buffer;
    vkCmdBindVertexBuffers(cmd, 0, 2, bufs, offsets);
    vkCmdDraw(cmd, 80, 1, 0, health_border_idx);
}

static void record_inventory_rendering(VkCommandBuffer cmd, Renderer *r, uint32_t img_idx,
                                        const Player *player, uint32_t bg_idx, uint32_t inventory_idx,
                                        uint32_t selection_idx, uint32_t health_bg_idx,
                                        uint32_t health_border_idx,
                                        uint32_t icons_start, uint32_t icon_count,
                                        const PushConstants *pc_overlay) {
    VkBuffer bufs[2];
    VkDeviceSize offsets[2] = {0, 0};
    
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r->pipeline_overlay);
    vkCmdPushConstants(cmd, r->pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(*pc_overlay), pc_overlay);
    
    bufs[0] = r->inventory_bg.buffer;
    bufs[1] = r->instance_buf.buffer;
    vkCmdBindVertexBuffers(cmd, 0, 2, bufs, offsets);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r->pipeline_layout, 0, 1,
                            &r->descriptor_sets_highlight[img_idx], 0, NULL);
    vkCmdDraw(cmd, 18, 1, 0, bg_idx);
    
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r->pipeline_crosshair);
    vkCmdPushConstants(cmd, r->pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(*pc_overlay), pc_overlay);
    
    bufs[0] = r->inventory_grid.buffer;
    vkCmdBindVertexBuffers(cmd, 0, 2, bufs, offsets);
    vkCmdDraw(cmd, 32, 1, 0, inventory_idx);
    
    bufs[0] = r->crafting_grid.buffer;
    vkCmdBindVertexBuffers(cmd, 0, 2, bufs, offsets);
    vkCmdDraw(cmd, 32, 1, 0, inventory_idx);
    
    bufs[0] = r->crafting_arrow.buffer;
    vkCmdBindVertexBuffers(cmd, 0, 2, bufs, offsets);
    vkCmdDraw(cmd, 16, 1, 0, inventory_idx);
    
    bufs[0] = r->crafting_result.buffer;
    vkCmdBindVertexBuffers(cmd, 0, 2, bufs, offsets);
    vkCmdDraw(cmd, 16, 1, 0, inventory_idx);
    
    bufs[0] = r->inventory_selection.buffer;
    vkCmdBindVertexBuffers(cmd, 0, 2, bufs, offsets);
    vkCmdDraw(cmd, 8, 1, 0, selection_idx);
    
    if (icon_count > 0) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r->pipeline_overlay);
        vkCmdPushConstants(cmd, r->pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(*pc_overlay), pc_overlay);
        
        bufs[0] = r->inventory_icon.buffer;
        bufs[1] = r->instance_buf.buffer;
        vkCmdBindVertexBuffers(cmd, 0, 2, bufs, offsets);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r->pipeline_layout, 0, 1,
                                &r->descriptor_sets_normal[img_idx], 0, NULL);
        vkCmdDraw(cmd, 6, icon_count, 0, icons_start);
    }
    
    Vertex count_verts[1500];
    uint32_t count_count = player_inventory_count_vertices(player, (float)r->extent.height / (float)r->extent.width,
                                                           count_verts, 1500);
    if (count_count > 0) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r->pipeline_crosshair);
        vkCmdPushConstants(cmd, r->pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(*pc_overlay), pc_overlay);
        
        bufs[0] = r->inventory_count.buffer;
        bufs[1] = r->instance_buf.buffer;
        vkCmdBindVertexBuffers(cmd, 0, 2, bufs, offsets);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r->pipeline_layout, 0, 1,
                                &r->descriptor_sets_highlight[img_idx], 0, NULL);
        vkCmdDraw(cmd, count_count, 1, 0, inventory_idx);
    }

    /* Render health bar */
    uint32_t health_count = player->health > 10 ? 10 : player->health;
    if (health_count > 0) {
        VkBuffer health_bufs[2];
        VkDeviceSize health_offsets[2] = {0, 0};
        
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r->pipeline_overlay);
        vkCmdPushConstants(cmd, r->pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(*pc_overlay), pc_overlay);
        
        health_bufs[0] = r->health_bar_bg.buffer;
        health_bufs[1] = r->instance_buf.buffer;
        vkCmdBindVertexBuffers(cmd, 0, 2, health_bufs, health_offsets);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r->pipeline_layout, 0, 1,
                                &r->descriptor_sets_highlight[img_idx], 0, NULL);
        vkCmdDraw(cmd, health_count * 6, 1, 0, health_bg_idx);
    }
    
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r->pipeline_crosshair);
    vkCmdPushConstants(cmd, r->pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(*pc_overlay), pc_overlay);
    
    VkBuffer border_bufs[2];
    VkDeviceSize border_offsets[2] = {0, 0};
    border_bufs[0] = r->health_bar_border.buffer;
    border_bufs[1] = r->instance_buf.buffer;
    vkCmdBindVertexBuffers(cmd, 0, 2, border_bufs, border_offsets);
    vkCmdDraw(cmd, 80, 1, 0, health_border_idx);
}

/* -------------------------------------------------------------------------- */
/* Frame Rendering                                                            */
/* -------------------------------------------------------------------------- */

void renderer_draw_frame(Renderer *r, World *world, const Player *player, Camera *camera,
                         bool highlight, IVec3 highlight_cell) {
    if (!r) return;
    
    VK_CHECK(vkWaitForFences(r->device, 1, &r->in_flight, VK_TRUE, UINT64_MAX));
    VK_CHECK(vkResetFences(r->device, 1, &r->in_flight));
    
    uint32_t img_idx;
    VkResult result = vkAcquireNextImageKHR(r->device, r->swapchain, UINT64_MAX,
                                             r->image_available, VK_NULL_HANDLE, &img_idx);
    
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        die("Failed to acquire swapchain image");
    }
    
    float aspect = (float)r->extent.height / (float)r->extent.width;
    
    uint32_t highlight_idx, crosshair_idx, inventory_idx, selection_idx, bg_idx;
    uint32_t health_bg_idx, health_border_idx, icons_start;
    int block_count = world_total_render_blocks(world);
    uint32_t icon_count = fill_instance_buffer(r, world, player, aspect, highlight, highlight_cell,
                                                &highlight_idx, &crosshair_idx, &inventory_idx,
                                                &selection_idx, &bg_idx,
                                                &health_bg_idx, &health_border_idx, &icons_start);
    
    update_ui_buffers(r, player, aspect);
    
    PushConstants pc = {
        .view = camera_view_matrix(camera),
        .proj = mat4_perspective(55.0f * M_PI / 180.0f,
                                 (float)r->extent.width / (float)r->extent.height, 0.1f, 200.0f)
    };
    
    PushConstants pc_overlay = {.view = mat4_identity(), .proj = mat4_identity()};
    pc_overlay.proj.m[5] = -1.0f;
    
    VkCommandBuffer cmd = r->command_buffers[img_idx];
    VK_CHECK(vkResetCommandBuffer(cmd, 0));
    
    VkCommandBufferBeginInfo begin_info = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    VK_CHECK(vkBeginCommandBuffer(cmd, &begin_info));
    
    VkClearValue clear_vals[2] = {
        {.color = {{0.1f, 0.12f, 0.18f, 1.0f}}},
        {.depthStencil = {1.0f, 0}}
    };
    
    VkRenderPassBeginInfo rp_begin = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = r->render_pass,
        .framebuffer = r->swapchain_framebuffers[img_idx],
        .renderArea = {.extent = r->extent},
        .clearValueCount = 2,
        .pClearValues = clear_vals
    };
    
    vkCmdBeginRenderPass(cmd, &rp_begin, VK_SUBPASS_CONTENTS_INLINE);
    
    record_world_rendering(cmd, r, img_idx, block_count, highlight_idx, highlight, &pc);
    
    if (!player->inventory_open) {
        record_crosshair_rendering(cmd, r, img_idx, player, crosshair_idx,
                                   health_bg_idx, health_border_idx, &pc_overlay);
    } else {
        record_inventory_rendering(cmd, r, img_idx, player, bg_idx, inventory_idx,
                                    selection_idx, health_bg_idx, health_border_idx,
                                    icons_start, icon_count, &pc_overlay);
    }
    
    vkCmdEndRenderPass(cmd);
    VK_CHECK(vkEndCommandBuffer(cmd));
    
    VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    
    VkSubmitInfo submit_info = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount = 1, .pWaitSemaphores = &r->image_available,
        .pWaitDstStageMask = &wait_stage,
        .commandBufferCount = 1, .pCommandBuffers = &cmd,
        .signalSemaphoreCount = 1, .pSignalSemaphores = &r->render_finished
    };
    
    VK_CHECK(vkQueueSubmit(r->graphics_queue, 1, &submit_info, r->in_flight));
    
    VkPresentInfoKHR present_info = {
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1, .pWaitSemaphores = &r->render_finished,
        .swapchainCount = 1, .pSwapchains = &r->swapchain,
        .pImageIndices = &img_idx
    };
    
    result = vkQueuePresentKHR(r->graphics_queue, &present_info);
    
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        die("Failed to present");
    }
}