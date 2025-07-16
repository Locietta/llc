#pragma once

#define VK_NO_PROTOTYPES
#include "vulkan/vulkan.h"

// This file provides basic loading and helper functions for using
// the Vulkan API.

// The Vulkan function pointers we will use in this example.
// clang-format off
#define VK_API_GLOBAL_PROCS(x) \
    x(vkGetInstanceProcAddr) \
    x(vkCreateInstance) \
    x(vkEnumerateInstanceLayerProperties) \
    x(vkDestroyInstance) \
    /* */

#define VK_API_INSTANCE_PROCS_OPT(x) \
    x(vkGetPhysicalDeviceFeatures2) \
    x(vkGetPhysicalDeviceProperties2) \
    x(vkCreateDebugReportCallbackEXT) \
    x(vkDestroyDebugReportCallbackEXT) \
    x(vkDebugReportMessageEXT) \
    /* */

#define VK_API_INSTANCE_PROCS(x) \
    x(vkCreateDevice) \
    x(vkDestroyDevice) \
    x(vkEnumeratePhysicalDevices) \
    x(vkGetPhysicalDeviceProperties) \
    x(vkGetPhysicalDeviceFeatures) \
    x(vkGetPhysicalDeviceMemoryProperties) \
    x(vkGetPhysicalDeviceQueueFamilyProperties) \
    x(vkGetPhysicalDeviceFormatProperties) \
    x(vkGetDeviceProcAddr) \
    /* */

#define VK_API_DEVICE_PROCS(x) \
    x(vkCreateDescriptorPool) \
    x(vkDestroyDescriptorPool) \
    x(vkGetDeviceQueue) \
    x(vkQueueSubmit) \
    x(vkQueueWaitIdle) \
    x(vkCreateBuffer) \
    x(vkAllocateMemory) \
    x(vkMapMemory) \
    x(vkUnmapMemory) \
    x(vkCmdCopyBuffer) \
    x(vkDestroyBuffer) \
    x(vkFreeMemory) \
    x(vkCreateDescriptorSetLayout) \
    x(vkDestroyDescriptorSetLayout) \
    x(vkAllocateDescriptorSets) \
    x(vkUpdateDescriptorSets) \
    x(vkCreatePipelineLayout) \
    x(vkDestroyPipelineLayout) \
    x(vkCreateComputePipelines) \
    x(vkDestroyPipeline) \
    x(vkCreateShaderModule) \
    x(vkDestroyShaderModule) \
    x(vkCreateCommandPool) \
    x(vkDestroyCommandPool) \
    \
    x(vkGetBufferMemoryRequirements) \
    \
    x(vkCmdBindPipeline) \
    x(vkCmdBindDescriptorSets) \
    x(vkCmdDispatch) \
    \
    x(vkFreeCommandBuffers) \
    x(vkAllocateCommandBuffers) \
    x(vkBeginCommandBuffer) \
    x(vkEndCommandBuffer) \
    x(vkBindBufferMemory) \
    /* */

#define VK_API_ALL_GLOBAL_PROCS(x) \
    VK_API_GLOBAL_PROCS(x)

#define VK_API_ALL_INSTANCE_PROCS(x) \
    VK_API_INSTANCE_PROCS(x) \

#define VK_API_ALL_PROCS(x) \
    VK_API_ALL_GLOBAL_PROCS(x) \
    VK_API_ALL_INSTANCE_PROCS(x) \
    VK_API_DEVICE_PROCS(x) \
    VK_API_INSTANCE_PROCS_OPT(x) \
    /* */

#define VK_API_DECLARE_PROC(NAME) PFN_##NAME NAME = nullptr;
// clang-format on

struct VulkanAPI {
    VkInstance instance = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkPhysicalDevice physical_device = VK_NULL_HANDLE;
    VkDebugReportCallbackEXT debug_report_callback = VK_NULL_HANDLE;
    void *vulkan_library_handle = nullptr;
    VkPhysicalDeviceProperties device_properties;
    VkPhysicalDeviceFeatures device_features;
    VkPhysicalDeviceMemoryProperties device_memory_properties;
    int queue_family_index = -1;

    // NOLINTNEXTLINE(readability-*)
    VK_API_ALL_PROCS(VK_API_DECLARE_PROC)

    /// Initialize the instance functions
    int init_instance_procs();

    /// Called before initDevice
    int init_physical_device(VkPhysicalDevice physical_device);

    /// Initialize the device functions
    int init_device_procs();

    /// Finds the proper memory type index for a given requirement.
    int find_memory_type_index(uint32_t type_bits, VkMemoryPropertyFlags properties);

    /// Clean up and frees device and instance.
    ~VulkanAPI();
};

#define RETURN_ON_FAIL(x) \
    {                     \
        auto _res = x;    \
        if (_res != 0) {  \
            return -1;    \
        }                 \
    }

// Loads Vulkan library and creates a VkDevice.
// Returns 0 if successful.
int initialize_vulkan_device(VulkanAPI &api);