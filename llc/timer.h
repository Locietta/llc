#pragma once

#include <slang-com-ptr.h>
#include <slang-rhi.h>

#include <span>
#include <vector>

#include <llc/types.hpp>

namespace llc {

struct ScopedGpuTimerSample;

struct GpuTimer {
    GpuTimer() = default;
    GpuTimer(rhi::IDevice *device, u32 max_samples) { init(device, max_samples); }

    bool init(rhi::IDevice *device, u32 max_samples);

    [[nodiscard]] bool is_available() const noexcept;
    explicit operator bool() const noexcept { return is_available(); }

    void reset();
    void begin_frame();
    bool resolve();

    struct [[nodiscard]] Frame {
        explicit Frame(GpuTimer &timer);

        Frame(const Frame &) = delete;
        Frame &operator=(const Frame &) = delete;

        Frame(Frame &&) noexcept = default;
        Frame &operator=(Frame &&) noexcept = default;

        bool resolve();
        [[nodiscard]] std::span<const uint64_t> raw_timestamps() const noexcept;
        [[nodiscard]] std::span<const double> pair_durations() const noexcept;

    private:
        GpuTimer *timer_ = nullptr;
    };

    // RAII helper to write GPU timestamps
    struct [[nodiscard]] Scope final {
        Scope() = default;
        Scope(GpuTimer &timer, rhi::IPassEncoder *pass);
        Scope(GpuTimer &timer, rhi::ICommandEncoder *encoder);
        ~Scope();

        Scope(const Scope &) = delete;
        Scope &operator=(const Scope &) = delete;

    private:
        GpuTimer *timer_ = nullptr;
        rhi::IPassEncoder *pass_ = nullptr;
        rhi::ICommandEncoder *encoder_ = nullptr;
        bool started_ = false;
    };

    bool write_timestamp(rhi::IPassEncoder *pass);
    bool write_timestamp(rhi::ICommandEncoder *encoder);

    [[nodiscard]] std::span<const uint64_t> raw_timestamps() const noexcept;
    [[nodiscard]] std::span<const double> pair_durations() const noexcept;

    [[nodiscard]] uint64_t timestamp_frequency() const noexcept { return timestamp_frequency_; }
    [[nodiscard]] u32 capacity() const noexcept { return capacity_; }
    [[nodiscard]] u32 query_count() const noexcept { return next_query_index_; }
    [[nodiscard]] double ticks_to_seconds(uint64_t ticks) const noexcept;

    [[nodiscard]] Scope scope(rhi::IPassEncoder *pass);
    [[nodiscard]] Scope scope(rhi::ICommandEncoder *encoder);
    void reset_queries();

private:
    bool can_record() const noexcept;

    rhi::IDevice *device_ = nullptr; // Non-owning
    Slang::ComPtr<rhi::IQueryPool> query_pool_;
    std::vector<uint64_t> results_;
    std::vector<double> pair_durations_;
    uint64_t timestamp_frequency_ = 0;
    u32 capacity_ = 0;
    u32 next_query_index_ = 0;
    bool resolved_ = false;
};

} // namespace llc
