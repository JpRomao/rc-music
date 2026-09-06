#include "m0/adapters/SingleWorkerDiagnosticWriter.hpp"

#include <chrono>
#include <optional>
#include <stdexcept>
#include <utility>

namespace music_player::m0 {
namespace {
constexpr std::uint64_t ingress_closed = std::uint64_t{1} << 63;
} // namespace

SingleWorkerDiagnosticWriter::SingleWorkerDiagnosticWriter(DiagnosticLogStore& store,
                                                           SingleWorkerDiagnosticWriterOptions options)
    : store_(store), options_(std::move(options)) {
    if (options_.capacity == 0 || options_.capacity > pending_.size())
        throw std::invalid_argument{"diagnostic queue capacity must be between 1 and 256"};
    if (!options_.now) {
        options_.now = [] {
            return std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::system_clock::now().time_since_epoch())
                .count();
        };
    }
    worker_ = std::thread{[this] { runWorker(); }};
}

SingleWorkerDiagnosticWriter::~SingleWorkerDiagnosticWriter() { static_cast<void>(shutdown()); }

bool SingleWorkerDiagnosticWriter::submit(const DiagnosticEntry entry) noexcept {
    auto producer_state = producer_state_.load(std::memory_order_acquire);
    do {
        if (producer_state >= ingress_closed - 1)
            return false;
    } while (!producer_state_.compare_exchange_weak(producer_state, producer_state + 1, std::memory_order_acq_rel));
    const auto finish = [this](const bool accepted) {
        producer_state_.fetch_sub(1, std::memory_order_acq_rel);
        wakeWorker();
        return accepted;
    };

    try {
        std::unique_lock lock{mutex_, std::try_to_lock};
        if (!lock.owns_lock()) {
            recordDropped();
            return finish(false);
        }
        if (producer_state_.load(std::memory_order_acquire) >= ingress_closed)
            return finish(false);
        if (size_ == options_.capacity) {
            recordDropped();
            return finish(false);
        }
        pending_[(head_ + size_) % options_.capacity] = entry;
        ++size_;
    } catch (...) {
        recordDropped();
        return finish(false);
    }
    return finish(true);
}

bool SingleWorkerDiagnosticWriter::shutdown() noexcept {
    try {
        std::lock_guard shutdown_lock{shutdown_mutex_};
        producer_state_.fetch_or(ingress_closed, std::memory_order_acq_rel);
        wakeWorker();
        if (worker_.joinable())
            worker_.join();
        return writes_succeeded_;
    } catch (...) {
        return false;
    }
}

void SingleWorkerDiagnosticWriter::wakeWorker() noexcept {
    work_signalled_.store(true, std::memory_order_release);
    work_signalled_.notify_one();
}

void SingleWorkerDiagnosticWriter::recordDropped() noexcept {
    auto count = dropped_.load(std::memory_order_relaxed);
    while (!dropped_.compare_exchange_weak(count, saturatingDiagnosticCount(count), std::memory_order_relaxed)) {
    }
}

void SingleWorkerDiagnosticWriter::appendEntry(const DiagnosticEntry entry) noexcept {
    try {
        const auto timestamp = options_.now();
        if (timestamp < 0 || !store_.append({timestamp, entry}))
            writes_succeeded_ = false;
    } catch (...) {
        // Never forward external exception strings or recursively log failures.
        writes_succeeded_ = false;
    }
}

void SingleWorkerDiagnosticWriter::runWorker() noexcept {
    for (;;) {
        // Atomic wait also observes a producer that could not acquire mutex_.
        // A condition-variable notification without that lock could be lost.
        work_signalled_.wait(false, std::memory_order_acquire);
        work_signalled_.store(false, std::memory_order_release);
        for (;;) {
            std::optional<DiagnosticEntry> next;
            {
                std::lock_guard lock{mutex_};
                if (size_ > 0) {
                    next = pending_[head_];
                    head_ = (head_ + 1) % options_.capacity;
                    --size_;
                }
            }
            const auto dropped = dropped_.exchange(0, std::memory_order_relaxed);
            if (next.has_value())
                appendEntry(*next);
            if (dropped > 0)
                appendEntry(
                    {DiagnosticSeverity::Warning, DiagnosticEvent::EventsDropped, DiagnosticCode::None, dropped});
            if (next.has_value() || dropped > 0)
                continue;
            if (producer_state_.load(std::memory_order_acquire) == ingress_closed) {
                // A producer may have finished after the earlier empty read.
                // Closed ingress plus zero active producers makes this final
                // queue/drop recheck stable, without relying on wake timing.
                std::lock_guard lock{mutex_};
                if (size_ == 0 && dropped_.load(std::memory_order_relaxed) == 0)
                    return;
                continue;
            }
            break;
        }
    }
}

} // namespace music_player::m0
