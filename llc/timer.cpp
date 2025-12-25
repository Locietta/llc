#include "timer.h"

#include <utility>

namespace llc {

bool GpuTimer::init(rhi::IDevice *device, u32 max_samples) {
    reset();
    if (!device || max_samples == 0) {
        return false;
    }
    if (!device->hasFeature(rhi::Feature::TimestampQuery)) {
        return false;
    }

    const uint64_t frequency = device->getInfo().timestampFrequency;
    if (frequency == 0) {
        return false;
    }

    rhi::QueryPoolDesc query_desc{};
    query_desc.type = rhi::QueryType::Timestamp;
    query_desc.count = max_samples;

    Slang::ComPtr<rhi::IQueryPool> pool;
    if (SLANG_FAILED(device->createQueryPool(query_desc, pool.writeRef()))) {
        return false;
    }

    device_ = device;
    query_pool_ = pool;
    timestamp_frequency_ = frequency;
    capacity_ = max_samples;
    next_query_index_ = 0;
    results_.assign(capacity_, 0);
    pair_durations_.clear();
    resolved_ = false;
    return true;
}

bool GpuTimer::is_available() const noexcept {
    return query_pool_ != nullptr && timestamp_frequency_ != 0 && capacity_ > 0;
}

void GpuTimer::reset() {
    query_pool_ = nullptr;
    device_ = nullptr;
    timestamp_frequency_ = 0;
    capacity_ = 0;
    next_query_index_ = 0;
    results_.clear();
    pair_durations_.clear();
    resolved_ = false;
}

void GpuTimer::begin_frame() {
    reset_queries();
    resolved_ = false;
}

void GpuTimer::reset_queries() {
    if (query_pool_) {
        (void) query_pool_->reset();
    }
    next_query_index_ = 0;
    resolved_ = false;
}

bool GpuTimer::can_record() const noexcept {
    return query_pool_ != nullptr && next_query_index_ < capacity_;
}

bool GpuTimer::write_timestamp(rhi::IPassEncoder *pass) {
    if (!pass || !can_record()) {
        return false;
    }
    pass->writeTimestamp(query_pool_.get(), next_query_index_++);
    resolved_ = false;
    return true;
}

bool GpuTimer::write_timestamp(rhi::ICommandEncoder *encoder) {
    if (!encoder || !can_record()) {
        return false;
    }
    encoder->writeTimestamp(query_pool_.get(), next_query_index_++);
    resolved_ = false;
    return true;
}

bool GpuTimer::resolve() {
    if (!query_pool_ || next_query_index_ == 0) {
        return false;
    }

    const u32 count = next_query_index_;
    if (results_.size() < count) {
        results_.resize(count);
    }

    if (SLANG_FAILED(query_pool_->getResult(0, count, results_.data()))) {
        resolved_ = false;
        return false;
    }

    if (timestamp_frequency_ == 0) {
        pair_durations_.clear();
        resolved_ = true;
        return true;
    }

    const usize pair_count = count / 2;
    pair_durations_.assign(pair_count, 0.0);
    const double tick_to_seconds = 1.0 / static_cast<double>(timestamp_frequency_);
    for (usize i = 0; i < pair_count; ++i) {
        const uint64_t start = results_[i * 2];
        const uint64_t end = results_[i * 2 + 1];
        pair_durations_[i] = (end >= start) ? static_cast<double>(end - start) * tick_to_seconds : 0.0;
    }

    resolved_ = true;
    return true;
}

std::span<const uint64_t> GpuTimer::raw_timestamps() const noexcept {
    if (!resolved_) {
        return {};
    }
    return {results_.data(), next_query_index_};
}

std::span<const double> GpuTimer::pair_durations() const noexcept {
    if (!resolved_) {
        return {};
    }
    return {pair_durations_.data(), pair_durations_.size()};
}

double GpuTimer::ticks_to_seconds(uint64_t ticks) const noexcept {
    if (timestamp_frequency_ == 0) {
        return 0.0;
    }
    return static_cast<double>(ticks) / static_cast<double>(timestamp_frequency_);
}

GpuTimer::Scope GpuTimer::scope(rhi::IPassEncoder *pass) {
    return Scope(*this, pass);
}

GpuTimer::Scope GpuTimer::scope(rhi::ICommandEncoder *encoder) {
    return Scope(*this, encoder);
}

GpuTimer::Scope::Scope(GpuTimer &timer, rhi::IPassEncoder *pass)
    : timer_(&timer), pass_(pass) {
    if (timer_ && pass_) {
        started_ = timer_->write_timestamp(pass_);
    }
}

GpuTimer::Scope::Scope(GpuTimer &timer, rhi::ICommandEncoder *encoder)
    : timer_(&timer), encoder_(encoder) {
    if (timer_ && encoder_) {
        started_ = timer_->write_timestamp(encoder_);
    }
}

GpuTimer::Scope::~Scope() {
    if (timer_ && started_) {
        if (pass_) {
            (void) timer_->write_timestamp(pass_);
        } else if (encoder_) {
            (void) timer_->write_timestamp(encoder_);
        }
    }
}

GpuTimer::Frame::Frame(GpuTimer &timer)
    : timer_(&timer) {
    if (timer_) {
        timer_->begin_frame();
    }
}

bool GpuTimer::Frame::resolve() {
    return timer_ ? timer_->resolve() : false;
}

std::span<const uint64_t> GpuTimer::Frame::raw_timestamps() const noexcept {
    return timer_ ? timer_->raw_timestamps() : std::span<const uint64_t>{};
}

std::span<const double> GpuTimer::Frame::pair_durations() const noexcept {
    return timer_ ? timer_->pair_durations() : std::span<const double>{};
}

} // namespace llc
