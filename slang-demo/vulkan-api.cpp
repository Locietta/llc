#include "vulkan-api.h"

#include "slang.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

#if SLANG_WINDOWS_FAMILY
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#if _DEBUG
#define ENABLE_VALIDATION_LAYER 1
#endif

VKAPI_ATTR VkBool32 VKAPI_CALL debug_message_callback(
    VkDebugReportFlagsEXT /*flags*/,
    VkDebugReportObjectTypeEXT /*objType*/,
    uint64_t /*srcObject*/,
    size_t /*location*/,
    int32_t /*msgCode*/,
    const char *p_layer_prefix,
    const char *p_msg,
    void * /*pUserData*/
) {
    printf("[%s]: %s\n", p_layer_prefix, p_msg);
    return 1;
}

int initialize_vulkan_device(VulkanAPI &api) {
    // Load vulkan library.
    const char *dynamic_library_name = "Unknown";

#if SLANG_WINDOWS_FAMILY
    dynamic_library_name = "vulkan-1.dll";
    HMODULE module = ::LoadLibraryA(dynamic_library_name);
    api.vulkan_library_handle = (void *) module;
#define VK_API_GET_GLOBAL_PROC(x) api.x = (PFN_##x) GetProcAddress(module, #x);
#elif SLANG_APPLE_FAMILY
    dynamicLibraryName = "libvulkan.dylib";
    api.vulkanLibraryHandle = dlopen(dynamicLibraryName, RTLD_NOW);
#define VK_API_GET_GLOBAL_PROC(x) api.x = (PFN_##x) dlsym(api.vulkanLibraryHandle, #x);
#else
    dynamicLibraryName = "libvulkan.so.1";
    api.vulkanLibraryHandle = dlopen(dynamicLibraryName, RTLD_NOW);
#define VK_API_GET_GLOBAL_PROC(x) api.x = (PFN_##x) dlsym(api.vulkanLibraryHandle, #x);
#endif

    // Initialize all the global functions.
    VK_API_ALL_GLOBAL_PROCS(VK_API_GET_GLOBAL_PROC)
    if (!api.vkCreateInstance)
        return -1;

    // Enable validation layer if available.
    std::vector<const char *> layers;
#ifdef ENABLE_VALIDATION_LAYER
    uint32_t propertyCount;
    if (api.vkEnumerateInstanceLayerProperties(&propertyCount, nullptr) != 0)
        return -1;
    std::vector<VkLayerProperties> properties(propertyCount);
    if (api.vkEnumerateInstanceLayerProperties(&propertyCount, properties.data()) != 0)
        return -1;
    for (const auto &p : properties) {
        if (strcmp(p.layerName, "VK_LAYER_KHRONOS_validation") == 0) {
            layers.push_back("VK_LAYER_KHRONOS_validation");
        }
    }
#endif

    // Create Vulkan Instance.
    VkApplicationInfo application_info = {VK_STRUCTURE_TYPE_APPLICATION_INFO};
    application_info.pApplicationName = "slang-hello-world";
    application_info.pEngineName = "slang-hello-world";
    application_info.apiVersion = VK_API_VERSION_1_2;
    application_info.engineVersion = 1;
    application_info.applicationVersion = 1;
    const char *instance_extensions[] = {
#if SLANG_APPLE_FAMILY
        VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME,
#endif
        VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME,
        VK_EXT_DEBUG_REPORT_EXTENSION_NAME,
    };
    VkInstanceCreateInfo instance_create_info = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
#if SLANG_APPLE_FAMILY
    instanceCreateInfo.flags = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
#endif
    instance_create_info.pApplicationInfo = &application_info;
    instance_create_info.enabledExtensionCount = SLANG_COUNT_OF(instance_extensions);
    instance_create_info.ppEnabledExtensionNames = &instance_extensions[0];
    if (layers.size()) {
        instance_create_info.ppEnabledLayerNames = &layers[0];
        instance_create_info.enabledLayerCount = (uint32_t) layers.size();
    }
    if (api.vkCreateInstance(&instance_create_info, nullptr, &api.instance) != 0)
        return -1;

    // Load instance functions.
    api.init_instance_procs();

    // Create debug report callback.
    if (api.vkCreateDebugReportCallbackEXT) {
        VkDebugReportFlagsEXT debug_flags =
            VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT;

        VkDebugReportCallbackCreateInfoEXT debug_create_info = {
            VK_STRUCTURE_TYPE_DEBUG_REPORT_CREATE_INFO_EXT};
        debug_create_info.pfnCallback = &debug_message_callback;
        debug_create_info.pUserData = nullptr;
        debug_create_info.flags = debug_flags;

        RETURN_ON_FAIL(api.vkCreateDebugReportCallbackEXT(
            api.instance,
            &debug_create_info,
            nullptr,
            &api.debug_report_callback));
    }

    // Enumerate physical devices.
    uint32_t num_physical_devices = 0;
    RETURN_ON_FAIL(api.vkEnumeratePhysicalDevices(api.instance, &num_physical_devices, nullptr));
    std::vector<VkPhysicalDevice> physical_devices;
    physical_devices.resize(num_physical_devices);
    RETURN_ON_FAIL(
        api.vkEnumeratePhysicalDevices(api.instance, &num_physical_devices, &physical_devices[0]));

    // We will use device 0.
    api.init_physical_device(physical_devices[0]);

    VkDeviceCreateInfo device_create_info = {VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    device_create_info.queueCreateInfoCount = 1;
    device_create_info.pEnabledFeatures = &api.device_features;

    // Find proper queue family index.
    uint32_t num_queue_families = 0;
    api.vkGetPhysicalDeviceQueueFamilyProperties(api.physical_device, &num_queue_families, nullptr);

    std::vector<VkQueueFamilyProperties> queue_families;
    queue_families.resize(num_queue_families);
    api.vkGetPhysicalDeviceQueueFamilyProperties(
        api.physical_device,
        &num_queue_families,
        &queue_families[0]);

    // Find a queue that can service our needs.
    auto required_queue_flags = VK_QUEUE_COMPUTE_BIT;
    for (int i = 0; i < int(num_queue_families); ++i) {
        if ((queue_families[i].queueFlags & required_queue_flags) == required_queue_flags) {
            api.queue_family_index = i;
            break;
        }
    }
    if (api.queue_family_index == -1)
        return -1;

#if SLANG_APPLE_FAMILY
    const char *deviceExtensions[] = {
        "VK_KHR_portability_subset",
    };
#endif

    VkDeviceQueueCreateInfo queue_create_info = {VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    float queue_priority = 0.0f;
    queue_create_info.queueFamilyIndex = api.queue_family_index;
    queue_create_info.queueCount = 1;
    queue_create_info.pQueuePriorities = &queue_priority;
    device_create_info.pQueueCreateInfos = &queue_create_info;
#if SLANG_APPLE_FAMILY
    deviceCreateInfo.enabledExtensionCount = SLANG_COUNT_OF(deviceExtensions);
    deviceCreateInfo.ppEnabledExtensionNames = &deviceExtensions[0];
#endif
    RETURN_ON_FAIL(api.vkCreateDevice(api.physical_device, &device_create_info, nullptr, &api.device));

    // Load device functions.
    api.init_device_procs();

    return 0;
}

int VulkanAPI::init_instance_procs() {
    assert(instance && vkGetInstanceProcAddr != nullptr);

#define VK_API_GET_INSTANCE_PROC(x) x = (PFN_##x) vkGetInstanceProcAddr(instance, #x);

    VK_API_ALL_INSTANCE_PROCS(VK_API_GET_INSTANCE_PROC)
    // Get optional
    VK_API_INSTANCE_PROCS_OPT(VK_API_GET_INSTANCE_PROC)

#undef VK_API_GET_INSTANCE_PROC

    return 0;
}

int VulkanAPI::init_physical_device(VkPhysicalDevice in_physical_device) {
    assert(physical_device == VK_NULL_HANDLE);
    physical_device = in_physical_device;

    vkGetPhysicalDeviceProperties(physical_device, &device_properties);
    vkGetPhysicalDeviceFeatures(physical_device, &device_features);
    vkGetPhysicalDeviceMemoryProperties(physical_device, &device_memory_properties);

    return 0;
}

int VulkanAPI::init_device_procs() {
    assert(instance && device && vkGetDeviceProcAddr != nullptr);

#define VK_API_GET_DEVICE_PROC(x) x = (PFN_##x) vkGetDeviceProcAddr(device, #x);
    VK_API_DEVICE_PROCS(VK_API_GET_DEVICE_PROC)
#undef VK_API_GET_DEVICE_PROC

    return 0;
}

int VulkanAPI::find_memory_type_index(uint32_t type_bits, VkMemoryPropertyFlags properties) {
    assert(type_bits);

    const int num_memory_types = int(device_memory_properties.memoryTypeCount);

    // bit holds current test bit against typeBits. Ie bit == 1 << typeBits

    uint32_t bit = 1;
    for (int i = 0; i < num_memory_types; ++i, bit += bit) {
        auto const &memory_type = device_memory_properties.memoryTypes[i];
        if ((type_bits & bit) && (memory_type.propertyFlags & properties) == properties) {
            return i;
        }
    }

    // assert(!"failed to find a usable memory type");
    return -1;
}

VulkanAPI::~VulkanAPI() {
    if (vkDestroyDevice) {
        vkDestroyDevice(device, nullptr);
    }
    if (vkDestroyDebugReportCallbackEXT) {
        vkDestroyDebugReportCallbackEXT(instance, debug_report_callback, nullptr);
    }
    if (vkDestroyInstance) {
        vkDestroyInstance(instance, nullptr);
    }
}