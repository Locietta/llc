#include "context.h"

#include <utility>

#include <llc/utils/pipeline_cache.h>

namespace llc {

Context::Context() noexcept = default;

std::optional<Context> Context::create(const ContextDesc &desc) {
    Context context;
    context.device_ = rhi::getRHI()->createDevice(desc.device);
    if (!context.device_) return std::nullopt;

    context.slang_session_ = context.device_->getSlangSession();
    context.pipeline_cache_ = std::make_unique<PipelineCache>();
    return context;
}

Context::Context(Context &&other) noexcept
    : device_(std::move(other.device_)),
      slang_session_(std::move(other.slang_session_)),
      pipeline_cache_(std::move(other.pipeline_cache_)) {}

Context &Context::operator=(Context &&other) noexcept {
    if (this != &other) {
        reset();
        device_ = std::move(other.device_);
        slang_session_ = std::move(other.slang_session_);
        pipeline_cache_ = std::move(other.pipeline_cache_);
    }
    return *this;
}

Context::~Context() {
    reset();
}

rhi::ICommandQueue *Context::queue() const noexcept {
    return device_ ? device_->getQueue(rhi::QueueType::Graphics) : nullptr;
}

void Context::reset() noexcept {
    pipeline_cache_.reset();
    slang_session_ = nullptr;
    device_ = nullptr;
}

PipelineCache &pipeline_cache(Context &context) noexcept {
    return *context.pipeline_cache_;
}

const PipelineCache &pipeline_cache(const Context &context) noexcept {
    return *context.pipeline_cache_;
}

} // namespace llc
