#include "YoutubeDownloadExecution.hpp"
#include "WindowsDownloadExecutionValidation.hpp"
#include "YoutubeAudioCodec.hpp"

#include <utility>

namespace music_player::m0 {
namespace {
QString videoId(const DownloadItem& item) {
    constexpr std::string_view prefix{"https://www.youtube.com/watch?v="};
    if (item.kind != DownloadMediaKind::Audio || !item.url.starts_with(prefix))
        return {};
    const auto id = item.url.substr(prefix.size());
    if (canonicalYoutubeWatchUrl(id).toUtf8().toStdString() != item.url)
        return {};
    return QString::fromLatin1(id.data(), static_cast<qsizetype>(id.size()));
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
        try {
            process_ = source_factory_(); // Passive construction on the worker.
            if (!process_) {
                fail(DownloadFailure::Unavailable);
                return false;
            }
            resolver_ = std::make_unique<YoutubeResolverSession>(*process_, limits_);
            if (!resolver_->start(id, video, now_ms, clock_())) {
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
            bool accepted{};
            try {
                accepted = destination_->startResolvedYoutube(resolution.audio, now_ms, utc);
            } catch (...) {
            }
            if (!accepted) {
                stop_error_ = DownloadFailure::Unavailable;
                stopDestination();
            }
            // Preparation may block. Return to the executor so cancellation
            // is observed before any transfer poll, with fresh clocks.
            return update_;
        }
        if (cancelled_ || stop_error_ != DownloadFailure::None)
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
            if (!cancelled_ && stop_error_ == DownloadFailure::None)
                update_ = child;
            return update_;
        }
        // A publication racing cancellation remains evidence for the existing
        // executor/importer; failure must not discard a committed final.
        if (child.state == DownloadExecutionState::Published)
            retained_ = child.published_mp3;
        if (stop_error_ != DownloadFailure::None)
            fail(stop_error_);
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
    DownloadFailure stop_error_{DownloadFailure::None};
    bool started_{}, cancelled_{}, destination_stop_requested_{};
};
} // namespace
class YoutubeDownloadExecutionFactory final {
  public:
    static std::unique_ptr<WindowsDownloadExecution> create(WindowsDownloadExecution::TaskFactory factory) {
        return std::unique_ptr<WindowsDownloadExecution>(new WindowsDownloadExecution(
            std::move(factory), [](const DownloadItem& item) { return !videoId(item).isEmpty(); }));
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
} // namespace music_player::m0
