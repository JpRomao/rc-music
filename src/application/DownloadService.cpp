#include "m0/application/DownloadService.hpp"

#include <algorithm>
#include <limits>
#include <utility>

namespace music_player::m0 {
namespace {

constexpr std::size_t max_batch_items = 20;
constexpr std::size_t max_url_bytes = 8192;
constexpr std::size_t max_title_bytes = 800;

char asciiLower(const char value) {
    return value >= 'A' && value <= 'Z' ? static_cast<char>(value - 'A' + 'a') : value;
}

bool validUuid(const std::string_view id) {
    if (id.size() != 36 || id[14] != '4')
        return false;
    const auto variant = asciiLower(id[19]);
    if (variant != '8' && variant != '9' && variant != 'a' && variant != 'b')
        return false;
    for (std::size_t index = 0; index < id.size(); ++index) {
        if (index == 8 || index == 13 || index == 18 || index == 23) {
            if (id[index] != '-')
                return false;
        } else {
            const auto digit = asciiLower(id[index]);
            if (!((digit >= '0' && digit <= '9') || (digit >= 'a' && digit <= 'f')))
                return false;
        }
    }
    return true;
}

bool validPrefix(const std::string_view prefix) {
    return !prefix.empty() && prefix.size() <= 40 && std::all_of(prefix.begin(), prefix.end(), [](const char value) {
        return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') || (value >= '0' && value <= '9') ||
               value == '_' || value == '-';
    });
}

bool validText(const std::string& text, const std::size_t max_bytes) {
    return !text.empty() && text.size() <= max_bytes && text.find('\0') == std::string::npos;
}

bool validRequest(const DownloadRequest& request) {
    return validUuid(request.request_id) && !request.items.empty() && request.items.size() <= max_batch_items &&
           std::all_of(request.items.begin(), request.items.end(), [](const DownloadItem& item) {
               return validText(item.url, max_url_bytes) && validText(item.title, max_title_bytes) &&
                      (item.kind == DownloadMediaKind::Audio || item.kind == DownloadMediaKind::Video);
           });
}

bool validFingerprint(const std::string_view fingerprint) {
    return fingerprint.size() == 64 && std::all_of(fingerprint.begin(), fingerprint.end(), [](const char value) {
               return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f');
           });
}

bool validPhase(const DownloadPhase phase) {
    switch (phase) {
    case DownloadPhase::Downloading:
    case DownloadPhase::Converting:
    case DownloadPhase::Publishing:
        return true;
    }
    return false;
}

bool terminalState(const DownloadExecutionState state) {
    switch (state) {
    case DownloadExecutionState::Published:
    case DownloadExecutionState::Failed:
    case DownloadExecutionState::Cancelled:
        return true;
    case DownloadExecutionState::Running:
        return false;
    }
    return false;
}

bool validFailure(const DownloadFailure error) {
    switch (error) {
    case DownloadFailure::None:
    case DownloadFailure::Unavailable:
    case DownloadFailure::Network:
    case DownloadFailure::Expired:
    case DownloadFailure::Restricted:
    case DownloadFailure::UnsupportedMedia:
    case DownloadFailure::NoAudio:
    case DownloadFailure::LimitExceeded:
    case DownloadFailure::Conversion:
    case DownloadFailure::Publication:
    case DownloadFailure::Import:
    case DownloadFailure::Timeout:
    case DownloadFailure::InvalidResult:
        return true;
    }
    return false;
}

bool validPublishedPath(const std::filesystem::path& path) {
    if (path.empty() || !path.is_absolute() || path != path.lexically_normal())
        return false;
    const auto& native = path.native();
    if (std::any_of(native.begin(), native.end(), [](const auto value) { return value == 0; }))
        return false;
    auto extension = path.extension().native();
    for (auto& value : extension) {
        if (value >= 'A' && value <= 'Z')
            value = static_cast<std::filesystem::path::value_type>(value - 'A' + 'a');
    }
    return extension == std::filesystem::path(".mp3").native();
}

bool validPayload(const DownloadExecutionUpdate& update) {
    if (!validPhase(update.phase) || !validFailure(update.error))
        return false;
    switch (update.state) {
    case DownloadExecutionState::Running:
    case DownloadExecutionState::Cancelled:
        return update.error == DownloadFailure::None && update.published_mp3.empty();
    case DownloadExecutionState::Published:
        return update.error == DownloadFailure::None && update.phase == DownloadPhase::Publishing &&
               validPublishedPath(update.published_mp3);
    case DownloadExecutionState::Failed:
        return update.error != DownloadFailure::None && update.published_mp3.empty();
    }
    return false;
}

DownloadJobState jobState(const DownloadPhase phase) {
    switch (phase) {
    case DownloadPhase::Downloading:
        return DownloadJobState::Downloading;
    case DownloadPhase::Converting:
        return DownloadJobState::Converting;
    case DownloadPhase::Publishing:
        return DownloadJobState::Publishing;
    }
    return DownloadJobState::Failed;
}

// Only outward calls set this guard. Read-only snapshots remain available, while a callback
// cannot mutate vectors, admit jobs, or recursively invoke an adapter while it is in use.
class PortCall final {
  public:
    explicit PortCall(bool& flag) : flag_(flag) { flag_ = true; }
    ~PortCall() { flag_ = false; }
    PortCall(const PortCall&) = delete;
    PortCall& operator=(const PortCall&) = delete;

  private:
    bool& flag_;
};

} // namespace

struct DownloadService::Impl {
    struct Receipt {
        DownloadRequest request;
        std::vector<std::string> job_ids;
        std::string fingerprint;
        std::size_t retained_request_bytes{0};
        bool recovered{false};
    };

    struct Job {
        DownloadJobSnapshot view;
        // Recovery evidence is private, never authority for import or execution.
        std::filesystem::path candidate_mp3{};
        std::size_t receipt_index{0};
        std::size_t item_index{0};
        DownloadPhase phase{DownloadPhase::Downloading};
        std::int64_t started_at{0};
        std::int64_t last_observed_at{0};
        bool awaiting_confirmation{false};
    };

    DownloadExecution& execution;
    DownloadImport& importer;
    DownloadRequestIdentity* const identity;
    const std::string prefix;
    const DownloadServiceLimits limits;
    std::vector<Receipt> receipts;
    std::vector<Job> jobs;
    std::optional<std::size_t> active;
    std::size_t retained_request_bytes{0};
    std::uint64_t next_id{1};
    bool valid_configuration;
    bool closed{false};
    bool calling_port{false};
    bool restoration_performed{false};

    Impl(DownloadExecution& worker, DownloadImport& local_import, std::string id_prefix, DownloadServiceLimits caps,
         DownloadRequestIdentity* request_identity)
        : execution(worker), importer(local_import), identity(request_identity), prefix(std::move(id_prefix)),
          limits(caps), valid_configuration(validPrefix(prefix) && limits.max_jobs > 0 && limits.max_requests > 0 &&
                                            limits.max_retained_request_bytes > 0 && limits.job_timeout_ms > 0) {}

    Job* find(const std::string_view id) {
        const auto found =
            std::find_if(jobs.begin(), jobs.end(), [id](const Job& job) { return job.view.job_id == id; });
        return found == jobs.end() ? nullptr : &*found;
    }

    void requestCancellation(Job& job, const DownloadFailure error) {
        if (job.view.state == DownloadJobState::Cancelling) {
            // A new failure during draining must not disappear behind a human cancellation,
            // replace an earlier cause, or repeat the adapter cancellation request.
            if (job.view.error == DownloadFailure::None)
                job.view.error = error;
            return;
        }
        job.view.state = DownloadJobState::Cancelling;
        job.view.error = error;
        // Set Cancelling before calling the adapter: even a throwing cancellation is not
        // proof that I/O has stopped. Never issue a successor until a terminal poll arrives.
        try {
            const PortCall guard(calling_port);
            execution.cancel(job.view.job_id);
        } catch (...) {
        }
    }

    bool import(Job& job) {
        bool accepted = false;
        try {
            const PortCall guard(calling_port);
            accepted = importer.accept(job.view.job_id, job.view.completed_mp3);
        } catch (...) {
        }
        job.view.state = accepted ? DownloadJobState::Completed : DownloadJobState::ImportFailed;
        job.view.error = accepted ? DownloadFailure::None : DownloadFailure::Import;
        return accepted;
    }

    bool validProgress(const Job& job, const DownloadExecutionUpdate& update) const {
        return validPhase(update.phase) && update.phase >= job.phase &&
               (update.phase != job.phase || update.completed_units >= job.view.completed_units) &&
               (!update.total_units.has_value() || update.completed_units <= *update.total_units);
    }

    void applyProgress(Job& job, const DownloadExecutionUpdate& update) {
        job.phase = update.phase;
        job.view.completed_units = update.completed_units;
        job.view.total_units = update.total_units;
    }

    void failTerminal(Job& job, const DownloadFailure error) {
        job.view.state = DownloadJobState::Failed;
        job.view.error = error;
        job.view.completed_mp3.clear();
        active.reset();
    }

    void observe(Job& job, const DownloadExecutionUpdate& update) {
        if (update.job_id != job.view.job_id)
            return;
        const bool terminal = terminalState(update.state);
        const bool payload_valid = validPayload(update);
        if (job.view.state == DownloadJobState::Cancelling) {
            // A stop acknowledgement need not repeat progress. Keep the last observed
            // counters, but never accept malformed payloads or unknown states as success.
            if (!terminal) {
                if (!payload_valid)
                    requestCancellation(job, DownloadFailure::InvalidResult);
                return;
            }
            if (job.view.error != DownloadFailure::None)
                failTerminal(job, job.view.error);
            else if (!payload_valid)
                failTerminal(job, DownloadFailure::InvalidResult);
            else if (update.state == DownloadExecutionState::Failed)
                failTerminal(job, update.error);
            else {
                // Publication racing with cancellation is not a local-import handoff.
                job.view.state = DownloadJobState::Cancelled;
                job.view.completed_mp3.clear();
                active.reset();
            }
            return;
        }

        const bool valid = payload_valid && validProgress(job, update) &&
                           (update.state != DownloadExecutionState::Published || !update.total_units.has_value() ||
                            update.completed_units == *update.total_units);
        if (!valid) {
            if (terminal)
                failTerminal(job, DownloadFailure::InvalidResult);
            else
                requestCancellation(job, DownloadFailure::InvalidResult);
            return;
        }

        applyProgress(job, update);
        switch (update.state) {
        case DownloadExecutionState::Running:
            job.view.state = jobState(update.phase);
            break;
        case DownloadExecutionState::Published:
            job.view.state = DownloadJobState::Publishing;
            job.view.completed_mp3 = update.published_mp3;
            (void)import(job);
            active.reset();
            break;
        case DownloadExecutionState::Failed:
            failTerminal(job, update.error);
            break;
        case DownloadExecutionState::Cancelled:
            job.view.state = DownloadJobState::Cancelled;
            active.reset();
            break;
        }
    }
};

DownloadService::DownloadService(DownloadExecution& execution, DownloadImport& importer, std::string prefix,
                                 DownloadServiceLimits limits, DownloadRequestIdentity* identity)
    : impl_(std::make_unique<Impl>(execution, importer, std::move(prefix), limits, identity)) {}
DownloadService::~DownloadService() = default;

std::optional<DownloadCheckpoint> DownloadService::checkpoint() {
    const auto& state = *impl_;
    if (state.calling_port || !state.valid_configuration || state.identity == nullptr)
        return std::nullopt;

    DownloadCheckpoint result;
    result.receipts.reserve(state.receipts.size());
    result.jobs.reserve(state.jobs.size());
    for (const auto& receipt : state.receipts) {
        if (!validFingerprint(receipt.fingerprint))
            return std::nullopt;
        result.receipts.push_back({.request_id = receipt.request.request_id,
                                   .fingerprint = receipt.fingerprint,
                                   .job_ids = receipt.job_ids,
                                   .retained_request_bytes = receipt.retained_request_bytes});
    }
    for (const auto& job : state.jobs) {
        auto view = job.view;
        if (!job.candidate_mp3.empty())
            view.completed_mp3 = job.candidate_mp3;
        result.jobs.push_back(std::move(view));
    }
    if (!validDownloadCheckpoint(result))
        return std::nullopt;
    return result;
}

bool DownloadService::restore(const DownloadCheckpoint& checkpoint) {
    auto& state = *impl_;
    if (state.calling_port || state.closed || !state.valid_configuration || state.identity == nullptr ||
        state.restoration_performed || state.active.has_value() || !state.receipts.empty() || !state.jobs.empty())
        return false;

    // Validate and allocate a complete replacement before committing any ownership.
    // Stored paths are copied only as private evidence, never passed to a port.
    try {
        if (!validDownloadCheckpoint(checkpoint) || checkpoint.receipts.size() > state.limits.max_requests ||
            checkpoint.jobs.size() > state.limits.max_jobs)
            return false;
        const auto namespace_prefix = state.prefix + '-';
        std::size_t retained_bytes = 0;
        for (const auto& receipt : checkpoint.receipts) {
            if (receipt.retained_request_bytes > state.limits.max_retained_request_bytes - retained_bytes)
                return false;
            retained_bytes += receipt.retained_request_bytes;
        }

        std::vector<Impl::Job> jobs;
        jobs.reserve(checkpoint.jobs.size());
        for (const auto& original : checkpoint.jobs) {
            if (original.job_id.starts_with(namespace_prefix))
                return false;
            auto view = original;
            auto candidate = std::move(view.completed_mp3);
            view.completed_mp3.clear();
            switch (view.state) {
            case DownloadJobState::Queued:
            case DownloadJobState::Downloading:
            case DownloadJobState::Converting:
            case DownloadJobState::Publishing:
            case DownloadJobState::Cancelling:
            case DownloadJobState::ImportFailed:
                view.state = DownloadJobState::Interrupted;
                view.error = DownloadFailure::Unavailable;
                break;
            case DownloadJobState::Completed:
            case DownloadJobState::Cancelled:
            case DownloadJobState::Failed:
            case DownloadJobState::Interrupted:
                break;
            }
            jobs.push_back({.view = std::move(view), .candidate_mp3 = std::move(candidate)});
        }

        std::vector<Impl::Receipt> receipts;
        receipts.reserve(checkpoint.receipts.size());
        for (std::size_t receipt_index = 0; receipt_index < checkpoint.receipts.size(); ++receipt_index) {
            const auto& original = checkpoint.receipts[receipt_index];
            for (std::size_t item_index = 0; item_index < original.job_ids.size(); ++item_index) {
                const auto found = std::find_if(jobs.begin(), jobs.end(), [&](const Impl::Job& job) {
                    return job.view.job_id == original.job_ids[item_index];
                });
                if (found == jobs.end())
                    return false;
                found->receipt_index = receipt_index;
                found->item_index = item_index;
            }
            receipts.push_back({.request = {.request_id = original.request_id},
                                .job_ids = original.job_ids,
                                .fingerprint = original.fingerprint,
                                .retained_request_bytes = original.retained_request_bytes,
                                .recovered = true});
        }

        state.receipts.swap(receipts);
        state.jobs.swap(jobs);
        state.retained_request_bytes = retained_bytes;
        state.restoration_performed = true;
        return true;
    } catch (...) {
        return false;
    }
}

DownloadSubmissionResult DownloadService::submit(const DownloadRequest& request) { return submitImpl(request, false); }

DownloadSubmissionResult DownloadService::submitStaged(const DownloadRequest& request) {
    return submitImpl(request, true);
}

bool DownloadService::confirmStaged(const std::string_view request_id) {
    auto& state = *impl_;
    if (state.calling_port || !validUuid(request_id))
        return false;
    auto canonical_id = std::string{request_id};
    std::transform(canonical_id.begin(), canonical_id.end(), canonical_id.begin(), asciiLower);
    const auto receipt = std::find_if(state.receipts.begin(), state.receipts.end(), [&](const Impl::Receipt& item) {
        return item.request.request_id == canonical_id;
    });
    if (receipt == state.receipts.end())
        return false;
    const auto index = static_cast<std::size_t>(receipt - state.receipts.begin());
    for (auto& job : state.jobs)
        if (job.receipt_index == index)
            job.awaiting_confirmation = false;
    // Confirmation is not a state transition: cancelled and recovered jobs stay inert.
    return true;
}

DownloadSubmissionResult DownloadService::submitImpl(const DownloadRequest& request, const bool staged) {
    auto& state = *impl_;
    if (state.calling_port)
        return {.error = DownloadSubmissionError::Busy};
    if (state.closed || !state.valid_configuration)
        return {.error = DownloadSubmissionError::NotAvailable};
    if (staged && state.identity == nullptr)
        return {.error = DownloadSubmissionError::NotAvailable};
    if (!validRequest(request))
        return {.error = DownloadSubmissionError::InvalidRequest};

    auto canonical_id = request.request_id;
    std::transform(canonical_id.begin(), canonical_id.end(), canonical_id.begin(), asciiLower);
    const auto previous =
        std::find_if(state.receipts.begin(), state.receipts.end(), [&canonical_id](const Impl::Receipt& receipt) {
            return receipt.request.request_id == canonical_id;
        });
    if (previous != state.receipts.end()) {
        if (previous->recovered) {
            std::string fingerprint;
            try {
                const PortCall guard(state.calling_port);
                fingerprint = state.identity->fingerprint(request);
            } catch (...) {
                return {.error = DownloadSubmissionError::NotAvailable};
            }
            if (!validFingerprint(fingerprint))
                return {.error = DownloadSubmissionError::NotAvailable};
            if (previous->fingerprint != fingerprint)
                return {.error = DownloadSubmissionError::InvalidRequest};
        } else if (previous->request.items != request.items) {
            return {.error = DownloadSubmissionError::InvalidRequest};
        }
        const auto receipt_index = static_cast<std::size_t>(previous - state.receipts.begin());
        if (!staged && std::any_of(state.jobs.begin(), state.jobs.end(), [&](const Impl::Job& job) {
                return job.receipt_index == receipt_index && job.awaiting_confirmation;
            }))
            return {.error = DownloadSubmissionError::Busy};
        return {.error = DownloadSubmissionError::None, .job_ids = previous->job_ids};
    }

    const auto count = request.items.size();
    if (state.receipts.size() >= state.limits.max_requests || count > state.limits.max_jobs - state.jobs.size() ||
        count > std::numeric_limits<std::uint64_t>::max() - state.next_id)
        return {.error = DownloadSubmissionError::LimitExceeded};

    // Strings were bounded before counting. Subtraction keeps configured SIZE_MAX budgets
    // safe; this counts retained request strings, not physical allocator/container overhead.
    std::size_t bytes = request.request_id.size();
    for (const auto& item : request.items)
        bytes += item.url.size() + item.title.size();
    if (bytes > state.limits.max_retained_request_bytes - state.retained_request_bytes)
        return {.error = DownloadSubmissionError::LimitExceeded};

    std::string fingerprint;
    if (state.identity != nullptr) {
        try {
            const PortCall guard(state.calling_port);
            fingerprint = state.identity->fingerprint(request);
        } catch (...) {
            return {.error = DownloadSubmissionError::NotAvailable};
        }
        if (!validFingerprint(fingerprint))
            return {.error = DownloadSubmissionError::NotAvailable};
    }

    Impl::Receipt receipt{.request = request, .fingerprint = std::move(fingerprint), .retained_request_bytes = bytes};
    receipt.request.request_id = std::move(canonical_id);
    receipt.job_ids.reserve(count);
    std::vector<Impl::Job> new_jobs;
    new_jobs.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        auto id = state.prefix + "-" + std::to_string(state.next_id + index);
        receipt.job_ids.push_back(id);
        new_jobs.push_back({.view = {.job_id = std::move(id), .title = request.items[index].title},
                            .receipt_index = state.receipts.size(),
                            .item_index = index,
                            .awaiting_confirmation = staged});
    }
    DownloadSubmissionResult result{.error = DownloadSubmissionError::None, .job_ids = receipt.job_ids};
    if (staged) {
        // Recovered candidate paths count towards the checkpoint's separate logical
        // budget. Validate the complete prospective representation before consuming
        // any IDs, request quota or ownership; configured service caps cannot relax it.
        auto prospective = checkpoint();
        if (!prospective)
            return {.error = DownloadSubmissionError::NotAvailable};
        prospective->receipts.push_back({.request_id = receipt.request.request_id,
                                         .fingerprint = receipt.fingerprint,
                                         .job_ids = receipt.job_ids,
                                         .retained_request_bytes = receipt.retained_request_bytes});
        for (const auto& job : new_jobs)
            prospective->jobs.push_back(job.view);
        if (!validDownloadCheckpoint(*prospective))
            return {.error = DownloadSubmissionError::LimitExceeded};
    }
    // All potentially allocating copies/reservations happen before admission is committed.
    // Stored receipt indices keep jobs independent of vector relocation and browser lifetime.
    state.receipts.reserve(state.receipts.size() + 1);
    state.jobs.reserve(state.jobs.size() + count);
    state.receipts.push_back(std::move(receipt));
    for (auto& job : new_jobs)
        state.jobs.push_back(std::move(job));
    state.next_id += count;
    state.retained_request_bytes += bytes;
    return result;
}

std::vector<DownloadJobSnapshot> DownloadService::snapshot() const {
    std::vector<DownloadJobSnapshot> result;
    result.reserve(impl_->jobs.size());
    for (const auto& job : impl_->jobs)
        result.push_back(job.view);
    return result;
}

void DownloadService::tick(const std::int64_t now_ms) {
    auto& state = *impl_;
    if (state.calling_port || !state.valid_configuration)
        return;
    if (!state.active.has_value()) {
        if (state.closed)
            return;
        const auto next = std::find_if(state.jobs.begin(), state.jobs.end(),
                                       [](const Impl::Job& job) { return job.view.state == DownloadJobState::Queued; });
        if (next == state.jobs.end() || next->awaiting_confirmation)
            return;
        state.active = static_cast<std::size_t>(next - state.jobs.begin());
        auto& job = *next;
        job.view.state = DownloadJobState::Downloading;
        job.started_at = job.last_observed_at = now_ms;
        try {
            bool accepted = false;
            {
                const PortCall guard(state.calling_port);
                accepted = state.execution.start(job.view.job_id,
                                                 state.receipts[job.receipt_index].request.items[job.item_index]);
            }
            if (!accepted)
                state.failTerminal(job, DownloadFailure::Unavailable);
        } catch (...) {
            state.requestCancellation(job, DownloadFailure::InvalidResult);
        }
        return;
    }

    auto& job = state.jobs[*state.active];
    if (now_ms < job.last_observed_at) {
        state.requestCancellation(job, DownloadFailure::InvalidResult);
    } else {
        job.last_observed_at = now_ms;
        const auto elapsed = static_cast<std::uint64_t>(now_ms) - static_cast<std::uint64_t>(job.started_at);
        if (elapsed >= static_cast<std::uint64_t>(state.limits.job_timeout_ms))
            state.requestCancellation(job, DownloadFailure::Timeout);
    }
    try {
        DownloadExecutionUpdate update;
        {
            const PortCall guard(state.calling_port);
            update = state.execution.poll(job.view.job_id);
        }
        state.observe(job, update);
    } catch (...) {
        state.requestCancellation(job, DownloadFailure::InvalidResult);
    }
}

bool DownloadService::cancel(const std::string_view job_id) {
    auto& state = *impl_;
    if (state.calling_port)
        return false;
    auto* job = state.find(job_id);
    if (job == nullptr)
        return false;
    if (job->view.state == DownloadJobState::Queued) {
        job->view.state = DownloadJobState::Cancelled;
        return true;
    }
    if (job->view.state != DownloadJobState::Downloading && job->view.state != DownloadJobState::Converting &&
        job->view.state != DownloadJobState::Publishing)
        return false;
    state.requestCancellation(*job, DownloadFailure::None);
    return true;
}

bool DownloadService::retryImport(const std::string_view job_id) {
    auto& state = *impl_;
    if (state.calling_port || state.closed)
        return false;
    auto* job = state.find(job_id);
    if (job == nullptr || job->view.state != DownloadJobState::ImportFailed)
        return false;
    return state.import(*job);
}

void DownloadService::shutdown() {
    auto& state = *impl_;
    if (state.calling_port || state.closed)
        return;
    state.closed = true;
    for (auto& job : state.jobs) {
        if (job.view.state == DownloadJobState::Queued)
            job.view.state = DownloadJobState::Cancelled;
    }
    if (state.active.has_value())
        state.requestCancellation(state.jobs[*state.active], DownloadFailure::None);
}

bool DownloadService::stopped() const { return impl_->closed && !impl_->active.has_value(); }

} // namespace music_player::m0
