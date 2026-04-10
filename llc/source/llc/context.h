#pragma once

#include <memory>
#include <optional>

#include <slang-com-ptr.h>
#include <slang-rhi.h>
#include <slang.h>

namespace llc {

struct PipelineCache;

struct ContextDesc final {
    rhi::DeviceDesc device;
};

struct Context final {
    static std::optional<Context> create(const ContextDesc &desc);

    Context() noexcept;
    Context(Context &&other) noexcept;
    Context &operator=(Context &&other) noexcept;
    Context(const Context &) = delete;
    Context &operator=(const Context &) = delete;
    ~Context();

    [[nodiscard]] rhi::IDevice *device() const noexcept { return device_.get(); }
    [[nodiscard]] slang::ISession *slang_session() const noexcept { return slang_session_.get(); }
    [[nodiscard]] rhi::ICommandQueue *queue() const noexcept;

private:
    void reset() noexcept;

    Slang::ComPtr<rhi::IDevice> device_;
    Slang::ComPtr<slang::ISession> slang_session_;
    std::unique_ptr<PipelineCache> pipeline_cache_;

    friend PipelineCache &pipeline_cache(Context &context) noexcept;
    friend const PipelineCache &pipeline_cache(const Context &context) noexcept;
};

PipelineCache &pipeline_cache(Context &context) noexcept;
const PipelineCache &pipeline_cache(const Context &context) noexcept;

} // namespace llc
