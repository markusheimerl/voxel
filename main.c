#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

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
    float x, y, z, w;
} vec4;

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

static mat4 mat4_identity(void) {
    mat4 m = {0};
    m.m[0] = m.m[5] = m.m[10] = m.m[15] = 1.0f;
    return m;
}

static mat4 mat4_perspective(float fov, float aspect, float near, float far) {
    mat4 m = {0};
    float tan_half_fov = tanf(fov / 2.0f);
    
    m.m[0] = 1.0f / (aspect * tan_half_fov);
    m.m[5] = -1.0f / tan_half_fov;  // Negative for Vulkan Y-down!
    m.m[10] = far / (near - far);
    m.m[11] = -1.0f;
    m.m[14] = -(far * near) / (far - near);
    
    return m;
}

static mat4 mat4_look_at(vec3 eye, vec3 center, vec3 up) {
    vec3 f = vec3_normalize(vec3_sub(center, eye));
    vec3 s = vec3_normalize(vec3_cross(f, up));
    vec3 u = vec3_cross(s, f);
    
    mat4 m = mat4_identity();
    m.m[0] = s.x;
    m.m[4] = s.y;
    m.m[8] = s.z;
    m.m[1] = u.x;
    m.m[5] = u.y;
    m.m[9] = u.z;
    m.m[2] = -f.x;
    m.m[6] = -f.y;
    m.m[10] = -f.z;
    m.m[12] = -vec3_dot(s, eye);
    m.m[13] = -vec3_dot(u, eye);
    m.m[14] = vec3_dot(f, eye);
    
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

static void camera_init(Camera *cam) {
    cam->position = (vec3){ 0.0f, 0.0f, 3.0f };
    cam->world_up = (vec3){ 0.0f, 1.0f, 0.0f };
    cam->yaw = -90.0f;
    cam->pitch = 0.0f;
    cam->speed = 2.5f;
    cam->sensitivity = 0.1f;
    
    cam->front.x = cosf(cam->yaw * M_PI / 180.0f) * cosf(cam->pitch * M_PI / 180.0f);
    cam->front.y = sinf(cam->pitch * M_PI / 180.0f);
    cam->front.z = sinf(cam->yaw * M_PI / 180.0f) * cosf(cam->pitch * M_PI / 180.0f);
    cam->front = vec3_normalize(cam->front);
    
    cam->right = vec3_normalize(vec3_cross(cam->front, cam->world_up));
    cam->up = vec3_normalize(vec3_cross(cam->right, cam->front));
}

static void camera_update_vectors(Camera *cam) {
    cam->front.x = cosf(cam->yaw * M_PI / 180.0f) * cosf(cam->pitch * M_PI / 180.0f);
    cam->front.y = sinf(cam->pitch * M_PI / 180.0f);
    cam->front.z = sinf(cam->yaw * M_PI / 180.0f) * cosf(cam->pitch * M_PI / 180.0f);
    cam->front = vec3_normalize(cam->front);
    
    cam->right = vec3_normalize(vec3_cross(cam->front, cam->world_up));
    cam->up = vec3_normalize(vec3_cross(cam->right, cam->front));
}

static void camera_process_mouse(Camera *cam, float xoffset, float yoffset) {
    xoffset *= cam->sensitivity;
    yoffset *= cam->sensitivity;
    
    cam->yaw += xoffset;
    cam->pitch += yoffset;
    
    if (cam->pitch > 89.0f) cam->pitch = 89.0f;
    if (cam->pitch < -89.0f) cam->pitch = -89.0f;
    
    camera_update_vectors(cam);
}

static mat4 camera_get_view_matrix(Camera *cam) {
    vec3 center = vec3_add(cam->position, cam->front);
    return mat4_look_at(cam->position, center, cam->up);
}

// ============================================================================
// Vertex Structure
// ============================================================================

typedef struct {
    vec3 pos;
    vec3 color;
} Vertex;

// ============================================================================
// Uniform Buffer Object
// ============================================================================

typedef struct {
    mat4 model;
    mat4 view;
    mat4 proj;
} UniformBufferObject;

// ============================================================================
// Vulkan Helpers
// ============================================================================

static const char *vk_result_str(VkResult result) {
    switch (result) {
        case VK_SUCCESS: return "VK_SUCCESS";
        case VK_NOT_READY: return "VK_NOT_READY";
        case VK_TIMEOUT: return "VK_TIMEOUT";
        case VK_EVENT_SET: return "VK_EVENT_SET";
        case VK_EVENT_RESET: return "VK_EVENT_RESET";
        case VK_INCOMPLETE: return "VK_INCOMPLETE";
        case VK_ERROR_OUT_OF_HOST_MEMORY: return "VK_ERROR_OUT_OF_HOST_MEMORY";
        case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
        case VK_ERROR_INITIALIZATION_FAILED: return "VK_ERROR_INITIALIZATION_FAILED";
        case VK_ERROR_DEVICE_LOST: return "VK_ERROR_DEVICE_LOST";
        case VK_ERROR_MEMORY_MAP_FAILED: return "VK_ERROR_MEMORY_MAP_FAILED";
        case VK_ERROR_LAYER_NOT_PRESENT: return "VK_ERROR_LAYER_NOT_PRESENT";
        case VK_ERROR_EXTENSION_NOT_PRESENT: return "VK_ERROR_EXTENSION_NOT_PRESENT";
        case VK_ERROR_FEATURE_NOT_PRESENT: return "VK_ERROR_FEATURE_NOT_PRESENT";
        case VK_ERROR_INCOMPATIBLE_DRIVER: return "VK_ERROR_INCOMPATIBLE_DRIVER";
        case VK_ERROR_TOO_MANY_OBJECTS: return "VK_ERROR_TOO_MANY_OBJECTS";
        case VK_ERROR_FORMAT_NOT_SUPPORTED: return "VK_ERROR_FORMAT_NOT_SUPPORTED";
        case VK_ERROR_FRAGMENTED_POOL: return "VK_ERROR_FRAGMENTED_POOL";
        case VK_ERROR_OUT_OF_POOL_MEMORY: return "VK_ERROR_OUT_OF_POOL_MEMORY";
        case VK_ERROR_INVALID_EXTERNAL_HANDLE: return "VK_ERROR_INVALID_EXTERNAL_HANDLE";
        case VK_ERROR_SURFACE_LOST_KHR: return "VK_ERROR_SURFACE_LOST_KHR";
        case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR: return "VK_ERROR_NATIVE_WINDOW_IN_USE_KHR";
        case VK_SUBOPTIMAL_KHR: return "VK_SUBOPTIMAL_KHR";
        case VK_ERROR_OUT_OF_DATE_KHR: return "VK_ERROR_OUT_OF_DATE_KHR";
        case VK_ERROR_INCOMPATIBLE_DISPLAY_KHR: return "VK_ERROR_INCOMPATIBLE_DISPLAY_KHR";
        default: return "VK_UNKNOWN";
    }
}

static void die(const char *message) {
    fprintf(stderr, "%s\n", message);
    exit(EXIT_FAILURE);
}

#define VK_CHECK(call) \
    do { \
        VkResult vk_check_result = (call); \
        if (vk_check_result != VK_SUCCESS) { \
            fprintf(stderr, "%s failed: %s (%d)\n", #call, vk_result_str(vk_check_result), vk_check_result); \
            exit(EXIT_FAILURE); \
        } \
    } while (0)

static uint32_t find_memory_type(VkPhysicalDevice physical_device, uint32_t type_filter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties mem_properties;
    vkGetPhysicalDeviceMemoryProperties(physical_device, &mem_properties);
    
    for (uint32_t i = 0; i < mem_properties.memoryTypeCount; i++) {
        if ((type_filter & (1 << i)) && (mem_properties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    
    die("Failed to find suitable memory type!");
    return 0;
}

static void create_buffer(VkDevice device, VkPhysicalDevice physical_device, VkDeviceSize size, 
                         VkBufferUsageFlags usage, VkMemoryPropertyFlags properties,
                         VkBuffer *buffer, VkDeviceMemory *buffer_memory) {
    VkBufferCreateInfo buffer_info = {0};
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = size;
    buffer_info.usage = usage;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    
    VK_CHECK(vkCreateBuffer(device, &buffer_info, NULL, buffer));
    
    VkMemoryRequirements mem_requirements;
    vkGetBufferMemoryRequirements(device, *buffer, &mem_requirements);
    
    VkMemoryAllocateInfo alloc_info = {0};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize = mem_requirements.size;
    alloc_info.memoryTypeIndex = find_memory_type(physical_device, mem_requirements.memoryTypeBits, properties);
    
    VK_CHECK(vkAllocateMemory(device, &alloc_info, NULL, buffer_memory));
    VK_CHECK(vkBindBufferMemory(device, *buffer, *buffer_memory, 0));
}

static void create_depth_resources(VkDevice device, VkPhysicalDevice physical_device, 
                                   VkExtent2D extent, VkFormat *depth_format,
                                   VkImage *depth_image, VkDeviceMemory *depth_image_memory,
                                   VkImageView *depth_image_view) {
    *depth_format = VK_FORMAT_D32_SFLOAT;
    
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
    
    VkMemoryRequirements mem_requirements;
    vkGetImageMemoryRequirements(device, *depth_image, &mem_requirements);
    
    VkMemoryAllocateInfo alloc_info = {0};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize = mem_requirements.size;
    alloc_info.memoryTypeIndex = find_memory_type(physical_device, mem_requirements.memoryTypeBits,
                                                   VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    
    VK_CHECK(vkAllocateMemory(device, &alloc_info, NULL, depth_image_memory));
    VK_CHECK(vkBindImageMemory(device, *depth_image, *depth_image_memory, 0));
    
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
    
    VK_CHECK(vkCreateImageView(device, &view_info, NULL, depth_image_view));
}

static char *read_file(const char *filename, size_t *size) {
    FILE *file = fopen(filename, "rb");
    if (!file) {
        fprintf(stderr, "Failed to open file: %s\n", filename);
        return NULL;
    }
    
    fseek(file, 0, SEEK_END);
    *size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    char *buffer = malloc(*size);
    if (!buffer) {
        fclose(file);
        return NULL;
    }
    
    fread(buffer, 1, *size, file);
    fclose(file);
    
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
    printf("Voxel Engine - Cube Renderer\n");

    const uint32_t window_width = 800;
    const uint32_t window_height = 600;

    // Create X11 window
    Display *display = XOpenDisplay(NULL);
    if (!display) {
        die("Failed to open X11 display.");
    }

    int screen = DefaultScreen(display);
    Window root = RootWindow(display, screen);
    Window window = XCreateSimpleWindow(display, root, 0, 0, window_width, window_height, 1,
                                        BlackPixel(display, screen), WhitePixel(display, screen));
    XStoreName(display, window, "Voxel Engine - Fly Around Cube");
    
    XSelectInput(display, window, ExposureMask | KeyPressMask | KeyReleaseMask | 
                 PointerMotionMask | StructureNotifyMask | ButtonPressMask);
    
    Atom wm_delete = XInternAtom(display, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(display, window, &wm_delete, 1);
    XMapWindow(display, window);
    
    // Grab pointer for mouse look
    XGrabPointer(display, window, True, PointerMotionMask, GrabModeAsync, GrabModeAsync, window, None, CurrentTime);
    
    // Hide cursor
    Cursor invisible_cursor;
    Pixmap bm_no;
    XColor black, dummy;
    Colormap colormap;
    static char bm_no_data[] = {0, 0, 0, 0, 0, 0, 0, 0};
    
    colormap = DefaultColormap(display, screen);
    XAllocNamedColor(display, colormap, "black", &black, &dummy);
    bm_no = XCreateBitmapFromData(display, window, bm_no_data, 8, 8);
    invisible_cursor = XCreatePixmapCursor(display, bm_no, bm_no, &black, &black, 0, 0);
    
    XDefineCursor(display, window, invisible_cursor);
    XFreeCursor(display, invisible_cursor);
    XFreePixmap(display, bm_no);

    // Create Vulkan instance
    VkApplicationInfo app_info = {0};
    app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName = "Voxel Engine";
    app_info.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
    app_info.pEngineName = "VoxelEngine";
    app_info.engineVersion = VK_MAKE_VERSION(0, 1, 0);
    app_info.apiVersion = VK_API_VERSION_1_1;

    const char *instance_extensions[] = {
        VK_KHR_SURFACE_EXTENSION_NAME,
        VK_KHR_XLIB_SURFACE_EXTENSION_NAME
    };

    VkInstanceCreateInfo create_info = {0};
    create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    create_info.pApplicationInfo = &app_info;
    create_info.enabledExtensionCount = 2;
    create_info.ppEnabledExtensionNames = instance_extensions;

    VkInstance instance = VK_NULL_HANDLE;
    VK_CHECK(vkCreateInstance(&create_info, NULL, &instance));

    // Create surface
    VkXlibSurfaceCreateInfoKHR surface_info = {0};
    surface_info.sType = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR;
    surface_info.dpy = display;
    surface_info.window = window;

    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VK_CHECK(vkCreateXlibSurfaceKHR(instance, &surface_info, NULL, &surface));

    // Pick physical device
    uint32_t physical_device_count = 0;
    VK_CHECK(vkEnumeratePhysicalDevices(instance, &physical_device_count, NULL));
    if (physical_device_count == 0) {
        die("No Vulkan physical devices found.");
    }

    VkPhysicalDevice *physical_devices = malloc(sizeof(VkPhysicalDevice) * physical_device_count);
    VK_CHECK(vkEnumeratePhysicalDevices(instance, &physical_device_count, physical_devices));

    VkPhysicalDevice physical_device = VK_NULL_HANDLE;
    uint32_t graphics_family = UINT32_MAX;
    
    for (uint32_t i = 0; i < physical_device_count; ++i) {
        uint32_t family_count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(physical_devices[i], &family_count, NULL);
        VkQueueFamilyProperties *families = malloc(sizeof(VkQueueFamilyProperties) * family_count);
        vkGetPhysicalDeviceQueueFamilyProperties(physical_devices[i], &family_count, families);

        for (uint32_t j = 0; j < family_count; ++j) {
            VkBool32 present_support = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(physical_devices[i], j, surface, &present_support);
            if ((families[j].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present_support) {
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
        die("No suitable GPU found.");
    }

    // Create logical device
    float queue_priority = 1.0f;
    VkDeviceQueueCreateInfo queue_info = {0};
    queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue_info.queueFamilyIndex = graphics_family;
    queue_info.queueCount = 1;
    queue_info.pQueuePriorities = &queue_priority;

    const char *device_extensions[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };

    VkPhysicalDeviceFeatures device_features = {0};

    VkDeviceCreateInfo device_info = {0};
    device_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    device_info.queueCreateInfoCount = 1;
    device_info.pQueueCreateInfos = &queue_info;
    device_info.enabledExtensionCount = 1;
    device_info.ppEnabledExtensionNames = device_extensions;
    device_info.pEnabledFeatures = &device_features;

    VkDevice device = VK_NULL_HANDLE;
    VK_CHECK(vkCreateDevice(physical_device, &device_info, NULL, &device));

    VkQueue graphics_queue = VK_NULL_HANDLE;
    vkGetDeviceQueue(device, graphics_family, 0, &graphics_queue);

    // Create swapchain
    VkSurfaceCapabilitiesKHR surface_caps = {0};
    VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device, surface, &surface_caps));

    uint32_t format_count = 0;
    VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &format_count, NULL));
    VkSurfaceFormatKHR *formats = malloc(sizeof(VkSurfaceFormatKHR) * format_count);
    VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &format_count, formats));

    VkSurfaceFormatKHR surface_format = formats[0];
    for (uint32_t i = 0; i < format_count; ++i) {
        if (formats[i].format == VK_FORMAT_B8G8R8A8_SRGB) {
            surface_format = formats[i];
            break;
        }
    }
    free(formats);

    uint32_t image_count = surface_caps.minImageCount + 1;
    if (surface_caps.maxImageCount > 0 && image_count > surface_caps.maxImageCount) {
        image_count = surface_caps.maxImageCount;
    }

    VkExtent2D swap_extent = surface_caps.currentExtent;
    if (swap_extent.width == UINT32_MAX) {
        swap_extent.width = window_width;
        swap_extent.height = window_height;
    }

    VkSwapchainCreateInfoKHR swap_info = {0};
    swap_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swap_info.surface = surface;
    swap_info.minImageCount = image_count;
    swap_info.imageFormat = surface_format.format;
    swap_info.imageColorSpace = surface_format.colorSpace;
    swap_info.imageExtent = swap_extent;
    swap_info.imageArrayLayers = 1;
    swap_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    swap_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    swap_info.preTransform = surface_caps.currentTransform;
    swap_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    swap_info.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    swap_info.clipped = VK_TRUE;

    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VK_CHECK(vkCreateSwapchainKHR(device, &swap_info, NULL, &swapchain));

    VK_CHECK(vkGetSwapchainImagesKHR(device, swapchain, &image_count, NULL));
    VkImage *swapchain_images = malloc(sizeof(VkImage) * image_count);
    VK_CHECK(vkGetSwapchainImagesKHR(device, swapchain, &image_count, swapchain_images));

    VkImageView *image_views = malloc(sizeof(VkImageView) * image_count);
    for (uint32_t i = 0; i < image_count; ++i) {
        VkImageViewCreateInfo view_info = {0};
        view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view_info.image = swapchain_images[i];
        view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view_info.format = surface_format.format;
        view_info.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        view_info.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        view_info.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        view_info.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        view_info.subresourceRange.baseMipLevel = 0;
        view_info.subresourceRange.levelCount = 1;
        view_info.subresourceRange.baseArrayLayer = 0;
        view_info.subresourceRange.layerCount = 1;
        VK_CHECK(vkCreateImageView(device, &view_info, NULL, &image_views[i]));
    }

    // Create render pass WITH DEPTH
    VkAttachmentDescription attachments[2] = {0};
    
    // Color attachment
    attachments[0].format = surface_format.format;
    attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    
    // Depth attachment
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
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo render_pass_info = {0};
    render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    render_pass_info.attachmentCount = 2;
    render_pass_info.pAttachments = attachments;
    render_pass_info.subpassCount = 1;
    render_pass_info.pSubpasses = &subpass;
    render_pass_info.dependencyCount = 1;
    render_pass_info.pDependencies = &dependency;

    VkRenderPass render_pass = VK_NULL_HANDLE;
    VK_CHECK(vkCreateRenderPass(device, &render_pass_info, NULL, &render_pass));

    // Load shaders
    size_t vert_size, frag_size;
    char *vert_code = read_file("shaders/vert.spv", &vert_size);
    char *frag_code = read_file("shaders/frag.spv", &frag_size);
    
    if (!vert_code || !frag_code) {
        die("Failed to load shader files. Make sure shaders/vert.spv and shaders/frag.spv exist.");
    }

    VkShaderModule vert_module = create_shader_module(device, vert_code, vert_size);
    VkShaderModule frag_module = create_shader_module(device, frag_code, frag_size);

    free(vert_code);
    free(frag_code);

    // Create depth resources
    VkFormat depth_format;
    VkImage depth_image;
    VkDeviceMemory depth_image_memory;
    VkImageView depth_image_view;
    create_depth_resources(device, physical_device, swap_extent, &depth_format,
                          &depth_image, &depth_image_memory, &depth_image_view);

    // Create descriptor set layout
    VkDescriptorSetLayoutBinding ubo_binding = {0};
    ubo_binding.binding = 0;
    ubo_binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    ubo_binding.descriptorCount = 1;
    ubo_binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutCreateInfo layout_info = {0};
    layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layout_info.bindingCount = 1;
    layout_info.pBindings = &ubo_binding;

    VkDescriptorSetLayout descriptor_set_layout;
    VK_CHECK(vkCreateDescriptorSetLayout(device, &layout_info, NULL, &descriptor_set_layout));

    // Create pipeline layout
    VkPipelineLayoutCreateInfo pipeline_layout_info = {0};
    pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipeline_layout_info.setLayoutCount = 1;
    pipeline_layout_info.pSetLayouts = &descriptor_set_layout;

    VkPipelineLayout pipeline_layout;
    VK_CHECK(vkCreatePipelineLayout(device, &pipeline_layout_info, NULL, &pipeline_layout));

    // Create graphics pipeline
    VkPipelineShaderStageCreateInfo vert_stage = {0};
    vert_stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vert_stage.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vert_stage.module = vert_module;
    vert_stage.pName = "main";

    VkPipelineShaderStageCreateInfo frag_stage = {0};
    frag_stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    frag_stage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    frag_stage.module = frag_module;
    frag_stage.pName = "main";

    VkPipelineShaderStageCreateInfo shader_stages[] = { vert_stage, frag_stage };

    VkVertexInputBindingDescription binding_desc = {0};
    binding_desc.binding = 0;
    binding_desc.stride = sizeof(Vertex);
    binding_desc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attribute_descs[2] = {0};
    attribute_descs[0].binding = 0;
    attribute_descs[0].location = 0;
    attribute_descs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attribute_descs[0].offset = offsetof(Vertex, pos);

    attribute_descs[1].binding = 0;
    attribute_descs[1].location = 1;
    attribute_descs[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    attribute_descs[1].offset = offsetof(Vertex, color);

    VkPipelineVertexInputStateCreateInfo vertex_input = {0};
    vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertex_input.vertexBindingDescriptionCount = 1;
    vertex_input.pVertexBindingDescriptions = &binding_desc;
    vertex_input.vertexAttributeDescriptionCount = 2;
    vertex_input.pVertexAttributeDescriptions = attribute_descs;

    VkPipelineInputAssemblyStateCreateInfo input_assembly = {0};
    input_assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkViewport viewport = {0};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = (float)swap_extent.width;
    viewport.height = (float)swap_extent.height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor = {0};
    scissor.offset.x = 0;
    scissor.offset.y = 0;
    scissor.extent = swap_extent;

    VkPipelineViewportStateCreateInfo viewport_state = {0};
    viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state.viewportCount = 1;
    viewport_state.pViewports = &viewport;
    viewport_state.scissorCount = 1;
    viewport_state.pScissors = &scissor;

    VkPipelineRasterizationStateCreateInfo rasterizer = {0};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

    VkPipelineMultisampleStateCreateInfo multisampling = {0};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // Enable depth testing
    VkPipelineDepthStencilStateCreateInfo depth_stencil = {0};
    depth_stencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth_stencil.depthTestEnable = VK_TRUE;
    depth_stencil.depthWriteEnable = VK_TRUE;
    depth_stencil.depthCompareOp = VK_COMPARE_OP_LESS;
    depth_stencil.depthBoundsTestEnable = VK_FALSE;
    depth_stencil.stencilTestEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState color_blend_attachment = {0};
    color_blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | 
                                           VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo color_blending = {0};
    color_blending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    color_blending.attachmentCount = 1;
    color_blending.pAttachments = &color_blend_attachment;

    VkGraphicsPipelineCreateInfo pipeline_info = {0};
    pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeline_info.stageCount = 2;
    pipeline_info.pStages = shader_stages;
    pipeline_info.pVertexInputState = &vertex_input;
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
    VK_CHECK(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipeline_info, NULL, &graphics_pipeline));

    vkDestroyShaderModule(device, vert_module, NULL);
    vkDestroyShaderModule(device, frag_module, NULL);

    // Create framebuffers WITH DEPTH
    VkFramebuffer *framebuffers = malloc(sizeof(VkFramebuffer) * image_count);
    for (uint32_t i = 0; i < image_count; ++i) {
        VkImageView attachments_fb[] = { image_views[i], depth_image_view };
        VkFramebufferCreateInfo fb_info = {0};
        fb_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fb_info.renderPass = render_pass;
        fb_info.attachmentCount = 2;
        fb_info.pAttachments = attachments_fb;
        fb_info.width = swap_extent.width;
        fb_info.height = swap_extent.height;
        fb_info.layers = 1;
        VK_CHECK(vkCreateFramebuffer(device, &fb_info, NULL, &framebuffers[i]));
    }

    // Create command pool
    VkCommandPoolCreateInfo pool_info = {0};
    pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_info.queueFamilyIndex = graphics_family;
    pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    VkCommandPool command_pool = VK_NULL_HANDLE;
    VK_CHECK(vkCreateCommandPool(device, &pool_info, NULL, &command_pool));

    // Create vertex buffer (cube)
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
        0, 1, 2, 2, 3, 0,       // Front
        6, 5, 4, 4, 7, 6,       // Back
        8, 11, 10, 10, 9, 8,    // Top
        12, 13, 14, 14, 15, 12, // Bottom
        16, 17, 18, 18, 19, 16, // Right
        22, 21, 20, 20, 23, 22  // Left
    };

    VkDeviceSize vertex_buffer_size = sizeof(vertices);
    VkDeviceSize index_buffer_size = sizeof(indices);

    VkBuffer vertex_buffer, index_buffer;
    VkDeviceMemory vertex_buffer_memory, index_buffer_memory;

    create_buffer(device, physical_device, vertex_buffer_size,
                 VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 &vertex_buffer, &vertex_buffer_memory);

    void *data;
    vkMapMemory(device, vertex_buffer_memory, 0, vertex_buffer_size, 0, &data);
    memcpy(data, vertices, vertex_buffer_size);
    vkUnmapMemory(device, vertex_buffer_memory);

    create_buffer(device, physical_device, index_buffer_size,
                 VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 &index_buffer, &index_buffer_memory);

    vkMapMemory(device, index_buffer_memory, 0, index_buffer_size, 0, &data);
    memcpy(data, indices, index_buffer_size);
    vkUnmapMemory(device, index_buffer_memory);

    // Create uniform buffers
    VkDeviceSize ubo_size = sizeof(UniformBufferObject);
    VkBuffer *uniform_buffers = malloc(sizeof(VkBuffer) * image_count);
    VkDeviceMemory *uniform_buffers_memory = malloc(sizeof(VkDeviceMemory) * image_count);

    for (uint32_t i = 0; i < image_count; i++) {
        create_buffer(device, physical_device, ubo_size,
                     VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     &uniform_buffers[i], &uniform_buffers_memory[i]);
    }

    // Create descriptor pool
    VkDescriptorPoolSize pool_size = {0};
    pool_size.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    pool_size.descriptorCount = image_count;

    VkDescriptorPoolCreateInfo pool_create_info = {0};
    pool_create_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_create_info.poolSizeCount = 1;
    pool_create_info.pPoolSizes = &pool_size;
    pool_create_info.maxSets = image_count;

    VkDescriptorPool descriptor_pool;
    VK_CHECK(vkCreateDescriptorPool(device, &pool_create_info, NULL, &descriptor_pool));

    // Create descriptor sets
    VkDescriptorSetLayout *layouts = malloc(sizeof(VkDescriptorSetLayout) * image_count);
    for (uint32_t i = 0; i < image_count; i++) {
        layouts[i] = descriptor_set_layout;
    }

    VkDescriptorSetAllocateInfo alloc_info = {0};
    alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc_info.descriptorPool = descriptor_pool;
    alloc_info.descriptorSetCount = image_count;
    alloc_info.pSetLayouts = layouts;

    VkDescriptorSet *descriptor_sets = malloc(sizeof(VkDescriptorSet) * image_count);
    VK_CHECK(vkAllocateDescriptorSets(device, &alloc_info, descriptor_sets));

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
        descriptor_write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptor_write.descriptorCount = 1;
        descriptor_write.pBufferInfo = &buffer_info;

        vkUpdateDescriptorSets(device, 1, &descriptor_write, 0, NULL);
    }

    // Create command buffers
    VkCommandBuffer *command_buffers = malloc(sizeof(VkCommandBuffer) * image_count);
    VkCommandBufferAllocateInfo cmd_alloc_info = {0};
    cmd_alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmd_alloc_info.commandPool = command_pool;
    cmd_alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmd_alloc_info.commandBufferCount = image_count;
    VK_CHECK(vkAllocateCommandBuffers(device, &cmd_alloc_info, command_buffers));

    // Create sync objects
    VkSemaphoreCreateInfo semaphore_info = {0};
    semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fence_info = {0};
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    VkSemaphore image_available, render_finished;
    VkFence in_flight;

    VK_CHECK(vkCreateSemaphore(device, &semaphore_info, NULL, &image_available));
    VK_CHECK(vkCreateSemaphore(device, &semaphore_info, NULL, &render_finished));
    VK_CHECK(vkCreateFence(device, &fence_info, NULL, &in_flight));

    // Initialize camera
    Camera camera;
    camera_init(&camera);

    // Input state
    bool keys[256] = {0};
    bool first_mouse = true;
    float last_x = window_width / 2.0f;
    float last_y = window_height / 2.0f;

    // Timing
    struct timespec last_time, current_time;
    clock_gettime(CLOCK_MONOTONIC, &last_time);

    // Main loop
    bool running = true;
    while (running) {
        // Handle events
        while (XPending(display)) {
            XEvent event;
            XNextEvent(display, &event);
            
            if (event.type == ClientMessage) {
                if ((Atom)event.xclient.data.l[0] == wm_delete) {
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
                float xpos = event.xmotion.x;
                float ypos = event.xmotion.y;

                if (first_mouse) {
                    last_x = xpos;
                    last_y = ypos;
                    first_mouse = false;
                }

                float xoffset = xpos - last_x;
                float yoffset = last_y - ypos;

                last_x = xpos;
                last_y = ypos;

                camera_process_mouse(&camera, xoffset, yoffset);
                
                // Warp pointer to center to prevent hitting window edges
                XWarpPointer(display, None, window, 0, 0, 0, 0, window_width / 2, window_height / 2);
                last_x = window_width / 2;
                last_y = window_height / 2;
            }
        }

        // Calculate delta time
        clock_gettime(CLOCK_MONOTONIC, &current_time);
        float delta_time = (current_time.tv_sec - last_time.tv_sec) + 
                          (current_time.tv_nsec - last_time.tv_nsec) / 1000000000.0f;
        last_time = current_time;

        // Update camera position
        float velocity = camera.speed * delta_time;
        
        if (keys['w'] || keys['W']) {
            camera.position = vec3_add(camera.position, vec3_scale(camera.front, velocity));
        }
        if (keys['s'] || keys['S']) {
            camera.position = vec3_sub(camera.position, vec3_scale(camera.front, velocity));
        }
        if (keys['a'] || keys['A']) {
            camera.position = vec3_sub(camera.position, vec3_scale(camera.right, velocity));
        }
        if (keys['d'] || keys['D']) {
            camera.position = vec3_add(camera.position, vec3_scale(camera.right, velocity));
        }
        if (keys['e'] || keys['E']) {
            camera.position = vec3_add(camera.position, vec3_scale(camera.world_up, velocity));
        }
        if (keys['q'] || keys['Q']) {
            camera.position = vec3_sub(camera.position, vec3_scale(camera.world_up, velocity));
        }
        // Keep Space and C for now
        if (keys[' ']) {
            camera.position = vec3_add(camera.position, vec3_scale(camera.world_up, velocity));
        }
        if (keys['c'] || keys['C']) {
            camera.position = vec3_sub(camera.position, vec3_scale(camera.world_up, velocity));
        }

        // Wait for previous frame
        VK_CHECK(vkWaitForFences(device, 1, &in_flight, VK_TRUE, UINT64_MAX));
        VK_CHECK(vkResetFences(device, 1, &in_flight));

        // Acquire image
        uint32_t image_index;
        VkResult result = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, image_available, VK_NULL_HANDLE, &image_index);
        if (result == VK_ERROR_OUT_OF_DATE_KHR) {
            continue;
        } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
            die("Failed to acquire swapchain image");
        }

        // Update uniform buffer
        UniformBufferObject ubo;
        ubo.model = mat4_identity();
        ubo.view = camera_get_view_matrix(&camera);
        ubo.proj = mat4_perspective(45.0f * M_PI / 180.0f, 
                                    (float)swap_extent.width / (float)swap_extent.height, 
                                    0.1f, 100.0f);

        vkMapMemory(device, uniform_buffers_memory[image_index], 0, sizeof(ubo), 0, &data);
        memcpy(data, &ubo, sizeof(ubo));
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

        VkRenderPassBeginInfo render_pass_begin = {0};
        render_pass_begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        render_pass_begin.renderPass = render_pass;
        render_pass_begin.framebuffer = framebuffers[image_index];
        render_pass_begin.renderArea.offset.x = 0;
        render_pass_begin.renderArea.offset.y = 0;
        render_pass_begin.renderArea.extent = swap_extent;
        render_pass_begin.clearValueCount = 2;
        render_pass_begin.pClearValues = clear_values;

        vkCmdBeginRenderPass(command_buffers[image_index], &render_pass_begin, VK_SUBPASS_CONTENTS_INLINE);

        vkCmdBindPipeline(command_buffers[image_index], VK_PIPELINE_BIND_POINT_GRAPHICS, graphics_pipeline);

        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(command_buffers[image_index], 0, 1, &vertex_buffer, offsets);
        vkCmdBindIndexBuffer(command_buffers[image_index], index_buffer, 0, VK_INDEX_TYPE_UINT16);

        vkCmdBindDescriptorSets(command_buffers[image_index], VK_PIPELINE_BIND_POINT_GRAPHICS, 
                               pipeline_layout, 0, 1, &descriptor_sets[image_index], 0, NULL);

        vkCmdDrawIndexed(command_buffers[image_index], sizeof(indices) / sizeof(indices[0]), 1, 0, 0, 0);

        vkCmdEndRenderPass(command_buffers[image_index]);

        VK_CHECK(vkEndCommandBuffer(command_buffers[image_index]));

        // Submit
        VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo submit_info = {0};
        submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit_info.waitSemaphoreCount = 1;
        submit_info.pWaitSemaphores = &image_available;
        submit_info.pWaitDstStageMask = &wait_stage;
        submit_info.commandBufferCount = 1;
        submit_info.pCommandBuffers = &command_buffers[image_index];
        submit_info.signalSemaphoreCount = 1;
        submit_info.pSignalSemaphores = &render_finished;

        VK_CHECK(vkQueueSubmit(graphics_queue, 1, &submit_info, in_flight));

        // Present
        VkPresentInfoKHR present_info = {0};
        present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        present_info.waitSemaphoreCount = 1;
        present_info.pWaitSemaphores = &render_finished;
        present_info.swapchainCount = 1;
        present_info.pSwapchains = &swapchain;
        present_info.pImageIndices = &image_index;

        vkQueuePresentKHR(graphics_queue, &present_info);
    }

    // Cleanup
    vkDeviceWaitIdle(device);

    // Clean up depth resources
    vkDestroyImageView(device, depth_image_view, NULL);
    vkDestroyImage(device, depth_image, NULL);
    vkFreeMemory(device, depth_image_memory, NULL);

    vkDestroyFence(device, in_flight, NULL);
    vkDestroySemaphore(device, render_finished, NULL);
    vkDestroySemaphore(device, image_available, NULL);

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

    for (uint32_t i = 0; i < image_count; ++i) {
        vkDestroyFramebuffer(device, framebuffers[i], NULL);
    }
    free(framebuffers);

    vkDestroyPipeline(device, graphics_pipeline, NULL);
    vkDestroyPipelineLayout(device, pipeline_layout, NULL);
    vkDestroyDescriptorSetLayout(device, descriptor_set_layout, NULL);
    vkDestroyRenderPass(device, render_pass, NULL);

    for (uint32_t i = 0; i < image_count; ++i) {
        vkDestroyImageView(device, image_views[i], NULL);
    }
    free(image_views);
    free(swapchain_images);

    vkDestroySwapchainKHR(device, swapchain, NULL);
    vkDestroyDevice(device, NULL);
    vkDestroySurfaceKHR(instance, surface, NULL);
    vkDestroyInstance(instance, NULL);

    XDestroyWindow(display, window);
    XCloseDisplay(display);

    printf("Shutdown complete.\n");
    return EXIT_SUCCESS;
}