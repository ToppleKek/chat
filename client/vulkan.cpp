/*
    Vulkan render backend module implementation.

    Author: Braeden Hong
      Date: October 30, 2023
*/

#include "vulkan.hpp"

#ifdef ICHIGO_VULKAN_DEBUG
#define VERIFICATION_LAYER_COUNT 1
#else
#define VERIFICATION_LAYER_COUNT 0
#endif

void IchigoVulkan::Context::init(const char **extensions, u32 num_extensions) {
    // Instance creation
    static const char *layers[] = {"VK_LAYER_KHRONOS_validation"};
    VkApplicationInfo app_info = {};
    app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName = "Chat Client";
    app_info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    app_info.pEngineName = "Ichigo";
    app_info.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    app_info.apiVersion = VK_API_VERSION_1_0;

    VkInstanceCreateInfo create_info = {};
    create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    create_info.pApplicationInfo = &app_info;
    create_info.enabledExtensionCount = num_extensions;
    create_info.ppEnabledExtensionNames = extensions;
    create_info.enabledLayerCount = VERIFICATION_LAYER_COUNT;
    create_info.ppEnabledLayerNames = layers;

    auto ret = vkCreateInstance(&create_info, nullptr, &vk_instance);
    assert(ret == VK_SUCCESS);

    // Physical GPU selection
    u32 gpu_count = 0;
    vkEnumeratePhysicalDevices(vk_instance, &gpu_count, nullptr);
    assert(gpu_count != 0);
    VkPhysicalDevice *gpus = reinterpret_cast<VkPhysicalDevice *>(platform_alloca(gpu_count * sizeof(VkPhysicalDevice)));
    vkEnumeratePhysicalDevices(vk_instance, &gpu_count, gpus);

    selected_gpu = VK_NULL_HANDLE;

    for (u32 i = 0; i < gpu_count; ++i) {
        const VkPhysicalDevice gpu = gpus[i];
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(gpu, &props);
        if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
            selected_gpu = gpu;
            break;
        }
    }

    if (selected_gpu == VK_NULL_HANDLE)
        selected_gpu = gpus[0];

    // Queue creation
    queue_family_index = 0;
    u32 queue_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(selected_gpu, &queue_count, nullptr);

    VkQueueFamilyProperties *queue_family_properties = reinterpret_cast<VkQueueFamilyProperties *>(platform_alloca(queue_count * sizeof(VkQueueFamilyProperties)));
    vkGetPhysicalDeviceQueueFamilyProperties(selected_gpu, &queue_count,
                                             queue_family_properties);

    for (u32 i = 0; i < queue_count; ++i) {
        if (queue_family_properties[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            queue_family_index = i;
            break;
        }
    }

    static const f32 queue_priority[] = {1.0f};
    VkDeviceQueueCreateInfo queue_create_info = {};
    queue_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue_create_info.queueFamilyIndex = queue_family_index;
    queue_create_info.queueCount = 1;
    queue_create_info.pQueuePriorities = queue_priority;

    static const char *device_extensions[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
    VkDeviceCreateInfo device_create_info = {};
    device_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    device_create_info.queueCreateInfoCount = 1;
    device_create_info.pQueueCreateInfos = &queue_create_info;
    device_create_info.enabledExtensionCount = 1;
    device_create_info.ppEnabledExtensionNames = device_extensions;

    assert(vkCreateDevice(selected_gpu, &device_create_info, nullptr,
                          &logical_device) == VK_SUCCESS);
    vkGetDeviceQueue(logical_device, queue_family_index, 0, &queue);

    // Descriptor pool creation
    static const VkDescriptorPoolSize pool_sizes[] = {{VK_DESCRIPTOR_TYPE_SAMPLER, 1000},
                                                      {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000},
                                                      {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000},
                                                      {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000},
                                                      {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000},
                                                      {VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000},
                                                      {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000},
                                                      {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000},
                                                      {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000},
                                                      {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000},
                                                      {VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000}};
    VkDescriptorPoolCreateInfo pool_info = {};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pool_info.maxSets = 1000 * (sizeof(pool_sizes) / sizeof(VkDescriptorPoolSize));
    pool_info.poolSizeCount = sizeof(pool_sizes) / sizeof(VkDescriptorPoolSize);
    pool_info.pPoolSizes = pool_sizes;

    assert(vkCreateDescriptorPool(logical_device, &pool_info, nullptr,
                                  &descriptor_pool) == VK_SUCCESS);
}

//
void IchigoVulkan::Context::create_buffer(u64 size, VkBufferUsageFlags usage_flags, VkMemoryPropertyFlags property_flags, VkBuffer *buffer, VkDeviceMemory *buffer_memory) {
        VkBufferCreateInfo info{};
        info.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        info.size        = size;
        info.usage       = usage_flags;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VK_ASSERT_OK(vkCreateBuffer(logical_device, &info, VK_NULL_HANDLE, buffer));

        VkMemoryRequirements requirements;
        vkGetBufferMemoryRequirements(logical_device, *buffer, &requirements);

        VkPhysicalDeviceMemoryProperties properties;
        vkGetPhysicalDeviceMemoryProperties(selected_gpu, &properties);

        u32 i = 0;
        for (; i < properties.memoryTypeCount; ++i) {
            if (requirements.memoryTypeBits & (1 << i) && properties.memoryTypes[i].propertyFlags & property_flags)
                goto memory_found;
        }

        assert(false && "Failed to find appropriate memory for allocation");

memory_found:
        VkMemoryAllocateInfo alloc_info{};
        alloc_info.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        alloc_info.allocationSize  = requirements.size;
        alloc_info.memoryTypeIndex = i;
        VK_ASSERT_OK(vkAllocateMemory(logical_device, &alloc_info, VK_NULL_HANDLE, buffer_memory));

        vkBindBufferMemory(logical_device, *buffer, *buffer_memory, 0);
}


void IchigoVulkan::Context::create_swapchain_and_images(u32 window_width, u32 window_height) {
    VkSurfaceCapabilitiesKHR surface_capabilities;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(selected_gpu, surface, &surface_capabilities);

    VkSwapchainCreateInfoKHR swapchain_create_info{};
    swapchain_create_info.sType              = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swapchain_create_info.surface            = surface;
    swapchain_create_info.imageFormat        = surface_format.format;
    swapchain_create_info.imageColorSpace    = surface_format.colorSpace;
    swapchain_create_info.imageArrayLayers   = 1;
    swapchain_create_info.imageUsage         = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    swapchain_create_info.imageSharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    swapchain_create_info.preTransform       = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    swapchain_create_info.compositeAlpha     = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    swapchain_create_info.presentMode        = present_mode;
    swapchain_create_info.clipped            = VK_TRUE;
    swapchain_create_info.oldSwapchain       = VK_NULL_HANDLE;

    if (surface_capabilities.currentExtent.width != UINT32_MAX) {
        std::printf("Using extent from surface capabilities\n");
        swapchain_create_info.imageExtent = surface_capabilities.currentExtent;
    } else {
        std::printf("Choosing best extent from framebuffer size\n");
        swapchain_create_info.imageExtent.width = Util::clamp(window_width, surface_capabilities.minImageExtent.width, surface_capabilities.maxImageExtent.width);
        swapchain_create_info.imageExtent.height = Util::clamp(window_height, surface_capabilities.minImageExtent.height, surface_capabilities.maxImageExtent.height);
    }

    extent = swapchain_create_info.imageExtent;

    switch (present_mode) {
        case VK_PRESENT_MODE_MAILBOX_KHR:      swapchain_create_info.minImageCount = 3; break;
        case VK_PRESENT_MODE_FIFO_RELAXED_KHR:
        case VK_PRESENT_MODE_FIFO_KHR:         swapchain_create_info.minImageCount = 2; break;
        case VK_PRESENT_MODE_IMMEDIATE_KHR:    swapchain_create_info.minImageCount = 1; break;
        default:                               swapchain_create_info.minImageCount = 1;
    }

    VK_ASSERT_OK(vkCreateSwapchainKHR(logical_device, &swapchain_create_info, VK_NULL_HANDLE, &swapchain));

    // ** Swapchain image setup **
    u32 image_count = 0;
    VK_ASSERT_OK(vkGetSwapchainImagesKHR(logical_device, swapchain, &image_count, nullptr));
    swapchain_images           = new VkImage[image_count];
    swapchain_image_count      = image_count;
    swapchain_image_views      = new VkImageView[image_count];
    swapchain_image_view_count = image_count;
    VK_ASSERT_OK(vkGetSwapchainImagesKHR(logical_device, swapchain, &image_count, swapchain_images));

    for (u32 i = 0; i < image_count; ++i) {
        VkImageViewCreateInfo iv_create_info{};
        iv_create_info.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        iv_create_info.image                           = swapchain_images[i];
        iv_create_info.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
        iv_create_info.format                          = surface_format.format;
        iv_create_info.components.r                    = VK_COMPONENT_SWIZZLE_IDENTITY;
        iv_create_info.components.g                    = VK_COMPONENT_SWIZZLE_IDENTITY;
        iv_create_info.components.b                    = VK_COMPONENT_SWIZZLE_IDENTITY;
        iv_create_info.components.a                    = VK_COMPONENT_SWIZZLE_IDENTITY;
        iv_create_info.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        iv_create_info.subresourceRange.baseMipLevel   = 0;
        iv_create_info.subresourceRange.levelCount     = 1;
        iv_create_info.subresourceRange.baseArrayLayer = 0;
        iv_create_info.subresourceRange.layerCount     = 1;

        VK_ASSERT_OK(vkCreateImageView(logical_device, &iv_create_info, VK_NULL_HANDLE, &swapchain_image_views[i]));
    }
}

void IchigoVulkan::Context::create_framebuffers() {
    frame_buffers.resize(swapchain_image_view_count);
    for (u32 i = 0; i < swapchain_image_view_count; ++i) {
        VkFramebufferCreateInfo framebuffer_create_info = {};
        framebuffer_create_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebuffer_create_info.renderPass = render_pass;
        framebuffer_create_info.attachmentCount = 1;
        framebuffer_create_info.pAttachments = &swapchain_image_views[i];
        framebuffer_create_info.width = extent.width;
        framebuffer_create_info.height = extent.height;
        framebuffer_create_info.layers = 1;

        VK_ASSERT_OK(vkCreateFramebuffer(logical_device, &framebuffer_create_info, nullptr, &frame_buffers.at(i)));
    }
}

void IchigoVulkan::Context::rebuild_swapchain(u32 window_width, u32 window_height) {
    u64 start = __rdtsc();
    vkDeviceWaitIdle(logical_device);
    std::printf("Waiting for device idle took %llu\n", __rdtsc() - start);
    start = __rdtsc();
    for (u32 i = 0; i < frame_buffers.size(); ++i)
        vkDestroyFramebuffer(logical_device, frame_buffers.at(i), VK_NULL_HANDLE);
    std::printf("Destroying framebuffer took %llu\n", __rdtsc() - start);

    start = __rdtsc();
    for (u32 i = 0; i < swapchain_image_view_count; ++i)
        vkDestroyImageView(logical_device, swapchain_image_views[i], VK_NULL_HANDLE);

    std::printf("Destroying imageview took %llu\n", __rdtsc() - start);
    start = __rdtsc();
    vkDestroySwapchainKHR(logical_device, swapchain, VK_NULL_HANDLE);
    std::printf("Destroying swapchain took %llu\n", __rdtsc() - start);

    delete[] swapchain_images;
    delete[] swapchain_image_views;

    start = __rdtsc();
    create_swapchain_and_images(window_width, window_height);
    create_framebuffers();
    std::printf("Recreation took %llu\n", __rdtsc() - start);
}
