#pragma once

#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include <slang-rhi.h>

namespace llc {

struct CachedPipeline final {
    rhi::IDevice *device = nullptr;
    std::string key;
    Slang::ComPtr<rhi::IComputePipeline> pipeline;
};

struct PipelineCache final {
    std::mutex mutex;
    std::vector<CachedPipeline> entries;
};

inline PipelineCache g_buffer_pipelines;

/// Thread-safe pipeline lookup with double-checked locking.
/// `create_fn(device)` is called outside the lock if no cache hit.
template <typename CreateFn>
Slang::ComPtr<rhi::IComputePipeline>
get_cached_pipeline(PipelineCache &cache, rhi::IDevice *device, std::string key, CreateFn create_fn) {
    {
        std::scoped_lock lock(cache.mutex);
        for (const auto &cached : cache.entries) {
            if (cached.device == device && cached.key == key) {
                return cached.pipeline;
            }
        }
    }

    auto pipeline = create_fn(device);
    if (!pipeline) return nullptr;

    std::scoped_lock lock(cache.mutex);
    for (const auto &cached : cache.entries) {
        if (cached.device == device && cached.key == key) {
            return cached.pipeline;
        }
    }
    cache.entries.push_back({device, std::move(key), pipeline});
    return pipeline;
}

} // namespace llc
