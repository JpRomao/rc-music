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

bool validYoutubeContext(const DownloadItem& item) {
    if (!item.youtube)
        return true;
    const auto& youtube = *item.youtube;
    constexpr std::string_view prefix{"https://www.youtube.com/watch?v="};
    const auto optional_text = [](const std::string& text, const std::size_t max_bytes) {
        return text.size() <= max_bytes && text.find('\0') == std::string::npos;
    };
    // Bounded/coherent data, never source authorization. The trusted adapter
    // applies the full YouTube/lyrics policy before invoking this service.
    return item.kind == DownloadMediaKind::Audio && youtube.video_id.size() == 11 &&
           std::all_of(youtube.video_id.begin(), youtube.video_id.end(),
                       [](const char value) {
                           return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') ||
                                  (value >= '0' && value <= '9') || value == '_' || value == '-';
                       }) &&
           item.url.starts_with(prefix) && std::string_view{item.url}.substr(prefix.size()) == youtube.video_id &&
           validText(youtube.title, 512) && youtube.title == item.title && optional_text(youtube.artist, 512) &&
           optional_text(youtube.lyrics_page, 2048);
}

bool validRequest(const DownloadRequest& request) {
    return validUuid(request.request_id) && !request.items.empty() && request.items.size() <= max_batch_items &&
           std::all_of(request.items.begin(), request.items.end(), [](const DownloadItem& item) {
               return validText(item.url, max_url_bytes) && validText(item.title, max_title_bytes) &&
                      (item.kind == DownloadMediaKind::Audio || item.kind == DownloadMediaKind::Video) &&
                      validYoutubeContext(item);
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
        bool lyrics_review_finished{false};
        // Recovery telemetry remains Interrupted until the durable receipt is
        // acknowledged; it is not reconstructed transfer progress.
        std::optional<DownloadImportState> recovered_import{};
    };

    struct ArchivePlan {
        std::vector<Receipt> receipts;
        std::vector<Job> jobs;
        std::vector<DownloadArchivedRequest> archived_receipts;
        std::vector<DownloadRecoveryItem> recovery_items;
        std::size_t retained_request_bytes{0};
    };

    DownloadExecution& execution;
    DownloadImport& importer;
    DownloadRequestIdentity* const identity;
    std::string prefix;
    const DownloadServiceLimits limits;
    std::vector<Receipt> receipts;
    std::vector<Job> jobs;
    std::vector<DownloadArchivedRequest> archived_receipts;
    std::vector<std::string> acknowledged_archived_job_ids;
    std::optional<ArchivePlan> staged_archive;
    DownloadRecoveryReport recovery_report{};
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

    Job* find(const std::string_view id) noexcept {
        const auto found =
            std::find_if(jobs.begin(), jobs.end(), [id](const Job& job) { return job.view.job_id == id; });
        return found == jobs.end() ? nullptr : &*found;
    }

    Job* recoveredDiscardTarget(const std::string_view id) noexcept {
        if (calling_port || closed || staged_archive || !valid_configuration)
            return nullptr;
        auto* const job = find(id);
        if (!job || job->view.state != DownloadJobState::Interrupted || job->recovered_import ||
            !receipts[job->receipt_index].recovered)
            return nullptr;
        return job;
    }

    DownloadSubmissionError checkReplayFingerprint(const DownloadRequest& request, const std::string_view expected) {
        if (identity == nullptr)
            return DownloadSubmissionError::NotAvailable;
        try {
            const PortCall guard(calling_port);
            const auto fingerprint = identity->fingerprint(request);
            if (!validFingerprint(fingerprint))
                return DownloadSubmissionError::NotAvailable;
            return fingerprint == expected ? DownloadSubmissionError::None : DownloadSubmissionError::InvalidRequest;
        } catch (...) {
            return DownloadSubmissionError::NotAvailable;
        }
    }

    const YoutubeAudioItem* livePublishedYoutubeSource(const Job& job) const noexcept {
        if (job.lyrics_review_finished || job.view.completed_mp3.empty() || job.receipt_index >= receipts.size() ||
            (job.view.state != DownloadJobState::Completed && job.view.state != DownloadJobState::Importing &&
             job.view.state != DownloadJobState::Imported && job.view.state != DownloadJobState::ImportFailed))
            return nullptr;
        const auto& receipt = receipts[job.receipt_index];
        if (receipt.recovered || job.item_index >= receipt.request.items.size())
            return nullptr;
        const auto& source = receipt.request.items[job.item_index].youtube;
        return source ? &*source : nullptr;
    }

    bool eligibleForArchive(const std::size_t receipt_index) const {
        return std::ranges::all_of(jobs, [&](const Job& job) {
            if (job.receipt_index != receipt_index)
                return true;
            switch (job.view.state) {
            case DownloadJobState::Cancelled:
            case DownloadJobState::Failed:
            case DownloadJobState::Completed:
            case DownloadJobState::Imported:
                break;
            default:
                return false;
            }
            // A live YouTube publication can still supply a lyric offer.
            const auto* source = livePublishedYoutubeSource(job);
            return !source || !source->fetch_lyrics;
        });
    }

    static std::optional<DownloadCheckpoint> makeCheckpoint(std::span<const Receipt> active_receipts,
                                                            std::span<const Job> active_jobs,
                                                            std::span<const DownloadArchivedRequest> archived) {
        DownloadCheckpoint result;
        result.receipts.reserve(active_receipts.size());
        result.jobs.reserve(active_jobs.size());
        result.archived_receipts.assign(archived.begin(), archived.end());
        for (const auto& receipt : active_receipts) {
            if (!validFingerprint(receipt.fingerprint))
                return std::nullopt;
            result.receipts.push_back({.request_id = receipt.request.request_id,
                                       .fingerprint = receipt.fingerprint,
                                       .job_ids = receipt.job_ids,
                                       .retained_request_bytes = receipt.retained_request_bytes});
        }
        for (const auto& job : active_jobs) {
            auto view = job.view;
            if (!job.candidate_mp3.empty())
                view.completed_mp3 = job.candidate_mp3;
            result.jobs.push_back(std::move(view));
        }
        if (!validDownloadCheckpoint(result))
            return std::nullopt;
        return result;
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

    void applyImport(Job& job, const DownloadImportState result) {
        switch (result) {
        case DownloadImportState::Pending:
            job.view.state = DownloadJobState::Importing;
            job.view.error = DownloadFailure::None;
            return;
        case DownloadImportState::Committed:
            job.view.state = DownloadJobState::Completed;
            job.view.error = DownloadFailure::None;
            return;
        case DownloadImportState::Failed:
            break;
        }
        // Unknown statuses and exceptions never manufacture commitment. Failed
        // is a handoff result, not evidence that borrowed session I/O is stopped.
        job.view.state = DownloadJobState::ImportFailed;
        job.view.error = DownloadFailure::Import;
    }

    void applyRecoveredImport(Job& job, const DownloadImportState result) {
        switch (result) {
        case DownloadImportState::Pending:
            job.recovered_import = result;
            job.view.state = DownloadJobState::Interrupted;
            job.view.error = DownloadFailure::Unavailable;
            return;
        case DownloadImportState::Committed:
            job.recovered_import = result;
            job.view.state = DownloadJobState::Imported;
            job.view.error = DownloadFailure::None;
            for (auto& item : recovery_report.items)
                if (item.job_id == job.view.job_id)
                    item.disposition = DownloadRecoveryDisposition::Imported;
            return;
        case DownloadImportState::Failed:
            break;
        }
        job.recovered_import = DownloadImportState::Failed;
        job.view.state = DownloadJobState::Interrupted;
        job.view.error = DownloadFailure::Import;
    }

    bool import(Job& job) {
        auto result = DownloadImportState::Failed;
        try {
            const PortCall guard(calling_port);
            result = importer.begin(job.view.job_id, job.view.completed_mp3);
        } catch (...) {
        }
        applyImport(job, result);
        return result == DownloadImportState::Pending || result == DownloadImportState::Committed;
    }

    void observeImports() {
        for (auto& job : jobs) {
            const bool recovered = job.recovered_import == DownloadImportState::Pending;
            if (!recovered && job.view.state != DownloadJobState::Importing)
                continue;
            auto result = DownloadImportState::Failed;
            try {
                const PortCall guard(calling_port);
                result = importer.pollImport(job.view.job_id);
            } catch (...) {
            }
            if (recovered)
                applyRecoveredImport(job, result);
            else
                applyImport(job, result);
        }
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

std::optional<PublishedYoutubeAudio> DownloadService::publishedYoutubeAudio(const std::string_view job_id) const {
    auto& state = *impl_;
    if (state.calling_port || job_id.empty() || job_id.size() > 64)
        return {};
    const auto* job = state.find(job_id);
    if (!job)
        return {};
    const auto* source = state.livePublishedYoutubeSource(*job);
    if (!source || !validPublishedPath(job->view.completed_mp3))
        return {};
    return PublishedYoutubeAudio{job->view.job_id, job->view.completed_mp3, *source};
}
bool DownloadService::finishPublishedLyricsReview(const std::string_view job_id) {
    auto& state = *impl_;
    if (state.calling_port || state.closed || job_id.empty() || job_id.size() > 64)
        return false;
    auto* job = state.find(job_id);
    if (!job)
        return false;
    const auto* source = state.livePublishedYoutubeSource(*job);
    if (!source || !source->fetch_lyrics)
        return false;
    if (state.staged_archive) {
        // An unfinished review kept this job in the archive's survivor set.
        auto& jobs = state.staged_archive->jobs;
        const auto survivor =
            std::ranges::find_if(jobs, [job_id](const auto& item) { return item.view.job_id == job_id; });
        if (survivor == jobs.end())
            return false;
        survivor->lyrics_review_finished = true;
    }
    job->lyrics_review_finished = true;
    return true;
}

std::optional<DownloadCheckpoint> DownloadService::checkpoint() {
    const auto& state = *impl_;
    if (state.calling_port || !state.valid_configuration || state.identity == nullptr)
        return std::nullopt;
    return Impl::makeCheckpoint(state.receipts, state.jobs, state.archived_receipts);
}

bool DownloadService::restore(const DownloadCheckpoint& checkpoint) { return restoreImpl(checkpoint, {}, false); }

bool DownloadService::restore(const DownloadCheckpoint& checkpoint,
                              const std::span<const DownloadImportReceipt> confirmed_imports) {
    return restoreImpl(checkpoint, confirmed_imports, true);
}

DownloadRecoveryReport DownloadService::recoveryReport() const { return impl_->recovery_report; }

bool DownloadService::restoreWithFreshNamespace(const DownloadCheckpoint& checkpoint) {
    return restoreImpl(checkpoint, {}, false, true);
}
bool DownloadService::restoreWithFreshNamespace(const DownloadCheckpoint& checkpoint,
                                                std::span<const DownloadImportReceipt> confirmed_imports) {
    return restoreImpl(checkpoint, confirmed_imports, true, true);
}

bool DownloadService::restoreImpl(const DownloadCheckpoint& checkpoint,
                                  const std::span<const DownloadImportReceipt> confirmed_imports, const bool reconcile,
                                  const bool fresh_namespace) {
    auto& state = *impl_;
    if (state.calling_port || state.closed || !state.valid_configuration || state.identity == nullptr ||
        state.restoration_performed || state.active.has_value() || !state.receipts.empty() || !state.jobs.empty() ||
        !state.archived_receipts.empty() || state.staged_archive)
        return false;

    const auto fail = [&](const DownloadRecoveryStatus status) {
        state.recovery_report = {.status = status};
        return false;
    };
    // Validate and allocate a complete replacement before committing any ownership.
    // Stored paths are copied only as private evidence, never passed to a port.
    try {
        if (!validDownloadCheckpoint(checkpoint) || checkpoint.receipts.size() > state.limits.max_requests ||
            checkpoint.jobs.size() > state.limits.max_jobs ||
            (reconcile && !isValidDownloadImportReceipts(confirmed_imports)))
            return fail(DownloadRecoveryStatus::InvalidHistory);
        auto prefix = state.prefix;
        const auto has_archived_id = [&](const auto& matches) {
            return std::ranges::any_of(checkpoint.archived_receipts, [&](const DownloadArchivedRequest& receipt) {
                return std::ranges::any_of(receipt.job_ids, matches);
            });
        };
        const auto conflicts = [&](const std::string& candidate) {
            const auto family = candidate + '-';
            const auto in_family = [&](const std::string_view id) { return id.starts_with(family); };
            return std::ranges::any_of(checkpoint.jobs, in_family, &DownloadJobSnapshot::job_id) ||
                   std::ranges::any_of(confirmed_imports, in_family, &DownloadImportReceipt::job_id) ||
                   has_archived_id(in_family);
        };
        if (conflicts(prefix)) {
            if (!fresh_namespace)
                return fail(DownloadRecoveryStatus::NamespaceConflict);
            auto maximum_choices = checkpoint.jobs.size() + confirmed_imports.size() + 1;
            for (const auto& archived : checkpoint.archived_receipts)
                maximum_choices += archived.job_ids.size();
            bool selected = false;
            // Keep the existing three-digit families, then widen the suffix
            // within the 40-byte prefix limit. The delimiter makes all families
            // disjoint, so each retained ID excludes at most one candidate.
            for (std::size_t index = 1; index <= maximum_choices; ++index) {
                const auto number = std::to_string(index);
                if (number.size() > 4)
                    return fail(DownloadRecoveryStatus::NamespaceConflict);
                const auto width = std::max<std::size_t>(3, number.size());
                auto candidate =
                    prefix.substr(0, 40 - 2 - width) + "-r" + std::string(width - number.size(), '0') + number;
                if (!conflicts(candidate)) {
                    prefix = std::move(candidate);
                    selected = true;
                    break;
                }
            }
            if (!selected)
                return fail(DownloadRecoveryStatus::NamespaceConflict);
        }
        std::size_t retained_bytes = 0;
        for (const auto& receipt : checkpoint.receipts) {
            if (receipt.retained_request_bytes > state.limits.max_retained_request_bytes - retained_bytes)
                return fail(DownloadRecoveryStatus::InvalidHistory);
            retained_bytes += receipt.retained_request_bytes;
        }

        DownloadRecoveryReport report;
        if (reconcile) {
            report.status = DownloadRecoveryStatus::Ready;
            report.items.reserve(checkpoint.jobs.size());
            report.session_only_job_ids.reserve(confirmed_imports.size());
            for (const auto& receipt : confirmed_imports)
                if (std::ranges::none_of(
                        checkpoint.jobs,
                        [&](const DownloadJobSnapshot& job) { return job.job_id == receipt.job_id; }) &&
                    !has_archived_id([&](const std::string& id) { return id == receipt.job_id; }))
                    report.session_only_job_ids.push_back(receipt.job_id);
        }
        std::vector<Impl::Job> jobs;
        jobs.reserve(checkpoint.jobs.size());
        for (const auto& original : checkpoint.jobs) {
            auto view = original;
            auto candidate = std::move(view.completed_mp3);
            view.completed_mp3.clear();
            auto disposition = DownloadRecoveryDisposition::Unchanged;
            const auto receipt = std::ranges::find(confirmed_imports, original.job_id, &DownloadImportReceipt::job_id);
            if (reconcile && receipt != confirmed_imports.end()) {
                if ((!candidate.empty() && candidate != receipt->source_path) ||
                    view.state == DownloadJobState::Failed || view.state == DownloadJobState::Cancelled ||
                    view.state == DownloadJobState::Cancelling)
                    return fail(DownloadRecoveryStatus::ReceiptConflict);
                // The confirmed receipt recognises the historical queue commit.
                // It supplies missing path evidence, not file/action authority,
                // and never invents progress absent from the older checkpoint.
                if (candidate.empty())
                    candidate = receipt->source_path;
                view.state = DownloadJobState::Imported;
                view.error = DownloadFailure::None;
                disposition = DownloadRecoveryDisposition::Imported;
            } else {
                switch (view.state) {
                case DownloadJobState::Queued:
                case DownloadJobState::Downloading:
                case DownloadJobState::Converting:
                case DownloadJobState::Publishing:
                case DownloadJobState::Cancelling:
                case DownloadJobState::ImportFailed:
                case DownloadJobState::Importing:
                    view.state = DownloadJobState::Interrupted;
                    view.error = DownloadFailure::Unavailable;
                    disposition = original.state == DownloadJobState::ImportFailed ||
                                          original.state == DownloadJobState::Importing
                                      ? DownloadRecoveryDisposition::Unconfirmed
                                      : DownloadRecoveryDisposition::Interrupted;
                    break;
                case DownloadJobState::Completed:
                case DownloadJobState::Imported:
                    if (reconcile) {
                        view.state = DownloadJobState::Interrupted;
                        view.error = DownloadFailure::Unavailable;
                        disposition = DownloadRecoveryDisposition::Unconfirmed;
                    }
                    break;
                case DownloadJobState::Cancelled:
                case DownloadJobState::Failed:
                case DownloadJobState::Interrupted:
                    break;
                }
            }
            if (reconcile)
                report.items.push_back({original.job_id, disposition});
            jobs.push_back({.view = std::move(view), .candidate_mp3 = std::move(candidate)});
        }

        // New receipt-supplied paths must fit the checkpoint's own stricter
        // representation bounds too. This remains pure lexical validation.
        if (reconcile) {
            auto prospective = checkpoint;
            for (std::size_t index = 0; index < jobs.size(); ++index) {
                prospective.jobs[index] = jobs[index].view;
                prospective.jobs[index].completed_mp3 = jobs[index].candidate_mp3;
            }
            if (!validDownloadCheckpoint(prospective))
                return fail(DownloadRecoveryStatus::InvalidHistory);
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
                    return fail(DownloadRecoveryStatus::InvalidHistory);
                found->receipt_index = receipt_index;
                found->item_index = item_index;
            }
            receipts.push_back({.request = {.request_id = original.request_id},
                                .job_ids = original.job_ids,
                                .fingerprint = original.fingerprint,
                                .retained_request_bytes = original.retained_request_bytes,
                                .recovered = true});
        }

        auto archived = checkpoint.archived_receipts;
        state.prefix.swap(prefix);
        state.receipts.swap(receipts);
        state.jobs.swap(jobs);
        state.archived_receipts.swap(archived);
        state.recovery_report = std::move(report);
        state.retained_request_bytes = retained_bytes;
        state.restoration_performed = true;
        return true;
    } catch (...) {
        return fail(DownloadRecoveryStatus::InvalidHistory);
    }
}

DownloadSubmissionResult DownloadService::submit(const DownloadRequest& request) { return submitImpl(request, false); }

DownloadSubmissionResult DownloadService::submitStaged(const DownloadRequest& request) {
    return submitImpl(request, true);
}

bool DownloadService::confirmStaged(const std::string_view request_id) {
    auto& state = *impl_;
    if (state.calling_port || state.staged_archive || !validUuid(request_id))
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
            const auto error = state.checkReplayFingerprint(request, previous->fingerprint);
            if (error != DownloadSubmissionError::None)
                return {.error = error};
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
    const auto archived =
        std::ranges::find(state.archived_receipts, canonical_id, &DownloadArchivedRequest::request_id);
    if (archived != state.archived_receipts.end()) {
        const auto error = state.checkReplayFingerprint(request, archived->fingerprint);
        if (error != DownloadSubmissionError::None)
            return {.error = error};
        return {.error = DownloadSubmissionError::None, .job_ids = archived->job_ids};
    }
    if (state.staged_archive)
        return {.error = DownloadSubmissionError::Busy};

    const auto count = request.items.size();
    if (state.receipts.size() >= state.limits.max_requests || count > state.limits.max_jobs - state.jobs.size() ||
        count > std::numeric_limits<std::uint64_t>::max() - state.next_id)
        return {.error = DownloadSubmissionError::LimitExceeded};

    // Strings were bounded before counting. Subtraction keeps configured SIZE_MAX budgets
    // safe; this counts retained strings and the explicit lyrics-choice byte,
    // not physical allocator/container overhead. Untyped requests retain v1 accounting.
    std::size_t bytes = request.request_id.size();
    for (const auto& item : request.items) {
        bytes += item.url.size() + item.title.size();
        if (item.youtube) {
            const auto& youtube = *item.youtube;
            bytes +=
                youtube.video_id.size() + youtube.title.size() + youtube.artist.size() + youtube.lyrics_page.size() + 1;
        }
    }
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
bool DownloadService::hasActiveExecution() const noexcept { return impl_->active.has_value(); }

DownloadServiceCapacity DownloadService::capacity() const noexcept {
    const auto& state = *impl_;
    return {.jobs = state.jobs.size(),
            .max_jobs = state.limits.max_jobs,
            .requests = state.receipts.size(),
            .max_requests = state.limits.max_requests,
            .retained_request_bytes = state.retained_request_bytes,
            .max_retained_request_bytes = state.limits.max_retained_request_bytes};
}

bool DownloadService::canArchiveSettled() const {
    const auto& state = *impl_;
    if (state.calling_port || state.closed || !state.valid_configuration || state.identity == nullptr ||
        state.staged_archive || state.active ||
        state.archived_receipts.size() >= download_checkpoint_max_archived_receipts ||
        std::ranges::any_of(state.jobs, [](const Impl::Job& job) {
            return job.awaiting_confirmation || job.view.state == DownloadJobState::Queued ||
                   job.view.state == DownloadJobState::Importing ||
                   job.recovered_import == DownloadImportState::Pending;
        }))
        return false;
    for (std::size_t index = 0; index < state.receipts.size(); ++index)
        if (state.eligibleForArchive(index))
            return true;
    return false;
}

std::optional<DownloadCheckpoint> DownloadService::stageSettledArchive() {
    if (!canArchiveSettled())
        return std::nullopt;
    auto& state = *impl_;
    try {
        Impl::ArchivePlan plan;
        plan.archived_receipts = state.archived_receipts;
        plan.receipts.reserve(state.receipts.size());
        plan.jobs.reserve(state.jobs.size());
        const auto removed = state.receipts.size();
        std::vector<std::size_t> retained_indices(state.receipts.size(), removed);
        for (std::size_t index = 0; index < state.receipts.size(); ++index) {
            const auto& receipt = state.receipts[index];
            if (plan.archived_receipts.size() < download_checkpoint_max_archived_receipts &&
                state.eligibleForArchive(index)) {
                plan.archived_receipts.push_back({.request_id = receipt.request.request_id,
                                                  .fingerprint = receipt.fingerprint,
                                                  .job_ids = receipt.job_ids});
            } else {
                retained_indices[index] = plan.receipts.size();
                plan.retained_request_bytes += receipt.retained_request_bytes;
                plan.receipts.push_back(receipt);
            }
        }
        for (const auto& job : state.jobs) {
            const auto index = retained_indices[job.receipt_index];
            if (index == removed)
                continue;
            plan.jobs.push_back(job);
            plan.jobs.back().receipt_index = index;
        }
        for (const auto& item : state.recovery_report.items) {
            if (std::ranges::any_of(plan.jobs, [&](const Impl::Job& job) { return job.view.job_id == item.job_id; }))
                plan.recovery_items.push_back(item);
        }
        auto candidate = Impl::makeCheckpoint(plan.receipts, plan.jobs, plan.archived_receipts);
        if (!candidate)
            return std::nullopt;
        state.staged_archive.emplace(std::move(plan));
        return candidate;
    } catch (...) {
        return std::nullopt;
    }
}

bool DownloadService::confirmSettledArchive() noexcept {
    auto& state = *impl_;
    if (state.calling_port || !state.staged_archive)
        return false;
    auto& plan = *state.staged_archive;
    state.receipts.swap(plan.receipts);
    state.jobs.swap(plan.jobs);
    state.archived_receipts.swap(plan.archived_receipts);
    state.recovery_report.items.swap(plan.recovery_items);
    state.retained_request_bytes = plan.retained_request_bytes;
    state.staged_archive.reset();
    return true;
}

void DownloadService::retireArchivedImportReceipts() noexcept {
    auto& state = *impl_;
    if (state.calling_port || state.archived_receipts.empty())
        return;
    try {
        std::size_t count = 0;
        for (const auto& receipt : state.archived_receipts)
            count += receipt.job_ids.size();
        std::vector<std::string> archived_job_ids;
        archived_job_ids.reserve(count);
        for (const auto& receipt : state.archived_receipts)
            archived_job_ids.insert(archived_job_ids.end(), receipt.job_ids.begin(), receipt.job_ids.end());
        state.acknowledged_archived_job_ids.swap(archived_job_ids);
        const PortCall guard(state.calling_port);
        state.importer.retireArchivedReceipts(state.acknowledged_archived_job_ids);
    } catch (...) {
        // The checkpoint is already committed. Receipt compaction may be
        // offered again on a later authoritative load or archive acknowledgement.
    }
}

void DownloadService::tick(const std::int64_t now_ms, const std::function<bool()>& allow_start) {
    auto& state = *impl_;
    if (state.calling_port || state.staged_archive || !state.valid_configuration)
        return;
    // A published job owns no native execution slot. Its durable handoff is
    // observed separately, including after shutdown, without a download clock
    // or execution cancellation being applied to the borrowed session writer.
    state.observeImports();
    if (!state.active.has_value()) {
        if (state.closed)
            return;
        const auto next = std::find_if(state.jobs.begin(), state.jobs.end(),
                                       [](const Impl::Job& job) { return job.view.state == DownloadJobState::Queued; });
        if (next == state.jobs.end() || next->awaiting_confirmation)
            return;
        ArchivedDownloadJobEvidence archive_evidence{.archived_job_ids = state.acknowledged_archived_job_ids};
        bool allowed = true;
        try {
            const PortCall guard(state.calling_port);
            if (!archive_evidence.archived_job_ids.empty()) {
                try {
                    archive_evidence.retired_import_job_ids = state.importer.committedRetiredImportJobIds();
                } catch (...) {
                    // Unavailable session proof cannot authorize retirement of a final.
                }
            }
            if (allow_start)
                allowed = allow_start();
        } catch (...) {
            allowed = false;
        }
        if (!allowed || state.closed || state.calling_port || state.active ||
            next->view.state != DownloadJobState::Queued || next->awaiting_confirmation)
            return;
        state.active = static_cast<std::size_t>(next - state.jobs.begin());
        auto& job = *next;
        job.view.state = DownloadJobState::Downloading;
        job.started_at = job.last_observed_at = now_ms;
        try {
            bool accepted = false;
            {
                const PortCall guard(state.calling_port);
                const auto& item = state.receipts[job.receipt_index].request.items[job.item_index];
                accepted = archive_evidence.archived_job_ids.empty()
                               ? state.execution.start(job.view.job_id, item)
                               : state.execution.startWithArchivedJobs(job.view.job_id, item, archive_evidence);
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
    if (state.calling_port || state.staged_archive)
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
    if (state.calling_port || state.closed || state.staged_archive)
        return false;
    auto* job = state.find(job_id);
    if (job == nullptr || job->view.state != DownloadJobState::ImportFailed)
        return false;
    return state.import(*job);
}

bool DownloadService::canSettleRecoveredDiscard(const std::string_view job_id) const noexcept {
    return impl_->recoveredDiscardTarget(job_id) != nullptr;
}

bool DownloadService::settleRecoveredDiscard(const std::string_view job_id) {
    auto& state = *impl_;
    auto* const job = state.recoveredDiscardTarget(job_id);
    if (!job)
        return false;
    job->view.state = DownloadJobState::Cancelled;
    job->view.error = DownloadFailure::None;
    job->candidate_mp3.clear();
    std::erase_if(state.recovery_report.items, [job](const auto& item) { return item.job_id == job->view.job_id; });
    return true;
}

bool DownloadService::canImportRecovered(const std::string_view job_id) const {
    auto& state = *impl_;
    if (state.calling_port || state.closed || state.staged_archive || !state.valid_configuration)
        return false;
    const auto* const job = state.find(job_id);
    if (job == nullptr || job->view.state != DownloadJobState::Interrupted ||
        job->recovered_import == DownloadImportState::Pending || !state.receipts[job->receipt_index].recovered)
        return false;
    try {
        const PortCall guard(state.calling_port);
        return state.importer.supportsDurableRecovery();
    } catch (...) {
        return false;
    }
}
bool DownloadService::beginRecoveredImport(const std::string_view job_id,
                                           const std::filesystem::path& trusted_published_path,
                                           const std::function<bool()>& allow_handoff) {
    if (!canImportRecovered(job_id))
        return false;
    auto& state = *impl_;
    auto* const job = state.find(job_id);
    // Prepare the complete persistence representation before publishing a path
    // to the importer. No file is opened and no stored path authorizes work.
    try {
        if (!validPublishedPath(trusted_published_path) ||
            (!job->candidate_mp3.empty() && job->candidate_mp3 != trusted_published_path))
            return false;
        auto prospective = checkpoint();
        if (!prospective)
            return false;
        const auto index = static_cast<std::size_t>(job - state.jobs.data());
        prospective->jobs[index].completed_mp3 = trusted_published_path;
        if (!validDownloadCheckpoint(*prospective))
            return false;
        auto candidate = trusted_published_path;
        // A capability callback may have requested shutdown of the borrowing
        // owner. Recheck that intent after all outward health queries and
        // before changing even private job evidence or invoking the importer.
        if (allow_handoff) {
            const PortCall guard(state.calling_port);
            if (!allow_handoff())
                return false;
        }
        job->candidate_mp3.swap(candidate);
    } catch (...) {
        return false;
    }
    state.applyRecoveredImport(*job, DownloadImportState::Pending);
    auto result = DownloadImportState::Failed;
    try {
        const PortCall guard(state.calling_port);
        result = state.importer.begin(job->view.job_id, job->candidate_mp3);
    } catch (...) {
    }
    state.applyRecoveredImport(*job, result);
    return result == DownloadImportState::Pending || result == DownloadImportState::Committed;
}
std::optional<DownloadImportState> DownloadService::recoveredImportState(const std::string_view job_id) const {
    const auto* const job = impl_->find(job_id);
    return job != nullptr ? job->recovered_import : std::nullopt;
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

bool DownloadService::stopped() const {
    return impl_->closed && !impl_->active.has_value() && std::ranges::none_of(impl_->jobs, [](const Impl::Job& job) {
               return job.view.state == DownloadJobState::Importing ||
                      job.recovered_import == DownloadImportState::Pending;
           });
}

} // namespace music_player::m0
