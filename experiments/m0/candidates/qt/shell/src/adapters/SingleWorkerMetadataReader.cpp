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
        cancelled_generation_.reset();
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

    cancelled_generation_ = generation;
    if (pending_.has_value() && pending_->request.generation == generation) {
        pending_.reset();
    }
    if (completion_.has_value() && completion_->completion.generation == generation) {
        completion_.reset();
    }
    return MetadataCommandResult::Accepted;
}

std::optional<MetadataReadCompletion> SingleWorkerMetadataReader::tryTakeCompletion() {
    std::lock_guard lock{mutex_};
    if (stopping_ || !completion_.has_value()) {
        return std::nullopt;
    }

    const auto observed_at = options_.now();
    const auto generation = completion_->completion.generation;
    if (generation != highest_generation_ || cancelled_generation_ == generation ||
        observed_at >= completion_->deadline) {
        completion_.reset();
        return std::nullopt;
    }

    auto result = std::move(completion_->completion);
    completion_.reset();
    return result;
}

MetadataCommandResult SingleWorkerMetadataReader::shutdown() {
    std::lock_guard shutdown_lock{shutdown_mutex_};
    {
        std::lock_guard lock{mutex_};
        if (!stopping_) {
            stopping_ = true;
            pending_.reset();
            completion_.reset();
            cancelled_generation_.reset();
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
        }

        std::optional<MetadataReadResult> result{};
        const auto started_at = observeTime();
        if (started_at.has_value() && *started_at < work.deadline) {
            try {
                result.emplace(extractor_->read(work.request.source_path));
            } catch (...) {
                result.emplace(exceptionResult());
            }
        }
        const auto finished_at = observeTime();

        std::lock_guard lock{mutex_};
        if (!stopping_ && result.has_value() && finished_at.has_value() && *finished_at < work.deadline &&
            work.request.generation == highest_generation_ && cancelled_generation_ != work.request.generation) {
            completion_.emplace(ReadyCompletion{
                MetadataReadCompletion{
                    work.request.generation,
                    std::move(work.request.source_path),
                    std::move(*result),
                },
                work.deadline,
            });
        }
    }
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
