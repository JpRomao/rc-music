#include "m0/adapters/SingleWorkerMetadataReader.hpp"

#include <stdexcept>
#include <utility>

namespace music_player::m0 {

SingleWorkerMetadataReader::SingleWorkerMetadataReader(std::unique_ptr<MetadataExtractor> extractor,
                                                       SingleWorkerMetadataReaderOptions options)
    : extractor_(std::move(extractor)), options_(std::move(options)) {
    if (!extractor_) {
        throw std::invalid_argument{"metadata extractor is required"};
    }
    if (options_.job_budget <= std::chrono::steady_clock::duration::zero()) {
        throw std::invalid_argument{"metadata job budget must be positive"};
    }
    if (!options_.now) {
        throw std::invalid_argument{"metadata clock is required"};
    }

    worker_ = std::jthread{[this] { runWorker(); }};
}

SingleWorkerMetadataReader::~SingleWorkerMetadataReader() {
    try {
        static_cast<void>(shutdown());
    } catch (...) {
    }
}

MetadataCommandResult SingleWorkerMetadataReader::startRead(MetadataReadRequest request) {
    if (request.generation.value == 0 || request.source_path.empty()) {
        std::lock_guard lock{mutex_};
        return stopping_ ? MetadataCommandResult::Shutdown : MetadataCommandResult::InvalidArgument;
    }

    {
        std::lock_guard lock{mutex_};
        if (stopping_) {
            return MetadataCommandResult::Shutdown;
        }
        if (request.generation.value <= highest_generation_.value) {
            return MetadataCommandResult::StaleGeneration;
        }
    }

    const auto deadline = options_.now() + options_.job_budget;
    {
        std::lock_guard lock{mutex_};
        if (stopping_) {
            return MetadataCommandResult::Shutdown;
        }
        if (request.generation.value <= highest_generation_.value) {
            return MetadataCommandResult::StaleGeneration;
        }

        highest_generation_ = request.generation;
        current_state_ = CurrentState::Pending;
        current_deadline_ = deadline;
        completion_.reset();
        pending_.emplace(WorkItem{std::move(request), deadline});
    }
    changed_.notify_one();
    return MetadataCommandResult::Accepted;
}

MetadataCommandResult SingleWorkerMetadataReader::cancelRead(const MetadataGeneration generation) {
    std::lock_guard lock{mutex_};
    if (stopping_) {
        return MetadataCommandResult::Shutdown;
    }
    if (generation.value == 0) {
        return MetadataCommandResult::InvalidArgument;
    }
    if (generation != highest_generation_) {
        return MetadataCommandResult::StaleGeneration;
    }

    current_state_ = CurrentState::Cancelled;
    current_deadline_.reset();
    if (pending_.has_value() && pending_->request.generation == generation) {
        pending_.reset();
    }
    if (completion_.has_value() && completion_->generation == generation) {
        completion_.reset();
    }
    return MetadataCommandResult::Accepted;
}

MetadataPollResult SingleWorkerMetadataReader::poll(const MetadataGeneration generation) {
    const auto observed_at = observeTime();
    std::lock_guard lock{mutex_};
    return pollLocked(generation, observed_at);
}

std::optional<MetadataReadCompletion> SingleWorkerMetadataReader::tryTakeCompletion() {
    const auto observed_at = observeTime();
    std::lock_guard lock{mutex_};
    auto result = pollLocked(highest_generation_, observed_at);
    if (result.status != MetadataPollStatus::Completed) {
        return std::nullopt;
    }
    return std::move(result.completion);
}

MetadataCommandResult SingleWorkerMetadataReader::shutdown() {
    std::lock_guard shutdown_lock{shutdown_mutex_};
    {
        std::lock_guard lock{mutex_};
        if (!stopping_) {
            stopping_ = true;
            pending_.reset();
            completion_.reset();
            current_deadline_.reset();
            current_state_ = CurrentState::Idle;
        }
    }
    changed_.notify_all();

    if (worker_.joinable()) {
        worker_.join();
    }
    return MetadataCommandResult::Accepted;
}

void SingleWorkerMetadataReader::runWorker() {
    for (;;) {
        WorkItem work;
        {
            std::unique_lock lock{mutex_};
            changed_.wait(lock, [this] { return stopping_ || pending_.has_value(); });
            if (stopping_) {
                return;
            }

            work = std::move(*pending_);
            pending_.reset();
            if (work.request.generation == highest_generation_ && current_state_ == CurrentState::Pending) {
                current_state_ = CurrentState::Active;
            }
        }

        std::optional<MetadataReadResult> result{};
        const auto started_at = observeTime();
        if (started_at.has_value() && *started_at < work.deadline) {
            try {
                result.emplace(extractor_->read(work.request.source_path, [this, &work] {
                    const auto observed_at = observeTime();
                    std::lock_guard lock{mutex_};
                    return isWorkInvalidatedLocked(work.request.generation) ||
                           (observed_at.has_value() && *observed_at >= work.deadline);
                }));
            } catch (...) {
                result.emplace(exceptionResult());
            }
        }
        const auto finished_at = observeTime();

        std::lock_guard lock{mutex_};
        if (isWorkInvalidatedLocked(work.request.generation)) {
            continue;
        }
        if ((started_at.has_value() && *started_at >= work.deadline) ||
            (finished_at.has_value() && *finished_at >= work.deadline)) {
            expireCurrentLocked();
            continue;
        }
        if (!result.has_value() || !finished_at.has_value()) {
            current_state_ = CurrentState::Idle;
            current_deadline_.reset();
            continue;
        }

        completion_.emplace(MetadataReadCompletion{
            work.request.generation,
            std::move(work.request.source_path),
            std::move(*result),
        });
        current_state_ = CurrentState::Completed;
    }
}

bool SingleWorkerMetadataReader::isWorkInvalidatedLocked(const MetadataGeneration generation) const noexcept {
    return stopping_ || generation != highest_generation_ || current_state_ == CurrentState::Cancelled ||
           current_state_ == CurrentState::Expired;
}

MetadataPollResult SingleWorkerMetadataReader::pollLocked(const MetadataGeneration generation,
                                                          const std::optional<Deadline> observed_at) {
    if (stopping_) {
        return {.status = MetadataPollStatus::Shutdown};
    }
    if (generation.value == 0 || generation.value > highest_generation_.value) {
        return {.status = MetadataPollStatus::InvalidArgument};
    }
    if (generation.value < highest_generation_.value) {
        return {.status = MetadataPollStatus::StaleGeneration};
    }
    if (current_state_ == CurrentState::Cancelled) {
        return {.status = MetadataPollStatus::Cancelled};
    }
    if (current_state_ == CurrentState::Expired) {
        return {.status = MetadataPollStatus::Expired};
    }
    if (current_state_ != CurrentState::Idle && current_deadline_.has_value() && observed_at.has_value() &&
        *observed_at >= *current_deadline_) {
        expireCurrentLocked();
        return {.status = MetadataPollStatus::Expired};
    }
    if (current_state_ == CurrentState::Completed && completion_.has_value()) {
        auto completion = std::move(*completion_);
        completion_.reset();
        current_deadline_.reset();
        current_state_ = CurrentState::Idle;
        return {
            .status = MetadataPollStatus::Completed,
            .completion = std::move(completion),
        };
    }
    if (current_state_ == CurrentState::Pending || current_state_ == CurrentState::Active) {
        return {.status = MetadataPollStatus::Pending};
    }

    return {.status = MetadataPollStatus::Idle};
}

void SingleWorkerMetadataReader::expireCurrentLocked() {
    if (pending_.has_value() && pending_->request.generation == highest_generation_) {
        pending_.reset();
    }
    completion_.reset();
    current_deadline_.reset();
    current_state_ = CurrentState::Expired;
}

MetadataReadResult SingleWorkerMetadataReader::exceptionResult() {
    return MetadataReadError{
        .code = MetadataReadErrorCode::ReadFailed,
        .message = "metadata extractor failed safely",
    };
}

std::optional<SingleWorkerMetadataReader::Deadline> SingleWorkerMetadataReader::observeTime() const noexcept {
    try {
        return options_.now();
    } catch (...) {
        return std::nullopt;
    }
}

} // namespace music_player::m0
