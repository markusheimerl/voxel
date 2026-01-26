#include <errno.h>
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

#include "math.h"
#include "camera.h"
#include "world.h"
#include "voxel.h"
#include "player.h"

static bool is_key_pressed(const bool *keys, KeySym sym) {
    if (sym < 256) return keys[sym];
    return false;
}

/* -------------------------------------------------------------------------- */
/* Main                                                                       */
/* -------------------------------------------------------------------------- */

int main(void) {
    uint32_t window_width = 800;
    uint32_t window_height = 600;

    /* ---------------------------------------------------------------------- */
    /* X11 Setup                                                              */
    /* ---------------------------------------------------------------------- */

    Display *display = XOpenDisplay(NULL);
    if (!display) die("Failed to open X11 display");

    int screen = DefaultScreen(display);
    Window root = RootWindow(display, screen);

    Window window = XCreateSimpleWindow(display, root,
                                        0, 0,
                                        window_width,
                                        window_height,
                                        1,
                                        BlackPixel(display, screen),
                                        WhitePixel(display, screen));

    const char *window_title_game = "Voxel Engine";
    XStoreName(display, window, window_title_game);
    XSelectInput(display, window,
                 ExposureMask | KeyPressMask | KeyReleaseMask |
                 PointerMotionMask | StructureNotifyMask | ButtonPressMask);

    Atom wm_delete_window = XInternAtom(display, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(display, window, &wm_delete_window, 1);
    XMapWindow(display, window);

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
    if (device_count == 0) die("No Vulkan-capable GPU found");

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
        if (physical_device != VK_NULL_HANDLE) break;
    }

    free(physical_devices);

    if (physical_device == VK_NULL_HANDLE) die("No suitable GPU found");

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

    Texture textures[BLOCK_TYPE_COUNT];
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
        texture_create_from_file(device, physical_device, command_pool, graphics_queue,
                                 texture_files[i], &textures[i]);
    }

    Texture black_texture;
    texture_create_solid(device, physical_device, command_pool, graphics_queue, 0, 0, 0, 255, &black_texture);

    /* ---------------------------------------------------------------------- */
    /* Geometry Buffers (static)                                               */
    /* ---------------------------------------------------------------------- */

    VkBuffer block_vertex_buffer;
    VkDeviceMemory block_vertex_memory;
    create_buffer(device, physical_device, sizeof(BLOCK_VERTICES),
                  VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                  &block_vertex_buffer, &block_vertex_memory);
    upload_buffer_data(device, block_vertex_memory, BLOCK_VERTICES, sizeof(BLOCK_VERTICES));

    VkBuffer block_index_buffer;
    VkDeviceMemory block_index_memory;
    create_buffer(device, physical_device, sizeof(BLOCK_INDICES),
                  VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                  &block_index_buffer, &block_index_memory);
    upload_buffer_data(device, block_index_memory, BLOCK_INDICES, sizeof(BLOCK_INDICES));

    VkBuffer edge_vertex_buffer;
    VkDeviceMemory edge_vertex_memory;
    create_buffer(device, physical_device, sizeof(EDGE_VERTICES),
                  VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                  &edge_vertex_buffer, &edge_vertex_memory);
    upload_buffer_data(device, edge_vertex_memory, EDGE_VERTICES, sizeof(EDGE_VERTICES));

    VkBuffer edge_index_buffer;
    VkDeviceMemory edge_index_memory;
    create_buffer(device, physical_device, sizeof(EDGE_INDICES),
                  VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                  &edge_index_buffer, &edge_index_memory);
    upload_buffer_data(device, edge_index_memory, EDGE_INDICES, sizeof(EDGE_INDICES));

    /* Crosshair vertices (in clip space, updated on resize to keep aspect) */
    VkBuffer crosshair_vertex_buffer;
    VkDeviceMemory crosshair_vertex_memory;
    create_buffer(device, physical_device, sizeof(Vertex) * 4,
                  VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                  &crosshair_vertex_buffer, &crosshair_vertex_memory);

    /* Inventory grid vertices (clip space, updated on resize) */
    const uint32_t INVENTORY_MAX_VERTICES = 32;
    VkBuffer inventory_vertex_buffer;
    VkDeviceMemory inventory_vertex_memory;
    uint32_t inventory_vertex_count = 0;
    create_buffer(device, physical_device, sizeof(Vertex) * INVENTORY_MAX_VERTICES,
                  VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                  &inventory_vertex_buffer, &inventory_vertex_memory);

    /* Inventory icon quad (clip space, updated on resize) */
    VkBuffer inventory_icon_vertex_buffer;
    VkDeviceMemory inventory_icon_vertex_memory;
    const uint32_t INVENTORY_ICON_VERTEX_COUNT = 6;
    create_buffer(device, physical_device, sizeof(Vertex) * INVENTORY_ICON_VERTEX_COUNT,
                  VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                  &inventory_icon_vertex_buffer, &inventory_icon_vertex_memory);

    /* Inventory count digits (clip space, updated per frame) */
    const uint32_t INVENTORY_COUNT_MAX_VERTICES = 1500;
    VkBuffer inventory_count_vertex_buffer;
    VkDeviceMemory inventory_count_vertex_memory;
    uint32_t inventory_count_vertex_count = 0;
    create_buffer(device, physical_device, sizeof(Vertex) * INVENTORY_COUNT_MAX_VERTICES,
                  VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                  &inventory_count_vertex_buffer, &inventory_count_vertex_memory);

    /* Inventory selection outline (clip space, updated per frame) */
    const uint32_t INVENTORY_SELECTION_VERTEX_COUNT = 8;
    VkBuffer inventory_selection_vertex_buffer;
    VkDeviceMemory inventory_selection_vertex_memory;
    uint32_t inventory_selection_vertex_count = 0;
    create_buffer(device, physical_device, sizeof(Vertex) * INVENTORY_SELECTION_VERTEX_COUNT,
                  VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                  &inventory_selection_vertex_buffer, &inventory_selection_vertex_memory);

    /* Inventory background (clip space, updated per frame) */
    const uint32_t INVENTORY_BG_VERTEX_COUNT = 6;
    VkBuffer inventory_bg_vertex_buffer;
    VkDeviceMemory inventory_bg_vertex_memory;
    uint32_t inventory_bg_vertex_count = 0;
    create_buffer(device, physical_device, sizeof(Vertex) * INVENTORY_BG_VERTEX_COUNT,
                  VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                  &inventory_bg_vertex_buffer, &inventory_bg_vertex_memory);

    /* Instance buffer (blocks + highlight + crosshair) */
    VkBuffer instance_buffer = VK_NULL_HANDLE;
    VkDeviceMemory instance_memory = VK_NULL_HANDLE;
    uint32_t instance_capacity = INITIAL_INSTANCE_CAPACITY;

    create_buffer(device, physical_device,
                  (VkDeviceSize)instance_capacity * sizeof(InstanceData),
                  VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                  &instance_buffer, &instance_memory);

    /* ---------------------------------------------------------------------- */
    /* Shaders, Descriptor Layout, Pipeline Layout                            */
    /* ---------------------------------------------------------------------- */

    VkShaderModule vert_shader = create_shader_module(device, "shaders/vert.spv");
    VkShaderModule frag_shader = create_shader_module(device, "shaders/frag.spv");

    /* NOTE: shaders are expected to use:
       - set=0 binding=0 sampler2D texSamplers[BLOCK_TYPE_COUNT]
       - vertex inputs: location 0/1 for Vertex, 2/3 for InstanceData
       - push constants: mat4 view, mat4 proj
    */

    VkDescriptorSetLayoutBinding sampler_binding = {0};
    sampler_binding.binding = 0;
    sampler_binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    sampler_binding.descriptorCount = BLOCK_TYPE_COUNT;
    sampler_binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layout_info = {0};
    layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layout_info.bindingCount = 1;
    layout_info.pBindings = &sampler_binding;

    VkDescriptorSetLayout descriptor_layout;
    VK_CHECK(vkCreateDescriptorSetLayout(device, &layout_info, NULL, &descriptor_layout));

    VkPushConstantRange push_range = {0};
    push_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    push_range.offset = 0;
    push_range.size = sizeof(PushConstants);

    VkPipelineLayoutCreateInfo pipeline_layout_info = {0};
    pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipeline_layout_info.setLayoutCount = 1;
    pipeline_layout_info.pSetLayouts = &descriptor_layout;
    pipeline_layout_info.pushConstantRangeCount = 1;
    pipeline_layout_info.pPushConstantRanges = &push_range;

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
        .texture_count = BLOCK_TYPE_COUNT,
        .black_texture = &black_texture
    };

    /* ---------------------------------------------------------------------- */
    /* World Save + Voxel World                                                */
    /* ---------------------------------------------------------------------- */

    WorldSave save;
    world_save_init(&save, WORLD_SAVE_FILE);
    (void)world_save_load(&save);

    World world;
    world_init(&world, &save);
    world_update_chunks(&world, world.spawn_position);
    if (!world.spawn_set) world.spawn_position = vec3(0.0f, 4.5f, 0.0f);

    /* ---------------------------------------------------------------------- */
    /* Player / Camera                                                        */
    /* ---------------------------------------------------------------------- */

    const float GRAVITY = 17.0f;
    const float JUMP_HEIGHT = 1.2f;
    const float JUMP_VELOCITY = sqrtf(2.0f * GRAVITY * JUMP_HEIGHT);
    const float EYE_HEIGHT = 1.6f;

    Player player = {0};
    player.position = world.spawn_position;
    player.inventory_open = false;
    player.selected_slot = 0;

    Camera camera;
    camera_init(&camera);
    camera.position = vec3_add(player.position, vec3(0.0f, EYE_HEIGHT, 0.0f));

    world_update_chunks(&world, player.position);

    /* Input State */
    bool keys[256] = {0};
    bool mouse_captured = false;
    bool first_mouse = true;
    float last_mouse_x = 0.0f;
    float last_mouse_y = 0.0f;


    struct timespec last_time;
    clock_gettime(CLOCK_MONOTONIC, &last_time);

    struct timespec last_autosave;
    clock_gettime(CLOCK_MONOTONIC, &last_autosave);

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

            window_width = (uint32_t)attrs.width;
            window_height = (uint32_t)attrs.height;

            swapchain_destroy(device, command_pool, &swapchain);
            swapchain_create(&swapchain_ctx, &swapchain, window_width, window_height);

            /* Update crosshair vertices to keep constant size in screen space */
            const float crosshair_size = 0.03f;
            float aspect_correction = (float)swapchain.extent.height / (float)swapchain.extent.width;

            Vertex crosshair_vertices[] = {
                {{-crosshair_size * aspect_correction, 0.0f, 0.0f}, {0.0f, 0.0f}},
                {{ crosshair_size * aspect_correction, 0.0f, 0.0f}, {1.0f, 0.0f}},
                {{ 0.0f, -crosshair_size, 0.0f}, {0.0f, 0.0f}},
                {{ 0.0f,  crosshair_size, 0.0f}, {1.0f, 0.0f}},
            };

            void *ch = NULL;
            VK_CHECK(vkMapMemory(device, crosshair_vertex_memory, 0, sizeof(crosshair_vertices), 0, &ch));
            memcpy(ch, crosshair_vertices, sizeof(crosshair_vertices));
            vkUnmapMemory(device, crosshair_vertex_memory);

            /* Inventory grid (3x9) in clip space, keep square on screen */
            float inv_h_step = 0.0f;
            float inv_v_step = 0.0f;
            Vertex inventory_vertices[INVENTORY_MAX_VERTICES];
            player_inventory_grid_vertices(aspect_correction,
                                            inventory_vertices,
                                            (uint32_t)ARRAY_LENGTH(inventory_vertices),
                                            &inventory_vertex_count,
                                            &inv_h_step,
                                            &inv_v_step);
            void *inv = NULL;
            VK_CHECK(vkMapMemory(device, inventory_vertex_memory, 0,
                                 inventory_vertex_count * sizeof(Vertex), 0, &inv));
            memcpy(inv, inventory_vertices, inventory_vertex_count * sizeof(Vertex));
            vkUnmapMemory(device, inventory_vertex_memory);

            /* Inventory icon quad (centered at origin, translated by instances) */
            Vertex icon_vertices[INVENTORY_ICON_VERTEX_COUNT];
            uint32_t icon_vertex_count = 0;
            player_inventory_icon_vertices(inv_h_step, inv_v_step,
                                            icon_vertices,
                                            INVENTORY_ICON_VERTEX_COUNT,
                                            &icon_vertex_count);

            void *icon = NULL;
            VK_CHECK(vkMapMemory(device, inventory_icon_vertex_memory, 0,
                                 icon_vertex_count * sizeof(Vertex), 0, &icon));
            memcpy(icon, icon_vertices, icon_vertex_count * sizeof(Vertex));
            vkUnmapMemory(device, inventory_icon_vertex_memory);

            swapchain_needs_recreate = false;
        }

        world_update_chunks(&world, player.position);

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
                if ((Atom)event.xclient.data.l[0] == wm_delete_window) running = false;
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
                bool was_down = (sym < 256) ? keys[sym] : false;
                if (sym == XK_Escape) {
                    if (mouse_captured) {
                        XUngrabPointer(display, CurrentTime);
                        XUndefineCursor(display, window);
                        mouse_captured = false;
                        first_mouse = true;
                    }
                } else if (sym == XK_E || sym == XK_e) {
                    if (!was_down) {
                        player.inventory_open = !player.inventory_open;
                        if (player.inventory_open && mouse_captured) {
                            XUngrabPointer(display, CurrentTime);
                            XUndefineCursor(display, window);
                            mouse_captured = false;
                            first_mouse = true;
                        } else if (!player.inventory_open && !mouse_captured) {
                            XGrabPointer(display, window, True, PointerMotionMask,
                                         GrabModeAsync, GrabModeAsync, window, None, CurrentTime);
                            XDefineCursor(display, window, invisible_cursor);
                            mouse_captured = true;
                            first_mouse = true;
                        }
                        XStoreName(display, window, window_title_game);
                    }
                } else if (!player.inventory_open && sym == XK_1) player.selected_slot = 0;
                else if (!player.inventory_open && sym == XK_2) player.selected_slot = 1;
                else if (!player.inventory_open && sym == XK_3) player.selected_slot = 2;
                else if (!player.inventory_open && sym == XK_4) player.selected_slot = 3;
                else if (!player.inventory_open && sym == XK_5) player.selected_slot = 4;
                else if (!player.inventory_open && sym == XK_6) player.selected_slot = 5;
                else if (!player.inventory_open && sym == XK_7) player.selected_slot = 6;
                else if (!player.inventory_open && sym == XK_8) player.selected_slot = 7;
                else if (!player.inventory_open && sym == XK_9) player.selected_slot = 8;

                if (sym < 256) keys[sym] = true;
            } break;

            case KeyRelease: {
                KeySym sym = XLookupKeysym(&event.xkey, 0);
                if (sym < 256) keys[sym] = false;
            } break;

            case MotionNotify:
                if (mouse_captured) {
                    mouse_x = (float)event.xmotion.x;
                    mouse_y = (float)event.xmotion.y;
                    mouse_moved = true;
                }
                break;

            case ButtonPress:
                if (player.inventory_open) {
                    if (event.xbutton.button == Button1) {
                        float aspect = (float)window_height / (float)window_width;
                        int slot = player_inventory_slot_from_mouse(aspect,
                                                                     (float)event.xbutton.x,
                                                                     (float)event.xbutton.y,
                                                                     (float)window_width,
                                                                     (float)window_height);
                        if (slot >= 0) player.selected_slot = (uint8_t)slot;
                    }
                    break;
                }
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
                    if (mouse_captured) right_click = true;
                }
                break;

            default:
                break;
            }
        }

        if (mouse_captured && mouse_moved) {
            int center_x = (int)window_width / 2;
            int center_y = (int)window_height / 2;

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

        const float MAX_DELTA_TIME = 0.1f;
        if (delta_time > MAX_DELTA_TIME) delta_time = MAX_DELTA_TIME;

        Vec3 move_delta = vec3(0.0f, 0.0f, 0.0f);
        bool wants_jump = false;

        if (mouse_captured && !player.inventory_open) {
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

            if (vec3_length(movement_dir) > 0.0f) movement_dir = vec3_normalize(movement_dir);

            move_delta = vec3_scale(movement_dir, camera.movement_speed * delta_time);
            wants_jump = is_key_pressed(keys, ' ') || is_key_pressed(keys, XK_space);
        }

        if (wants_jump && player.on_ground) {
            player.velocity_y = JUMP_VELOCITY;
            player.on_ground = false;
        }

        if (!player.on_ground) player.velocity_y -= GRAVITY * delta_time;
        else player.velocity_y = 0.0f;

        player.position.x += move_delta.x;
        resolve_collision_axis(&world, &player.position, move_delta.x, 0);

        player.position.z += move_delta.z;
        resolve_collision_axis(&world, &player.position, move_delta.z, 2);

        player.position.y += player.velocity_y * delta_time;
        resolve_collision_y(&world, &player.position, &player.velocity_y, &player.on_ground);

        camera.position = vec3_add(player.position, vec3(0.0f, EYE_HEIGHT, 0.0f));

        RayHit ray_hit = raycast_blocks(&world, camera.position, camera.front, 6.0f);

        if (mouse_captured && !player.inventory_open && (left_click || right_click)) {
            if (ray_hit.hit) {
                if (left_click) {
                    world_remove_block(&world, ray_hit.cell);
                    player_inventory_add(&player, ray_hit.type);
                }

                if (right_click) {
                    if (!(ray_hit.normal.x == 0 && ray_hit.normal.y == 0 && ray_hit.normal.z == 0)) {
                        IVec3 place = ivec3_add(ray_hit.cell, ray_hit.normal);
                        if (!world_block_exists(&world, place) && !block_overlaps_player(&player, place)) {
                            uint8_t slot = player.selected_slot;
                            if (slot < 9 && player.inventory_counts[slot] > 0) {
                                uint8_t place_type = player.inventory[slot];
                                world_add_block(&world, place, place_type);
                                if (player.inventory_counts[slot] > 0) {
                                    player.inventory_counts[slot]--;
                                    if (player.inventory_counts[slot] == 0) {
                                        player.inventory[slot] = 0;
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        /* Periodic autosave (single-file) */
        double since_autosave =
            (now.tv_sec - last_autosave.tv_sec) +
            (now.tv_nsec - last_autosave.tv_nsec) / 1000000000.0;

        if (since_autosave > 5.0) {
            world_save_flush(&save);
            last_autosave = now;
        }

        if (swapchain_needs_recreate) continue;

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

        /* Build instance list */
        int render_blocks = world_total_render_blocks(&world);
        uint32_t inventory_icon_count =
            player_inventory_icon_instances(&player,
                                            (float)swapchain.extent.height / (float)swapchain.extent.width,
                                            NULL,
                                            0);

        uint32_t highlight_instance_index = (uint32_t)render_blocks;
        uint32_t crosshair_instance_index = (uint32_t)render_blocks + 1;
        uint32_t inventory_instance_index = (uint32_t)render_blocks + 2;
        uint32_t inventory_selection_instance_index = (uint32_t)render_blocks + 3;
        uint32_t inventory_bg_instance_index = (uint32_t)render_blocks + 4;
        uint32_t inventory_icons_start = (uint32_t)render_blocks + 5;
        uint32_t total_instances = (uint32_t)render_blocks + 5 + inventory_icon_count;

        if (total_instances > instance_capacity) {
            uint32_t new_cap = instance_capacity;
            while (new_cap < total_instances) new_cap *= 2;
            if (new_cap > MAX_INSTANCE_CAPACITY) die("Exceeded maximum instance buffer capacity");

            vkDeviceWaitIdle(device);
            vkDestroyBuffer(device, instance_buffer, NULL);
            vkFreeMemory(device, instance_memory, NULL);

            instance_capacity = new_cap;
            create_buffer(device, physical_device,
                          (VkDeviceSize)instance_capacity * sizeof(InstanceData),
                          VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                          &instance_buffer, &instance_memory);
        }

        InstanceData *instances = NULL;
        VK_CHECK(vkMapMemory(device, instance_memory, 0,
                             (VkDeviceSize)total_instances * sizeof(InstanceData),
                             0, (void **)&instances));

        uint32_t w = 0;
        for (int ci = 0; ci < world.chunk_count; ++ci) {
            Chunk *chunk = world.chunks[ci];
            for (int bi = 0; bi < chunk->block_count; ++bi) {
                Block b = chunk->blocks[bi];
                instances[w++] = (InstanceData){ (float)b.pos.x, (float)b.pos.y, (float)b.pos.z, (uint32_t)b.type };
            }
        }

        /* Highlight + crosshair instances */
        if (ray_hit.hit) {
            instances[highlight_instance_index] = (InstanceData){
                (float)ray_hit.cell.x, (float)ray_hit.cell.y, (float)ray_hit.cell.z, (uint32_t)HIGHLIGHT_TEXTURE_INDEX
            };
        } else {
            instances[highlight_instance_index] = (InstanceData){0,0,0,(uint32_t)HIGHLIGHT_TEXTURE_INDEX};
        }
        instances[crosshair_instance_index] = (InstanceData){0,0,0,(uint32_t)CROSSHAIR_TEXTURE_INDEX};
        instances[inventory_instance_index] = (InstanceData){0,0,0,(uint32_t)HIGHLIGHT_TEXTURE_INDEX};
        instances[inventory_selection_instance_index] = (InstanceData){0,0,0,(uint32_t)INVENTORY_SELECTION_TEXTURE_INDEX};
        instances[inventory_bg_instance_index] = (InstanceData){0,0,0,(uint32_t)INVENTORY_BG_TEXTURE_INDEX};

        if (inventory_icon_count > 0) {
            float aspect = (float)swapchain.extent.height / (float)swapchain.extent.width;
            player_inventory_icon_instances(&player,
                                            aspect,
                                            &instances[inventory_icons_start],
                                            inventory_icon_count);
        }

        inventory_selection_vertex_count = 0;
        inventory_bg_vertex_count = 0;
        inventory_count_vertex_count = 0;
        if (player.inventory_open) {
            float aspect = (float)swapchain.extent.height / (float)swapchain.extent.width;

            Vertex bg_vertices[INVENTORY_BG_VERTEX_COUNT];
            const float inv_half = 0.2f;
            const float inv_half_x = inv_half * aspect * ((float)INVENTORY_COLS / (float)INVENTORY_ROWS);
            const float left = -inv_half_x;
            const float right = inv_half_x;
            const float bottom = -inv_half;
            const float top = inv_half;

            bg_vertices[0] = (Vertex){{left,  top,    0.0f}, {0.0f, 0.0f}};
            bg_vertices[1] = (Vertex){{right, top,    0.0f}, {1.0f, 0.0f}};
            bg_vertices[2] = (Vertex){{right, bottom, 0.0f}, {1.0f, 1.0f}};
            bg_vertices[3] = (Vertex){{left,  top,    0.0f}, {0.0f, 0.0f}};
            bg_vertices[4] = (Vertex){{right, bottom, 0.0f}, {1.0f, 1.0f}};
            bg_vertices[5] = (Vertex){{left,  bottom, 0.0f}, {0.0f, 1.0f}};

            inventory_bg_vertex_count = INVENTORY_BG_VERTEX_COUNT;
            void *bg = NULL;
            VK_CHECK(vkMapMemory(device, inventory_bg_vertex_memory, 0,
                                 inventory_bg_vertex_count * sizeof(Vertex), 0, &bg));
            memcpy(bg, bg_vertices, inventory_bg_vertex_count * sizeof(Vertex));
            vkUnmapMemory(device, inventory_bg_vertex_memory);

            Vertex selection_vertices[INVENTORY_SELECTION_VERTEX_COUNT];
            player_inventory_selection_vertices((int)player.selected_slot,
                                                 aspect,
                                                 selection_vertices,
                                                 INVENTORY_SELECTION_VERTEX_COUNT,
                                                 &inventory_selection_vertex_count);
            if (inventory_selection_vertex_count > 0) {
                void *sel = NULL;
                VK_CHECK(vkMapMemory(device, inventory_selection_vertex_memory, 0,
                                     inventory_selection_vertex_count * sizeof(Vertex), 0, &sel));
                memcpy(sel, selection_vertices, inventory_selection_vertex_count * sizeof(Vertex));
                vkUnmapMemory(device, inventory_selection_vertex_memory);
            }

            Vertex count_vertices[INVENTORY_COUNT_MAX_VERTICES];
            inventory_count_vertex_count =
                player_inventory_count_vertices(&player, aspect,
                                                count_vertices,
                                                INVENTORY_COUNT_MAX_VERTICES);

            if (inventory_count_vertex_count > 0) {
                void *cnt = NULL;
                VK_CHECK(vkMapMemory(device, inventory_count_vertex_memory, 0,
                                     inventory_count_vertex_count * sizeof(Vertex), 0, &cnt));
                memcpy(cnt, count_vertices, inventory_count_vertex_count * sizeof(Vertex));
                vkUnmapMemory(device, inventory_count_vertex_memory);
            }
        }

        vkUnmapMemory(device, instance_memory);

        /* Matrices */
        PushConstants pc = {0};
        pc.view = camera_view_matrix(&camera);
        pc.proj = mat4_perspective(55.0f * (float)M_PI / 180.0f,
                                   (float)swapchain.extent.width / (float)swapchain.extent.height,
                                   0.1f,
                                   200.0f);

        PushConstants pc_identity = {0};
        pc_identity.view = mat4_identity();
        pc_identity.proj = mat4_identity();

        PushConstants pc_overlay = pc_identity;
        pc_overlay.proj.m[5] = -1.0f;

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

        VkDeviceSize offsets[2] = {0, 0};
        VkBuffer vbs[2];

        /* Draw all blocks in one instanced draw */
        vkCmdBindPipeline(swapchain.command_buffers[image_index], VK_PIPELINE_BIND_POINT_GRAPHICS, swapchain.pipeline_solid);
        vkCmdPushConstants(swapchain.command_buffers[image_index], pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc), &pc);

        vbs[0] = block_vertex_buffer;
        vbs[1] = instance_buffer;
        vkCmdBindVertexBuffers(swapchain.command_buffers[image_index], 0, 2, vbs, offsets);
        vkCmdBindIndexBuffer(swapchain.command_buffers[image_index], block_index_buffer, 0, VK_INDEX_TYPE_UINT16);

        vkCmdBindDescriptorSets(swapchain.command_buffers[image_index],
                                VK_PIPELINE_BIND_POINT_GRAPHICS,
                                pipeline_layout,
                                0,
                                1,
                                &swapchain.descriptor_sets_normal[image_index],
                                0,
                                NULL);

        if (render_blocks > 0) {
            vkCmdDrawIndexed(swapchain.command_buffers[image_index],
                             (uint32_t)ARRAY_LENGTH(BLOCK_INDICES),
                             (uint32_t)render_blocks,
                             0, 0, 0);
        }

        /* Highlight wireframe (1 instance, different mesh/indices) */
        if (ray_hit.hit) {
            vkCmdBindPipeline(swapchain.command_buffers[image_index], VK_PIPELINE_BIND_POINT_GRAPHICS, swapchain.pipeline_wireframe);
            vkCmdPushConstants(swapchain.command_buffers[image_index], pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc), &pc);

            vbs[0] = edge_vertex_buffer;
            vbs[1] = instance_buffer;
            vkCmdBindVertexBuffers(swapchain.command_buffers[image_index], 0, 2, vbs, offsets);
            vkCmdBindIndexBuffer(swapchain.command_buffers[image_index], edge_index_buffer, 0, VK_INDEX_TYPE_UINT16);

            vkCmdBindDescriptorSets(swapchain.command_buffers[image_index],
                                    VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    pipeline_layout,
                                    0,
                                    1,
                                    &swapchain.descriptor_sets_highlight[image_index],
                                    0,
                                    NULL);

            vkCmdDrawIndexed(swapchain.command_buffers[image_index],
                             (uint32_t)ARRAY_LENGTH(EDGE_INDICES),
                             1,
                             0, 0, highlight_instance_index);
        }

        /* Crosshair (lines, clip-space geometry, identity matrices) */
        if (!player.inventory_open) {
            vkCmdBindPipeline(swapchain.command_buffers[image_index], VK_PIPELINE_BIND_POINT_GRAPHICS, swapchain.pipeline_crosshair);
            vkCmdPushConstants(swapchain.command_buffers[image_index], pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc_overlay), &pc_overlay);

            vbs[0] = crosshair_vertex_buffer;
            vbs[1] = instance_buffer;
            vkCmdBindVertexBuffers(swapchain.command_buffers[image_index], 0, 2, vbs, offsets);

            vkCmdBindDescriptorSets(swapchain.command_buffers[image_index],
                                    VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    pipeline_layout,
                                    0,
                                    1,
                                    &swapchain.descriptor_sets_highlight[image_index],
                                    0,
                                    NULL);

            vkCmdDraw(swapchain.command_buffers[image_index], 4, 1, 0, crosshair_instance_index);
        }

        /* Inventory grid (lines, clip-space geometry, identity matrices) */
        if (player.inventory_open && inventory_vertex_count > 0) {
            if (inventory_bg_vertex_count > 0) {
                vkCmdBindPipeline(swapchain.command_buffers[image_index], VK_PIPELINE_BIND_POINT_GRAPHICS, swapchain.pipeline_overlay);
                vkCmdPushConstants(swapchain.command_buffers[image_index], pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc_overlay), &pc_overlay);

                vbs[0] = inventory_bg_vertex_buffer;
                vbs[1] = instance_buffer;
                vkCmdBindVertexBuffers(swapchain.command_buffers[image_index], 0, 2, vbs, offsets);

                vkCmdBindDescriptorSets(swapchain.command_buffers[image_index],
                                        VK_PIPELINE_BIND_POINT_GRAPHICS,
                                        pipeline_layout,
                                        0,
                                        1,
                                        &swapchain.descriptor_sets_highlight[image_index],
                                        0,
                                        NULL);

                vkCmdDraw(swapchain.command_buffers[image_index], inventory_bg_vertex_count, 1, 0, inventory_bg_instance_index);
            }

            vkCmdBindPipeline(swapchain.command_buffers[image_index], VK_PIPELINE_BIND_POINT_GRAPHICS, swapchain.pipeline_crosshair);
            vkCmdPushConstants(swapchain.command_buffers[image_index], pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc_overlay), &pc_overlay);

            vbs[0] = inventory_vertex_buffer;
            vbs[1] = instance_buffer;
            vkCmdBindVertexBuffers(swapchain.command_buffers[image_index], 0, 2, vbs, offsets);

            vkCmdBindDescriptorSets(swapchain.command_buffers[image_index],
                                    VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    pipeline_layout,
                                    0,
                                    1,
                                    &swapchain.descriptor_sets_highlight[image_index],
                                    0,
                                    NULL);

            vkCmdDraw(swapchain.command_buffers[image_index], inventory_vertex_count, 1, 0, inventory_instance_index);
        }

        /* Inventory selection outline */
        if (player.inventory_open && inventory_selection_vertex_count > 0) {
            vkCmdBindPipeline(swapchain.command_buffers[image_index], VK_PIPELINE_BIND_POINT_GRAPHICS, swapchain.pipeline_crosshair);
            vkCmdPushConstants(swapchain.command_buffers[image_index], pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc_overlay), &pc_overlay);

            vbs[0] = inventory_selection_vertex_buffer;
            vbs[1] = instance_buffer;
            vkCmdBindVertexBuffers(swapchain.command_buffers[image_index], 0, 2, vbs, offsets);

            vkCmdBindDescriptorSets(swapchain.command_buffers[image_index],
                                    VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    pipeline_layout,
                                    0,
                                    1,
                                    &swapchain.descriptor_sets_highlight[image_index],
                                    0,
                                    NULL);

            vkCmdDraw(swapchain.command_buffers[image_index], inventory_selection_vertex_count, 1, 0, inventory_selection_instance_index);
        }

        if (player.inventory_open && inventory_icon_count > 0) {
            vkCmdBindPipeline(swapchain.command_buffers[image_index], VK_PIPELINE_BIND_POINT_GRAPHICS, swapchain.pipeline_solid);
            vkCmdPushConstants(swapchain.command_buffers[image_index], pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc_overlay), &pc_overlay);

            vbs[0] = inventory_icon_vertex_buffer;
            vbs[1] = instance_buffer;
            vkCmdBindVertexBuffers(swapchain.command_buffers[image_index], 0, 2, vbs, offsets);

            vkCmdBindDescriptorSets(swapchain.command_buffers[image_index],
                                    VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    pipeline_layout,
                                    0,
                                    1,
                                    &swapchain.descriptor_sets_normal[image_index],
                                    0,
                                    NULL);

            vkCmdDraw(swapchain.command_buffers[image_index], INVENTORY_ICON_VERTEX_COUNT, inventory_icon_count, 0, inventory_icons_start);
        }

        if (player.inventory_open && inventory_count_vertex_count > 0) {
            vkCmdBindPipeline(swapchain.command_buffers[image_index], VK_PIPELINE_BIND_POINT_GRAPHICS, swapchain.pipeline_crosshair);
            vkCmdPushConstants(swapchain.command_buffers[image_index], pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc_overlay), &pc_overlay);

            vbs[0] = inventory_count_vertex_buffer;
            vbs[1] = instance_buffer;
            vkCmdBindVertexBuffers(swapchain.command_buffers[image_index], 0, 2, vbs, offsets);

            vkCmdBindDescriptorSets(swapchain.command_buffers[image_index],
                                    VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    pipeline_layout,
                                    0,
                                    1,
                                    &swapchain.descriptor_sets_highlight[image_index],
                                    0,
                                    NULL);

            vkCmdDraw(swapchain.command_buffers[image_index], inventory_count_vertex_count, 1, 0, inventory_instance_index);
        }

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

    /* Flush all chunks into the single save file */
    world_destroy(&world);
    world_save_destroy(&save);

    swapchain_destroy(device, command_pool, &swapchain);

    vkDestroyFence(device, in_flight, NULL);
    vkDestroySemaphore(device, render_finished, NULL);
    vkDestroySemaphore(device, image_available, NULL);

    vkDestroyBuffer(device, instance_buffer, NULL);
    vkFreeMemory(device, instance_memory, NULL);

    vkDestroyBuffer(device, crosshair_vertex_buffer, NULL);
    vkFreeMemory(device, crosshair_vertex_memory, NULL);

    vkDestroyBuffer(device, inventory_vertex_buffer, NULL);
    vkFreeMemory(device, inventory_vertex_memory, NULL);

    vkDestroyBuffer(device, inventory_icon_vertex_buffer, NULL);
    vkFreeMemory(device, inventory_icon_vertex_memory, NULL);

    vkDestroyBuffer(device, inventory_count_vertex_buffer, NULL);
    vkFreeMemory(device, inventory_count_vertex_memory, NULL);

    vkDestroyBuffer(device, inventory_selection_vertex_buffer, NULL);
    vkFreeMemory(device, inventory_selection_vertex_memory, NULL);

    vkDestroyBuffer(device, inventory_bg_vertex_buffer, NULL);
    vkFreeMemory(device, inventory_bg_vertex_memory, NULL);

    vkDestroyBuffer(device, edge_vertex_buffer, NULL);
    vkFreeMemory(device, edge_vertex_memory, NULL);
    vkDestroyBuffer(device, edge_index_buffer, NULL);
    vkFreeMemory(device, edge_index_memory, NULL);

    vkDestroyBuffer(device, block_vertex_buffer, NULL);
    vkFreeMemory(device, block_vertex_memory, NULL);
    vkDestroyBuffer(device, block_index_buffer, NULL);
    vkFreeMemory(device, block_index_memory, NULL);

    for (uint32_t i = 0; i < BLOCK_TYPE_COUNT; ++i) {
        texture_destroy(device, &textures[i]);
    }
    texture_destroy(device, &black_texture);

    vkDestroyPipelineLayout(device, pipeline_layout, NULL);
    vkDestroyDescriptorSetLayout(device, descriptor_layout, NULL);

    vkDestroyShaderModule(device, vert_shader, NULL);
    vkDestroyShaderModule(device, frag_shader, NULL);

    vkDestroyCommandPool(device, command_pool, NULL);
    vkDestroyDevice(device, NULL);
    vkDestroySurfaceKHR(instance, surface, NULL);
    vkDestroyInstance(instance, NULL);

    XFreeCursor(display, invisible_cursor);
    XDestroyWindow(display, window);
    XCloseDisplay(display);

    return 0;
}