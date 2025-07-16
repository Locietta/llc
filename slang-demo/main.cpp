// main.cpp

// This file provides the application code for the `hello-world` example.
//

// This example uses Vulkan to run a simple compute shader written in Slang.
// The goal is to demonstrate how to use the Slang API to cross compile
// shader code.
//
#include "example-base.h"
#include "test-base.h"
#include "slang-com-ptr.h"
#include "slang.h"
#include "vulkan-api.h"

using Slang::ComPtr;

static const ExampleResources k_resource_base("shaders");

struct HelloWorldExample : public TestBase {
    // The Vulkan functions pointers result from loading the vulkan library.
    VulkanAPI vk_api;

    // Vulkan objects used in this example.
    VkQueue queue;
    VkCommandPool command_pool = VK_NULL_HANDLE;

    // Input and output buffers.
    VkBuffer inout_buffers[3] = {};
    VkDeviceMemory buffer_memories[3] = {};

    const size_t input_element_count = 16;
    const size_t buffer_size = sizeof(float) * input_element_count;

    // We use a staging buffer allocated on host-visible memory to
    // upload/download data from GPU.
    VkBuffer staging_buffer = VK_NULL_HANDLE;
    VkDeviceMemory staging_memory = VK_NULL_HANDLE;

    VkDescriptorSetLayout descriptor_set_layout = VK_NULL_HANDLE;
    VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;

    // Initializes the Vulkan instance and device.
    int init_vulkan_instance_and_device();

    // This function contains the most interesting part of this example.
    // It loads the `hello-world.slang` shader and compile it using the Slang API
    // into a SPIRV module, then create a Vulkan pipeline from the compiled shader.
    int create_compute_pipeline_from_shader();

    // Creates the input and output buffers.
    int create_in_out_buffers();

    // Sets up descriptor set bindings and dispatches the compute task.
    int dispatch_compute();

    // Reads back and prints the result of the compute task.
    int print_compute_results();

    // Main logic of this example.
    int run();

    ~HelloWorldExample();
};

int main(int argc, char **argv) {
    init_debug_callback();
    HelloWorldExample example;
    example.parse_option(argc, argv);
    return example.run();
}

/************************************************************/
/* HelloWorldExample Implementation */
/************************************************************/

int HelloWorldExample::run() {
    // If VK failed to initialize, skip running but return success anyway.
    // This allows our automated testing to distinguish between essential failures and the
    // case where the application is just not supported.
    if (int result = init_vulkan_instance_and_device())
        return (vk_api.device == VK_NULL_HANDLE) ? 0 : result;
    RETURN_ON_FAIL(create_compute_pipeline_from_shader());
    RETURN_ON_FAIL(create_in_out_buffers());
    RETURN_ON_FAIL(dispatch_compute());
    RETURN_ON_FAIL(print_compute_results());
    return 0;
}

int HelloWorldExample::init_vulkan_instance_and_device() {
    if (initialize_vulkan_device(vk_api) != 0) {
        printf("Failed to load Vulkan.\n");
        return -1;
    }

    VkCommandPoolCreateInfo pool_create_info = {};
    pool_create_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_create_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool_create_info.queueFamilyIndex = vk_api.queue_family_index;
    RETURN_ON_FAIL(vk_api.vkCreateCommandPool(vk_api.device, &pool_create_info, nullptr, &command_pool));

    vk_api.vkGetDeviceQueue(vk_api.device, vk_api.queue_family_index, 0, &queue);
    return 0;
}

int HelloWorldExample::create_compute_pipeline_from_shader() {
    // First we need to create slang global session with work with the Slang API.
    ComPtr<slang::IGlobalSession> slang_global_session;
    RETURN_ON_FAIL(slang::createGlobalSession(slang_global_session.writeRef()));

    // Next we create a compilation session to generate SPIRV code from Slang source.
    slang::SessionDesc session_desc = {};
    slang::TargetDesc target_desc = {};
    target_desc.format = SLANG_SPIRV;
    target_desc.profile = slang_global_session->findProfile("spirv_1_5");
    target_desc.flags = 0;

    session_desc.targets = &target_desc;
    session_desc.targetCount = 1;
    session_desc.compilerOptionEntryCount = 0;

    ComPtr<slang::ISession> session;
    RETURN_ON_FAIL(slang_global_session->createSession(session_desc, session.writeRef()));

    // Once the session has been obtained, we can start loading code into it.
    //
    // The simplest way to load code is by calling `loadModule` with the name of a Slang
    // module. A call to `loadModule("hello-world")` will behave more or less as if you
    // wrote:
    //
    //      import hello_world;
    //
    // In a Slang shader file. The compiler will use its search paths to try to locate
    // `hello-world.slang`, then compile and load that file. If a matching module had
    // already been loaded previously, that would be used directly.
    slang::IModule *slang_module = nullptr;
    {
        ComPtr<slang::IBlob> diagnostic_blob;
        auto path = k_resource_base.resolve_resource("hello-world.slang");
        slang_module = session->loadModule(path.c_str(), diagnostic_blob.writeRef());
        diagnose_if_needed(diagnostic_blob);
        if (!slang_module)
            return -1;
    }

    // Loading the `hello-world` module will compile and check all the shader code in it,
    // including the shader entry points we want to use. Now that the module is loaded
    // we can look up those entry points by name.
    //
    // Note: If you are using this `loadModule` approach to load your shader code it is
    // important to tag your entry point functions with the `[shader("...")]` attribute
    // (e.g., `[shader("compute")] void computeMain(...)`). Without that information there
    // is no umambiguous way for the compiler to know which functions represent entry
    // points when it parses your code via `loadModule()`.
    //
    ComPtr<slang::IEntryPoint> entry_point;
    slang_module->findEntryPointByName("computeMain", entry_point.writeRef());

    // At this point we have a few different Slang API objects that represent
    // pieces of our code: `module`, `vertexEntryPoint`, and `fragmentEntryPoint`.
    //
    // A single Slang module could contain many different entry points (e.g.,
    // four vertex entry points, three fragment entry points, and two compute
    // shaders), and before we try to generate output code for our target API
    // we need to identify which entry points we plan to use together.
    //
    // Modules and entry points are both examples of *component types* in the
    // Slang API. The API also provides a way to build a *composite* out of
    // other pieces, and that is what we are going to do with our module
    // and entry points.
    //


    std::vector<slang::IComponentType *> component_types;
    component_types.push_back(slang_module);
    component_types.push_back(entry_point);

    // Actually creating the composite component type is a single operation
    // on the Slang session, but the operation could potentially fail if
    // something about the composite was invalid (e.g., you are trying to
    // combine multiple copies of the same module), so we need to deal
    // with the possibility of diagnostic output.
    //
    ComPtr<slang::IComponentType> composed_program;
    {
        ComPtr<slang::IBlob> diagnostics_blob;
        SlangResult result = session->createCompositeComponentType(
            component_types.data(),
            component_types.size(),
            composed_program.writeRef(),
            diagnostics_blob.writeRef());
        diagnose_if_needed(diagnostics_blob);
        RETURN_ON_FAIL(result);
    }

    // Now we can call `composedProgram->getEntryPointCode()` to retrieve the
    // compiled SPIRV code that we will use to create a vulkan compute pipeline.
    // This will trigger the final Slang compilation and spirv code generation.
    ComPtr<slang::IBlob> spirv_code;
    {
        ComPtr<slang::IBlob> diagnostics_blob;
        SlangResult result = composed_program->getEntryPointCode(
            0,
            0,
            spirv_code.writeRef(),
            diagnostics_blob.writeRef());
        diagnose_if_needed(diagnostics_blob);
        RETURN_ON_FAIL(result);

        if (is_test_mode()) {
            print_entrypoint_hashes(1, 1, composed_program);
        }
    }

    // The following steps are all Vulkan API calls to create a pipeline.

    // First we need to create a descriptor set layout and a pipeline layout.
    // In this example, the pipeline layout is simple: we have a single descriptor
    // set with three buffer descriptors for our input/output storage buffers.
    // General applications typically has much more complicated pipeline layouts,
    // and should consider using Slang's reflection API to learn about the shader
    // parameter layout of a shader program. However, Slang's reflection API is
    // out of scope of this example.
    VkDescriptorSetLayoutCreateInfo desc_set_layout_create_info = {
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    desc_set_layout_create_info.bindingCount = 3;
    VkDescriptorSetLayoutBinding bindings[3];
    for (int i = 0; i < 3; i++) {
        auto &binding = bindings[i];
        binding.binding = i;
        binding.descriptorCount = 1;
        binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        binding.stageFlags = VK_SHADER_STAGE_ALL;
        binding.pImmutableSamplers = nullptr;
    }
    desc_set_layout_create_info.pBindings = bindings;
    RETURN_ON_FAIL(vk_api.vkCreateDescriptorSetLayout(
        vk_api.device,
        &desc_set_layout_create_info,
        nullptr,
        &descriptor_set_layout));
    VkPipelineLayoutCreateInfo pipeline_layout_create_info = {
        VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pipeline_layout_create_info.setLayoutCount = 1;
    pipeline_layout_create_info.pSetLayouts = &descriptor_set_layout;
    RETURN_ON_FAIL(vk_api.vkCreatePipelineLayout(
        vk_api.device,
        &pipeline_layout_create_info,
        nullptr,
        &pipeline_layout));

    // Next we create a shader module from the compiled SPIRV code.
    VkShaderModuleCreateInfo shader_create_info = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    shader_create_info.codeSize = spirv_code->getBufferSize();
    shader_create_info.pCode = static_cast<const uint32_t *>(spirv_code->getBufferPointer());
    VkShaderModule vk_shader_module;
    RETURN_ON_FAIL(
        vk_api.vkCreateShaderModule(vk_api.device, &shader_create_info, nullptr, &vk_shader_module));

    // Now we have all we need to create a compute pipeline.
    VkComputePipelineCreateInfo pipeline_create_info = {
        VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    pipeline_create_info.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    pipeline_create_info.stage.module = vk_shader_module;
    pipeline_create_info.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    pipeline_create_info.stage.pName = "main";
    pipeline_create_info.layout = pipeline_layout;
    RETURN_ON_FAIL(vk_api.vkCreateComputePipelines(
        vk_api.device,
        VK_NULL_HANDLE,
        1,
        &pipeline_create_info,
        nullptr,
        &pipeline));

    // We can destroy shader module now since it will no longer be used.
    vk_api.vkDestroyShaderModule(vk_api.device, vk_shader_module, nullptr);

    return 0;
}

int HelloWorldExample::create_in_out_buffers() {
    // Create input and output buffers that resides in device-local memory.
    for (int i = 0; i < 3; i++) {
        VkBufferCreateInfo buffer_create_info = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        buffer_create_info.size = buffer_size;
        buffer_create_info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                   VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                                   VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        RETURN_ON_FAIL(
            vk_api.vkCreateBuffer(vk_api.device, &buffer_create_info, nullptr, &inout_buffers[i]));
        VkMemoryRequirements memory_reqs = {};
        vk_api.vkGetBufferMemoryRequirements(vk_api.device, inout_buffers[i], &memory_reqs);

        int memory_type_index = vk_api.find_memory_type_index(
            memory_reqs.memoryTypeBits,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        assert(memory_type_index >= 0);

        VkMemoryPropertyFlags actual_memory_properites =
            vk_api.device_memory_properties.memoryTypes[memory_type_index].propertyFlags;

        VkMemoryAllocateInfo allocate_info = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        allocate_info.allocationSize = memory_reqs.size;
        allocate_info.memoryTypeIndex = memory_type_index;
        RETURN_ON_FAIL(
            vk_api.vkAllocateMemory(vk_api.device, &allocate_info, nullptr, &buffer_memories[i]));
        RETURN_ON_FAIL(
            vk_api.vkBindBufferMemory(vk_api.device, inout_buffers[i], buffer_memories[i], 0));
    }

    // Create the device memory and buffer object used for reading/writing
    // data to/from the device local buffers.
    {
        VkBufferCreateInfo buffer_create_info = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        buffer_create_info.size = buffer_size;
        buffer_create_info.usage =
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        RETURN_ON_FAIL(
            vk_api.vkCreateBuffer(vk_api.device, &buffer_create_info, nullptr, &staging_buffer));
        VkMemoryRequirements memory_reqs = {};
        vk_api.vkGetBufferMemoryRequirements(vk_api.device, staging_buffer, &memory_reqs);

        int memory_type_index = vk_api.find_memory_type_index(
            memory_reqs.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        assert(memory_type_index >= 0);

        VkMemoryPropertyFlags actual_memory_properites =
            vk_api.device_memory_properties.memoryTypes[memory_type_index].propertyFlags;

        VkMemoryAllocateInfo allocate_info = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        allocate_info.allocationSize = memory_reqs.size;
        allocate_info.memoryTypeIndex = memory_type_index;
        RETURN_ON_FAIL(
            vk_api.vkAllocateMemory(vk_api.device, &allocate_info, nullptr, &staging_memory));
        RETURN_ON_FAIL(vk_api.vkBindBufferMemory(vk_api.device, staging_buffer, staging_memory, 0));
    }

    // Map staging buffer and writes in the initial input content.
    float *staging_buffer_data = nullptr;
    vk_api.vkMapMemory(vk_api.device, staging_memory, 0, buffer_size, 0, (void **) &staging_buffer_data);
    if (!staging_buffer_data)
        return -1;
    for (size_t i = 0; i < input_element_count; i++)
        staging_buffer_data[i] = static_cast<float>(i);
    vk_api.vkUnmapMemory(vk_api.device, staging_memory);

    // Create a temporary command buffer for recording commands that writes initial
    // data into the input buffers.
    VkCommandBuffer upload_command_buffer;
    VkCommandBufferAllocateInfo command_buffer_alloc_info = {
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    command_buffer_alloc_info.commandBufferCount = 1;
    command_buffer_alloc_info.commandPool = command_pool;
    command_buffer_alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    RETURN_ON_FAIL(vk_api.vkAllocateCommandBuffers(
        vk_api.device,
        &command_buffer_alloc_info,
        &upload_command_buffer));

    VkCommandBufferBeginInfo begin_info = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    vk_api.vkBeginCommandBuffer(upload_command_buffer, &begin_info);
    VkBufferCopy buffer_copy = {};
    buffer_copy.size = buffer_size;
    vk_api.vkCmdCopyBuffer(upload_command_buffer, staging_buffer, inout_buffers[0], 1, &buffer_copy);
    vk_api.vkCmdCopyBuffer(upload_command_buffer, staging_buffer, inout_buffers[1], 1, &buffer_copy);
    vk_api.vkEndCommandBuffer(upload_command_buffer);
    VkSubmitInfo submit_info = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &upload_command_buffer;
    vk_api.vkQueueSubmit(queue, 1, &submit_info, VK_NULL_HANDLE);
    vk_api.vkQueueWaitIdle(queue);
    vk_api.vkFreeCommandBuffers(vk_api.device, command_pool, 1, &upload_command_buffer);
    return 0;
}

int HelloWorldExample::dispatch_compute() {
    // Create a descriptor pool.
    VkDescriptorPoolCreateInfo descriptor_pool_create_info = {
        VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    VkDescriptorPoolSize pool_sizes[] = {
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 16}};
    descriptor_pool_create_info.maxSets = 4;
    descriptor_pool_create_info.poolSizeCount = sizeof(pool_sizes) / sizeof(VkDescriptorPoolSize);
    descriptor_pool_create_info.pPoolSizes = pool_sizes;
    descriptor_pool_create_info.flags = 0;
    VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
    RETURN_ON_FAIL(vk_api.vkCreateDescriptorPool(
        vk_api.device,
        &descriptor_pool_create_info,
        nullptr,
        &descriptor_pool));

    // Allocate descriptor set.
    VkDescriptorSetAllocateInfo desc_set_alloc_info = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    desc_set_alloc_info.descriptorPool = descriptor_pool;
    desc_set_alloc_info.descriptorSetCount = 1;
    desc_set_alloc_info.pSetLayouts = &descriptor_set_layout;
    VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
    RETURN_ON_FAIL(vk_api.vkAllocateDescriptorSets(vk_api.device, &desc_set_alloc_info, &descriptor_set));

    // Write descriptor set.
    VkWriteDescriptorSet descriptor_set_writes[3] = {};
    VkDescriptorBufferInfo buffer_info[3];
    for (int i = 0; i < 3; i++) {
        buffer_info[i].buffer = inout_buffers[i];
        buffer_info[i].offset = 0;
        buffer_info[i].range = buffer_size;

        descriptor_set_writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptor_set_writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        descriptor_set_writes[i].descriptorCount = 1;
        descriptor_set_writes[i].dstBinding = i;
        descriptor_set_writes[i].dstSet = descriptor_set;
        descriptor_set_writes[i].pBufferInfo = &buffer_info[i];
    }
    vk_api.vkUpdateDescriptorSets(vk_api.device, 3, descriptor_set_writes, 0, nullptr);

    // Allocate command buffer and record dispatch commands.
    VkCommandBuffer command_buffer;
    VkCommandBufferAllocateInfo command_buffer_alloc_info = {
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    command_buffer_alloc_info.commandBufferCount = 1;
    command_buffer_alloc_info.commandPool = command_pool;
    command_buffer_alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    RETURN_ON_FAIL(
        vk_api.vkAllocateCommandBuffers(vk_api.device, &command_buffer_alloc_info, &command_buffer));
    VkCommandBufferBeginInfo begin_info = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    vk_api.vkBeginCommandBuffer(command_buffer, &begin_info);
    vk_api.vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    vk_api.vkCmdBindDescriptorSets(
        command_buffer,
        VK_PIPELINE_BIND_POINT_COMPUTE,
        pipeline_layout,
        0,
        1,
        &descriptor_set,
        0,
        nullptr);
    vk_api.vkCmdDispatch(command_buffer, (uint32_t) input_element_count, 1, 1);
    vk_api.vkEndCommandBuffer(command_buffer);

    // Submit command buffer and wait.
    VkSubmitInfo submit_info = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &command_buffer;
    vk_api.vkQueueSubmit(queue, 1, &submit_info, VK_NULL_HANDLE);
    vk_api.vkQueueWaitIdle(queue);
    vk_api.vkFreeCommandBuffers(vk_api.device, command_pool, 1, &command_buffer);

    // Clean up.
    vk_api.vkDestroyDescriptorPool(vk_api.device, descriptor_pool, nullptr);
    return 0;
}

int HelloWorldExample::print_compute_results() {
    // Allocate command buffer to read back data.
    VkCommandBuffer command_buffer;
    VkCommandBufferAllocateInfo command_buffer_alloc_info = {
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    command_buffer_alloc_info.commandBufferCount = 1;
    command_buffer_alloc_info.commandPool = command_pool;
    command_buffer_alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    RETURN_ON_FAIL(
        vk_api.vkAllocateCommandBuffers(vk_api.device, &command_buffer_alloc_info, &command_buffer));

    // Record commands to copy output buffer into staging buffer.
    VkCommandBufferBeginInfo begin_info = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    vk_api.vkBeginCommandBuffer(command_buffer, &begin_info);
    VkBufferCopy buffer_copy = {};
    buffer_copy.size = buffer_size;
    vk_api.vkCmdCopyBuffer(command_buffer, inout_buffers[2], staging_buffer, 1, &buffer_copy);
    vk_api.vkEndCommandBuffer(command_buffer);

    // Execute command buffer and wait.
    VkSubmitInfo submit_info = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &command_buffer;
    vk_api.vkQueueSubmit(queue, 1, &submit_info, VK_NULL_HANDLE);
    vk_api.vkQueueWaitIdle(queue);
    vk_api.vkFreeCommandBuffers(vk_api.device, command_pool, 1, &command_buffer);

    // Map and read back staging buffer.
    float *staging_buffer_data = nullptr;
    vk_api.vkMapMemory(vk_api.device, staging_memory, 0, buffer_size, 0, (void **) &staging_buffer_data);
    if (!staging_buffer_data)
        return -1;
    for (size_t i = 0; i < input_element_count; i++) {
        printf("%f\n", staging_buffer_data[i]);
    }
    return 0;
}

HelloWorldExample::~HelloWorldExample() {
    if (vk_api.device == VK_NULL_HANDLE)
        return;

    vk_api.vkDestroyPipeline(vk_api.device, pipeline, nullptr);
    for (int i = 0; i < 3; i++) {
        vk_api.vkDestroyBuffer(vk_api.device, inout_buffers[i], nullptr);
        vk_api.vkFreeMemory(vk_api.device, buffer_memories[i], nullptr);
    }
    vk_api.vkDestroyBuffer(vk_api.device, staging_buffer, nullptr);
    vk_api.vkFreeMemory(vk_api.device, staging_memory, nullptr);
    vk_api.vkDestroyPipelineLayout(vk_api.device, pipeline_layout, nullptr);
    vk_api.vkDestroyDescriptorSetLayout(vk_api.device, descriptor_set_layout, nullptr);
    vk_api.vkDestroyCommandPool(vk_api.device, command_pool, nullptr);
}