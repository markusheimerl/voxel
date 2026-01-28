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
/* Error Handling                                                             */
/* -------------------------------------------------------------------------- */

_Noreturn void die(const char *message) {
    fprintf(stderr, "Error: %s\n", message);
    exit(EXIT_FAILURE);
}

const char *vk_result_to_string(VkResult result) {
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
        fprintf(stderr, "%s failed: %s\n", #call, vk_result_to_string(result__)); \
        exit(EXIT_FAILURE); \
    } \
} while (0)

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

    VkImage textures[BLOCK_TYPE_COUNT];
    VkDeviceMemory textures_memory[BLOCK_TYPE_COUNT];
    VkImageView textures_view[BLOCK_TYPE_COUNT];
    VkSampler textures_sampler[BLOCK_TYPE_COUNT];

    VkBuffer block_vertex_buffer, block_index_buffer;
    VkDeviceMemory block_vertex_memory, block_index_memory;

    VkBuffer edge_vertex_buffer, edge_index_buffer;
    VkDeviceMemory edge_vertex_memory, edge_index_memory;

    VkBuffer crosshair_buffer, inventory_grid_buffer, inventory_icon_buffer;
    VkBuffer inventory_count_buffer, inventory_selection_buffer, inventory_bg_buffer;
    VkDeviceMemory crosshair_memory, inventory_grid_memory, inventory_icon_memory;
    VkDeviceMemory inventory_count_memory, inventory_selection_memory, inventory_bg_memory;
    
    uint32_t inventory_grid_count, inventory_icon_count;

    VkBuffer instance_buffer;
    VkDeviceMemory instance_memory;
    uint32_t instance_capacity;

    VkDescriptorSetLayout descriptor_layout;
    VkPipelineLayout pipeline_layout;

    VkSwapchainKHR swapchain;
    VkImageView *swapchain_views;
    VkFramebuffer *swapchain_framebuffers;
    VkRenderPass render_pass;
    
    VkPipeline pipeline_solid, pipeline_wireframe, pipeline_crosshair, pipeline_overlay;

    VkDescriptorPool descriptor_pool;
    VkDescriptorSet *descriptor_sets_normal;
    VkDescriptorSet *descriptor_sets_highlight;

    VkCommandBuffer *command_buffers;

    VkImage depth_image;
    VkDeviceMemory depth_memory;
    VkImageView depth_view;

    uint32_t image_count;
    VkExtent2D extent;
    VkFormat surface_format;

    VkSemaphore image_available, render_finished;
    VkFence in_flight;
};

/* -------------------------------------------------------------------------- */
/* Memory Helpers                                                             */
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

static void upload_buffer(VkDevice dev, VkDeviceMemory mem, const void *data, VkDeviceSize size) {
    void *mapped;
    VK_CHECK(vkMapMemory(dev, mem, 0, size, 0, &mapped));
    memcpy(mapped, data, size);
    vkUnmapMemory(dev, mem);
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

static void load_texture(Renderer *r, const char *filename, VkImage *img, VkDeviceMemory *mem,
                        VkImageView *view, VkSampler *sampler) {
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
    
    uint32_t w = png_get_image_width(png, info);
    uint32_t h = png_get_image_height(png, info);
    
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
    uint8_t *pixels = malloc(row_bytes * h);
    png_bytep *row_ptrs = malloc(sizeof(png_bytep) * h);
    
    for (uint32_t y = 0; y < h; y++) row_ptrs[y] = pixels + y * row_bytes;
    
    png_read_image(png, row_ptrs);
    png_read_end(png, NULL);
    
    free(row_ptrs);
    png_destroy_read_struct(&png, &info, NULL);
    fclose(fp);
    
    VkDeviceSize img_size = w * h * 4;
    
    VkBuffer staging_buf;
    VkDeviceMemory staging_mem;
    create_buffer(r, img_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 &staging_buf, &staging_mem);
    
    upload_buffer(r->device, staging_mem, pixels, img_size);
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

static VkPipeline create_pipeline(Renderer *r, VkShaderModule vert, VkShaderModule frag,
                                 VkPrimitiveTopology topology, VkPolygonMode polygon_mode,
                                 VkCullModeFlags cull, bool depth_test, bool depth_write) {
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
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT
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
/* Renderer Creation                                                          */
/* -------------------------------------------------------------------------- */

Renderer *renderer_create(void *display, unsigned long window, uint32_t fb_w, uint32_t fb_h) {
    Renderer *r = calloc(1, sizeof(*r));
    if (!r) die("Failed to allocate renderer");
    
    // Instance
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
    
    // Surface
    VkXlibSurfaceCreateInfoKHR surf_info = {
        .sType = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR,
        .dpy = (Display *)display,
        .window = (Window)window
    };
    VK_CHECK(vkCreateXlibSurfaceKHR(r->instance, &surf_info, NULL, &r->surface));
    
    // Physical Device
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
    
    // Logical Device
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
    
    // Command Pool
    VkCommandPoolCreateInfo pool_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .queueFamilyIndex = r->graphics_family,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT
    };
    VK_CHECK(vkCreateCommandPool(r->device, &pool_info, NULL, &r->command_pool));
    
    // Textures
    const char *tex_files[BLOCK_TYPE_COUNT] = {
        "textures/dirt.png", "textures/stone.png", "textures/grass.png",
        "textures/sand.png", "textures/water.png", "textures/wood.png", "textures/leaves.png"
    };
    
    for (uint32_t i = 0; i < BLOCK_TYPE_COUNT; i++) {
        load_texture(r, tex_files[i], &r->textures[i], &r->textures_memory[i],
                    &r->textures_view[i], &r->textures_sampler[i]);
    }
    
    // Static Geometry Buffers
    create_buffer(r, sizeof(BLOCK_VERTICES), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 &r->block_vertex_buffer, &r->block_vertex_memory);
    upload_buffer(r->device, r->block_vertex_memory, BLOCK_VERTICES, sizeof(BLOCK_VERTICES));
    
    create_buffer(r, sizeof(BLOCK_INDICES), VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 &r->block_index_buffer, &r->block_index_memory);
    upload_buffer(r->device, r->block_index_memory, BLOCK_INDICES, sizeof(BLOCK_INDICES));
    
    create_buffer(r, sizeof(EDGE_VERTICES), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 &r->edge_vertex_buffer, &r->edge_vertex_memory);
    upload_buffer(r->device, r->edge_vertex_memory, EDGE_VERTICES, sizeof(EDGE_VERTICES));
    
    create_buffer(r, sizeof(EDGE_INDICES), VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 &r->edge_index_buffer, &r->edge_index_memory);
    upload_buffer(r->device, r->edge_index_memory, EDGE_INDICES, sizeof(EDGE_INDICES));
    
    // Dynamic Buffers
    create_buffer(r, sizeof(Vertex) * 4, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 &r->crosshair_buffer, &r->crosshair_memory);
    
    create_buffer(r, sizeof(Vertex) * 32, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 &r->inventory_grid_buffer, &r->inventory_grid_memory);
    
    create_buffer(r, sizeof(Vertex) * 6, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 &r->inventory_icon_buffer, &r->inventory_icon_memory);
    
    create_buffer(r, sizeof(Vertex) * 1500, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 &r->inventory_count_buffer, &r->inventory_count_memory);
    
    create_buffer(r, sizeof(Vertex) * 8, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 &r->inventory_selection_buffer, &r->inventory_selection_memory);
    
    create_buffer(r, sizeof(Vertex) * 6, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 &r->inventory_bg_buffer, &r->inventory_bg_memory);
    
    r->instance_capacity = INITIAL_INSTANCE_CAPACITY;
    create_buffer(r, r->instance_capacity * sizeof(InstanceData), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 &r->instance_buffer, &r->instance_memory);
    
    // Descriptor Set Layout
    VkDescriptorSetLayoutBinding sampler_binding = {
        .binding = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = BLOCK_TYPE_COUNT,
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT
    };
    
    VkDescriptorSetLayoutCreateInfo layout_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1,
        .pBindings = &sampler_binding
    };
    VK_CHECK(vkCreateDescriptorSetLayout(r->device, &layout_info, NULL, &r->descriptor_layout));
    
    // Pipeline Layout
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
    
    // Swapchain
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
    
    // Depth Buffer
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
    
    // Render Pass
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
    
    // Pipelines
    VkShaderModule vert = load_shader(r->device, "shaders/vert.spv");
    VkShaderModule frag = load_shader(r->device, "shaders/frag.spv");
    
    r->pipeline_solid = create_pipeline(r, vert, frag, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
                                       VK_POLYGON_MODE_FILL, VK_CULL_MODE_BACK_BIT, true, true);
    
    r->pipeline_wireframe = create_pipeline(r, vert, frag, VK_PRIMITIVE_TOPOLOGY_LINE_LIST,
                                           VK_POLYGON_MODE_LINE, VK_CULL_MODE_NONE, true, false);
    
    r->pipeline_crosshair = create_pipeline(r, vert, frag, VK_PRIMITIVE_TOPOLOGY_LINE_LIST,
                                           VK_POLYGON_MODE_LINE, VK_CULL_MODE_NONE, false, false);
    
    r->pipeline_overlay = create_pipeline(r, vert, frag, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
                                         VK_POLYGON_MODE_FILL, VK_CULL_MODE_BACK_BIT, false, false);
    
    vkDestroyShaderModule(r->device, vert, NULL);
    vkDestroyShaderModule(r->device, frag, NULL);
    
    // Framebuffers
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
    
    // Descriptor Pool
    VkDescriptorPoolSize pool_size = {
        .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = r->image_count * BLOCK_TYPE_COUNT * 2
    };
    
    VkDescriptorPoolCreateInfo desc_pool_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .poolSizeCount = 1, .pPoolSizes = &pool_size,
        .maxSets = r->image_count * 2
    };
    VK_CHECK(vkCreateDescriptorPool(r->device, &desc_pool_info, NULL, &r->descriptor_pool));
    
    // Descriptor Sets
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
        VkDescriptorImageInfo img_infos[BLOCK_TYPE_COUNT];
        
        for (uint32_t j = 0; j < BLOCK_TYPE_COUNT; j++) {
            img_infos[j].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            img_infos[j].imageView = r->textures_view[j];
            img_infos[j].sampler = r->textures_sampler[j];
        }
        
        VkWriteDescriptorSet write = {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstBinding = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = BLOCK_TYPE_COUNT,
            .pImageInfo = img_infos
        };
        
        write.dstSet = r->descriptor_sets_normal[i];
        vkUpdateDescriptorSets(r->device, 1, &write, 0, NULL);
        
        write.dstSet = r->descriptor_sets_highlight[i];
        vkUpdateDescriptorSets(r->device, 1, &write, 0, NULL);
    }
    
    // Command Buffers
    r->command_buffers = malloc(sizeof(VkCommandBuffer) * r->image_count);
    VkCommandBufferAllocateInfo cmd_alloc = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = r->command_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = r->image_count
    };
    VK_CHECK(vkAllocateCommandBuffers(r->device, &cmd_alloc, r->command_buffers));
    
    // Sync Objects
    VkSemaphoreCreateInfo sem_info = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VkFenceCreateInfo fence_info = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, .flags = VK_FENCE_CREATE_SIGNALED_BIT};
    
    VK_CHECK(vkCreateSemaphore(r->device, &sem_info, NULL, &r->image_available));
    VK_CHECK(vkCreateSemaphore(r->device, &sem_info, NULL, &r->render_finished));
    VK_CHECK(vkCreateFence(r->device, &fence_info, NULL, &r->in_flight));
    
    // Initialize Crosshair
    float aspect = (float)r->extent.height / (float)r->extent.width;
    float ch_size = 0.03f;
    Vertex ch_verts[4] = {
        {{-ch_size * aspect, 0, 0}, {0, 0}}, {{ ch_size * aspect, 0, 0}, {1, 0}},
        {{0, -ch_size, 0}, {0, 0}}, {{0,  ch_size, 0}, {1, 0}}
    };
    upload_buffer(r->device, r->crosshair_memory, ch_verts, sizeof(ch_verts));
    
    // Initialize Inventory Grid
    float h_step, v_step;
    Vertex grid_verts[32];
    player_inventory_grid_vertices(aspect, grid_verts, 32, &r->inventory_grid_count, &h_step, &v_step);
    upload_buffer(r->device, r->inventory_grid_memory, grid_verts, r->inventory_grid_count * sizeof(Vertex));
    
    // Initialize Inventory Icon Quad
    Vertex icon_verts[6];
    player_inventory_icon_vertices(h_step, v_step, icon_verts, 6, &r->inventory_icon_count);
    upload_buffer(r->device, r->inventory_icon_memory, icon_verts, r->inventory_icon_count * sizeof(Vertex));
    
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
    
    vkDestroyBuffer(r->device, r->instance_buffer, NULL);
    vkFreeMemory(r->device, r->instance_memory, NULL);
    
    vkDestroyBuffer(r->device, r->inventory_bg_buffer, NULL);
    vkFreeMemory(r->device, r->inventory_bg_memory, NULL);
    vkDestroyBuffer(r->device, r->inventory_selection_buffer, NULL);
    vkFreeMemory(r->device, r->inventory_selection_memory, NULL);
    vkDestroyBuffer(r->device, r->inventory_count_buffer, NULL);
    vkFreeMemory(r->device, r->inventory_count_memory, NULL);
    vkDestroyBuffer(r->device, r->inventory_icon_buffer, NULL);
    vkFreeMemory(r->device, r->inventory_icon_memory, NULL);
    vkDestroyBuffer(r->device, r->inventory_grid_buffer, NULL);
    vkFreeMemory(r->device, r->inventory_grid_memory, NULL);
    vkDestroyBuffer(r->device, r->crosshair_buffer, NULL);
    vkFreeMemory(r->device, r->crosshair_memory, NULL);
    
    vkDestroyBuffer(r->device, r->edge_index_buffer, NULL);
    vkFreeMemory(r->device, r->edge_index_memory, NULL);
    vkDestroyBuffer(r->device, r->edge_vertex_buffer, NULL);
    vkFreeMemory(r->device, r->edge_vertex_memory, NULL);
    
    vkDestroyBuffer(r->device, r->block_index_buffer, NULL);
    vkFreeMemory(r->device, r->block_index_memory, NULL);
    vkDestroyBuffer(r->device, r->block_vertex_buffer, NULL);
    vkFreeMemory(r->device, r->block_vertex_memory, NULL);
    
    for (uint32_t i = 0; i < BLOCK_TYPE_COUNT; i++) {
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
    
    // Calculate instance counts
    int block_count = world_total_render_blocks(world);
    float aspect = (float)r->extent.height / (float)r->extent.width;
    uint32_t icon_count = player_inventory_icon_instances(player, aspect, NULL, 0);
    
    uint32_t total_instances = block_count + 5 + icon_count;
    
    // Grow instance buffer if needed
    if (total_instances > r->instance_capacity) {
        uint32_t new_cap = r->instance_capacity;
        while (new_cap < total_instances) new_cap *= 2;
        if (new_cap > MAX_INSTANCE_CAPACITY) die("Instance buffer overflow");
        
        vkDeviceWaitIdle(r->device);
        vkDestroyBuffer(r->device, r->instance_buffer, NULL);
        vkFreeMemory(r->device, r->instance_memory, NULL);
        
        r->instance_capacity = new_cap;
        create_buffer(r, r->instance_capacity * sizeof(InstanceData), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     &r->instance_buffer, &r->instance_memory);
    }
    
    // Map and fill instance buffer
    InstanceData *instances;
    VK_CHECK(vkMapMemory(r->device, r->instance_memory, 0, total_instances * sizeof(InstanceData), 0, (void **)&instances));
    
    uint32_t idx = 0;
    
    for (int i = 0; i < world->chunk_count; i++) {
        Chunk *chunk = world->chunks[i];
        for (int j = 0; j < chunk->block_count; j++) {
            Block b = chunk->blocks[j];
            instances[idx++] = (InstanceData){b.pos.x, b.pos.y, b.pos.z, b.type};
        }
    }
    
    uint32_t highlight_idx = idx++;
    uint32_t crosshair_idx = idx++;
    uint32_t inventory_idx = idx++;
    uint32_t selection_idx = idx++;
    uint32_t bg_idx = idx++;
    uint32_t icons_start = idx;
    
    instances[highlight_idx] = (InstanceData){
        highlight ? (float)highlight_cell.x : 0, highlight ? (float)highlight_cell.y : 0,
        highlight ? (float)highlight_cell.z : 0, HIGHLIGHT_TEXTURE_INDEX
    };
    instances[crosshair_idx] = (InstanceData){0, 0, 0, CROSSHAIR_TEXTURE_INDEX};
    instances[inventory_idx] = (InstanceData){0, 0, 0, HIGHLIGHT_TEXTURE_INDEX};
    instances[selection_idx] = (InstanceData){0, 0, 0, INVENTORY_SELECTION_TEXTURE_INDEX};
    instances[bg_idx] = (InstanceData){0, 0, 0, INVENTORY_BG_TEXTURE_INDEX};
    
    if (icon_count > 0) {
        player_inventory_icon_instances(player, aspect, &instances[icons_start], icon_count);
    }
    
    vkUnmapMemory(r->device, r->instance_memory);
    
    // Update inventory buffers
    uint32_t sel_count = 0, bg_count = 0, count_count = 0;
    
    if (player->inventory_open) {
        Vertex sel_verts[8];
        player_inventory_selection_vertices((int)player->selected_slot, aspect, sel_verts, 8, &sel_count);
        if (sel_count > 0) {
            upload_buffer(r->device, r->inventory_selection_memory, sel_verts, sel_count * sizeof(Vertex));
        }
        
        const float inv_half = 0.2f;
        const float inv_half_x = inv_half * aspect * ((float)INVENTORY_COLS / (float)INVENTORY_ROWS);
        
        Vertex bg_verts[6] = {
            {{-inv_half_x,  inv_half, 0}, {0, 0}}, {{ inv_half_x, -inv_half, 0}, {1, 1}}, {{ inv_half_x,  inv_half, 0}, {1, 0}},
            {{-inv_half_x,  inv_half, 0}, {0, 0}}, {{-inv_half_x, -inv_half, 0}, {0, 1}}, {{ inv_half_x, -inv_half, 0}, {1, 1}}
        };
        bg_count = 6;
        upload_buffer(r->device, r->inventory_bg_memory, bg_verts, bg_count * sizeof(Vertex));
        
        Vertex count_verts[1500];
        count_count = player_inventory_count_vertices(player, aspect, count_verts, 1500);
        if (count_count > 0) {
            upload_buffer(r->device, r->inventory_count_memory, count_verts, count_count * sizeof(Vertex));
        }
    }
    
    // Build push constants
    PushConstants pc = {
        .view = camera_view_matrix(camera),
        .proj = mat4_perspective(55.0f * M_PI / 180.0f,
                                (float)r->extent.width / (float)r->extent.height, 0.1f, 200.0f)
    };
    
    PushConstants pc_overlay = {.view = mat4_identity(), .proj = mat4_identity()};
    pc_overlay.proj.m[5] = -1.0f;
    
    // Record command buffer
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
    
    VkBuffer bufs[2];
    VkDeviceSize offsets[2] = {0, 0};
    
    // Draw blocks
    if (block_count > 0) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r->pipeline_solid);
        vkCmdPushConstants(cmd, r->pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc), &pc);
        
        bufs[0] = r->block_vertex_buffer;
        bufs[1] = r->instance_buffer;
        vkCmdBindVertexBuffers(cmd, 0, 2, bufs, offsets);
        vkCmdBindIndexBuffer(cmd, r->block_index_buffer, 0, VK_INDEX_TYPE_UINT16);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r->pipeline_layout, 0, 1,
                               &r->descriptor_sets_normal[img_idx], 0, NULL);
        
        vkCmdDrawIndexed(cmd, ARRAY_LENGTH(BLOCK_INDICES), block_count, 0, 0, 0);
    }
    
    // Draw highlight
    if (highlight) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r->pipeline_wireframe);
        vkCmdPushConstants(cmd, r->pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc), &pc);
        
        bufs[0] = r->edge_vertex_buffer;
        bufs[1] = r->instance_buffer;
        vkCmdBindVertexBuffers(cmd, 0, 2, bufs, offsets);
        vkCmdBindIndexBuffer(cmd, r->edge_index_buffer, 0, VK_INDEX_TYPE_UINT16);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r->pipeline_layout, 0, 1,
                               &r->descriptor_sets_highlight[img_idx], 0, NULL);
        
        vkCmdDrawIndexed(cmd, ARRAY_LENGTH(EDGE_INDICES), 1, 0, 0, highlight_idx);
    }
    
    // Draw crosshair
    if (!player->inventory_open) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r->pipeline_crosshair);
        vkCmdPushConstants(cmd, r->pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc_overlay), &pc_overlay);
        
        bufs[0] = r->crosshair_buffer;
        bufs[1] = r->instance_buffer;
        vkCmdBindVertexBuffers(cmd, 0, 2, bufs, offsets);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r->pipeline_layout, 0, 1,
                               &r->descriptor_sets_highlight[img_idx], 0, NULL);
        
        vkCmdDraw(cmd, 4, 1, 0, crosshair_idx);
    }
    
    // Draw inventory UI
    if (player->inventory_open) {
        if (bg_count > 0) {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r->pipeline_overlay);
            vkCmdPushConstants(cmd, r->pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc_overlay), &pc_overlay);
            
            bufs[0] = r->inventory_bg_buffer;
            bufs[1] = r->instance_buffer;
            vkCmdBindVertexBuffers(cmd, 0, 2, bufs, offsets);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r->pipeline_layout, 0, 1,
                                   &r->descriptor_sets_highlight[img_idx], 0, NULL);
            
            vkCmdDraw(cmd, bg_count, 1, 0, bg_idx);
        }
        
        if (r->inventory_grid_count > 0) {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r->pipeline_crosshair);
            vkCmdPushConstants(cmd, r->pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc_overlay), &pc_overlay);
            
            bufs[0] = r->inventory_grid_buffer;
            bufs[1] = r->instance_buffer;
            vkCmdBindVertexBuffers(cmd, 0, 2, bufs, offsets);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r->pipeline_layout, 0, 1,
                                   &r->descriptor_sets_highlight[img_idx], 0, NULL);
            
            vkCmdDraw(cmd, r->inventory_grid_count, 1, 0, inventory_idx);
        }
        
        if (sel_count > 0) {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r->pipeline_crosshair);
            vkCmdPushConstants(cmd, r->pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc_overlay), &pc_overlay);
            
            bufs[0] = r->inventory_selection_buffer;
            bufs[1] = r->instance_buffer;
            vkCmdBindVertexBuffers(cmd, 0, 2, bufs, offsets);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r->pipeline_layout, 0, 1,
                                   &r->descriptor_sets_highlight[img_idx], 0, NULL);
            
            vkCmdDraw(cmd, sel_count, 1, 0, selection_idx);
        }
        
        if (icon_count > 0) {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r->pipeline_solid);
            vkCmdPushConstants(cmd, r->pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc_overlay), &pc_overlay);
            
            bufs[0] = r->inventory_icon_buffer;
            bufs[1] = r->instance_buffer;
            vkCmdBindVertexBuffers(cmd, 0, 2, bufs, offsets);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r->pipeline_layout, 0, 1,
                                   &r->descriptor_sets_normal[img_idx], 0, NULL);
            
            vkCmdDraw(cmd, r->inventory_icon_count, icon_count, 0, icons_start);
        }
        
        if (count_count > 0) {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r->pipeline_crosshair);
            vkCmdPushConstants(cmd, r->pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc_overlay), &pc_overlay);
            
            bufs[0] = r->inventory_count_buffer;
            bufs[1] = r->instance_buffer;
            vkCmdBindVertexBuffers(cmd, 0, 2, bufs, offsets);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r->pipeline_layout, 0, 1,
                                   &r->descriptor_sets_highlight[img_idx], 0, NULL);
            
            vkCmdDraw(cmd, count_count, 1, 0, inventory_idx);
        }
    }
    
    vkCmdEndRenderPass(cmd);
    VK_CHECK(vkEndCommandBuffer(cmd));
    
    // Submit
    VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    
    VkSubmitInfo submit_info = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount = 1, .pWaitSemaphores = &r->image_available,
        .pWaitDstStageMask = &wait_stage,
        .commandBufferCount = 1, .pCommandBuffers = &cmd,
        .signalSemaphoreCount = 1, .pSignalSemaphores = &r->render_finished
    };
    
    VK_CHECK(vkQueueSubmit(r->graphics_queue, 1, &submit_info, r->in_flight));
    
    // Present
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