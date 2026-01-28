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
/* Vulkan Error Helpers                                                       */
/* -------------------------------------------------------------------------- */

#define VK_CHECK(call)                                                            \
    do {                                                                          \
        VkResult vk_check_result__ = (call);                                      \
        if (vk_check_result__ != VK_SUCCESS) {                                    \
            fprintf(stderr, "%s failed: %s\n", #call, vk_result_to_string(vk_check_result__)); \
            exit(EXIT_FAILURE);                                                   \
        }                                                                         \
    } while (0)

#define VK_DESTROY(device, type, handle)                                          \
    do {                                                                          \
        if ((handle) != VK_NULL_HANDLE) {                                         \
            vkDestroy##type((device), (handle), NULL);                            \
            (handle) = VK_NULL_HANDLE;                                            \
        }                                                                         \
    } while (0)

#define CREATE_BUFFER_WITH_DATA(dev, pdev, data, usage, buf, mem)                \
    do {                                                                          \
        create_buffer((dev), (pdev), sizeof(data), (usage),                       \
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, \
                     (buf), (mem));                                               \
        upload_buffer_data((dev), *(mem), (data), sizeof(data));                 \
    } while (0)

/* -------------------------------------------------------------------------- */
/* Vulkan Error Helpers                                                       */
/* -------------------------------------------------------------------------- */

void die(const char *message) {
    fprintf(stderr, "Error: %s\n", message);
    exit(EXIT_FAILURE);
}

const char *vk_result_to_string(VkResult result) {
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

/* -------------------------------------------------------------------------- */
/* Renderer Types (Vulkan internal)                                           */
/* -------------------------------------------------------------------------- */

struct Renderer {
    void *display;
    unsigned long window;

    VkInstance instance;
    VkSurfaceKHR surface;
    VkPhysicalDevice physical_device;
    VkDevice device;
    VkQueue graphics_queue;
    uint32_t graphics_family;
    VkCommandPool command_pool;

    VkImage textures_image[BLOCK_TYPE_COUNT];
    VkDeviceMemory textures_memory[BLOCK_TYPE_COUNT];
    VkImageView textures_view[BLOCK_TYPE_COUNT];
    VkSampler textures_sampler[BLOCK_TYPE_COUNT];
    uint32_t textures_width[BLOCK_TYPE_COUNT];
    uint32_t textures_height[BLOCK_TYPE_COUNT];

    VkImage black_texture_image;
    VkDeviceMemory black_texture_memory;
    VkImageView black_texture_view;
    VkSampler black_texture_sampler;
    uint32_t black_texture_width;
    uint32_t black_texture_height;

    VkBuffer block_vertex_buffer;
    VkDeviceMemory block_vertex_memory;
    VkBuffer block_index_buffer;
    VkDeviceMemory block_index_memory;

    VkBuffer edge_vertex_buffer;
    VkDeviceMemory edge_vertex_memory;
    VkBuffer edge_index_buffer;
    VkDeviceMemory edge_index_memory;

    VkBuffer crosshair_vertex_buffer;
    VkDeviceMemory crosshair_vertex_memory;

    VkBuffer inventory_vertex_buffer;
    VkDeviceMemory inventory_vertex_memory;
    uint32_t inventory_vertex_count;

    VkBuffer inventory_icon_vertex_buffer;
    VkDeviceMemory inventory_icon_vertex_memory;
    uint32_t inventory_icon_vertex_count;

    VkBuffer inventory_count_vertex_buffer;
    VkDeviceMemory inventory_count_vertex_memory;
    uint32_t inventory_count_vertex_count;

    VkBuffer inventory_selection_vertex_buffer;
    VkDeviceMemory inventory_selection_vertex_memory;
    uint32_t inventory_selection_vertex_count;

    VkBuffer inventory_bg_vertex_buffer;
    VkDeviceMemory inventory_bg_vertex_memory;
    uint32_t inventory_bg_vertex_count;

    VkBuffer instance_buffer;
    VkDeviceMemory instance_memory;
    uint32_t instance_capacity;

    VkShaderModule vert_shader;
    VkShaderModule frag_shader;
    VkDescriptorSetLayout descriptor_layout;
    VkPipelineLayout pipeline_layout;

    VkSwapchainKHR swapchain;
    VkImage *swapchain_images;
    VkImageView *swapchain_image_views;
    VkFramebuffer *swapchain_framebuffers;
    VkRenderPass swapchain_render_pass;
    VkPipeline swapchain_pipeline_solid;
    VkPipeline swapchain_pipeline_wireframe;
    VkPipeline swapchain_pipeline_crosshair;
    VkPipeline swapchain_pipeline_overlay;

    VkDescriptorPool swapchain_descriptor_pool;
    VkDescriptorSet *swapchain_descriptor_sets_normal;
    VkDescriptorSet *swapchain_descriptor_sets_highlight;

    VkCommandBuffer *swapchain_command_buffers;

    VkImage swapchain_depth_image;
    VkDeviceMemory swapchain_depth_memory;
    VkImageView swapchain_depth_view;

    uint32_t swapchain_image_count;
    VkExtent2D swapchain_extent;
    VkFormat swapchain_format;

    VkSemaphore image_available;
    VkSemaphore render_finished;
    VkFence in_flight;
};

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
        if (supported && matches) return i;
    }

    die("Failed to find suitable memory type");
    return 0;
}

void create_buffer(VkDevice device,
                    VkPhysicalDevice physical_device,
                    VkDeviceSize size,
                    VkBufferUsageFlags usage,
                    VkMemoryPropertyFlags properties,
                    VkBuffer *buffer,
                    VkDeviceMemory *memory) {
    VkBufferCreateInfo buffer_info = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                                      .size = size,
                                      .usage = usage,
                                      .sharingMode = VK_SHARING_MODE_EXCLUSIVE};

    VK_CHECK(vkCreateBuffer(device, &buffer_info, NULL, buffer));

    VkMemoryRequirements requirements;
    vkGetBufferMemoryRequirements(device, *buffer, &requirements);

    VkMemoryAllocateInfo alloc_info = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                                       .allocationSize = requirements.size,
                                       .memoryTypeIndex = find_memory_type(physical_device,
                                                                           requirements.memoryTypeBits,
                                                                           properties)};

    VK_CHECK(vkAllocateMemory(device, &alloc_info, NULL, memory));
    VK_CHECK(vkBindBufferMemory(device, *buffer, *memory, 0));
}

void upload_buffer_data(VkDevice device, VkDeviceMemory memory, const void *data, size_t size) {
    void *mapped;
    VK_CHECK(vkMapMemory(device, memory, 0, size, 0, &mapped));
    memcpy(mapped, data, size);
    vkUnmapMemory(device, memory);
}

static void create_image_view(VkDevice device, VkImage image, VkFormat format,
                               VkImageAspectFlags aspect_mask, VkImageView *view) {
    VkImageViewCreateInfo view_info = {0};
    view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image = image;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = format;
    view_info.subresourceRange.aspectMask = aspect_mask;
    view_info.subresourceRange.levelCount = 1;
    view_info.subresourceRange.layerCount = 1;
    VK_CHECK(vkCreateImageView(device, &view_info, NULL, view));
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
    VkImageCreateInfo image_info = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
                                    .imageType = VK_IMAGE_TYPE_2D,
                                    .extent = {width, height, 1},
                                    .mipLevels = 1,
                                    .arrayLayers = 1,
                                    .format = format,
                                    .tiling = tiling,
                                    .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                                    .usage = usage,
                                    .samples = VK_SAMPLE_COUNT_1_BIT,
                                    .sharingMode = VK_SHARING_MODE_EXCLUSIVE};

    VK_CHECK(vkCreateImage(device, &image_info, NULL, image));

    VkMemoryRequirements requirements;
    vkGetImageMemoryRequirements(device, *image, &requirements);

    VkMemoryAllocateInfo alloc_info = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                                       .allocationSize = requirements.size};
    alloc_info.memoryTypeIndex = find_memory_type(physical_device, requirements.memoryTypeBits, properties);

    VK_CHECK(vkAllocateMemory(device, &alloc_info, NULL, memory));
    VK_CHECK(vkBindImageMemory(device, *image, *memory, 0));
}

static VkCommandBuffer begin_single_use_commands(VkDevice device, VkCommandPool command_pool) {
    VkCommandBufferAllocateInfo alloc_info = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
                                              .commandPool = command_pool,
                                              .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                                              .commandBufferCount = 1};

    VkCommandBuffer command_buffer;
    VK_CHECK(vkAllocateCommandBuffers(device, &alloc_info, &command_buffer));

    VkCommandBufferBeginInfo begin_info = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                                           .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};

    VK_CHECK(vkBeginCommandBuffer(command_buffer, &begin_info));
    return command_buffer;
}

static void end_single_use_commands(VkDevice device, VkCommandPool command_pool,
                                    VkQueue graphics_queue, VkCommandBuffer command_buffer) {
    VK_CHECK(vkEndCommandBuffer(command_buffer));

    VkSubmitInfo submit_info = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                                .commandBufferCount = 1,
                                .pCommandBuffers = &command_buffer};

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

    VkImageMemoryBarrier barrier = {.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                                    .oldLayout = old_layout,
                                    .newLayout = new_layout,
                                    .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                    .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                    .image = image,
                                    .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};

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

/* -------------------------------------------------------------------------- */
/* Texture Handling                                                           */
/* -------------------------------------------------------------------------- */

static void texture_create_from_pixels(VkDevice device,
                                       VkPhysicalDevice physical_device,
                                       VkCommandPool command_pool,
                                       VkQueue graphics_queue,
                                       const uint8_t *pixels,
                                       uint32_t width,
                                       uint32_t height,
                                       VkImage *image,
                                       VkDeviceMemory *memory,
                                       VkImageView *view,
                                       VkSampler *sampler,
                                       uint32_t *out_width,
                                       uint32_t *out_height) {
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
                 image,
                 memory);

    transition_image_layout(device, command_pool, graphics_queue,
                            *image, VK_FORMAT_R8G8B8A8_SRGB,
                            VK_IMAGE_LAYOUT_UNDEFINED,
                            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    VkCommandBuffer copy_cmd = begin_single_use_commands(device, command_pool);
    VkBufferImageCopy region = {.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                                .imageExtent = {width, height, 1}};
    vkCmdCopyBufferToImage(copy_cmd,
                           staging_buffer,
                           *image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           1,
                           &region);
    end_single_use_commands(device, command_pool, graphics_queue, copy_cmd);

    transition_image_layout(device, command_pool, graphics_queue,
                            *image, VK_FORMAT_R8G8B8A8_SRGB,
                            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    vkDestroyBuffer(device, staging_buffer, NULL);
    vkFreeMemory(device, staging_memory, NULL);

    create_image_view(device, *image, VK_FORMAT_R8G8B8A8_SRGB,
                      VK_IMAGE_ASPECT_COLOR_BIT, view);

    VkSamplerCreateInfo sampler_info = {.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
                                        .magFilter = VK_FILTER_NEAREST,
                                        .minFilter = VK_FILTER_NEAREST,
                                        .addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
                                        .addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
                                        .addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
                                        .anisotropyEnable = VK_FALSE,
                                        .maxAnisotropy = 1.0f,
                                        .borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK,
                                        .unnormalizedCoordinates = VK_FALSE,
                                        .compareEnable = VK_FALSE,
                                        .compareOp = VK_COMPARE_OP_ALWAYS,
                                        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST};

    VK_CHECK(vkCreateSampler(device, &sampler_info, NULL, sampler));

    if (out_width) *out_width = width;
    if (out_height) *out_height = height;
}

void texture_create(VkDevice device,
                    VkPhysicalDevice physical_device,
                    VkCommandPool command_pool,
                    VkQueue graphics_queue,
                    const char *filename,
                    VkImage *image,
                    VkDeviceMemory *memory,
                    VkImageView *view,
                    VkSampler *sampler,
                    uint32_t *out_width,
                    uint32_t *out_height) {

    FILE *fp = fopen(filename, "rb");
    if (!fp) die("Failed to load PNG texture");

    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png) { fclose(fp); die("Failed to load PNG texture"); }

    png_infop info = png_create_info_struct(png);
    if (!info) { png_destroy_read_struct(&png, NULL, NULL); fclose(fp); die("Failed to load PNG texture"); }

    if (setjmp(png_jmpbuf(png))) {
        png_destroy_read_struct(&png, &info, NULL);
        fclose(fp);
        die("Failed to load PNG texture");
    }

    png_init_io(png, fp);
    png_read_info(png, info);

    png_uint_32 width = png_get_image_width(png, info);
    png_uint_32 height = png_get_image_height(png, info);
    png_byte bit_depth = png_get_bit_depth(png, info);
    png_byte color_type = png_get_color_type(png, info);

    if (bit_depth == 16) png_set_strip_16(png);
    if (color_type == PNG_COLOR_TYPE_PALETTE) png_set_palette_to_rgb(png);
    if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8) png_set_expand_gray_1_2_4_to_8(png);
    if (png_get_valid(png, info, PNG_INFO_tRNS)) png_set_tRNS_to_alpha(png);
    if (color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_GRAY_ALPHA) png_set_gray_to_rgb(png);
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
        die("Failed to load PNG texture");
    }

    png_bytep *row_pointers = malloc(sizeof(png_bytep) * height);
    if (!row_pointers) {
        free(pixels);
        png_destroy_read_struct(&png, &info, NULL);
        fclose(fp);
        die("Failed to load PNG texture");
    }

    for (png_uint_32 y = 0; y < height; ++y) {
        row_pointers[y] = pixels + y * row_bytes;
    }

    png_read_image(png, row_pointers);
    png_read_end(png, NULL);

    free(row_pointers);
    png_destroy_read_struct(&png, &info, NULL);
    fclose(fp);

    texture_create_from_pixels(device, physical_device, command_pool, graphics_queue,
                               pixels, (uint32_t)width, (uint32_t)height,
                               image, memory, view, sampler, out_width, out_height);

    free(pixels);
}

void texture_create_solid(VkDevice device,
                                 VkPhysicalDevice physical_device,
                                 VkCommandPool command_pool,
                                 VkQueue graphics_queue,
                                 uint8_t r, uint8_t g, uint8_t b, uint8_t a,
                                 VkImage *image,
                                 VkDeviceMemory *memory,
                                 VkImageView *view,
                                 VkSampler *sampler,
                                 uint32_t *out_width,
                                 uint32_t *out_height) {
    uint8_t pixel[4] = {r, g, b, a};
    texture_create_from_pixels(device, physical_device, command_pool, graphics_queue,
                               pixel, 1, 1,
                               image, memory, view, sampler, out_width, out_height);
}

void texture_destroy(VkDevice device,
                     VkImage *image,
                     VkDeviceMemory *memory,
                     VkImageView *view,
                     VkSampler *sampler,
                     uint32_t *out_width,
                     uint32_t *out_height) {
    if (sampler && *sampler != VK_NULL_HANDLE) {
        vkDestroySampler(device, *sampler, NULL);
        *sampler = VK_NULL_HANDLE;
    }
    if (view && *view != VK_NULL_HANDLE) {
        vkDestroyImageView(device, *view, NULL);
        *view = VK_NULL_HANDLE;
    }
    if (image && *image != VK_NULL_HANDLE) {
        vkDestroyImage(device, *image, NULL);
        *image = VK_NULL_HANDLE;
    }
    if (memory && *memory != VK_NULL_HANDLE) {
        vkFreeMemory(device, *memory, NULL);
        *memory = VK_NULL_HANDLE;
    }
    if (out_width) *out_width = 0;
    if (out_height) *out_height = 0;
}

/* -------------------------------------------------------------------------- */
/* Shader Helpers                                                             */
/* -------------------------------------------------------------------------- */

VkShaderModule create_shader_module(VkDevice device, const char *filepath) {
    FILE *file = fopen(filepath, "rb");
    if (!file) die("Failed to read shader file");

    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    if (file_size <= 0) { fclose(file); die("Failed to read shader file"); }

    char *code = malloc((size_t)file_size);
    if (!code) { fclose(file); die("Failed to read shader file"); }

    size_t read = fread(code, 1, (size_t)file_size, file);
    fclose(file);

    if (read != (size_t)file_size) {
        free(code);
        die("Failed to read shader file");
    }

    VkShaderModuleCreateInfo create_info = {.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
                                            .codeSize = (size_t)file_size,
                                            .pCode = (const uint32_t *)code};

    VkShaderModule module;
    VK_CHECK(vkCreateShaderModule(device, &create_info, NULL, &module));

    free(code);
    return module;
}

/* -------------------------------------------------------------------------- */
/* Renderer                                                                   */
/* -------------------------------------------------------------------------- */

Renderer *renderer_create(void *display,
                          unsigned long window,
                          uint32_t framebuffer_width,
                          uint32_t framebuffer_height) {
    Renderer *renderer = calloc(1, sizeof(*renderer));
    if (!renderer) die("Failed to allocate renderer");

    renderer->display = display;
    renderer->window = window;

    const char *instance_extensions[] = {
        VK_KHR_SURFACE_EXTENSION_NAME,
        VK_KHR_XLIB_SURFACE_EXTENSION_NAME
    };

    VkApplicationInfo app_info = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
                                  .pApplicationName = "Voxel Engine",
                                  .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
                                  .pEngineName = "No Engine",
                                  .engineVersion = VK_MAKE_VERSION(1, 0, 0),
                                  .apiVersion = VK_API_VERSION_1_1};

    VkInstanceCreateInfo instance_info = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
                                          .pApplicationInfo = &app_info,
                                          .enabledExtensionCount = ARRAY_LENGTH(instance_extensions),
                                          .ppEnabledExtensionNames = instance_extensions};

    VK_CHECK(vkCreateInstance(&instance_info, NULL, &renderer->instance));

    VkXlibSurfaceCreateInfoKHR surface_info = {.sType = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR,
                                               .dpy = (Display *)display,
                                               .window = (Window)window};

    VK_CHECK(vkCreateXlibSurfaceKHR(renderer->instance, &surface_info, NULL, &renderer->surface));

    uint32_t device_count = 0;
    VK_CHECK(vkEnumeratePhysicalDevices(renderer->instance, &device_count, NULL));
    if (device_count == 0) die("No Vulkan-capable GPU found");

    VkPhysicalDevice *physical_devices = malloc(sizeof(VkPhysicalDevice) * device_count);
    VK_CHECK(vkEnumeratePhysicalDevices(renderer->instance, &device_count, physical_devices));

    VkPhysicalDevice physical_device = VK_NULL_HANDLE;
    uint32_t graphics_family = UINT32_MAX;

    for (uint32_t i = 0; i < device_count; ++i) {
        uint32_t queue_family_count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(physical_devices[i], &queue_family_count, NULL);

        VkQueueFamilyProperties *families = malloc(sizeof(VkQueueFamilyProperties) * queue_family_count);
        vkGetPhysicalDeviceQueueFamilyProperties(physical_devices[i], &queue_family_count, families);

        for (uint32_t j = 0; j < queue_family_count; ++j) {
            VkBool32 present = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(physical_devices[i], j, renderer->surface, &present);

            bool has_graphics = (families[j].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0;
            if (has_graphics && present) {
                physical_device = physical_devices[i];
                graphics_family = j;
                break;
            }
        }

        free(families);
        if (physical_device != VK_NULL_HANDLE) break;
    }

    free(physical_devices);

    if (physical_device == VK_NULL_HANDLE) die("No suitable GPU found");

    renderer->physical_device = physical_device;
    renderer->graphics_family = graphics_family;

    float queue_priority = 1.0f;
    VkDeviceQueueCreateInfo queue_info = {.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                                          .queueFamilyIndex = graphics_family,
                                          .queueCount = 1,
                                          .pQueuePriorities = &queue_priority};

    const char *device_extensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};

    VkPhysicalDeviceFeatures supported_features;
    vkGetPhysicalDeviceFeatures(physical_device, &supported_features);

    VkPhysicalDeviceFeatures enabled_features = {.wideLines = supported_features.wideLines ? VK_TRUE : VK_FALSE};

    VkDeviceCreateInfo device_info = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
                                      .queueCreateInfoCount = 1,
                                      .pQueueCreateInfos = &queue_info,
                                      .enabledExtensionCount = ARRAY_LENGTH(device_extensions),
                                      .ppEnabledExtensionNames = device_extensions,
                                      .pEnabledFeatures = &enabled_features};

    VK_CHECK(vkCreateDevice(physical_device, &device_info, NULL, &renderer->device));

    vkGetDeviceQueue(renderer->device, graphics_family, 0, &renderer->graphics_queue);

    VkCommandPoolCreateInfo pool_info = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
                                         .queueFamilyIndex = graphics_family,
                                         .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT};

    VK_CHECK(vkCreateCommandPool(renderer->device, &pool_info, NULL, &renderer->command_pool));

    const char *texture_files[BLOCK_TYPE_COUNT] = {
        "textures/dirt.png",
        "textures/stone.png",
        "textures/grass.png",
        "textures/sand.png",
        "textures/water.png",
        "textures/wood.png",
        "textures/leaves.png"
    };

    for (uint32_t i = 0; i < BLOCK_TYPE_COUNT; ++i) {
        texture_create(renderer->device, renderer->physical_device,
                       renderer->command_pool, renderer->graphics_queue,
                       texture_files[i],
                       &renderer->textures_image[i],
                       &renderer->textures_memory[i],
                       &renderer->textures_view[i],
                       &renderer->textures_sampler[i],
                       &renderer->textures_width[i],
                       &renderer->textures_height[i]);
    }

    texture_create_solid(renderer->device, renderer->physical_device,
                         renderer->command_pool, renderer->graphics_queue,
                         0, 0, 0, 255,
                         &renderer->black_texture_image,
                         &renderer->black_texture_memory,
                         &renderer->black_texture_view,
                         &renderer->black_texture_sampler,
                         &renderer->black_texture_width,
                         &renderer->black_texture_height);

    CREATE_BUFFER_WITH_DATA(renderer->device, renderer->physical_device, BLOCK_VERTICES,
                            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                            &renderer->block_vertex_buffer, &renderer->block_vertex_memory);

    CREATE_BUFFER_WITH_DATA(renderer->device, renderer->physical_device, BLOCK_INDICES,
                            VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                            &renderer->block_index_buffer, &renderer->block_index_memory);

    CREATE_BUFFER_WITH_DATA(renderer->device, renderer->physical_device, EDGE_VERTICES,
                            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                            &renderer->edge_vertex_buffer, &renderer->edge_vertex_memory);

    CREATE_BUFFER_WITH_DATA(renderer->device, renderer->physical_device, EDGE_INDICES,
                            VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                            &renderer->edge_index_buffer, &renderer->edge_index_memory);

    create_buffer(renderer->device, renderer->physical_device, sizeof(Vertex) * 4,
                  VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                  &renderer->crosshair_vertex_buffer, &renderer->crosshair_vertex_memory);

    const uint32_t INVENTORY_MAX_VERTICES = 32;
    create_buffer(renderer->device, renderer->physical_device, sizeof(Vertex) * INVENTORY_MAX_VERTICES,
                  VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                  &renderer->inventory_vertex_buffer, &renderer->inventory_vertex_memory);

    const uint32_t INVENTORY_ICON_VERTEX_COUNT = 6;
    create_buffer(renderer->device, renderer->physical_device, sizeof(Vertex) * INVENTORY_ICON_VERTEX_COUNT,
                  VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                  &renderer->inventory_icon_vertex_buffer, &renderer->inventory_icon_vertex_memory);

    const uint32_t INVENTORY_COUNT_MAX_VERTICES = 1500;
    create_buffer(renderer->device, renderer->physical_device, sizeof(Vertex) * INVENTORY_COUNT_MAX_VERTICES,
                  VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                  &renderer->inventory_count_vertex_buffer, &renderer->inventory_count_vertex_memory);

    const uint32_t INVENTORY_SELECTION_VERTEX_COUNT = 8;
    create_buffer(renderer->device, renderer->physical_device, sizeof(Vertex) * INVENTORY_SELECTION_VERTEX_COUNT,
                  VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                  &renderer->inventory_selection_vertex_buffer, &renderer->inventory_selection_vertex_memory);

    const uint32_t INVENTORY_BG_VERTEX_COUNT = 6;
    create_buffer(renderer->device, renderer->physical_device, sizeof(Vertex) * INVENTORY_BG_VERTEX_COUNT,
                  VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                  &renderer->inventory_bg_vertex_buffer, &renderer->inventory_bg_vertex_memory);

    renderer->instance_capacity = INITIAL_INSTANCE_CAPACITY;
    create_buffer(renderer->device, renderer->physical_device,
                  (VkDeviceSize)renderer->instance_capacity * sizeof(InstanceData),
                  VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                  &renderer->instance_buffer, &renderer->instance_memory);

    renderer->vert_shader = create_shader_module(renderer->device, "shaders/vert.spv");
    renderer->frag_shader = create_shader_module(renderer->device, "shaders/frag.spv");

    VkDescriptorSetLayoutBinding sampler_binding = {.binding = 0,
                                                     .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                                     .descriptorCount = BLOCK_TYPE_COUNT,
                                                     .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT};

    VkDescriptorSetLayoutCreateInfo layout_info = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                                                    .bindingCount = 1,
                                                    .pBindings = &sampler_binding};

    VK_CHECK(vkCreateDescriptorSetLayout(renderer->device, &layout_info, NULL, &renderer->descriptor_layout));

    VkPushConstantRange push_range = {.stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
                                      .offset = 0,
                                      .size = sizeof(PushConstants)};

    VkPipelineLayoutCreateInfo pipeline_layout_info = {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                                                        .setLayoutCount = 1,
                                                        .pSetLayouts = &renderer->descriptor_layout,
                                                        .pushConstantRangeCount = 1,
                                                        .pPushConstantRanges = &push_range};

    VK_CHECK(vkCreatePipelineLayout(renderer->device, &pipeline_layout_info, NULL, &renderer->pipeline_layout));

    VkSemaphoreCreateInfo semaphore_info = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VkFenceCreateInfo fence_info = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
                                    .flags = VK_FENCE_CREATE_SIGNALED_BIT};

    VK_CHECK(vkCreateSemaphore(renderer->device, &semaphore_info, NULL, &renderer->image_available));
    VK_CHECK(vkCreateSemaphore(renderer->device, &semaphore_info, NULL, &renderer->render_finished));
    VK_CHECK(vkCreateFence(renderer->device, &fence_info, NULL, &renderer->in_flight));

    /* Swapchain creation */
    VkPhysicalDevice swap_physical_device = renderer->physical_device;
    VkDevice swap_device = renderer->device;
    VkSurfaceKHR swap_surface = renderer->surface;

    VkSurfaceCapabilitiesKHR swap_capabilities;
    VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(swap_physical_device, swap_surface, &swap_capabilities));

    uint32_t swap_format_count = 0;
    VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(swap_physical_device, swap_surface, &swap_format_count, NULL));
    VkSurfaceFormatKHR *swap_formats = malloc(sizeof(VkSurfaceFormatKHR) * swap_format_count);
    VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(swap_physical_device, swap_surface, &swap_format_count, swap_formats));

    VkSurfaceFormatKHR swap_surface_format = swap_formats[0];
    for (uint32_t i = 0; i < swap_format_count; ++i) {
        if (swap_formats[i].format == VK_FORMAT_B8G8R8A8_SRGB &&
            swap_formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
        {
            swap_surface_format = swap_formats[i];
            break;
        }
    }
    free(swap_formats);

    uint32_t swap_image_count = swap_capabilities.minImageCount + 1;
    if (swap_capabilities.maxImageCount > 0 && swap_image_count > swap_capabilities.maxImageCount) {
        swap_image_count = swap_capabilities.maxImageCount;
    }

    VkExtent2D swap_extent = swap_capabilities.currentExtent;
    if (swap_extent.width == UINT32_MAX) {
        swap_extent.width = framebuffer_width;
        swap_extent.height = framebuffer_height;
    }

    VkSwapchainCreateInfoKHR swapchain_info = {.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
                                                .surface = swap_surface,
                                                .minImageCount = swap_image_count,
                                                .imageFormat = swap_surface_format.format,
                                                .imageColorSpace = swap_surface_format.colorSpace,
                                                .imageExtent = swap_extent,
                                                .imageArrayLayers = 1,
                                                .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                                                .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
                                                .preTransform = swap_capabilities.currentTransform,
                                                .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
                                                .presentMode = VK_PRESENT_MODE_FIFO_KHR,
                                                .clipped = VK_TRUE};

    VK_CHECK(vkCreateSwapchainKHR(swap_device, &swapchain_info, NULL, &renderer->swapchain));

    VK_CHECK(vkGetSwapchainImagesKHR(swap_device, renderer->swapchain, &swap_image_count, NULL));
    renderer->swapchain_images = malloc(sizeof(VkImage) * swap_image_count);
    VK_CHECK(vkGetSwapchainImagesKHR(swap_device, renderer->swapchain, &swap_image_count, renderer->swapchain_images));
    renderer->swapchain_image_count = swap_image_count;
    renderer->swapchain_extent = swap_extent;
    renderer->swapchain_format = swap_surface_format.format;

    renderer->swapchain_image_views = malloc(sizeof(VkImageView) * swap_image_count);
    for (uint32_t i = 0; i < swap_image_count; ++i) {
        create_image_view(swap_device, renderer->swapchain_images[i], swap_surface_format.format,
                          VK_IMAGE_ASPECT_COLOR_BIT, &renderer->swapchain_image_views[i]);
    }

    VkFormat swap_depth_format = VK_FORMAT_D32_SFLOAT;
    create_image(swap_device,
                 swap_physical_device,
                 swap_extent.width,
                 swap_extent.height,
                 swap_depth_format,
                 VK_IMAGE_TILING_OPTIMAL,
                 VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                 &renderer->swapchain_depth_image,
                 &renderer->swapchain_depth_memory);
    create_image_view(swap_device, renderer->swapchain_depth_image, swap_depth_format, VK_IMAGE_ASPECT_DEPTH_BIT, &renderer->swapchain_depth_view);

    VkAttachmentDescription swap_attachments[2] = {
        {.format = swap_surface_format.format, .samples = VK_SAMPLE_COUNT_1_BIT,
         .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR, .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
         .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE, .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
         .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED, .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR},
        {.format = swap_depth_format, .samples = VK_SAMPLE_COUNT_1_BIT,
         .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR, .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
         .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE, .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
         .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED, .finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL}
    };

    VkAttachmentReference swap_color_ref = {.attachment = 0, .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference swap_depth_ref = {.attachment = 1, .layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

    VkSubpassDescription swap_subpass = {.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    .colorAttachmentCount = 1,
                                    .pColorAttachments = &swap_color_ref,
                                    .pDepthStencilAttachment = &swap_depth_ref};

    VkSubpassDependency swap_dependency = {.srcSubpass = VK_SUBPASS_EXTERNAL,
                                      .dstSubpass = 0,
                                      .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
                                      .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
                                      .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT};

    VkRenderPassCreateInfo swap_render_pass_info = {.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
                                                .attachmentCount = ARRAY_LENGTH(swap_attachments),
                                                .pAttachments = swap_attachments,
                                                .subpassCount = 1,
                                                .pSubpasses = &swap_subpass,
                                                .dependencyCount = 1,
                                                .pDependencies = &swap_dependency};

    VK_CHECK(vkCreateRenderPass(swap_device, &swap_render_pass_info, NULL, &renderer->swapchain_render_pass));

    VkPipelineShaderStageCreateInfo swap_shader_stages[2] = {
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = renderer->vert_shader, .pName = "main"},
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = renderer->frag_shader, .pName = "main"}
    };

    /* Vertex binding 0: per-vertex; binding 1: per-instance */
    VkVertexInputBindingDescription swap_bindings[2] = {
        {.binding = 0, .stride = sizeof(Vertex), .inputRate = VK_VERTEX_INPUT_RATE_VERTEX},
        {.binding = 1, .stride = sizeof(InstanceData), .inputRate = VK_VERTEX_INPUT_RATE_INSTANCE}
    };

    VkVertexInputAttributeDescription swap_attributes[4] = {
        {.binding = 0, .location = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(Vertex, pos)},
        {.binding = 0, .location = 1, .format = VK_FORMAT_R32G32_SFLOAT, .offset = offsetof(Vertex, uv)},
        {.binding = 1, .location = 2, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(InstanceData, x)},
        {.binding = 1, .location = 3, .format = VK_FORMAT_R32_UINT, .offset = offsetof(InstanceData, type)}
    };

    VkPipelineVertexInputStateCreateInfo swap_vertex_input = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
                                                          .vertexBindingDescriptionCount = ARRAY_LENGTH(swap_bindings),
                                                          .pVertexBindingDescriptions = swap_bindings,
                                                          .vertexAttributeDescriptionCount = ARRAY_LENGTH(swap_attributes),
                                                          .pVertexAttributeDescriptions = swap_attributes};

    VkPipelineInputAssemblyStateCreateInfo swap_input_assembly_tri = {.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
                                                                  .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};

    VkPipelineInputAssemblyStateCreateInfo swap_input_assembly_lines = {.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
                                                                    .topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST};

    VkViewport swap_viewport = {.width = (float)swap_extent.width, .height = (float)swap_extent.height, .maxDepth = 1.0f};

    VkRect2D swap_scissor = {.extent = swap_extent};

    VkPipelineViewportStateCreateInfo swap_viewport_state = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
                                                         .viewportCount = 1,
                                                         .pViewports = &swap_viewport,
                                                         .scissorCount = 1,
                                                         .pScissors = &swap_scissor};

    VkPipelineRasterizationStateCreateInfo swap_raster_solid = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_BACK_BIT,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .lineWidth = 3.0f
    };
    VkPipelineRasterizationStateCreateInfo swap_raster_wire = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_LINE,
        .cullMode = VK_CULL_MODE_NONE,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .lineWidth = 3.0f
    };

    VkPipelineMultisampleStateCreateInfo swap_multisample = {.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
                                                         .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT};

    VkPipelineDepthStencilStateCreateInfo swap_depth_solid = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = VK_TRUE,
        .depthWriteEnable = VK_TRUE,
        .depthCompareOp = VK_COMPARE_OP_LESS
    };
    VkPipelineDepthStencilStateCreateInfo swap_depth_wire = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = VK_TRUE,
        .depthWriteEnable = VK_FALSE,
        .depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL
    };
    VkPipelineDepthStencilStateCreateInfo swap_depth_cross = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = VK_FALSE,
        .depthWriteEnable = VK_FALSE,
        .depthCompareOp = VK_COMPARE_OP_ALWAYS
    };

    VkPipelineColorBlendAttachmentState swap_color_blend = {
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT
    };

    VkPipelineColorBlendStateCreateInfo swap_color_state = {.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
                                                        .attachmentCount = 1,
                                                        .pAttachments = &swap_color_blend};

    VkGraphicsPipelineCreateInfo swap_pipeline_info = {.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
                                                   .stageCount = ARRAY_LENGTH(swap_shader_stages),
                                                   .pStages = swap_shader_stages,
                                                   .pVertexInputState = &swap_vertex_input,
                                                   .pViewportState = &swap_viewport_state,
                                                   .pMultisampleState = &swap_multisample,
                                                   .pColorBlendState = &swap_color_state,
                                                   .layout = renderer->pipeline_layout,
                                                   .renderPass = renderer->swapchain_render_pass};

    swap_pipeline_info.pInputAssemblyState = &swap_input_assembly_tri;
    swap_pipeline_info.pRasterizationState = &swap_raster_solid;
    swap_pipeline_info.pDepthStencilState = &swap_depth_solid;

    VK_CHECK(vkCreateGraphicsPipelines(swap_device, VK_NULL_HANDLE, 1, &swap_pipeline_info, NULL, &renderer->swapchain_pipeline_solid));

    swap_pipeline_info.pInputAssemblyState = &swap_input_assembly_lines;
    swap_pipeline_info.pRasterizationState = &swap_raster_wire;
    swap_pipeline_info.pDepthStencilState = &swap_depth_wire;
    VK_CHECK(vkCreateGraphicsPipelines(swap_device, VK_NULL_HANDLE, 1, &swap_pipeline_info, NULL, &renderer->swapchain_pipeline_wireframe));

    swap_pipeline_info.pRasterizationState = &swap_raster_wire;
    swap_pipeline_info.pDepthStencilState = &swap_depth_cross;
    VK_CHECK(vkCreateGraphicsPipelines(swap_device, VK_NULL_HANDLE, 1, &swap_pipeline_info, NULL, &renderer->swapchain_pipeline_crosshair));

    swap_pipeline_info.pInputAssemblyState = &swap_input_assembly_tri;
    swap_pipeline_info.pRasterizationState = &swap_raster_solid;
    swap_pipeline_info.pDepthStencilState = &swap_depth_cross;
    VK_CHECK(vkCreateGraphicsPipelines(swap_device, VK_NULL_HANDLE, 1, &swap_pipeline_info, NULL, &renderer->swapchain_pipeline_overlay));

    renderer->swapchain_framebuffers = malloc(sizeof(VkFramebuffer) * swap_image_count);
    for (uint32_t i = 0; i < swap_image_count; ++i) {
        VkImageView attachments_views[] = {renderer->swapchain_image_views[i], renderer->swapchain_depth_view};

        VkFramebufferCreateInfo framebuffer_info = {.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
                                                     .renderPass = renderer->swapchain_render_pass,
                                                     .attachmentCount = ARRAY_LENGTH(attachments_views),
                                                     .pAttachments = attachments_views,
                                                     .width = swap_extent.width,
                                                     .height = swap_extent.height,
                                                     .layers = 1};

        VK_CHECK(vkCreateFramebuffer(swap_device, &framebuffer_info, NULL, &renderer->swapchain_framebuffers[i]));
    }

    /* Descriptors: only sampler array */
    VkDescriptorPoolSize swap_pool_size = {.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                      .descriptorCount = swap_image_count * BLOCK_TYPE_COUNT * 2};

    VkDescriptorPoolCreateInfo swap_pool_info = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
                                            .poolSizeCount = 1,
                                            .pPoolSizes = &swap_pool_size,
                                            .maxSets = swap_image_count * 2};

    VK_CHECK(vkCreateDescriptorPool(swap_device, &swap_pool_info, NULL, &renderer->swapchain_descriptor_pool));

    VkDescriptorSetLayout *swap_layouts = malloc(sizeof(VkDescriptorSetLayout) * swap_image_count);
    for (uint32_t i = 0; i < swap_image_count; ++i) swap_layouts[i] = renderer->descriptor_layout;

    VkDescriptorSetAllocateInfo swap_alloc_info = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                                              .descriptorPool = renderer->swapchain_descriptor_pool,
                                              .descriptorSetCount = swap_image_count,
                                              .pSetLayouts = swap_layouts};

    renderer->swapchain_descriptor_sets_normal = malloc(sizeof(VkDescriptorSet) * swap_image_count);
    renderer->swapchain_descriptor_sets_highlight = malloc(sizeof(VkDescriptorSet) * swap_image_count);

    VK_CHECK(vkAllocateDescriptorSets(swap_device, &swap_alloc_info, renderer->swapchain_descriptor_sets_normal));
    VK_CHECK(vkAllocateDescriptorSets(swap_device, &swap_alloc_info, renderer->swapchain_descriptor_sets_highlight));

    free(swap_layouts);

    for (uint32_t i = 0; i < swap_image_count; ++i) {
        VkDescriptorImageInfo normal_images[BLOCK_TYPE_COUNT];
        VkDescriptorImageInfo highlight_images[BLOCK_TYPE_COUNT];

        for (uint32_t tex = 0; tex < BLOCK_TYPE_COUNT; ++tex) {
            normal_images[tex].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            normal_images[tex].imageView = renderer->textures_view[tex];
            normal_images[tex].sampler = renderer->textures_sampler[tex];

            highlight_images[tex] = normal_images[tex];
        }

        if (BLOCK_TYPE_COUNT > HIGHLIGHT_TEXTURE_INDEX) {
            highlight_images[HIGHLIGHT_TEXTURE_INDEX].imageView = renderer->black_texture_view;
            highlight_images[HIGHLIGHT_TEXTURE_INDEX].sampler = renderer->black_texture_sampler;
        }

        VkWriteDescriptorSet swap_write = {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                                      .dstBinding = 0,
                                      .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                      .descriptorCount = BLOCK_TYPE_COUNT};

        swap_write.dstSet = renderer->swapchain_descriptor_sets_normal[i];
        swap_write.pImageInfo = normal_images;
        vkUpdateDescriptorSets(swap_device, 1, &swap_write, 0, NULL);

        swap_write.dstSet = renderer->swapchain_descriptor_sets_highlight[i];
        swap_write.pImageInfo = highlight_images;
        vkUpdateDescriptorSets(swap_device, 1, &swap_write, 0, NULL);
    }

    renderer->swapchain_command_buffers = malloc(sizeof(VkCommandBuffer) * swap_image_count);

    VkCommandBufferAllocateInfo swap_command_alloc = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
                                                 .commandPool = renderer->command_pool,
                                                 .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                                                 .commandBufferCount = swap_image_count};

    VK_CHECK(vkAllocateCommandBuffers(swap_device, &swap_command_alloc, renderer->swapchain_command_buffers));

    const float crosshair_size = 0.03f;
    float aspect_correction = (float)renderer->swapchain_extent.height / (float)renderer->swapchain_extent.width;

    Vertex crosshair_vertices[] = {
        {{-crosshair_size * aspect_correction, 0.0f, 0.0f}, {0.0f, 0.0f}},
        {{ crosshair_size * aspect_correction, 0.0f, 0.0f}, {1.0f, 0.0f}},
        {{ 0.0f, -crosshair_size, 0.0f}, {0.0f, 0.0f}},
        {{ 0.0f,  crosshair_size, 0.0f}, {1.0f, 0.0f}},
    };

    upload_buffer_data(renderer->device, renderer->crosshair_vertex_memory,
                       crosshair_vertices, sizeof(crosshair_vertices));

    float inv_h_step = 0.0f;
    float inv_v_step = 0.0f;

    Vertex inventory_vertices[INVENTORY_MAX_VERTICES];
    player_inventory_grid_vertices(aspect_correction,
                                   inventory_vertices,
                                   (uint32_t)ARRAY_LENGTH(inventory_vertices),
                                   &renderer->inventory_vertex_count,
                                   &inv_h_step,
                                   &inv_v_step);
    upload_buffer_data(renderer->device, renderer->inventory_vertex_memory,
                       inventory_vertices, renderer->inventory_vertex_count * sizeof(Vertex));

    Vertex icon_vertices[INVENTORY_ICON_VERTEX_COUNT];
    player_inventory_icon_vertices(inv_h_step, inv_v_step,
                                   icon_vertices,
                                   INVENTORY_ICON_VERTEX_COUNT,
                                   &renderer->inventory_icon_vertex_count);

    upload_buffer_data(renderer->device, renderer->inventory_icon_vertex_memory,
                       icon_vertices, renderer->inventory_icon_vertex_count * sizeof(Vertex));

    return renderer;
}

void renderer_destroy(Renderer *renderer) {
    if (!renderer) return;

    vkDeviceWaitIdle(renderer->device);

    VkDevice device = renderer->device;
    VkCommandPool command_pool = renderer->command_pool;

    if (renderer->swapchain_command_buffers) {
        vkFreeCommandBuffers(device, command_pool, renderer->swapchain_image_count, renderer->swapchain_command_buffers);
        free(renderer->swapchain_command_buffers);
        renderer->swapchain_command_buffers = NULL;
    }

    VK_DESTROY(device, DescriptorPool, renderer->swapchain_descriptor_pool);

    if (renderer->swapchain_framebuffers) {
        for (uint32_t i = 0; i < renderer->swapchain_image_count; ++i) {
            vkDestroyFramebuffer(device, renderer->swapchain_framebuffers[i], NULL);
        }
        free(renderer->swapchain_framebuffers);
        renderer->swapchain_framebuffers = NULL;
    }

    VK_DESTROY(device, Pipeline, renderer->swapchain_pipeline_crosshair);
    VK_DESTROY(device, Pipeline, renderer->swapchain_pipeline_overlay);
    VK_DESTROY(device, Pipeline, renderer->swapchain_pipeline_wireframe);
    VK_DESTROY(device, Pipeline, renderer->swapchain_pipeline_solid);

    VK_DESTROY(device, RenderPass, renderer->swapchain_render_pass);

    VK_DESTROY(device, ImageView, renderer->swapchain_depth_view);
    VK_DESTROY(device, Image, renderer->swapchain_depth_image);
    if (renderer->swapchain_depth_memory != VK_NULL_HANDLE) {
        vkFreeMemory(device, renderer->swapchain_depth_memory, NULL);
        renderer->swapchain_depth_memory = VK_NULL_HANDLE;
    }

    if (renderer->swapchain_image_views) {
        for (uint32_t i = 0; i < renderer->swapchain_image_count; ++i) {
            vkDestroyImageView(device, renderer->swapchain_image_views[i], NULL);
        }
        free(renderer->swapchain_image_views);
        renderer->swapchain_image_views = NULL;
    }

    free(renderer->swapchain_images);
    renderer->swapchain_images = NULL;

    VK_DESTROY(device, SwapchainKHR, renderer->swapchain);

    free(renderer->swapchain_descriptor_sets_normal);
    free(renderer->swapchain_descriptor_sets_highlight);
    renderer->swapchain_descriptor_sets_normal = NULL;
    renderer->swapchain_descriptor_sets_highlight = NULL;

    renderer->swapchain_image_count = 0;
    renderer->swapchain_extent = (VkExtent2D){0, 0};
    renderer->swapchain_format = VK_FORMAT_UNDEFINED;

    vkDestroyFence(renderer->device, renderer->in_flight, NULL);
    vkDestroySemaphore(renderer->device, renderer->render_finished, NULL);
    vkDestroySemaphore(renderer->device, renderer->image_available, NULL);

    vkDestroyBuffer(renderer->device, renderer->instance_buffer, NULL);
    vkFreeMemory(renderer->device, renderer->instance_memory, NULL);

    vkDestroyBuffer(renderer->device, renderer->crosshair_vertex_buffer, NULL);
    vkFreeMemory(renderer->device, renderer->crosshair_vertex_memory, NULL);
    vkDestroyBuffer(renderer->device, renderer->inventory_vertex_buffer, NULL);
    vkFreeMemory(renderer->device, renderer->inventory_vertex_memory, NULL);
    vkDestroyBuffer(renderer->device, renderer->inventory_icon_vertex_buffer, NULL);
    vkFreeMemory(renderer->device, renderer->inventory_icon_vertex_memory, NULL);
    vkDestroyBuffer(renderer->device, renderer->inventory_count_vertex_buffer, NULL);
    vkFreeMemory(renderer->device, renderer->inventory_count_vertex_memory, NULL);
    vkDestroyBuffer(renderer->device, renderer->inventory_selection_vertex_buffer, NULL);
    vkFreeMemory(renderer->device, renderer->inventory_selection_vertex_memory, NULL);
    vkDestroyBuffer(renderer->device, renderer->inventory_bg_vertex_buffer, NULL);
    vkFreeMemory(renderer->device, renderer->inventory_bg_vertex_memory, NULL);

    vkDestroyBuffer(renderer->device, renderer->edge_vertex_buffer, NULL);
    vkFreeMemory(renderer->device, renderer->edge_vertex_memory, NULL);
    vkDestroyBuffer(renderer->device, renderer->edge_index_buffer, NULL);
    vkFreeMemory(renderer->device, renderer->edge_index_memory, NULL);

    vkDestroyBuffer(renderer->device, renderer->block_vertex_buffer, NULL);
    vkFreeMemory(renderer->device, renderer->block_vertex_memory, NULL);
    vkDestroyBuffer(renderer->device, renderer->block_index_buffer, NULL);
    vkFreeMemory(renderer->device, renderer->block_index_memory, NULL);

    for (uint32_t i = 0; i < BLOCK_TYPE_COUNT; ++i) {
        texture_destroy(renderer->device,
                        &renderer->textures_image[i],
                        &renderer->textures_memory[i],
                        &renderer->textures_view[i],
                        &renderer->textures_sampler[i],
                        &renderer->textures_width[i],
                        &renderer->textures_height[i]);
    }
    texture_destroy(renderer->device,
                    &renderer->black_texture_image,
                    &renderer->black_texture_memory,
                    &renderer->black_texture_view,
                    &renderer->black_texture_sampler,
                    &renderer->black_texture_width,
                    &renderer->black_texture_height);

    vkDestroyPipelineLayout(renderer->device, renderer->pipeline_layout, NULL);
    vkDestroyDescriptorSetLayout(renderer->device, renderer->descriptor_layout, NULL);
    vkDestroyShaderModule(renderer->device, renderer->vert_shader, NULL);
    vkDestroyShaderModule(renderer->device, renderer->frag_shader, NULL);

    vkDestroyCommandPool(renderer->device, renderer->command_pool, NULL);
    vkDestroyDevice(renderer->device, NULL);
    vkDestroySurfaceKHR(renderer->instance, renderer->surface, NULL);
    vkDestroyInstance(renderer->instance, NULL);

    free(renderer);
}

void renderer_draw_frame(Renderer *renderer,
                         World *world,
                         const Player *player,
                         Camera *camera,
                         bool highlight,
                         IVec3 highlight_cell) {
    if (!renderer) return;

    VK_CHECK(vkWaitForFences(renderer->device, 1, &renderer->in_flight, VK_TRUE, UINT64_MAX));
    VK_CHECK(vkResetFences(renderer->device, 1, &renderer->in_flight));

    uint32_t image_index = 0;
    VkResult acquire = vkAcquireNextImageKHR(renderer->device,
                                             renderer->swapchain,
                                             UINT64_MAX,
                                             renderer->image_available,
                                             VK_NULL_HANDLE,
                                             &image_index);

    if (acquire == VK_ERROR_OUT_OF_DATE_KHR) {
        die("Swapchain out of date during image acquire");
    } else if (acquire != VK_SUCCESS && acquire != VK_SUBOPTIMAL_KHR) {
        die("Failed to acquire swapchain image");
    }

    int render_blocks = world_total_render_blocks(world);
    uint32_t inventory_icon_count =
        player_inventory_icon_instances(player,
                                        (float)renderer->swapchain_extent.height / (float)renderer->swapchain_extent.width,
                                        NULL,
                                        0);

    uint32_t highlight_instance_index = (uint32_t)render_blocks;
    uint32_t crosshair_instance_index = (uint32_t)render_blocks + 1;
    uint32_t inventory_instance_index = (uint32_t)render_blocks + 2;
    uint32_t inventory_selection_instance_index = (uint32_t)render_blocks + 3;
    uint32_t inventory_bg_instance_index = (uint32_t)render_blocks + 4;
    uint32_t inventory_icons_start = (uint32_t)render_blocks + 5;
    uint32_t total_instances = (uint32_t)render_blocks + 5 + inventory_icon_count;

    if (total_instances > renderer->instance_capacity) {
        uint32_t new_cap = renderer->instance_capacity;
        while (new_cap < total_instances) new_cap *= 2;
        if (new_cap > MAX_INSTANCE_CAPACITY) die("Exceeded maximum instance buffer capacity");

        vkDeviceWaitIdle(renderer->device);
        vkDestroyBuffer(renderer->device, renderer->instance_buffer, NULL);
        vkFreeMemory(renderer->device, renderer->instance_memory, NULL);

        renderer->instance_capacity = new_cap;
        create_buffer(renderer->device, renderer->physical_device,
                      (VkDeviceSize)renderer->instance_capacity * sizeof(InstanceData),
                      VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      &renderer->instance_buffer, &renderer->instance_memory);
    }

    InstanceData *instances = NULL;
    VK_CHECK(vkMapMemory(renderer->device, renderer->instance_memory, 0,
                         (VkDeviceSize)total_instances * sizeof(InstanceData),
                         0, (void **)&instances));

    uint32_t w = 0;
    for (int ci = 0; ci < world->chunk_count; ++ci) {
        Chunk *chunk = world->chunks[ci];
        for (int bi = 0; bi < chunk->block_count; ++bi) {
            Block b = chunk->blocks[bi];
            instances[w++] = (InstanceData){ (float)b.pos.x, (float)b.pos.y, (float)b.pos.z, (uint32_t)b.type };
        }
    }

    if (highlight) {
        instances[highlight_instance_index] = (InstanceData){
            (float)highlight_cell.x, (float)highlight_cell.y, (float)highlight_cell.z, (uint32_t)HIGHLIGHT_TEXTURE_INDEX
        };
    } else {
        instances[highlight_instance_index] = (InstanceData){0,0,0,(uint32_t)HIGHLIGHT_TEXTURE_INDEX};
    }
    instances[crosshair_instance_index] = (InstanceData){0,0,0,(uint32_t)CROSSHAIR_TEXTURE_INDEX};
    instances[inventory_instance_index] = (InstanceData){0,0,0,(uint32_t)HIGHLIGHT_TEXTURE_INDEX};
    instances[inventory_selection_instance_index] = (InstanceData){0,0,0,(uint32_t)INVENTORY_SELECTION_TEXTURE_INDEX};
    instances[inventory_bg_instance_index] = (InstanceData){0,0,0,(uint32_t)INVENTORY_BG_TEXTURE_INDEX};

    if (inventory_icon_count > 0) {
        float aspect = (float)renderer->swapchain_extent.height / (float)renderer->swapchain_extent.width;
        player_inventory_icon_instances(player,
                                        aspect,
                                        &instances[inventory_icons_start],
                                        inventory_icon_count);
    }

    renderer->inventory_selection_vertex_count = 0;
    renderer->inventory_bg_vertex_count = 0;
    renderer->inventory_count_vertex_count = 0;
    if (player->inventory_open) {
        float aspect = (float)renderer->swapchain_extent.height / (float)renderer->swapchain_extent.width;

        const uint32_t INVENTORY_BG_VERTEX_COUNT = 6;
        Vertex bg_vertices[INVENTORY_BG_VERTEX_COUNT];
        const float inv_half = 0.2f;
        const float inv_half_x = inv_half * aspect * ((float)INVENTORY_COLS / (float)INVENTORY_ROWS);
        const float left = -inv_half_x;
        const float right = inv_half_x;
        const float bottom = -inv_half;
        const float top = inv_half;

        bg_vertices[0] = (Vertex){{left,  top,    0.0f}, {0.0f, 0.0f}};
        bg_vertices[1] = (Vertex){{right, bottom, 0.0f}, {1.0f, 1.0f}};
        bg_vertices[2] = (Vertex){{right, top,    0.0f}, {1.0f, 0.0f}};
        bg_vertices[3] = (Vertex){{left,  top,    0.0f}, {0.0f, 0.0f}};
        bg_vertices[4] = (Vertex){{left,  bottom, 0.0f}, {0.0f, 1.0f}};
        bg_vertices[5] = (Vertex){{right, bottom, 0.0f}, {1.0f, 1.0f}};

        renderer->inventory_bg_vertex_count = INVENTORY_BG_VERTEX_COUNT;
        upload_buffer_data(renderer->device, renderer->inventory_bg_vertex_memory,
                           bg_vertices, renderer->inventory_bg_vertex_count * sizeof(Vertex));

        const uint32_t INVENTORY_SELECTION_VERTEX_COUNT = 8;
        Vertex selection_vertices[INVENTORY_SELECTION_VERTEX_COUNT];
        player_inventory_selection_vertices((int)player->selected_slot,
                                             aspect,
                                             selection_vertices,
                                             INVENTORY_SELECTION_VERTEX_COUNT,
                                             &renderer->inventory_selection_vertex_count);
        if (renderer->inventory_selection_vertex_count > 0) {
            upload_buffer_data(renderer->device, renderer->inventory_selection_vertex_memory,
                               selection_vertices, renderer->inventory_selection_vertex_count * sizeof(Vertex));
        }

        const uint32_t INVENTORY_COUNT_MAX_VERTICES = 1500;
        Vertex count_vertices[INVENTORY_COUNT_MAX_VERTICES];
        renderer->inventory_count_vertex_count =
            player_inventory_count_vertices(player, aspect,
                                            count_vertices,
                                            INVENTORY_COUNT_MAX_VERTICES);

        if (renderer->inventory_count_vertex_count > 0) {
            upload_buffer_data(renderer->device, renderer->inventory_count_vertex_memory,
                               count_vertices, renderer->inventory_count_vertex_count * sizeof(Vertex));
        }
    }

    vkUnmapMemory(renderer->device, renderer->instance_memory);

    PushConstants pc = {0};
    pc.view = camera_view_matrix(camera);
    pc.proj = mat4_perspective(55.0f * (float)M_PI / 180.0f,
                               (float)renderer->swapchain_extent.width / (float)renderer->swapchain_extent.height,
                               0.1f,
                               200.0f);

    PushConstants pc_identity = {0};
    pc_identity.view = mat4_identity();
    pc_identity.proj = mat4_identity();

    PushConstants pc_overlay = pc_identity;
    pc_overlay.proj.m[5] = -1.0f;

    VK_CHECK(vkResetCommandBuffer(renderer->swapchain_command_buffers[image_index], 0));

    VkCommandBufferBeginInfo begin_info = {0};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    VK_CHECK(vkBeginCommandBuffer(renderer->swapchain_command_buffers[image_index], &begin_info));

    VkClearValue clears[2];
    clears[0].color.float32[0] = 0.1f;
    clears[0].color.float32[1] = 0.12f;
    clears[0].color.float32[2] = 0.18f;
    clears[0].color.float32[3] = 1.0f;
    clears[1].depthStencil.depth = 1.0f;

    VkRenderPassBeginInfo rp_begin = {0};
    rp_begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp_begin.renderPass = renderer->swapchain_render_pass;
    rp_begin.framebuffer = renderer->swapchain_framebuffers[image_index];
    rp_begin.renderArea.extent = renderer->swapchain_extent;
    rp_begin.clearValueCount = ARRAY_LENGTH(clears);
    rp_begin.pClearValues = clears;

    vkCmdBeginRenderPass(renderer->swapchain_command_buffers[image_index], &rp_begin, VK_SUBPASS_CONTENTS_INLINE);

    VkDeviceSize offsets[2] = {0, 0};
    VkBuffer vbs[2];

    vkCmdBindPipeline(renderer->swapchain_command_buffers[image_index], VK_PIPELINE_BIND_POINT_GRAPHICS, renderer->swapchain_pipeline_solid);
    vkCmdPushConstants(renderer->swapchain_command_buffers[image_index], renderer->pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc), &pc);

    vbs[0] = renderer->block_vertex_buffer;
    vbs[1] = renderer->instance_buffer;
    vkCmdBindVertexBuffers(renderer->swapchain_command_buffers[image_index], 0, 2, vbs, offsets);
    vkCmdBindIndexBuffer(renderer->swapchain_command_buffers[image_index], renderer->block_index_buffer, 0, VK_INDEX_TYPE_UINT16);

    vkCmdBindDescriptorSets(renderer->swapchain_command_buffers[image_index],
                            VK_PIPELINE_BIND_POINT_GRAPHICS,
                            renderer->pipeline_layout,
                            0,
                            1,
                            &renderer->swapchain_descriptor_sets_normal[image_index],
                            0,
                            NULL);

    if (render_blocks > 0) {
        vkCmdDrawIndexed(renderer->swapchain_command_buffers[image_index],
                         (uint32_t)ARRAY_LENGTH(BLOCK_INDICES),
                         (uint32_t)render_blocks,
                         0, 0, 0);
    }

    if (highlight) {
        vkCmdBindPipeline(renderer->swapchain_command_buffers[image_index], VK_PIPELINE_BIND_POINT_GRAPHICS, renderer->swapchain_pipeline_wireframe);
        vkCmdPushConstants(renderer->swapchain_command_buffers[image_index], renderer->pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc), &pc);

        vbs[0] = renderer->edge_vertex_buffer;
        vbs[1] = renderer->instance_buffer;
        vkCmdBindVertexBuffers(renderer->swapchain_command_buffers[image_index], 0, 2, vbs, offsets);
        vkCmdBindIndexBuffer(renderer->swapchain_command_buffers[image_index], renderer->edge_index_buffer, 0, VK_INDEX_TYPE_UINT16);

        vkCmdBindDescriptorSets(renderer->swapchain_command_buffers[image_index],
                                VK_PIPELINE_BIND_POINT_GRAPHICS,
                                renderer->pipeline_layout,
                                0,
                                1,
                    &renderer->swapchain_descriptor_sets_highlight[image_index],
                                0,
                                NULL);

        vkCmdDrawIndexed(renderer->swapchain_command_buffers[image_index],
                         (uint32_t)ARRAY_LENGTH(EDGE_INDICES),
                         1,
                         0, 0, highlight_instance_index);
    }

    if (!player->inventory_open) {
        vkCmdBindPipeline(renderer->swapchain_command_buffers[image_index], VK_PIPELINE_BIND_POINT_GRAPHICS, renderer->swapchain_pipeline_crosshair);
        vkCmdPushConstants(renderer->swapchain_command_buffers[image_index], renderer->pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc_overlay), &pc_overlay);

        vbs[0] = renderer->crosshair_vertex_buffer;
        vbs[1] = renderer->instance_buffer;
        vkCmdBindVertexBuffers(renderer->swapchain_command_buffers[image_index], 0, 2, vbs, offsets);

        vkCmdBindDescriptorSets(renderer->swapchain_command_buffers[image_index],
                                VK_PIPELINE_BIND_POINT_GRAPHICS,
                                renderer->pipeline_layout,
                                0,
                                1,
                    &renderer->swapchain_descriptor_sets_highlight[image_index],
                                0,
                                NULL);

        vkCmdDraw(renderer->swapchain_command_buffers[image_index], 4, 1, 0, crosshair_instance_index);
    }

    if (player->inventory_open && renderer->inventory_vertex_count > 0) {
        if (renderer->inventory_bg_vertex_count > 0) {
            vkCmdBindPipeline(renderer->swapchain_command_buffers[image_index], VK_PIPELINE_BIND_POINT_GRAPHICS, renderer->swapchain_pipeline_overlay);
            vkCmdPushConstants(renderer->swapchain_command_buffers[image_index], renderer->pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc_overlay), &pc_overlay);

            vbs[0] = renderer->inventory_bg_vertex_buffer;
            vbs[1] = renderer->instance_buffer;
            vkCmdBindVertexBuffers(renderer->swapchain_command_buffers[image_index], 0, 2, vbs, offsets);

            vkCmdBindDescriptorSets(renderer->swapchain_command_buffers[image_index],
                                    VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    renderer->pipeline_layout,
                                    0,
                                    1,
                                    &renderer->swapchain_descriptor_sets_highlight[image_index],
                                    0,
                                    NULL);

            vkCmdDraw(renderer->swapchain_command_buffers[image_index], renderer->inventory_bg_vertex_count, 1, 0, inventory_bg_instance_index);
        }

        vkCmdBindPipeline(renderer->swapchain_command_buffers[image_index], VK_PIPELINE_BIND_POINT_GRAPHICS, renderer->swapchain_pipeline_crosshair);
        vkCmdPushConstants(renderer->swapchain_command_buffers[image_index], renderer->pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc_overlay), &pc_overlay);

        vbs[0] = renderer->inventory_vertex_buffer;
        vbs[1] = renderer->instance_buffer;
        vkCmdBindVertexBuffers(renderer->swapchain_command_buffers[image_index], 0, 2, vbs, offsets);

        vkCmdBindDescriptorSets(renderer->swapchain_command_buffers[image_index],
                                VK_PIPELINE_BIND_POINT_GRAPHICS,
                                renderer->pipeline_layout,
                                0,
                                1,
                    &renderer->swapchain_descriptor_sets_highlight[image_index],
                                0,
                                NULL);

        vkCmdDraw(renderer->swapchain_command_buffers[image_index], renderer->inventory_vertex_count, 1, 0, inventory_instance_index);
    }

    if (player->inventory_open && renderer->inventory_selection_vertex_count > 0) {
        vkCmdBindPipeline(renderer->swapchain_command_buffers[image_index], VK_PIPELINE_BIND_POINT_GRAPHICS, renderer->swapchain_pipeline_crosshair);
        vkCmdPushConstants(renderer->swapchain_command_buffers[image_index], renderer->pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc_overlay), &pc_overlay);

        vbs[0] = renderer->inventory_selection_vertex_buffer;
        vbs[1] = renderer->instance_buffer;
        vkCmdBindVertexBuffers(renderer->swapchain_command_buffers[image_index], 0, 2, vbs, offsets);

        vkCmdBindDescriptorSets(renderer->swapchain_command_buffers[image_index],
                                VK_PIPELINE_BIND_POINT_GRAPHICS,
                                renderer->pipeline_layout,
                                0,
                                1,
                    &renderer->swapchain_descriptor_sets_highlight[image_index],
                                0,
                                NULL);

        vkCmdDraw(renderer->swapchain_command_buffers[image_index], renderer->inventory_selection_vertex_count, 1, 0, inventory_selection_instance_index);
    }

    if (player->inventory_open && inventory_icon_count > 0) {
        vkCmdBindPipeline(renderer->swapchain_command_buffers[image_index], VK_PIPELINE_BIND_POINT_GRAPHICS, renderer->swapchain_pipeline_solid);
        vkCmdPushConstants(renderer->swapchain_command_buffers[image_index], renderer->pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc_overlay), &pc_overlay);

        vbs[0] = renderer->inventory_icon_vertex_buffer;
        vbs[1] = renderer->instance_buffer;
        vkCmdBindVertexBuffers(renderer->swapchain_command_buffers[image_index], 0, 2, vbs, offsets);

        vkCmdBindDescriptorSets(renderer->swapchain_command_buffers[image_index],
                                VK_PIPELINE_BIND_POINT_GRAPHICS,
                                renderer->pipeline_layout,
                                0,
                                1,
                    &renderer->swapchain_descriptor_sets_normal[image_index],
                                0,
                                NULL);

        vkCmdDraw(renderer->swapchain_command_buffers[image_index], renderer->inventory_icon_vertex_count, inventory_icon_count, 0, inventory_icons_start);
    }

    if (player->inventory_open && renderer->inventory_count_vertex_count > 0) {
        vkCmdBindPipeline(renderer->swapchain_command_buffers[image_index], VK_PIPELINE_BIND_POINT_GRAPHICS, renderer->swapchain_pipeline_crosshair);
        vkCmdPushConstants(renderer->swapchain_command_buffers[image_index], renderer->pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc_overlay), &pc_overlay);

        vbs[0] = renderer->inventory_count_vertex_buffer;
        vbs[1] = renderer->instance_buffer;
        vkCmdBindVertexBuffers(renderer->swapchain_command_buffers[image_index], 0, 2, vbs, offsets);

        vkCmdBindDescriptorSets(renderer->swapchain_command_buffers[image_index],
                                VK_PIPELINE_BIND_POINT_GRAPHICS,
                                renderer->pipeline_layout,
                                0,
                                1,
                    &renderer->swapchain_descriptor_sets_highlight[image_index],
                                0,
                                NULL);

        vkCmdDraw(renderer->swapchain_command_buffers[image_index], renderer->inventory_count_vertex_count, 1, 0, inventory_instance_index);
    }

    vkCmdEndRenderPass(renderer->swapchain_command_buffers[image_index]);
    VK_CHECK(vkEndCommandBuffer(renderer->swapchain_command_buffers[image_index]));

    VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

    VkSubmitInfo submit = {0};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.waitSemaphoreCount = 1;
    submit.pWaitSemaphores = &renderer->image_available;
    submit.pWaitDstStageMask = &wait_stage;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &renderer->swapchain_command_buffers[image_index];
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores = &renderer->render_finished;

    VK_CHECK(vkQueueSubmit(renderer->graphics_queue, 1, &submit, renderer->in_flight));

    VkPresentInfoKHR present = {0};
    present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = &renderer->render_finished;
    present.swapchainCount = 1;
    present.pSwapchains = &renderer->swapchain;
    present.pImageIndices = &image_index;

    VkResult present_result = vkQueuePresentKHR(renderer->graphics_queue, &present);
    if (present_result == VK_ERROR_OUT_OF_DATE_KHR) {
        die("Swapchain out of date during present");
    } else if (present_result != VK_SUCCESS && present_result != VK_SUBOPTIMAL_KHR) {
        die("Failed to present swapchain image");
    }
}