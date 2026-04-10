#pragma once

#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include <slang-rhi.h>

namespace llc {

struct CachedPipeline final {
    std::string key;
    Slang::ComPtr<rhi::IComputePipeline> pipeline;
};

struct PipelineCache final {
    std::mutex mutex;
    std::vector<CachedPipeline> entries;

    void clear() noexcept {
        std::scoped_lock lock(mutex);
        entries.clear();
    }
};

/// Thread-safe pipeline lookup with double-checked locking.
/// `create_fn()` is called outside the lock if no cache hit.
template <typename CreateFn>
Slang::ComPtr<rhi::IComputePipeline> get_cached_pipeline(PipelineCache &cache, std::string key, CreateFn create_fn) {
    {
        std::scoped_lock lock(cache.mutex);
        for (const auto &cached : cache.entries) {
            if (cached.key == key) {
                return cached.pipeline;
            }
        }
    }

    auto pipeline = create_fn();
    if (!pipeline) return nullptr;

    std::scoped_lock lock(cache.mutex);
    for (const auto &cached : cache.entries) {
        if (cached.key == key) {
            return cached.pipeline;
        }
    }
    cache.entries.push_back({std::move(key), pipeline});
    return pipeline;
}

} // namespace llc
