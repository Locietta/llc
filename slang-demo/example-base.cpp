#include "example-base.h"
#include "util/math.h"
#include "util/types.h"

#include <chrono>

#ifdef _WIN32
#include <windows.h>
#endif

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <fmt/format.h>

using namespace Slang;
using namespace gfx;

using namespace loia::types;

namespace platform = loia::platform;

Slang::Result WindowedAppBase::initialize_base(
    const char *title,
    int width,
    int height,
    DeviceType device_type) {
    // Initialize the rendering layer.
#ifdef _DEBUG
    // Enable debug layer in debug config.
    gfxEnableDebugLayer(true);
#endif
    IDevice::Desc device_desc = {};
    device_desc.deviceType = device_type;
    gfx::Result res = gfxCreateDevice(&device_desc, g_device.writeRef());
    if (SLANG_FAILED(res))
        return res;

    ICommandQueue::Desc queue_desc = {};
    queue_desc.type = ICommandQueue::QueueType::Graphics;
    g_queue = g_device->createCommandQueue(queue_desc);

    window_width = width;
    window_height = height;

    IFramebufferLayout::TargetLayout render_target_layout = {gfx::Format::R8G8B8A8_UNORM, 1};
    IFramebufferLayout::TargetLayout depth_layout = {gfx::Format::D32_FLOAT, 1};
    IFramebufferLayout::Desc framebuffer_layout_desc;
    framebuffer_layout_desc.renderTargetCount = 1;
    framebuffer_layout_desc.renderTargets = &render_target_layout;
    framebuffer_layout_desc.depthStencil = &depth_layout;
    SLANG_RETURN_ON_FAIL(
        g_device->createFramebufferLayout(framebuffer_layout_desc, g_framebuffer_layout.writeRef()));

    // Do not create swapchain and windows in test mode, because there won't be any display.
    if (!is_test_mode()) {
        // Create a window for our application to render into.
        //
        platform::WindowDesc window_desc;
        window_desc.title = title;
        window_desc.width = width;
        window_desc.height = height;
        window_desc.style = platform::WindowStyle::DEFAULT;
        g_window = platform::Application::create_window(window_desc);
        g_window->events.main_loop = [this]() { main_loop(); };
        g_window->events.size_changed = [this] { WindowedAppBase::window_size_changed(); };

        auto device_info = g_device->getDeviceInfo();
        auto title_string = fmt::format("{} ({}: {})", title, device_info.apiName, device_info.adapterName);
        g_window->set_title(title_string.c_str());

        // Create swapchain and framebuffers.
        gfx::ISwapchain::Desc swapchain_desc = {};
        swapchain_desc.format = gfx::Format::R8G8B8A8_UNORM;
        swapchain_desc.width = width;
        swapchain_desc.height = height;
        swapchain_desc.imageCount = k_k_swapchain_image_count;
        swapchain_desc.queue = g_queue;
        gfx::WindowHandle window_handle = g_window->get_handle().convert<gfx::WindowHandle>();
        g_swapchain = g_device->createSwapchain(swapchain_desc, window_handle);
        create_swapchain_framebuffers();
    } else {
        create_offline_framebuffers();
    }

    for (uint32_t i = 0; i < k_k_swapchain_image_count; i++) {
        gfx::ITransientResourceHeap::Desc transient_heap_desc = {};
        transient_heap_desc.constantBufferSize = 4096 * 1024;
        auto transient_heap = g_device->createTransientResourceHeap(transient_heap_desc);
        g_transient_heaps.push_back(transient_heap);
    }

    gfx::IRenderPassLayout::Desc render_pass_desc = {};
    render_pass_desc.framebufferLayout = g_framebuffer_layout;
    render_pass_desc.renderTargetCount = 1;
    IRenderPassLayout::TargetAccessDesc render_target_access = {};
    IRenderPassLayout::TargetAccessDesc depth_stencil_access = {};
    render_target_access.loadOp = IRenderPassLayout::TargetLoadOp::Clear;
    render_target_access.storeOp = IRenderPassLayout::TargetStoreOp::Store;
    render_target_access.initialState = ResourceState::Undefined;
    render_target_access.finalState = ResourceState::Present;
    depth_stencil_access.loadOp = IRenderPassLayout::TargetLoadOp::Clear;
    depth_stencil_access.storeOp = IRenderPassLayout::TargetStoreOp::Store;
    depth_stencil_access.initialState = ResourceState::DepthWrite;
    depth_stencil_access.finalState = ResourceState::DepthWrite;
    render_pass_desc.renderTargetAccess = &render_target_access;
    render_pass_desc.depthStencilAccess = &depth_stencil_access;
    g_render_pass = g_device->createRenderPassLayout(render_pass_desc);

    return SLANG_OK;
}

void WindowedAppBase::main_loop() {
    int frame_buffer_index = g_swapchain->acquireNextImage();

    g_transient_heaps[frame_buffer_index]->synchronizeAndReset();
    render_frame(frame_buffer_index);
    g_transient_heaps[frame_buffer_index]->finish();
}

void WindowedAppBase::offline_render() {
    g_transient_heaps[0]->synchronizeAndReset();
    render_frame(0);
    g_transient_heaps[0]->finish();
}

void WindowedAppBase::create_framebuffers(
    uint32_t width,
    uint32_t height,
    gfx::Format color_format,
    uint32_t frame_buffer_count) {
    for (uint32_t i = 0; i < frame_buffer_count; i++) {
        gfx::ITextureResource::Desc depth_buffer_desc;
        depth_buffer_desc.type = IResource::Type::Texture2D;
        depth_buffer_desc.size.width = width;
        depth_buffer_desc.size.height = height;
        depth_buffer_desc.size.depth = 1;
        depth_buffer_desc.format = gfx::Format::D32_FLOAT;
        depth_buffer_desc.defaultState = ResourceState::DepthWrite;
        depth_buffer_desc.allowedStates = ResourceStateSet(ResourceState::DepthWrite);
        ClearValue depth_clear_value = {};
        depth_buffer_desc.optimalClearValue = &depth_clear_value;
        ComPtr<gfx::ITextureResource> depth_buffer_resource =
            g_device->createTextureResource(depth_buffer_desc, nullptr);

        ComPtr<gfx::ITextureResource> color_buffer;
        if (is_test_mode()) {
            gfx::ITextureResource::Desc color_buffer_desc;
            color_buffer_desc.type = IResource::Type::Texture2D;
            color_buffer_desc.size.width = width;
            color_buffer_desc.size.height = height;
            color_buffer_desc.size.depth = 1;
            color_buffer_desc.format = color_format;
            color_buffer_desc.defaultState = ResourceState::RenderTarget;
            color_buffer_desc.allowedStates =
                ResourceStateSet(ResourceState::RenderTarget, ResourceState::CopyDestination);
            color_buffer = g_device->createTextureResource(color_buffer_desc, nullptr);
        } else {
            g_swapchain->getImage(i, color_buffer.writeRef());
        }

        gfx::IResourceView::Desc color_buffer_view_desc;
        memset(&color_buffer_view_desc, 0, sizeof(color_buffer_view_desc));
        color_buffer_view_desc.format = color_format;
        color_buffer_view_desc.renderTarget.shape = gfx::IResource::Type::Texture2D;
        color_buffer_view_desc.type = gfx::IResourceView::Type::RenderTarget;
        ComPtr<gfx::IResourceView> rtv =
            g_device->createTextureView(color_buffer.get(), color_buffer_view_desc);

        gfx::IResourceView::Desc depth_buffer_view_desc;
        memset(&depth_buffer_view_desc, 0, sizeof(depth_buffer_view_desc));
        depth_buffer_view_desc.format = gfx::Format::D32_FLOAT;
        depth_buffer_view_desc.renderTarget.shape = gfx::IResource::Type::Texture2D;
        depth_buffer_view_desc.type = gfx::IResourceView::Type::DepthStencil;
        ComPtr<gfx::IResourceView> dsv =
            g_device->createTextureView(depth_buffer_resource.get(), depth_buffer_view_desc);

        gfx::IFramebuffer::Desc framebuffer_desc;
        framebuffer_desc.renderTargetCount = 1;
        framebuffer_desc.depthStencilView = dsv.get();
        framebuffer_desc.renderTargetViews = rtv.readRef();
        framebuffer_desc.layout = g_framebuffer_layout;
        ComPtr<gfx::IFramebuffer> frame_buffer = g_device->createFramebuffer(framebuffer_desc);

        g_framebuffers.push_back(frame_buffer);
    }
}

void WindowedAppBase::create_offline_framebuffers() {
    g_framebuffers.clear();
    create_framebuffers(window_width, window_height, gfx::Format::R8G8B8A8_UNORM, 1);
}

void WindowedAppBase::create_swapchain_framebuffers() {
    g_framebuffers.clear();
    create_framebuffers(
        g_swapchain->getDesc().width,
        g_swapchain->getDesc().height,
        g_swapchain->getDesc().format,
        k_k_swapchain_image_count);
}

ComPtr<gfx::IResourceView> WindowedAppBase::create_texture_from_file(
    std::string file_name,
    int &texture_width,
    int &texture_height) {
    int channels_in_file = 0;
    auto texture_content =
        stbi_load(file_name.c_str(), &texture_width, &texture_height, &channels_in_file, 4);
    gfx::ITextureResource::Desc texture_desc = {};
    texture_desc.allowedStates.add(ResourceState::ShaderResource);
    texture_desc.format = gfx::Format::R8G8B8A8_UNORM;
    texture_desc.numMipLevels = loia::log2_ceil(u32(std::min(texture_width, texture_height))) + 1;
    texture_desc.type = gfx::IResource::Type::Texture2D;
    texture_desc.size.width = texture_width;
    texture_desc.size.height = texture_height;
    texture_desc.size.depth = 1;
    std::vector<gfx::ITextureResource::SubresourceData> subres_data;
    std::vector<std::vector<uint32_t>> mip_map_data;
    mip_map_data.resize(texture_desc.numMipLevels);
    subres_data.resize(texture_desc.numMipLevels);
    mip_map_data[0].resize(texture_width * texture_height);
    memcpy(mip_map_data[0].data(), texture_content, texture_width * texture_height * 4);
    stbi_image_free(texture_content);
    subres_data[0].data = mip_map_data[0].data();
    subres_data[0].strideY = texture_width * 4;
    subres_data[0].strideZ = texture_width * texture_height * 4;

    // Build mipmaps.
    struct RGBA {
        uint8_t v[4];
    };
    auto cast_to_rgba = [](uint32_t v) {
        RGBA result;
        memcpy(&result, &v, 4);
        return result;
    };
    auto cast_to_uint = [](RGBA v) {
        uint32_t result;
        memcpy(&result, &v, 4);
        return result;
    };

    int last_mip_width = texture_width;
    int last_mip_height = texture_height;
    for (int m = 1; m < texture_desc.numMipLevels; m++) {
        auto last_mipmap_data = mip_map_data[m - 1].data();
        int w = last_mip_width / 2;
        int h = last_mip_height / 2;
        mip_map_data[m].resize(w * h);
        subres_data[m].data = mip_map_data[m].data();
        subres_data[m].strideY = w * 4;
        subres_data[m].strideZ = h * w * 4;
        for (int x = 0; x < w; x++) {
            for (int y = 0; y < h; y++) {
                auto pix1 = cast_to_rgba(last_mipmap_data[(y * 2) * last_mip_width + (x * 2)]);
                auto pix2 = cast_to_rgba(last_mipmap_data[(y * 2) * last_mip_width + (x * 2 + 1)]);
                auto pix3 = cast_to_rgba(last_mipmap_data[(y * 2 + 1) * last_mip_width + (x * 2)]);
                auto pix4 = cast_to_rgba(last_mipmap_data[(y * 2 + 1) * last_mip_width + (x * 2 + 1)]);
                RGBA pix;
                for (int c = 0; c < 4; c++) {
                    pix.v[c] =
                        (uint8_t) (((uint32_t) pix1.v[c] + pix2.v[c] + pix3.v[c] + pix4.v[c]) / 4);
                }
                mip_map_data[m][y * w + x] = cast_to_uint(pix);
            }
        }
        last_mip_width = w;
        last_mip_height = h;
    }

    auto texture = g_device->createTextureResource(texture_desc, subres_data.data());

    gfx::IResourceView::Desc view_desc = {};
    view_desc.type = gfx::IResourceView::Type::ShaderResource;
    return g_device->createTextureView(texture.get(), view_desc);
}

void WindowedAppBase::window_size_changed() {
    // Wait for the GPU to finish.
    g_queue->waitOnHost();

    auto client_rect = g_window->get_client_rect();
    if (client_rect.width > 0 && client_rect.height > 0) {
        // Free all framebuffers before resizing swapchain.
        g_framebuffers = decltype(g_framebuffers)();

        // Resize swapchain.
        if (g_swapchain->resize(client_rect.width, client_rect.height) == SLANG_OK) {
            // Recreate framebuffers for each swapchain back buffer image.
            create_swapchain_framebuffers();
            window_width = client_rect.width;
            window_height = client_rect.height;
        }
    }
}

i64 get_current_time() {
    return std::chrono::high_resolution_clock::now().time_since_epoch().count();
}

i64 get_timer_frequency() {
    return std::chrono::high_resolution_clock::period::den;
}

class DebugCallback : public IDebugCallback {
public:
    virtual SLANG_NO_THROW void SLANG_MCALL
    handleMessage(DebugMessageType type, DebugMessageSource source, const char *message) override {
        const char *type_str = "";
        switch (type) {
            case DebugMessageType::Info:
                type_str = "INFO: ";
                break;
            case DebugMessageType::Warning:
                type_str = "WARNING: ";
                break;
            case DebugMessageType::Error:
                type_str = "ERROR: ";
                break;
            default:
                break;
        }
        const char *source_str = "[GraphicsLayer]: ";
        switch (source) {
            case DebugMessageSource::Slang:
                source_str = "[Slang]: ";
                break;
            case DebugMessageSource::Driver:
                source_str = "[Driver]: ";
                break;
            case DebugMessageSource::Layer:
                source_str = "[Layer]: ";
                break;
        }
        printf("%s%s%s\n", source_str, type_str, message);
#ifdef _WIN32
        OutputDebugStringA(source_str);
        OutputDebugStringA(type_str);
        OutputDebugStringW((wchar_t *) (message));
        OutputDebugStringW(L"\n");
#endif
    }
};

void init_debug_callback() {
    static DebugCallback callback = {};
    gfxSetDebugCallback(&callback);
}

#ifdef _WIN32
void _Win32OutputDebugString(const char *str) {
    OutputDebugStringW((wchar_t *) (str));
}
#endif