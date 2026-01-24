#include "voxel.h"
#include <stdlib.h>
#include <string.h>

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

static void die(const char *message) {
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

static bool load_png_rgba(const char *filename, ImageData *out_image) {
    FILE *fp = fopen(filename, "rb");
    if (!fp) return false;

    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png) { fclose(fp); return false; }

    png_infop info = png_create_info_struct(png);
    if (!info) { png_destroy_read_struct(&png, NULL, NULL); fclose(fp); return false; }

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
    free(image->pixels);
    *image = (ImageData){0};
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

    transition_image_layout(device, command_pool, graphics_queue,
                            texture->image, VK_FORMAT_R8G8B8A8_SRGB,
                            VK_IMAGE_LAYOUT_UNDEFINED,
                            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    copy_buffer_to_image(device, command_pool, graphics_queue,
                         staging_buffer, texture->image, width, height);

    transition_image_layout(device, command_pool, graphics_queue,
                            texture->image, VK_FORMAT_R8G8B8A8_SRGB,
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

void texture_create_from_file(VkDevice device,
                                     VkPhysicalDevice physical_device,
                                     VkCommandPool command_pool,
                                     VkQueue graphics_queue,
                                     const char *filename,
                                     Texture *texture) {
    ImageData image = {0};
    if (!load_png_rgba(filename, &image)) die("Failed to load PNG texture");

    texture_create_from_pixels(device, physical_device, command_pool, graphics_queue,
                               image.pixels, image.width, image.height, texture);

    free_image(&image);
}

void texture_create_solid(VkDevice device,
                                 VkPhysicalDevice physical_device,
                                 VkCommandPool command_pool,
                                 VkQueue graphics_queue,
                                 uint8_t r, uint8_t g, uint8_t b, uint8_t a,
                                 Texture *texture) {
    uint8_t pixel[4] = {r, g, b, a};
    texture_create_from_pixels(device, physical_device, command_pool, graphics_queue,
                               pixel, 1, 1, texture);
}

void texture_destroy(VkDevice device, Texture *texture) {
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
    if (!file) return NULL;

    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    if (file_size <= 0) { fclose(file); return NULL; }

    char *buffer = malloc((size_t)file_size);
    if (!buffer) { fclose(file); return NULL; }

    size_t read = fread(buffer, 1, (size_t)file_size, file);
    fclose(file);

    if (read != (size_t)file_size) {
        free(buffer);
        return NULL;
    }

    *out_size = (size_t)file_size;
    return buffer;
}

VkShaderModule create_shader_module(VkDevice device, const char *filepath) {
    size_t size = 0;
    char *code = read_binary_file(filepath, &size);
    if (!code) die("Failed to read shader file");

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
/* Vulkan Swapchain Resources                                                 */
/* -------------------------------------------------------------------------- */

void swapchain_resources_reset(SwapchainResources *res) {
    *res = (SwapchainResources){0};
}

void swapchain_destroy(VkDevice device,
                        VkCommandPool command_pool,
                        SwapchainResources *res) {
    if (res->command_buffers) {
        vkFreeCommandBuffers(device, command_pool, res->image_count, res->command_buffers);
        free(res->command_buffers);
        res->command_buffers = NULL;
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

    free(res->images);
    res->images = NULL;

    if (res->swapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(device, res->swapchain, NULL);
        res->swapchain = VK_NULL_HANDLE;
    }

    free(res->descriptor_sets_normal);
    free(res->descriptor_sets_highlight);
    res->descriptor_sets_normal = NULL;
    res->descriptor_sets_highlight = NULL;

    res->image_count = 0;
    res->extent = (VkExtent2D){0, 0};
    res->format = VK_FORMAT_UNDEFINED;
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
/* Swapchain creation                                                         */
/* -------------------------------------------------------------------------- */

void swapchain_create(SwapchainContext *ctx,
                        SwapchainResources *res,
                        uint32_t framebuffer_width,
                        uint32_t framebuffer_height) {
    VkPhysicalDevice physical_device = ctx->physical_device;
    VkDevice device = ctx->device;
    VkSurfaceKHR surface = ctx->surface;

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

    create_depth_resources(device, physical_device, extent,
                           &res->depth_image, &res->depth_memory, &res->depth_view);

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

    /* Vertex binding 0: per-vertex; binding 1: per-instance */
    VkVertexInputBindingDescription bindings[2] = {0};
    bindings[0].binding = 0;
    bindings[0].stride = sizeof(Vertex);
    bindings[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    bindings[1].binding = 1;
    bindings[1].stride = sizeof(InstanceData);
    bindings[1].inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;

    VkVertexInputAttributeDescription attributes[4] = {0};
    attributes[0].binding = 0;
    attributes[0].location = 0;
    attributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributes[0].offset = offsetof(Vertex, pos);

    attributes[1].binding = 0;
    attributes[1].location = 1;
    attributes[1].format = VK_FORMAT_R32G32_SFLOAT;
    attributes[1].offset = offsetof(Vertex, uv);

    attributes[2].binding = 1;
    attributes[2].location = 2;
    attributes[2].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributes[2].offset = offsetof(InstanceData, x);

    attributes[3].binding = 1;
    attributes[3].location = 3;
    attributes[3].format = VK_FORMAT_R32_UINT;
    attributes[3].offset = offsetof(InstanceData, type);

    VkPipelineVertexInputStateCreateInfo vertex_input = {0};
    vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertex_input.vertexBindingDescriptionCount = ARRAY_LENGTH(bindings);
    vertex_input.pVertexBindingDescriptions = bindings;
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

    /* Descriptors: only sampler array */
    VkDescriptorPoolSize pool_size = {0};
    pool_size.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    pool_size.descriptorCount = image_count * ctx->texture_count * 2;

    VkDescriptorPoolCreateInfo pool_info = {0};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.poolSizeCount = 1;
    pool_info.pPoolSizes = &pool_size;
    pool_info.maxSets = image_count * 2;

    VK_CHECK(vkCreateDescriptorPool(device, &pool_info, NULL, &res->descriptor_pool));

    VkDescriptorSetLayout *layouts = malloc(sizeof(VkDescriptorSetLayout) * image_count);
    for (uint32_t i = 0; i < image_count; ++i) layouts[i] = ctx->descriptor_set_layout;

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
        VkDescriptorImageInfo normal_images[BLOCK_TYPE_COUNT];
        VkDescriptorImageInfo highlight_images[BLOCK_TYPE_COUNT];

        for (uint32_t tex = 0; tex < ctx->texture_count; ++tex) {
            normal_images[tex].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            normal_images[tex].imageView = ctx->textures[tex].view;
            normal_images[tex].sampler = ctx->textures[tex].sampler;

            highlight_images[tex] = normal_images[tex];
        }

        if (ctx->texture_count > HIGHLIGHT_TEXTURE_INDEX) {
            highlight_images[HIGHLIGHT_TEXTURE_INDEX].imageView = ctx->black_texture->view;
            highlight_images[HIGHLIGHT_TEXTURE_INDEX].sampler = ctx->black_texture->sampler;
        }

        VkWriteDescriptorSet write = {0};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstBinding = 0;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.descriptorCount = ctx->texture_count;

        write.dstSet = res->descriptor_sets_normal[i];
        write.pImageInfo = normal_images;
        vkUpdateDescriptorSets(device, 1, &write, 0, NULL);

        write.dstSet = res->descriptor_sets_highlight[i];
        write.pImageInfo = highlight_images;
        vkUpdateDescriptorSets(device, 1, &write, 0, NULL);
    }

    res->command_buffers = malloc(sizeof(VkCommandBuffer) * image_count);

    VkCommandBufferAllocateInfo command_alloc = {0};
    command_alloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    command_alloc.commandPool = ctx->command_pool;
    command_alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    command_alloc.commandBufferCount = image_count;

    VK_CHECK(vkAllocateCommandBuffers(device, &command_alloc, res->command_buffers));
}