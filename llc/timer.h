#pragma once

#include <slang-com-ptr.h>
#include <slang-rhi.h>

#include <span>
#include <string>
#include <string_view>
#include <optional>
#include <vector>
#include <ranges>

#include <llc/types.hpp>

namespace llc {

struct GpuTimer {
    static std::optional<GpuTimer> create(rhi::IDevice *device, u32 pass_count);

    void reset();
    bool resolve();

    // RAII helper to write GPU timestamps
    struct [[nodiscard]] Scope final {
        Scope() = default;
        Scope(GpuTimer &timer, rhi::IPassEncoder *pass, std::string_view label = {});
        Scope(GpuTimer &timer, rhi::ICommandEncoder *encoder, std::string_view label = {});
        ~Scope();

        Scope(const Scope &) = delete;
        Scope &operator=(const Scope &) = delete;
        Scope(Scope &&other) noexcept;
        Scope &operator=(Scope &&other) noexcept;

    private:
        GpuTimer *timer_ = nullptr;
        rhi::IPassEncoder *pass_ = nullptr;
        rhi::ICommandEncoder *encoder_ = nullptr;
        bool started_ = false;
    };

    [[nodiscard]] std::span<const u64> raw_timestamps() const noexcept;
    [[nodiscard]] std::span<const double> pair_durations() const noexcept;
    [[nodiscard]] std::span<const std::string> labels() const noexcept;
    [[nodiscard]] auto labeled_durations() const noexcept {
        return std::views::zip(labels(), pair_durations());
    }

    [[nodiscard]] u64 timestamp_frequency() const noexcept { return timestamp_frequency_; }
    [[nodiscard]] u32 capacity() const noexcept { return capacity_; }
    [[nodiscard]] u32 query_count() const noexcept { return next_query_index_; }
    [[nodiscard]] double ticks_to_seconds(u64 ticks) const noexcept;

    [[nodiscard]] Scope scope(rhi::IPassEncoder *pass, std::string_view label = {});
    [[nodiscard]] Scope scope(rhi::ICommandEncoder *encoder, std::string_view label = {});

private:
    GpuTimer() = default;

    bool write_timestamp(rhi::IPassEncoder *pass);
    bool write_timestamp(rhi::ICommandEncoder *encoder);

    bool can_record() const noexcept;
    void record_scope_label(std::string_view label);

    Slang::ComPtr<rhi::IQueryPool> query_pool_;
    std::vector<u64> results_;
    std::vector<f64> pair_durations_;
    std::vector<std::string> pair_labels_;
    u64 timestamp_frequency_ = 0;
    u32 capacity_ = 0;
    u32 next_query_index_ = 0;
    bool resolved_ = false;
};

} // namespace llc
