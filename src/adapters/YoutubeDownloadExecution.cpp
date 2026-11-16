#include "YoutubeDownloadExecution.hpp"
#include "DownloadNetworkPolicy.hpp"
#include "WindowsDownloadExecutionValidation.hpp"
#include "YoutubeAudioCodec.hpp"

#include <utility>

namespace music_player::m0 {
namespace {
QString videoId(const DownloadItem& item) {
    constexpr std::string_view prefix{"https://www.youtube.com/watch?v="};
    if (item.kind != DownloadMediaKind::Audio || item.url.size() != prefix.size() + 11 || !item.url.starts_with(prefix))
        return {};
    const auto id = item.url.substr(prefix.size());
    if (canonicalYoutubeWatchUrl(id).toUtf8().toStdString() != item.url)
        return {};
    return QString::fromLatin1(id.data(), static_cast<qsizetype>(id.size()));
}
bool youtubeContext(const DownloadItem& item) {
    if (!item.youtube || item.youtube->video_id.size() != 11 || item.youtube->title.size() > 512 ||
        item.youtube->artist.size() > 512 || item.youtube->lyrics_page.size() > 2048 ||
        item.youtube->title != item.title)
        return false;
    const auto id = videoId(item);
    return !id.isEmpty() && id.toStdString() == item.youtube->video_id &&
           !encodeYoutubeAudioRequest({"01234567-89ab-4cde-8fab-0123456789ab", {*item.youtube}}).isEmpty();
}
bool directItem(const DownloadItem& item) {
    if (item.youtube || item.url.empty() || item.url.size() > 8192 || item.url.find('\0') != std::string::npos ||
        (item.kind != DownloadMediaKind::Audio && item.kind != DownloadMediaKind::Video))
        return false;
    const auto url = QString::fromUtf8(item.url);
    return url.toUtf8().toStdString() == item.url && parseDownloadNetworkTarget(url).has_value();
}
bool terminal(DownloadExecutionState state) {
    return state == DownloadExecutionState::Published || state == DownloadExecutionState::Failed ||
           state == DownloadExecutionState::Cancelled;
}
class YoutubeSessionTask final : public WindowsDownloadTask {
  public:
    YoutubeSessionTask(YoutubeResolverProcessFactory source, WindowsDownloadExecution::TaskFactory destination,
                       YoutubeResolverEpochClock clock, YoutubeResolverLimits limits)
        : source_factory_(std::move(source)), destination_factory_(std::move(destination)), clock_(std::move(clock)),
          limits_(limits) {}
    bool start(const std::string& id, const DownloadItem& item, const qint64 now_ms) override {
        if (started_)
            return false;
        started_ = true;
        update_.job_id = id;
        const auto video = videoId(item);
        if (video.isEmpty()) {
            fail(DownloadFailure::InvalidResult);
            return false;
        }
        video_ = video;
        try {
            if (item.youtube) {
                const auto& youtube = *item.youtube;
                publication_.metadata = {QString::fromStdString(youtube.title), QString::fromStdString(youtube.artist)};
                publication_.file_name =
                    youtube.file_name.empty() ? publication_.metadata.title : QString::fromStdString(youtube.file_name);
                if (youtube.file_name.empty() && !publication_.metadata.artist.isEmpty())
                    publication_.file_name += QStringLiteral(" - ") + publication_.metadata.artist;
            }
        } catch (...) {
            fail(DownloadFailure::Unavailable);
            return false;
        }
        return startResolver(now_ms);
    }
    void cancel() override {
        if (terminal(update_.state))
            return;
        cancelled_ = true;
        if (destination_)
            stopDestination();
        else if (resolver_)
            resolver_->cancel();
        else
            update_.state = DownloadExecutionState::Cancelled;
    }
    DownloadExecutionUpdate poll(const qint64 now_ms) override {
        if (terminal(update_.state))
            return update_;
        if (!destination_) {
            if (!resolver_) {
                fail(DownloadFailure::InvalidResult);
                return update_;
            }
            qint64 utc{-1};
            try {
                utc = clock_();
            } catch (...) {
            }
            const auto resolution = resolver_->poll(now_ms, utc);
            if (resolution.state == YoutubeResolverState::Resolving ||
                resolution.state == YoutubeResolverState::Stopping)
                return update_;
            if (resolution.state == YoutubeResolverState::Failed) {
                fail(resolution.error == DownloadFailure::None ? DownloadFailure::InvalidResult : resolution.error);
                return update_;
            }
            if (resolution.state == YoutubeResolverState::Cancelled || cancelled_) {
                update_.state = DownloadExecutionState::Cancelled;
                return update_;
            }
            if (resolution.state != YoutubeResolverState::Ready || !resolution.audio) {
                fail(DownloadFailure::InvalidResult);
                return update_;
            }
            if (utc < 0 || resolution.audio->expiresAtSeconds() <= utc) {
                fail(utc < 0 ? DownloadFailure::InvalidResult : DownloadFailure::Expired);
                return update_;
            }
            try {
                destination_ = destination_factory_();
            } catch (...) {
                fail(DownloadFailure::Unavailable);
                return update_;
            }
            if (!destination_) {
                fail(DownloadFailure::Unavailable);
                return update_;
            }
            // A refused start still needs the destination's terminal ACK.
            try {
                destination_start_refused_ =
                    !destination_->startResolvedYoutube(resolution.audio, now_ms, utc, publication_);
            } catch (...) {
                stop_error_ = DownloadFailure::Unavailable;
            }
            if (destination_start_refused_ || stop_error_ != DownloadFailure::None)
                stopDestination();
            // Preparation may block. Return to the executor so cancellation
            // is observed before any transfer poll, with fresh clocks.
            return update_;
        }
        const bool stopping = cancelled_ || destination_start_refused_ || stop_error_ != DownloadFailure::None;
        if (stopping)
            stopDestination();
        DownloadExecutionUpdate child;
        try {
            child = destination_->poll(now_ms);
        } catch (...) {
            stop_error_ = DownloadFailure::InvalidResult;
            stopDestination();
            return update_;
        }
        if (!download_execution_detail::validUpdate(child, update_.job_id)) {
            stop_error_ = DownloadFailure::InvalidResult;
            stopDestination();
            return update_; // A malformed DTO cannot acknowledge this owner.
        }
        if (!terminal(child.state)) {
            // After a new resolution the transfer restarts from zero; the reported
            // progress holds until the new attempt passes it, so it never goes back.
            if (!stopping && (child.phase > update_.phase ||
                              (child.phase == update_.phase && child.completed_units >= update_.completed_units)))
                update_ = child;
            return update_;
        }
        // A refused stream URL (HTTP 401/403) usually expired or was bound to another
        // session: resolve the video once more and download it again from the start.
        if (child.state == DownloadExecutionState::Failed && child.error == DownloadFailure::SourceRejected &&
            !stopping && !resolved_again_) {
            resolved_again_ = true;
            destination_.reset(); // Terminal, so its native work is already acknowledged.
            destination_stop_requested_ = false;
            static_cast<void>(startResolver(now_ms));
            return update_;
        }
        // A publication racing cancellation remains evidence for the existing
        // executor/importer; failure must not discard a committed final.
        if (child.state == DownloadExecutionState::Published)
            retained_ = child.published_mp3;
        if (stop_error_ != DownloadFailure::None)
            fail(stop_error_);
        else if (destination_start_refused_ && child.state != DownloadExecutionState::Failed)
            fail(DownloadFailure::Unavailable);
        else
            update_ = std::move(child);
        return update_;
    }
    std::filesystem::path retainedPublicationPath() const override {
        return !retained_.empty() ? retained_
               : destination_     ? destination_->retainedPublicationPath()
                                  : std::filesystem::path{};
    }

  private:
    bool startResolver(const qint64 now_ms) {
        try {
            resolver_.reset();            // Before its process: the session borrows it.
            process_ = source_factory_(); // Passive construction on the worker.
            if (!process_) {
                fail(DownloadFailure::Unavailable);
                return false;
            }
            resolver_ = std::make_unique<YoutubeResolverSession>(*process_, limits_);
            if (!resolver_->start(update_.job_id, video_, now_ms, clock_())) {
                fail(DownloadFailure::InvalidResult);
                return false;
            }
        } catch (...) {
            // Factories are passive. The session catches uncertainty once a
            // process start is attempted and owns it through its terminal ACK.
            fail(DownloadFailure::Unavailable);
            return false;
        }
        return true;
    }
    void fail(DownloadFailure error) {
        update_.state = DownloadExecutionState::Failed;
        update_.error = error;
        update_.published_mp3.clear();
    }
    void stopDestination() {
        if (destination_stop_requested_)
            return;
        try {
            destination_->cancel();
            destination_stop_requested_ = true;
        } catch (...) {
        }
    }
    YoutubeResolverProcessFactory source_factory_;
    WindowsDownloadExecution::TaskFactory destination_factory_;
    YoutubeResolverEpochClock clock_;
    YoutubeResolverLimits limits_;
    // All of these are destroyed on the worker after terminal acknowledgement.
    std::unique_ptr<YoutubeResolverProcess> process_;
    std::unique_ptr<YoutubeResolverSession> resolver_;
    std::unique_ptr<WindowsDownloadTask> destination_;
    DownloadExecutionUpdate update_;
    std::filesystem::path retained_;
    YoutubePublication publication_;
    QString video_;
    DownloadFailure stop_error_{DownloadFailure::None};
    bool started_{}, cancelled_{}, destination_stop_requested_{}, destination_start_refused_{}, resolved_again_{};
};
class RoutedSessionTask final : public WindowsDownloadTask {
  public:
    RoutedSessionTask(YoutubeResolverProcessFactory source, WindowsDownloadExecution::TaskFactory destination,
                      YoutubeResolverEpochClock clock, YoutubeResolverLimits limits)
        : source_(std::move(source)), destination_(std::move(destination)), clock_(std::move(clock)), limits_(limits) {}
    bool start(const std::string& id, const DownloadItem& item, qint64 now_ms) override {
        if (attempted_)
            return false;
        attempted_ = true;
        failure_ = {.job_id = id, .state = DownloadExecutionState::Failed, .error = DownloadFailure::Unavailable};
        if (item.youtube)
            task_ = std::make_unique<YoutubeSessionTask>(source_, destination_, clock_, limits_);
        else
            task_ = destination_();
        return task_ && task_->start(id, item, now_ms);
    }
    void cancel() override {
        if (task_)
            task_->cancel();
    }
    DownloadExecutionUpdate poll(qint64 now_ms) override { return task_ ? task_->poll(now_ms) : failure_; }
    std::filesystem::path retainedPublicationPath() const override {
        return task_ ? task_->retainedPublicationPath() : std::filesystem::path{};
    }

  private:
    YoutubeResolverProcessFactory source_;
    WindowsDownloadExecution::TaskFactory destination_;
    YoutubeResolverEpochClock clock_;
    YoutubeResolverLimits limits_;
    std::unique_ptr<WindowsDownloadTask> task_;
    DownloadExecutionUpdate failure_;
    bool attempted_{};
};
} // namespace
class YoutubeDownloadExecutionFactory final {
  public:
    static std::unique_ptr<WindowsDownloadExecution> create(WindowsDownloadExecution::TaskFactory factory,
                                                            const bool mixed = false,
                                                            WindowsDownloadExecution::Preflight preflight = {}) {
        return std::unique_ptr<WindowsDownloadExecution>(new WindowsDownloadExecution(
            std::move(factory),
            [mixed](const DownloadItem& item) {
                return mixed ? (item.youtube ? youtubeContext(item) : directItem(item))
                             : (!videoId(item).isEmpty() && (!item.youtube || youtubeContext(item)));
            },
            std::move(preflight)));
    }
};
std::unique_ptr<WindowsDownloadExecution>
makeWindowsYoutubeAudioExecution(YoutubeResolverProcessFactory source,
                                 WindowsDownloadExecution::TaskFactory destination, YoutubeResolverEpochClock clock,
                                 YoutubeResolverLimits limits) {
    if (!source || !destination || !clock)
        return {};
    return YoutubeDownloadExecutionFactory::create(
        [source = std::move(source), destination = std::move(destination), clock = std::move(clock), limits] {
            return std::make_unique<YoutubeSessionTask>(source, destination, clock, limits);
        });
}
std::unique_ptr<WindowsDownloadExecution>
makeWindowsMediaAndYoutubeExecution(YoutubeResolverProcessFactory source,
                                    WindowsDownloadExecution::TaskFactory destination, YoutubeResolverEpochClock clock,
                                    YoutubeResolverLimits limits, WindowsDownloadExecution::Preflight preflight) {
    if (!source || !destination || !clock)
        return {};
    return YoutubeDownloadExecutionFactory::create(
        [source = std::move(source), destination = std::move(destination), clock = std::move(clock), limits] {
            return std::make_unique<RoutedSessionTask>(source, destination, clock, limits);
        },
        true, std::move(preflight));
}
} // namespace music_player::m0
