#include "timer.h"

namespace llc {

std::optional<GpuTimer> GpuTimer::create(rhi::IDevice *device, u32 max_samples) {
    GpuTimer timer;
    if (!device || max_samples == 0) { return std::nullopt; }
    if (!device->hasFeature(rhi::Feature::TimestampQuery)) { return std::nullopt; }

    const u64 frequency = device->getInfo().timestampFrequency;
    if (frequency == 0) { return std::nullopt; }

    rhi::QueryPoolDesc query_desc{
        .type = rhi::QueryType::Timestamp,
        .count = max_samples,
    };

    Slang::ComPtr<rhi::IQueryPool> pool;
    if (SLANG_FAILED(device->createQueryPool(query_desc, pool.writeRef()))) {
        return std::nullopt;
    }

    timer.query_pool_ = pool;
    timer.timestamp_frequency_ = frequency;
    timer.capacity_ = max_samples;
    timer.next_query_index_ = 0;
    timer.results_.resize(timer.capacity_);
    timer.resolved_ = false;
    return timer;
}

void GpuTimer::reset() {
    if (query_pool_) {
        (void) query_pool_->reset();
    }
    next_query_index_ = 0;
    pair_labels_.clear();
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

    const usize pair_count = count / 2;
    pair_durations_.resize(pair_count);
    assert(pair_durations_.size() == pair_count);

    const double tick_to_seconds = 1.0 / static_cast<double>(timestamp_frequency_);
    for (usize i = 0; i < pair_count; ++i) {
        const u64 start = results_[i * 2];
        const u64 end = results_[i * 2 + 1];
        pair_durations_[i] = (end >= start) ? static_cast<double>(end - start) * tick_to_seconds : 0.0;
    }

    resolved_ = true;
    return true;
}

std::span<const u64> GpuTimer::raw_timestamps() const noexcept {
    assert(resolved_);
    if (!resolved_) {
        return {};
    }
    return {results_.data(), next_query_index_};
}

std::span<const double> GpuTimer::pair_durations() const noexcept {
    assert(resolved_);
    if (!resolved_) {
        return {};
    }
    return {pair_durations_.data(), pair_durations_.size()};
}

std::span<const std::string> GpuTimer::labels() const noexcept {
    assert(resolved_);
    if (!resolved_) {
        return {};
    }
    return {pair_labels_.data(), pair_labels_.size()};
}

double GpuTimer::ticks_to_seconds(u64 ticks) const noexcept {
    return static_cast<double>(ticks) / static_cast<double>(timestamp_frequency_);
}

GpuTimer::Scope GpuTimer::scope(rhi::IPassEncoder *pass, std::string_view label) {
    return Scope(*this, pass, label);
}

GpuTimer::Scope GpuTimer::scope(rhi::ICommandEncoder *encoder, std::string_view label) {
    return Scope(*this, encoder, label);
}

void GpuTimer::record_scope_label(std::string_view label) {
    pair_labels_.emplace_back(label);
}

GpuTimer::Scope::Scope(GpuTimer &timer, rhi::IPassEncoder *pass, std::string_view label)
    : timer_(&timer), pass_(pass) {
    if (timer_ && pass_) {
        started_ = timer_->write_timestamp(pass_);
        if (started_) {
            timer_->record_scope_label(label);
        }
    }
}

GpuTimer::Scope::Scope(GpuTimer &timer, rhi::ICommandEncoder *encoder, std::string_view label)
    : timer_(&timer), encoder_(encoder) {
    if (timer_ && encoder_) {
        started_ = timer_->write_timestamp(encoder_);
        if (started_) {
            timer_->record_scope_label(label);
        }
    }
}

GpuTimer::Scope::Scope(Scope &&other) noexcept
    : timer_(other.timer_), pass_(other.pass_), encoder_(other.encoder_), started_(other.started_) {
    other.timer_ = nullptr;
    other.pass_ = nullptr;
    other.encoder_ = nullptr;
    other.started_ = false;
}

GpuTimer::Scope &GpuTimer::Scope::operator=(Scope &&other) noexcept {
    if (this != &other) {
        timer_ = other.timer_;
        pass_ = other.pass_;
        encoder_ = other.encoder_;
        started_ = other.started_;

        other.timer_ = nullptr;
        other.pass_ = nullptr;
        other.encoder_ = nullptr;
        other.started_ = false;
    }
    return *this;
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

} // namespace llc
