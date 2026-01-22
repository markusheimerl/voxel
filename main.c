#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <math.h>
#include <time.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

#define VK_USE_PLATFORM_XLIB_KHR
#include <vulkan/vulkan.h>

// ============================================================================
// Math Library
// ============================================================================

typedef struct {
    float x, y, z;
} vec3;

typedef struct {
    float m[16];
} mat4;

typedef struct {
    int x, y, z;
} ivec3;

// Vector operations
static vec3 vec3_add(vec3 a, vec3 b) {
    return (vec3){ a.x + b.x, a.y + b.y, a.z + b.z };
}

static vec3 vec3_sub(vec3 a, vec3 b) {
    return (vec3){ a.x - b.x, a.y - b.y, a.z - b.z };
}

static vec3 vec3_scale(vec3 v, float s) {
    return (vec3){ v.x * s, v.y * s, v.z * s };
}

static float vec3_length(vec3 v) {
    return sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
}

static vec3 vec3_normalize(vec3 v) {
    float len = vec3_length(v);
    if (len > 0.0f) {
        return vec3_scale(v, 1.0f / len);
    }
    return v;
}

static vec3 vec3_cross(vec3 a, vec3 b) {
    return (vec3){
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    };
}

static float vec3_dot(vec3 a, vec3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

// Matrix operations
static mat4 mat4_identity(void) {
    mat4 m = {0};
    m.m[0] = 1.0f;
    m.m[5] = 1.0f;
    m.m[10] = 1.0f;
    m.m[15] = 1.0f;
    return m;
}

static mat4 mat4_translate(vec3 v) {
    mat4 m = mat4_identity();
    m.m[12] = v.x;
    m.m[13] = v.y;
    m.m[14] = v.z;
    return m;
}

static mat4 mat4_perspective(float fov, float aspect, float near, float far) {
    mat4 m = {0};
    float tan_half_fov = tanf(fov * 0.5f);
    
    m.m[0] = 1.0f / (aspect * tan_half_fov);
    m.m[5] = -1.0f / tan_half_fov;
    m.m[10] = far / (near - far);
    m.m[11] = -1.0f;
    m.m[14] = -(far * near) / (far - near);
    
    return m;
}

static mat4 mat4_look_at(vec3 eye, vec3 center, vec3 up) {
    vec3 forward = vec3_normalize(vec3_sub(center, eye));
    vec3 side = vec3_normalize(vec3_cross(forward, up));
    vec3 up_actual = vec3_cross(side, forward);
    
    mat4 m = mat4_identity();
    m.m[0] = side.x;
    m.m[4] = side.y;
    m.m[8] = side.z;
    m.m[1] = up_actual.x;
    m.m[5] = up_actual.y;
    m.m[9] = up_actual.z;
    m.m[2] = -forward.x;
    m.m[6] = -forward.y;
    m.m[10] = -forward.z;
    m.m[12] = -vec3_dot(side, eye);
    m.m[13] = -vec3_dot(up_actual, eye);
    m.m[14] = vec3_dot(forward, eye);
    
    return m;
}

// ============================================================================
// Camera
// ============================================================================

typedef struct {
    vec3 position;
    vec3 front;
    vec3 up;
    vec3 right;
    vec3 world_up;
    float yaw;
    float pitch;
    float speed;
    float sensitivity;
} Camera;

static void camera_update_vectors(Camera *cam) {
    float yaw_rad = cam->yaw * M_PI / 180.0f;
    float pitch_rad = cam->pitch * M_PI / 180.0f;
    
    cam->front.x = cosf(yaw_rad) * cosf(pitch_rad);
    cam->front.y = sinf(pitch_rad);
    cam->front.z = sinf(yaw_rad) * cosf(pitch_rad);
    cam->front = vec3_normalize(cam->front);
    
    cam->right = vec3_normalize(vec3_cross(cam->front, cam->world_up));
    cam->up = vec3_normalize(vec3_cross(cam->right, cam->front));
}

static void camera_init(Camera *cam) {
    cam->position = (vec3){ 0.0f, 0.0f, 3.0f };
    cam->world_up = (vec3){ 0.0f, 1.0f, 0.0f };
    cam->yaw = -90.0f;
    cam->pitch = 0.0f;
    cam->speed = 2.5f;
    cam->sensitivity = 0.1f;
    
    camera_update_vectors(cam);
}

static void camera_process_mouse(Camera *cam, float xoffset, float yoffset) {
    xoffset *= cam->sensitivity;
    yoffset *= cam->sensitivity;
    
    cam->yaw += xoffset;
    cam->pitch += yoffset;
    
    // Clamp pitch to prevent gimbal lock
    if (cam->pitch > 89.0f) cam->pitch = 89.0f;
    if (cam->pitch < -89.0f) cam->pitch = -89.0f;
    
    camera_update_vectors(cam);
}

static mat4 camera_get_view_matrix(Camera *cam) {
    vec3 center = vec3_add(cam->position, cam->front);
    return mat4_look_at(cam->position, center, cam->up);
}

// ============================================================================
// Vertex & Uniform Buffer Object
// ============================================================================

typedef struct {
    vec3 pos;
    vec3 color;
} Vertex;

typedef struct {
    mat4 model;
    mat4 view;
    mat4 proj;
} UniformBufferObject;

// ============================================================================
// Voxel Helpers
// ============================================================================

static bool ivec3_equal(ivec3 a, ivec3 b) {
    return a.x == b.x && a.y == b.y && a.z == b.z;
}

static ivec3 ivec3_add(ivec3 a, ivec3 b) {
    return (ivec3){ a.x + b.x, a.y + b.y, a.z + b.z };
}

static int sign_int(int v) {
    return (v > 0) - (v < 0);
}

static ivec3 world_to_cell(vec3 p) {
    return (ivec3){
        (int)floorf(p.x + 0.5f),
        (int)floorf(p.y + 0.5f),
        (int)floorf(p.z + 0.5f)
    };
}

static int cube_index_of(ivec3 *cubes, int count, ivec3 pos) {
    for (int i = 0; i < count; i++) {
        if (ivec3_equal(cubes[i], pos)) {
            return i;
        }
    }
    return -1;
}

static bool cube_add(ivec3 *cubes, int *count, int max_count, ivec3 pos) {
    if (*count >= max_count) return false;
    if (cube_index_of(cubes, *count, pos) >= 0) return false;
    cubes[*count] = pos;
    (*count)++;
    return true;
}

static bool cube_remove(ivec3 *cubes, int *count, ivec3 pos) {
    int idx = cube_index_of(cubes, *count, pos);
    if (idx < 0) return false;
    cubes[idx] = cubes[*count - 1];
    (*count)--;
    return true;
}

typedef struct {
    bool hit;
    ivec3 cell;
    ivec3 normal;
} RayHit;

static RayHit raycast_blocks(vec3 origin, vec3 dir, ivec3 *cubes, int count, float max_dist) {
    RayHit hit = {0};
    float step = 0.05f;
    vec3 d = vec3_normalize(dir);
    
    ivec3 prev = world_to_cell(origin);
    
    for (float t = 0.0f; t <= max_dist; t += step) {
        vec3 p = vec3_add(origin, vec3_scale(d, t));
        ivec3 cell = world_to_cell(p);
        
        if (!ivec3_equal(cell, prev)) {
            if (cube_index_of(cubes, count, cell) >= 0) {
                hit.hit = true;
                hit.cell = cell;
                hit.normal = (ivec3){
                    sign_int(prev.x - cell.x),
                    sign_int(prev.y - cell.y),
                    sign_int(prev.z - cell.z)
                };
                return hit;
            }
            prev = cell;
        }
    }
    return hit;
}

// ============================================================================
// Vulkan Helpers
// ============================================================================

static const char *vk_result_to_string(VkResult result) {
    switch (result) {
        case VK_SUCCESS: return "VK_SUCCESS";
        case VK_ERROR_OUT_OF_HOST_MEMORY: return "VK_ERROR_OUT_OF_HOST_MEMORY";
        case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
        case VK_ERROR_INITIALIZATION_FAILED: return "VK_ERROR_INITIALIZATION_FAILED";
        case VK_ERROR_DEVICE_LOST: return "VK_ERROR_DEVICE_LOST";
        case VK_ERROR_MEMORY_MAP_FAILED: return "VK_ERROR_MEMORY_MAP_FAILED";
        case VK_ERROR_LAYER_NOT_PRESENT: return "VK_ERROR_LAYER_NOT_PRESENT";
        case VK_ERROR_EXTENSION_NOT_PRESENT: return "VK_ERROR_EXTENSION_NOT_PRESENT";
        case VK_ERROR_INCOMPATIBLE_DRIVER: return "VK_ERROR_INCOMPATIBLE_DRIVER";
        case VK_ERROR_OUT_OF_DATE_KHR: return "VK_ERROR_OUT_OF_DATE_KHR";
        case VK_SUBOPTIMAL_KHR: return "VK_SUBOPTIMAL_KHR";
        default: return "UNKNOWN_ERROR";
    }
}

#define VK_CHECK(call) \
    do { \
        VkResult result = (call); \
        if (result != VK_SUCCESS) { \
            fprintf(stderr, "%s failed: %s\n", #call, vk_result_to_string(result)); \
            exit(EXIT_FAILURE); \
        } \
    } while (0)

static void die(const char *msg) {
    fprintf(stderr, "Error: %s\n", msg);
    exit(EXIT_FAILURE);
}

static uint32_t find_memory_type(VkPhysicalDevice physical_device, 
                                 uint32_t type_filter, 
                                 VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties mem_properties;
    vkGetPhysicalDeviceMemoryProperties(physical_device, &mem_properties);
    
    for (uint32_t i = 0; i < mem_properties.memoryTypeCount; i++) {
        bool type_supported = (type_filter & (1 << i)) != 0;
        bool has_properties = (mem_properties.memoryTypes[i].propertyFlags & properties) == properties;
        
        if (type_supported && has_properties) {
            return i;
        }
    }
    
    die("Failed to find suitable memory type");
    return 0;
}

static void create_buffer(VkDevice device, VkPhysicalDevice physical_device,
                         VkDeviceSize size, VkBufferUsageFlags usage,
                         VkMemoryPropertyFlags properties,
                         VkBuffer *buffer, VkDeviceMemory *memory) {
    // Create buffer
    VkBufferCreateInfo buffer_info = {0};
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = size;
    buffer_info.usage = usage;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    
    VK_CHECK(vkCreateBuffer(device, &buffer_info, NULL, buffer));
    
    // Get memory requirements
    VkMemoryRequirements mem_requirements;
    vkGetBufferMemoryRequirements(device, *buffer, &mem_requirements);
    
    // Allocate memory
    VkMemoryAllocateInfo alloc_info = {0};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize = mem_requirements.size;
    alloc_info.memoryTypeIndex = find_memory_type(physical_device, 
                                                   mem_requirements.memoryTypeBits, 
                                                   properties);
    
    VK_CHECK(vkAllocateMemory(device, &alloc_info, NULL, memory));
    VK_CHECK(vkBindBufferMemory(device, *buffer, *memory, 0));
}

static void create_depth_resources(VkDevice device, VkPhysicalDevice physical_device,
                                   VkExtent2D extent, VkFormat *depth_format,
                                   VkImage *depth_image, VkDeviceMemory *depth_memory,
                                   VkImageView *depth_view) {
    *depth_format = VK_FORMAT_D32_SFLOAT;
    
    // Create depth image
    VkImageCreateInfo image_info = {0};
    image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.extent.width = extent.width;
    image_info.extent.height = extent.height;
    image_info.extent.depth = 1;
    image_info.mipLevels = 1;
    image_info.arrayLayers = 1;
    image_info.format = *depth_format;
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    image_info.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    
    VK_CHECK(vkCreateImage(device, &image_info, NULL, depth_image));
    
    // Allocate memory
    VkMemoryRequirements mem_requirements;
    vkGetImageMemoryRequirements(device, *depth_image, &mem_requirements);
    
    VkMemoryAllocateInfo alloc_info = {0};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize = mem_requirements.size;
    alloc_info.memoryTypeIndex = find_memory_type(physical_device,
                                                   mem_requirements.memoryTypeBits,
                                                   VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    
    VK_CHECK(vkAllocateMemory(device, &alloc_info, NULL, depth_memory));
    VK_CHECK(vkBindImageMemory(device, *depth_image, *depth_memory, 0));
    
    // Create image view
    VkImageViewCreateInfo view_info = {0};
    view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image = *depth_image;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = *depth_format;
    view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    view_info.subresourceRange.baseMipLevel = 0;
    view_info.subresourceRange.levelCount = 1;
    view_info.subresourceRange.baseArrayLayer = 0;
    view_info.subresourceRange.layerCount = 1;
    
    VK_CHECK(vkCreateImageView(device, &view_info, NULL, depth_view));
}

static char *read_shader_file(const char *filename, size_t *out_size) {
    FILE *file = fopen(filename, "rb");
    if (!file) {
        fprintf(stderr, "Failed to open shader file: %s\n", filename);
        return NULL;
    }
    
    fseek(file, 0, SEEK_END);
    size_t size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    char *buffer = malloc(size);
    if (!buffer) {
        fclose(file);
        return NULL;
    }
    
    fread(buffer, 1, size, file);
    fclose(file);
    
    *out_size = size;
    return buffer;
}

static VkShaderModule create_shader_module(VkDevice device, const char *code, size_t size) {
    VkShaderModuleCreateInfo create_info = {0};
    create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    create_info.codeSize = size;
    create_info.pCode = (const uint32_t *)code;
    
    VkShaderModule shader_module;
    VK_CHECK(vkCreateShaderModule(device, &create_info, NULL, &shader_module));
    
    return shader_module;
}

// ============================================================================
// Main
// ============================================================================

int main(void) {
    printf("Voxel Engine\n");
    
    const uint32_t WINDOW_WIDTH = 800;
    const uint32_t WINDOW_HEIGHT = 600;
    const int CENTER_X = WINDOW_WIDTH / 2;
    const int CENTER_Y = WINDOW_HEIGHT / 2;
    
    const int MAX_CUBES = 4096;
    
    // ========================================================================
    // X11 Window Setup
    // ========================================================================
    
    Display *display = XOpenDisplay(NULL);
    if (!display) {
        die("Failed to open X11 display");
    }
    
    int screen = DefaultScreen(display);
    Window root = RootWindow(display, screen);
    
    Window window = XCreateSimpleWindow(display, root, 0, 0, 
                                       WINDOW_WIDTH, WINDOW_HEIGHT, 1,
                                       BlackPixel(display, screen),
                                       WhitePixel(display, screen));
    
    XStoreName(display, window, "Voxel Engine");
    XSelectInput(display, window, 
                 ExposureMask | KeyPressMask | KeyReleaseMask | 
                 PointerMotionMask | StructureNotifyMask | ButtonPressMask);
    
    Atom wm_delete_window = XInternAtom(display, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(display, window, &wm_delete_window, 1);
    
    XMapWindow(display, window);
    XGrabPointer(display, window, True, PointerMotionMask, 
                GrabModeAsync, GrabModeAsync, window, None, CurrentTime);
    
    // Hide cursor
    char cursor_data[] = {0, 0, 0, 0, 0, 0, 0, 0};
    Colormap colormap = DefaultColormap(display, screen);
    XColor black_color, dummy;
    XAllocNamedColor(display, colormap, "black", &black_color, &dummy);
    
    Pixmap blank_pixmap = XCreateBitmapFromData(display, window, cursor_data, 8, 8);
    Cursor invisible_cursor = XCreatePixmapCursor(display, blank_pixmap, blank_pixmap,
                                                  &black_color, &black_color, 0, 0);
    XDefineCursor(display, window, invisible_cursor);
    XFreeCursor(display, invisible_cursor);
    XFreePixmap(display, blank_pixmap);
    
    // ========================================================================
    // Vulkan Instance
    // ========================================================================
    
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
    instance_info.enabledExtensionCount = 2;
    instance_info.ppEnabledExtensionNames = instance_extensions;
    
    VkInstance instance;
    VK_CHECK(vkCreateInstance(&instance_info, NULL, &instance));
    
    // ========================================================================
    // Vulkan Surface
    // ========================================================================
    
    VkXlibSurfaceCreateInfoKHR surface_info = {0};
    surface_info.sType = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR;
    surface_info.dpy = display;
    surface_info.window = window;
    
    VkSurfaceKHR surface;
    VK_CHECK(vkCreateXlibSurfaceKHR(instance, &surface_info, NULL, &surface));
    
    // ========================================================================
    // Physical Device Selection
    // ========================================================================
    
    uint32_t device_count = 0;
    vkEnumeratePhysicalDevices(instance, &device_count, NULL);
    if (device_count == 0) {
        die("No Vulkan-capable GPUs found");
    }
    
    VkPhysicalDevice *physical_devices = malloc(sizeof(VkPhysicalDevice) * device_count);
    vkEnumeratePhysicalDevices(instance, &device_count, physical_devices);
    
    VkPhysicalDevice physical_device = VK_NULL_HANDLE;
    uint32_t graphics_family = UINT32_MAX;
    
    for (uint32_t i = 0; i < device_count; i++) {
        uint32_t queue_family_count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(physical_devices[i], &queue_family_count, NULL);
        
        VkQueueFamilyProperties *queue_families = 
            malloc(sizeof(VkQueueFamilyProperties) * queue_family_count);
        vkGetPhysicalDeviceQueueFamilyProperties(physical_devices[i], 
                                                &queue_family_count, queue_families);
        
        for (uint32_t j = 0; j < queue_family_count; j++) {
            VkBool32 present_support = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(physical_devices[i], j, surface, &present_support);
            
            bool has_graphics = (queue_families[j].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0;
            
            if (has_graphics && present_support) {
                physical_device = physical_devices[i];
                graphics_family = j;
                break;
            }
        }
        
        free(queue_families);
        
        if (physical_device != VK_NULL_HANDLE) {
            break;
        }
    }
    
    free(physical_devices);
    
    if (physical_device == VK_NULL_HANDLE) {
        die("No suitable GPU found");
    }
    
    // ========================================================================
    // Logical Device
    // ========================================================================
    
    float queue_priority = 1.0f;
    
    VkDeviceQueueCreateInfo queue_info = {0};
    queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue_info.queueFamilyIndex = graphics_family;
    queue_info.queueCount = 1;
    queue_info.pQueuePriorities = &queue_priority;
    
    const char *device_extensions[] = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME
    };
    
    VkPhysicalDeviceFeatures device_features = {0};
    
    VkDeviceCreateInfo device_info = {0};
    device_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    device_info.queueCreateInfoCount = 1;
    device_info.pQueueCreateInfos = &queue_info;
    device_info.enabledExtensionCount = 1;
    device_info.ppEnabledExtensionNames = device_extensions;
    device_info.pEnabledFeatures = &device_features;
    
    VkDevice device;
    VK_CHECK(vkCreateDevice(physical_device, &device_info, NULL, &device));
    
    VkQueue graphics_queue;
    vkGetDeviceQueue(device, graphics_family, 0, &graphics_queue);
    
    // ========================================================================
    // Swapchain
    // ========================================================================
    
    VkSurfaceCapabilitiesKHR surface_capabilities;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device, surface, &surface_capabilities);
    
    uint32_t format_count;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &format_count, NULL);
    VkSurfaceFormatKHR *surface_formats = malloc(sizeof(VkSurfaceFormatKHR) * format_count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &format_count, surface_formats);
    
    VkSurfaceFormatKHR chosen_format = surface_formats[0];
    for (uint32_t i = 0; i < format_count; i++) {
        if (surface_formats[i].format == VK_FORMAT_B8G8R8A8_SRGB) {
            chosen_format = surface_formats[i];
            break;
        }
    }
    free(surface_formats);
    
    uint32_t image_count = surface_capabilities.minImageCount + 1;
    if (surface_capabilities.maxImageCount > 0 && image_count > surface_capabilities.maxImageCount) {
        image_count = surface_capabilities.maxImageCount;
    }
    
    VkExtent2D swapchain_extent = surface_capabilities.currentExtent;
    if (swapchain_extent.width == UINT32_MAX) {
        swapchain_extent.width = WINDOW_WIDTH;
        swapchain_extent.height = WINDOW_HEIGHT;
    }
    
    VkSwapchainCreateInfoKHR swapchain_info = {0};
    swapchain_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swapchain_info.surface = surface;
    swapchain_info.minImageCount = image_count;
    swapchain_info.imageFormat = chosen_format.format;
    swapchain_info.imageColorSpace = chosen_format.colorSpace;
    swapchain_info.imageExtent = swapchain_extent;
    swapchain_info.imageArrayLayers = 1;
    swapchain_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    swapchain_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    swapchain_info.preTransform = surface_capabilities.currentTransform;
    swapchain_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    swapchain_info.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    swapchain_info.clipped = VK_TRUE;
    
    VkSwapchainKHR swapchain;
    VK_CHECK(vkCreateSwapchainKHR(device, &swapchain_info, NULL, &swapchain));
    
    // Get swapchain images
    vkGetSwapchainImagesKHR(device, swapchain, &image_count, NULL);
    VkImage *swapchain_images = malloc(sizeof(VkImage) * image_count);
    vkGetSwapchainImagesKHR(device, swapchain, &image_count, swapchain_images);
    
    // Create image views
    VkImageView *swapchain_image_views = malloc(sizeof(VkImageView) * image_count);
    for (uint32_t i = 0; i < image_count; i++) {
        VkImageViewCreateInfo view_info = {0};
        view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view_info.image = swapchain_images[i];
        view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view_info.format = chosen_format.format;
        view_info.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        view_info.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        view_info.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        view_info.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        view_info.subresourceRange.baseMipLevel = 0;
        view_info.subresourceRange.levelCount = 1;
        view_info.subresourceRange.baseArrayLayer = 0;
        view_info.subresourceRange.layerCount = 1;
        
        VK_CHECK(vkCreateImageView(device, &view_info, NULL, &swapchain_image_views[i]));
    }
    
    // ========================================================================
    // Depth Resources
    // ========================================================================
    
    VkFormat depth_format;
    VkImage depth_image;
    VkDeviceMemory depth_memory;
    VkImageView depth_view;
    
    create_depth_resources(device, physical_device, swapchain_extent, 
                          &depth_format, &depth_image, &depth_memory, &depth_view);
    
    // ========================================================================
    // Render Pass
    // ========================================================================
    
    VkAttachmentDescription attachments[2] = {0};
    
    // Color attachment
    attachments[0].format = chosen_format.format;
    attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    
    // Depth attachment
    attachments[1].format = depth_format;
    attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    
    VkAttachmentReference color_attachment_ref = {0};
    color_attachment_ref.attachment = 0;
    color_attachment_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    
    VkAttachmentReference depth_attachment_ref = {0};
    depth_attachment_ref.attachment = 1;
    depth_attachment_ref.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    
    VkSubpassDescription subpass = {0};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color_attachment_ref;
    subpass.pDepthStencilAttachment = &depth_attachment_ref;
    
    VkSubpassDependency dependency = {0};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | 
                             VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | 
                             VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | 
                              VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    
    VkRenderPassCreateInfo render_pass_info = {0};
    render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    render_pass_info.attachmentCount = 2;
    render_pass_info.pAttachments = attachments;
    render_pass_info.subpassCount = 1;
    render_pass_info.pSubpasses = &subpass;
    render_pass_info.dependencyCount = 1;
    render_pass_info.pDependencies = &dependency;
    
    VkRenderPass render_pass;
    VK_CHECK(vkCreateRenderPass(device, &render_pass_info, NULL, &render_pass));
    
    // ========================================================================
    // Shaders
    // ========================================================================
    
    size_t vert_shader_size, frag_shader_size;
    char *vert_shader_code = read_shader_file("shaders/vert.spv", &vert_shader_size);
    char *frag_shader_code = read_shader_file("shaders/frag.spv", &frag_shader_size);
    
    if (!vert_shader_code || !frag_shader_code) {
        die("Failed to load shader files");
    }
    
    VkShaderModule vert_shader = create_shader_module(device, vert_shader_code, vert_shader_size);
    VkShaderModule frag_shader = create_shader_module(device, frag_shader_code, frag_shader_size);
    
    free(vert_shader_code);
    free(frag_shader_code);
    
    // ========================================================================
    // Descriptor Set Layout
    // ========================================================================
    
    VkDescriptorSetLayoutBinding ubo_layout_binding = {0};
    ubo_layout_binding.binding = 0;
    ubo_layout_binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    ubo_layout_binding.descriptorCount = 1;
    ubo_layout_binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    
    VkDescriptorSetLayoutCreateInfo descriptor_layout_info = {0};
    descriptor_layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    descriptor_layout_info.bindingCount = 1;
    descriptor_layout_info.pBindings = &ubo_layout_binding;
    
    VkDescriptorSetLayout descriptor_set_layout;
    VK_CHECK(vkCreateDescriptorSetLayout(device, &descriptor_layout_info, NULL, 
                                        &descriptor_set_layout));
    
    // ========================================================================
    // Pipeline Layout
    // ========================================================================
    
    VkPipelineLayoutCreateInfo pipeline_layout_info = {0};
    pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipeline_layout_info.setLayoutCount = 1;
    pipeline_layout_info.pSetLayouts = &descriptor_set_layout;
    
    VkPipelineLayout pipeline_layout;
    VK_CHECK(vkCreatePipelineLayout(device, &pipeline_layout_info, NULL, &pipeline_layout));
    
    // ========================================================================
    // Graphics Pipeline
    // ========================================================================
    
    VkPipelineShaderStageCreateInfo shader_stages[2] = {0};
    
    shader_stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shader_stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    shader_stages[0].module = vert_shader;
    shader_stages[0].pName = "main";
    
    shader_stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shader_stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    shader_stages[1].module = frag_shader;
    shader_stages[1].pName = "main";
    
    VkVertexInputBindingDescription vertex_binding = {0};
    vertex_binding.binding = 0;
    vertex_binding.stride = sizeof(Vertex);
    vertex_binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    
    VkVertexInputAttributeDescription vertex_attributes[2] = {0};
    
    vertex_attributes[0].binding = 0;
    vertex_attributes[0].location = 0;
    vertex_attributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    vertex_attributes[0].offset = offsetof(Vertex, pos);
    
    vertex_attributes[1].binding = 0;
    vertex_attributes[1].location = 1;
    vertex_attributes[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    vertex_attributes[1].offset = offsetof(Vertex, color);
    
    VkPipelineVertexInputStateCreateInfo vertex_input_info = {0};
    vertex_input_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertex_input_info.vertexBindingDescriptionCount = 1;
    vertex_input_info.pVertexBindingDescriptions = &vertex_binding;
    vertex_input_info.vertexAttributeDescriptionCount = 2;
    vertex_input_info.pVertexAttributeDescriptions = vertex_attributes;
    
    VkPipelineInputAssemblyStateCreateInfo input_assembly = {0};
    input_assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    input_assembly.primitiveRestartEnable = VK_FALSE;
    
    VkViewport viewport = {0};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = (float)swapchain_extent.width;
    viewport.height = (float)swapchain_extent.height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    
    VkRect2D scissor = {0};
    scissor.offset.x = 0;
    scissor.offset.y = 0;
    scissor.extent = swapchain_extent;
    
    VkPipelineViewportStateCreateInfo viewport_state = {0};
    viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state.viewportCount = 1;
    viewport_state.pViewports = &viewport;
    viewport_state.scissorCount = 1;
    viewport_state.pScissors = &scissor;
    
    VkPipelineRasterizationStateCreateInfo rasterizer = {0};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;
    
    VkPipelineMultisampleStateCreateInfo multisampling = {0};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    
    VkPipelineDepthStencilStateCreateInfo depth_stencil = {0};
    depth_stencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth_stencil.depthTestEnable = VK_TRUE;
    depth_stencil.depthWriteEnable = VK_TRUE;
    depth_stencil.depthCompareOp = VK_COMPARE_OP_LESS;
    depth_stencil.depthBoundsTestEnable = VK_FALSE;
    depth_stencil.stencilTestEnable = VK_FALSE;
    
    VkPipelineColorBlendAttachmentState color_blend_attachment = {0};
    color_blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | 
                                           VK_COLOR_COMPONENT_G_BIT |
                                           VK_COLOR_COMPONENT_B_BIT | 
                                           VK_COLOR_COMPONENT_A_BIT;
    color_blend_attachment.blendEnable = VK_FALSE;
    
    VkPipelineColorBlendStateCreateInfo color_blending = {0};
    color_blending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    color_blending.logicOpEnable = VK_FALSE;
    color_blending.attachmentCount = 1;
    color_blending.pAttachments = &color_blend_attachment;
    
    VkGraphicsPipelineCreateInfo pipeline_info = {0};
    pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeline_info.stageCount = 2;
    pipeline_info.pStages = shader_stages;
    pipeline_info.pVertexInputState = &vertex_input_info;
    pipeline_info.pInputAssemblyState = &input_assembly;
    pipeline_info.pViewportState = &viewport_state;
    pipeline_info.pRasterizationState = &rasterizer;
    pipeline_info.pMultisampleState = &multisampling;
    pipeline_info.pDepthStencilState = &depth_stencil;
    pipeline_info.pColorBlendState = &color_blending;
    pipeline_info.layout = pipeline_layout;
    pipeline_info.renderPass = render_pass;
    pipeline_info.subpass = 0;
    
    VkPipeline graphics_pipeline;
    VK_CHECK(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipeline_info, 
                                      NULL, &graphics_pipeline));
    
    vkDestroyShaderModule(device, vert_shader, NULL);
    vkDestroyShaderModule(device, frag_shader, NULL);
    
    // ========================================================================
    // Framebuffers
    // ========================================================================
    
    VkFramebuffer *framebuffers = malloc(sizeof(VkFramebuffer) * image_count);
    for (uint32_t i = 0; i < image_count; i++) {
        VkImageView attachments_array[] = { swapchain_image_views[i], depth_view };
        
        VkFramebufferCreateInfo framebuffer_info = {0};
        framebuffer_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebuffer_info.renderPass = render_pass;
        framebuffer_info.attachmentCount = 2;
        framebuffer_info.pAttachments = attachments_array;
        framebuffer_info.width = swapchain_extent.width;
        framebuffer_info.height = swapchain_extent.height;
        framebuffer_info.layers = 1;
        
        VK_CHECK(vkCreateFramebuffer(device, &framebuffer_info, NULL, &framebuffers[i]));
    }
    
    // ========================================================================
    // Command Pool
    // ========================================================================
    
    VkCommandPoolCreateInfo pool_info = {0};
    pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_info.queueFamilyIndex = graphics_family;
    pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    
    VkCommandPool command_pool;
    VK_CHECK(vkCreateCommandPool(device, &pool_info, NULL, &command_pool));
    
    // ========================================================================
    // Vertex & Index Buffers (Cube)
    // ========================================================================
    
    Vertex vertices[] = {
        // Front face (red)
        {{-0.5f, -0.5f,  0.5f}, {1.0f, 0.0f, 0.0f}},
        {{ 0.5f, -0.5f,  0.5f}, {1.0f, 0.0f, 0.0f}},
        {{ 0.5f,  0.5f,  0.5f}, {1.0f, 0.0f, 0.0f}},
        {{-0.5f,  0.5f,  0.5f}, {1.0f, 0.0f, 0.0f}},
        
        // Back face (green)
        {{-0.5f, -0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}},
        {{ 0.5f, -0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}},
        {{ 0.5f,  0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}},
        {{-0.5f,  0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}},
        
        // Top face (blue)
        {{-0.5f,  0.5f, -0.5f}, {0.0f, 0.0f, 1.0f}},
        {{ 0.5f,  0.5f, -0.5f}, {0.0f, 0.0f, 1.0f}},
        {{ 0.5f,  0.5f,  0.5f}, {0.0f, 0.0f, 1.0f}},
        {{-0.5f,  0.5f,  0.5f}, {0.0f, 0.0f, 1.0f}},
        
        // Bottom face (yellow)
        {{-0.5f, -0.5f, -0.5f}, {1.0f, 1.0f, 0.0f}},
        {{ 0.5f, -0.5f, -0.5f}, {1.0f, 1.0f, 0.0f}},
        {{ 0.5f, -0.5f,  0.5f}, {1.0f, 1.0f, 0.0f}},
        {{-0.5f, -0.5f,  0.5f}, {1.0f, 1.0f, 0.0f}},
        
        // Right face (cyan)
        {{ 0.5f, -0.5f, -0.5f}, {0.0f, 1.0f, 1.0f}},
        {{ 0.5f,  0.5f, -0.5f}, {0.0f, 1.0f, 1.0f}},
        {{ 0.5f,  0.5f,  0.5f}, {0.0f, 1.0f, 1.0f}},
        {{ 0.5f, -0.5f,  0.5f}, {0.0f, 1.0f, 1.0f}},
        
        // Left face (magenta)
        {{-0.5f, -0.5f, -0.5f}, {1.0f, 0.0f, 1.0f}},
        {{-0.5f,  0.5f, -0.5f}, {1.0f, 0.0f, 1.0f}},
        {{-0.5f,  0.5f,  0.5f}, {1.0f, 0.0f, 1.0f}},
        {{-0.5f, -0.5f,  0.5f}, {1.0f, 0.0f, 1.0f}},
    };
    
    uint16_t indices[] = {
        0, 1, 2,  2, 3, 0,      // Front
        6, 5, 4,  4, 7, 6,      // Back
        8, 11, 10, 10, 9, 8,    // Top
        12, 13, 14, 14, 15, 12, // Bottom
        16, 17, 18, 18, 19, 16, // Right
        22, 21, 20, 20, 23, 22  // Left
    };
    
    VkBuffer vertex_buffer;
    VkDeviceMemory vertex_buffer_memory;
    
    create_buffer(device, physical_device, sizeof(vertices),
                 VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 &vertex_buffer, &vertex_buffer_memory);
    
    void *vertex_data;
    vkMapMemory(device, vertex_buffer_memory, 0, sizeof(vertices), 0, &vertex_data);
    memcpy(vertex_data, vertices, sizeof(vertices));
    vkUnmapMemory(device, vertex_buffer_memory);
    
    VkBuffer index_buffer;
    VkDeviceMemory index_buffer_memory;
    
    create_buffer(device, physical_device, sizeof(indices),
                 VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 &index_buffer, &index_buffer_memory);
    
    void *index_data;
    vkMapMemory(device, index_buffer_memory, 0, sizeof(indices), 0, &index_data);
    memcpy(index_data, indices, sizeof(indices));
    vkUnmapMemory(device, index_buffer_memory);
    
    // ========================================================================
    // Uniform Buffers (Dynamic)
    // ========================================================================
    
    VkPhysicalDeviceProperties device_properties;
    vkGetPhysicalDeviceProperties(physical_device, &device_properties);
    
    VkDeviceSize ubo_stride = sizeof(UniformBufferObject);
    VkDeviceSize min_alignment = device_properties.limits.minUniformBufferOffsetAlignment;
    if (min_alignment > 0) {
        ubo_stride = (ubo_stride + min_alignment - 1) & ~(min_alignment - 1);
    }
    
    VkDeviceSize uniform_buffer_size = ubo_stride * (VkDeviceSize)MAX_CUBES;
    
    VkBuffer *uniform_buffers = malloc(sizeof(VkBuffer) * image_count);
    VkDeviceMemory *uniform_buffers_memory = malloc(sizeof(VkDeviceMemory) * image_count);
    
    for (uint32_t i = 0; i < image_count; i++) {
        create_buffer(device, physical_device, uniform_buffer_size,
                     VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     &uniform_buffers[i], &uniform_buffers_memory[i]);
    }
    
    // ========================================================================
    // Descriptor Pool & Sets
    // ========================================================================
    
    VkDescriptorPoolSize pool_size = {0};
    pool_size.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    pool_size.descriptorCount = image_count;
    
    VkDescriptorPoolCreateInfo descriptor_pool_info = {0};
    descriptor_pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    descriptor_pool_info.poolSizeCount = 1;
    descriptor_pool_info.pPoolSizes = &pool_size;
    descriptor_pool_info.maxSets = image_count;
    
    VkDescriptorPool descriptor_pool;
    VK_CHECK(vkCreateDescriptorPool(device, &descriptor_pool_info, NULL, &descriptor_pool));
    
    VkDescriptorSetLayout *layouts = malloc(sizeof(VkDescriptorSetLayout) * image_count);
    for (uint32_t i = 0; i < image_count; i++) {
        layouts[i] = descriptor_set_layout;
    }
    
    VkDescriptorSetAllocateInfo descriptor_alloc_info = {0};
    descriptor_alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    descriptor_alloc_info.descriptorPool = descriptor_pool;
    descriptor_alloc_info.descriptorSetCount = image_count;
    descriptor_alloc_info.pSetLayouts = layouts;
    
    VkDescriptorSet *descriptor_sets = malloc(sizeof(VkDescriptorSet) * image_count);
    VK_CHECK(vkAllocateDescriptorSets(device, &descriptor_alloc_info, descriptor_sets));
    
    free(layouts);
    
    for (uint32_t i = 0; i < image_count; i++) {
        VkDescriptorBufferInfo buffer_info = {0};
        buffer_info.buffer = uniform_buffers[i];
        buffer_info.offset = 0;
        buffer_info.range = sizeof(UniformBufferObject);
        
        VkWriteDescriptorSet descriptor_write = {0};
        descriptor_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptor_write.dstSet = descriptor_sets[i];
        descriptor_write.dstBinding = 0;
        descriptor_write.dstArrayElement = 0;
        descriptor_write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        descriptor_write.descriptorCount = 1;
        descriptor_write.pBufferInfo = &buffer_info;
        
        vkUpdateDescriptorSets(device, 1, &descriptor_write, 0, NULL);
    }
    
    // ========================================================================
    // Command Buffers
    // ========================================================================
    
    VkCommandBuffer *command_buffers = malloc(sizeof(VkCommandBuffer) * image_count);
    
    VkCommandBufferAllocateInfo command_buffer_alloc_info = {0};
    command_buffer_alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    command_buffer_alloc_info.commandPool = command_pool;
    command_buffer_alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    command_buffer_alloc_info.commandBufferCount = image_count;
    
    VK_CHECK(vkAllocateCommandBuffers(device, &command_buffer_alloc_info, command_buffers));
    
    // ========================================================================
    // Synchronization Objects
    // ========================================================================
    
    VkSemaphoreCreateInfo semaphore_info = {0};
    semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    
    VkFenceCreateInfo fence_info = {0};
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    
    VkSemaphore image_available_semaphore;
    VkSemaphore render_finished_semaphore;
    VkFence in_flight_fence;
    
    VK_CHECK(vkCreateSemaphore(device, &semaphore_info, NULL, &image_available_semaphore));
    VK_CHECK(vkCreateSemaphore(device, &semaphore_info, NULL, &render_finished_semaphore));
    VK_CHECK(vkCreateFence(device, &fence_info, NULL, &in_flight_fence));
    
    // ========================================================================
    // Camera & Input Setup
    // ========================================================================
    
    Camera camera;
    camera_init(&camera);
    
    bool keys[256] = {0};
    bool first_mouse = true;
    float last_mouse_x = (float)CENTER_X;
    float last_mouse_y = (float)CENTER_Y;
    
    // ========================================================================
    // Voxel World Setup
    // ========================================================================
    
    ivec3 *cubes = malloc(sizeof(ivec3) * MAX_CUBES);
    int cube_count = 0;
    cube_add(cubes, &cube_count, MAX_CUBES, (ivec3){0, 0, 0});
    
    // ========================================================================
    // Timing
    // ========================================================================
    
    struct timespec last_frame_time;
    clock_gettime(CLOCK_MONOTONIC, &last_frame_time);
    
    // ========================================================================
    // Main Loop
    // ========================================================================
    
    bool running = true;
    
    while (running) {
        // Process all pending X11 events
        bool mouse_moved = false;
        float current_mouse_x = 0.0f;
        float current_mouse_y = 0.0f;
        bool left_click = false;
        bool right_click = false;
        
        while (XPending(display) > 0) {
            XEvent event;
            XNextEvent(display, &event);
            
            if (event.type == ClientMessage) {
                if ((Atom)event.xclient.data.l[0] == wm_delete_window) {
                    running = false;
                }
            } else if (event.type == KeyPress) {
                KeySym key = XLookupKeysym(&event.xkey, 0);
                if (key == XK_Escape) {
                    running = false;
                } else if (key < 256) {
                    keys[key] = true;
                }
            } else if (event.type == KeyRelease) {
                KeySym key = XLookupKeysym(&event.xkey, 0);
                if (key < 256) {
                    keys[key] = false;
                }
            } else if (event.type == MotionNotify) {
                current_mouse_x = (float)event.xmotion.x;
                current_mouse_y = (float)event.xmotion.y;
                mouse_moved = true;
            } else if (event.type == ButtonPress) {
                if (event.xbutton.button == Button1) {
                    left_click = true;
                } else if (event.xbutton.button == Button3) {
                    right_click = true;
                }
            }
        }
        
        // Process mouse movement (only once per frame)
        if (mouse_moved) {
            if (first_mouse) {
                last_mouse_x = current_mouse_x;
                last_mouse_y = current_mouse_y;
                first_mouse = false;
            }
            
            float xoffset = current_mouse_x - last_mouse_x;
            float yoffset = last_mouse_y - current_mouse_y;
            
            camera_process_mouse(&camera, xoffset, yoffset);
            
            // Warp pointer back to center
            XWarpPointer(display, None, window, 0, 0, 0, 0, CENTER_X, CENTER_Y);
            XFlush(display);
            
            last_mouse_x = (float)CENTER_X;
            last_mouse_y = (float)CENTER_Y;
        }
        
        // Calculate delta time
        struct timespec current_frame_time;
        clock_gettime(CLOCK_MONOTONIC, &current_frame_time);
        
        float delta_time = (current_frame_time.tv_sec - last_frame_time.tv_sec) +
                          (current_frame_time.tv_nsec - last_frame_time.tv_nsec) / 1000000000.0f;
        last_frame_time = current_frame_time;
        
        // Camera movement
        float camera_velocity = camera.speed * delta_time;
        
        if (keys['w'] || keys['W']) {
            camera.position = vec3_add(camera.position, vec3_scale(camera.front, camera_velocity));
        }
        if (keys['s'] || keys['S']) {
            camera.position = vec3_sub(camera.position, vec3_scale(camera.front, camera_velocity));
        }
        if (keys['a'] || keys['A']) {
            camera.position = vec3_sub(camera.position, vec3_scale(camera.right, camera_velocity));
        }
        if (keys['d'] || keys['D']) {
            camera.position = vec3_add(camera.position, vec3_scale(camera.right, camera_velocity));
        }
        if (keys['e'] || keys['E'] || keys[' ']) {
            camera.position = vec3_add(camera.position, vec3_scale(camera.world_up, camera_velocity));
        }
        if (keys['q'] || keys['Q'] || keys['c'] || keys['C']) {
            camera.position = vec3_sub(camera.position, vec3_scale(camera.world_up, camera_velocity));
        }
        
        // Handle block placement / destruction
        if (left_click || right_click) {
            RayHit hit = raycast_blocks(camera.position, camera.front, cubes, cube_count, 6.0f);
            if (hit.hit) {
                if (left_click) {
                    cube_remove(cubes, &cube_count, hit.cell);
                }
                if (right_click) {
                    if (!(hit.normal.x == 0 && hit.normal.y == 0 && hit.normal.z == 0)) {
                        ivec3 place_pos = ivec3_add(hit.cell, hit.normal);
                        cube_add(cubes, &cube_count, MAX_CUBES, place_pos);
                    }
                }
            }
        }
        
        // ====================================================================
        // Render Frame
        // ====================================================================
        
        VK_CHECK(vkWaitForFences(device, 1, &in_flight_fence, VK_TRUE, UINT64_MAX));
        VK_CHECK(vkResetFences(device, 1, &in_flight_fence));
        
        uint32_t image_index;
        VkResult acquire_result = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX,
                                                       image_available_semaphore, 
                                                       VK_NULL_HANDLE, &image_index);
        
        if (acquire_result == VK_ERROR_OUT_OF_DATE_KHR) {
            continue;
        } else if (acquire_result != VK_SUCCESS && acquire_result != VK_SUBOPTIMAL_KHR) {
            die("Failed to acquire swapchain image");
        }
        
        // Update uniform buffer array
        mat4 view = camera_get_view_matrix(&camera);
        mat4 proj = mat4_perspective(45.0f * M_PI / 180.0f,
                                    (float)swapchain_extent.width / (float)swapchain_extent.height,
                                    0.1f, 100.0f);
        
        size_t mapped_size = (size_t)ubo_stride * (size_t)cube_count;
        void *ubo_data;
        vkMapMemory(device, uniform_buffers_memory[image_index], 0, mapped_size, 0, &ubo_data);
        char *ubo_ptr = (char *)ubo_data;
        
        for (int i = 0; i < cube_count; i++) {
            UniformBufferObject ubo;
            ubo.model = mat4_translate((vec3){ (float)cubes[i].x, (float)cubes[i].y, (float)cubes[i].z });
            ubo.view = view;
            ubo.proj = proj;
            memcpy(ubo_ptr + (size_t)i * (size_t)ubo_stride, &ubo, sizeof(ubo));
        }
        
        vkUnmapMemory(device, uniform_buffers_memory[image_index]);
        
        // Record command buffer
        VK_CHECK(vkResetCommandBuffer(command_buffers[image_index], 0));
        
        VkCommandBufferBeginInfo begin_info = {0};
        begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        
        VK_CHECK(vkBeginCommandBuffer(command_buffers[image_index], &begin_info));
        
        VkClearValue clear_values[2];
        clear_values[0].color.float32[0] = 0.1f;
        clear_values[0].color.float32[1] = 0.12f;
        clear_values[0].color.float32[2] = 0.18f;
        clear_values[0].color.float32[3] = 1.0f;
        clear_values[1].depthStencil.depth = 1.0f;
        clear_values[1].depthStencil.stencil = 0;
        
        VkRenderPassBeginInfo render_pass_begin_info = {0};
        render_pass_begin_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        render_pass_begin_info.renderPass = render_pass;
        render_pass_begin_info.framebuffer = framebuffers[image_index];
        render_pass_begin_info.renderArea.offset.x = 0;
        render_pass_begin_info.renderArea.offset.y = 0;
        render_pass_begin_info.renderArea.extent = swapchain_extent;
        render_pass_begin_info.clearValueCount = 2;
        render_pass_begin_info.pClearValues = clear_values;
        
        vkCmdBeginRenderPass(command_buffers[image_index], &render_pass_begin_info, 
                            VK_SUBPASS_CONTENTS_INLINE);
        
        vkCmdBindPipeline(command_buffers[image_index], VK_PIPELINE_BIND_POINT_GRAPHICS, 
                         graphics_pipeline);
        
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(command_buffers[image_index], 0, 1, &vertex_buffer, offsets);
        vkCmdBindIndexBuffer(command_buffers[image_index], index_buffer, 0, VK_INDEX_TYPE_UINT16);
        
        for (int i = 0; i < cube_count; i++) {
            uint32_t dynamic_offset = (uint32_t)((VkDeviceSize)i * ubo_stride);
            vkCmdBindDescriptorSets(command_buffers[image_index], VK_PIPELINE_BIND_POINT_GRAPHICS,
                                   pipeline_layout, 0, 1, &descriptor_sets[image_index],
                                   1, &dynamic_offset);
            vkCmdDrawIndexed(command_buffers[image_index], 36, 1, 0, 0, 0);
        }
        
        vkCmdEndRenderPass(command_buffers[image_index]);
        
        VK_CHECK(vkEndCommandBuffer(command_buffers[image_index]));
        
        // Submit
        VkPipelineStageFlags wait_stages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
        
        VkSubmitInfo submit_info = {0};
        submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit_info.waitSemaphoreCount = 1;
        submit_info.pWaitSemaphores = &image_available_semaphore;
        submit_info.pWaitDstStageMask = wait_stages;
        submit_info.commandBufferCount = 1;
        submit_info.pCommandBuffers = &command_buffers[image_index];
        submit_info.signalSemaphoreCount = 1;
        submit_info.pSignalSemaphores = &render_finished_semaphore;
        
        VK_CHECK(vkQueueSubmit(graphics_queue, 1, &submit_info, in_flight_fence));
        
        // Present
        VkPresentInfoKHR present_info = {0};
        present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        present_info.waitSemaphoreCount = 1;
        present_info.pWaitSemaphores = &render_finished_semaphore;
        present_info.swapchainCount = 1;
        present_info.pSwapchains = &swapchain;
        present_info.pImageIndices = &image_index;
        
        vkQueuePresentKHR(graphics_queue, &present_info);
    }
    
    // ========================================================================
    // Cleanup
    // ========================================================================
    
    vkDeviceWaitIdle(device);
    
    vkDestroyFence(device, in_flight_fence, NULL);
    vkDestroySemaphore(device, render_finished_semaphore, NULL);
    vkDestroySemaphore(device, image_available_semaphore, NULL);
    
    vkFreeCommandBuffers(device, command_pool, image_count, command_buffers);
    free(command_buffers);
    
    for (uint32_t i = 0; i < image_count; i++) {
        vkDestroyBuffer(device, uniform_buffers[i], NULL);
        vkFreeMemory(device, uniform_buffers_memory[i], NULL);
    }
    free(uniform_buffers);
    free(uniform_buffers_memory);
    
    vkDestroyDescriptorPool(device, descriptor_pool, NULL);
    free(descriptor_sets);
    
    vkDestroyBuffer(device, vertex_buffer, NULL);
    vkFreeMemory(device, vertex_buffer_memory, NULL);
    vkDestroyBuffer(device, index_buffer, NULL);
    vkFreeMemory(device, index_buffer_memory, NULL);
    
    vkDestroyCommandPool(device, command_pool, NULL);
    
    for (uint32_t i = 0; i < image_count; i++) {
        vkDestroyFramebuffer(device, framebuffers[i], NULL);
    }
    free(framebuffers);
    
    vkDestroyPipeline(device, graphics_pipeline, NULL);
    vkDestroyPipelineLayout(device, pipeline_layout, NULL);
    vkDestroyDescriptorSetLayout(device, descriptor_set_layout, NULL);
    vkDestroyRenderPass(device, render_pass, NULL);
    
    vkDestroyImageView(device, depth_view, NULL);
    vkDestroyImage(device, depth_image, NULL);
    vkFreeMemory(device, depth_memory, NULL);
    
    for (uint32_t i = 0; i < image_count; i++) {
        vkDestroyImageView(device, swapchain_image_views[i], NULL);
    }
    free(swapchain_image_views);
    free(swapchain_images);
    
    vkDestroySwapchainKHR(device, swapchain, NULL);
    vkDestroyDevice(device, NULL);
    vkDestroySurfaceKHR(instance, surface, NULL);
    vkDestroyInstance(instance, NULL);
    
    free(cubes);
    
    XDestroyWindow(display, window);
    XCloseDisplay(display);
    
    printf("Shutdown complete.\n");
    return 0;
}