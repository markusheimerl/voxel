#ifndef VK_INIT_H
#define VK_INIT_H

#include <vulkan/vulkan.h>
#include <X11/Xlib.h>

typedef struct {
    VkInstance instance;
    VkPhysicalDevice physical_device;
    VkDevice device;
    VkSurfaceKHR surface;
    VkQueue graphics_queue;
    uint32_t graphics_family;
    VkCommandPool command_pool;
    VkDescriptorSetLayout descriptor_set_layout;
    VkPipelineLayout pipeline_layout;
    VkShaderModule vert_shader;
    VkShaderModule frag_shader;
    VkSemaphore image_available;
    VkSemaphore render_finished;
    VkFence in_flight;
} VulkanContext;

void vk_context_create(Display *display, Window window, VulkanContext *vk);
void vk_context_destroy(VulkanContext *vk);

#endif /* VK_INIT_H */
