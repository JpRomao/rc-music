#include "YoutubeResolverSession.hpp"
#include "YoutubeAudioCodec.hpp"

#include <algorithm>
#include <utility>

namespace music_player::m0 {
namespace {
bool validJobId(const std::string& id) {
    return !id.empty() && id.size() <= 64 && std::all_of(id.begin(), id.end(), [](const unsigned char unit) {
        return (unit >= 'a' && unit <= 'z') || (unit >= 'A' && unit <= 'Z') || (unit >= '0' && unit <= '9') ||
               unit == '_' || unit == '-';
    });
}
bool terminal(const YoutubeResolverState state) {
    return state == YoutubeResolverState::Ready || state == YoutubeResolverState::Failed ||
           state == YoutubeResolverState::Cancelled;
}
} // namespace
struct YoutubeResolverSession::Impl final {
    YoutubeResolverProcess& process;
    YoutubeResolverLimits limits;
    YoutubeResolverSnapshot update;
    std::string job_id;
    QString video_id;
    QByteArray output;
    quint64 stdout_bytes{}, stderr_bytes{};
    qint64 started_ms{}, previous_ms{};
    bool cancel_requested{}, stop_requested{};

    Impl(YoutubeResolverProcess& source, YoutubeResolverLimits configuration)
        : process(source), limits(configuration) {}
    void stop(const DownloadFailure error = DownloadFailure::None) {
        if (error != DownloadFailure::None && update.error == DownloadFailure::None)
            update.error = error;
        update.state = YoutubeResolverState::Stopping;
        output.clear();
        if (!stop_requested) {
            try {
                process.cancel();
                stop_requested = true;
            } catch (...) {
                // Uncertainty never releases ownership or acknowledges native exit.
            }
        }
    }
    void finishFailure() {
        update.audio.reset();
        update.state =
            update.error == DownloadFailure::None ? YoutubeResolverState::Cancelled : YoutubeResolverState::Failed;
        output.clear();
    }
};
YoutubeResolverSession::YoutubeResolverSession(YoutubeResolverProcess& process, YoutubeResolverLimits limits)
    : impl_(std::make_unique<Impl>(process, limits)) {}
YoutubeResolverSession::~YoutubeResolverSession() = default;
bool YoutubeResolverSession::start(std::string job_id, QString video_id, const qint64 now_ms,
                                   const qint64 utc_seconds) {
    auto& state = *impl_;
    if (state.update.state != YoutubeResolverState::Idle || !validJobId(job_id) || now_ms < 0 || utc_seconds < 0 ||
        canonicalYoutubeWatchUrl(video_id.toStdString()).isEmpty() || state.limits.max_stdout_bytes == 0 ||
        state.limits.max_stdout_bytes > 1024 * 1024 || state.limits.max_stderr_bytes == 0 ||
        state.limits.max_stderr_bytes > 128 * 1024 || state.limits.total_timeout_ms <= 0 ||
        state.limits.minimum_remaining_seconds < 0)
        return false;
    state.job_id = std::move(job_id);
    state.video_id = QString(video_id.constData(), video_id.size());
    state.started_ms = state.previous_ms = now_ms;
    state.update.state = YoutubeResolverState::Resolving;
    try {
        if (!state.process.start(state.video_id, now_ms))
            state.stop(DownloadFailure::Unavailable);
    } catch (...) {
        state.stop(DownloadFailure::Unavailable);
    }
    return true; // Own the admitted attempt until a terminal process acknowledgement.
}
YoutubeResolverSnapshot YoutubeResolverSession::poll(const qint64 now_ms, const qint64 utc_seconds) {
    auto& state = *impl_;
    if (state.update.state == YoutubeResolverState::Idle || terminal(state.update.state))
        return state.update;
    if (state.update.state == YoutubeResolverState::Stopping && !state.stop_requested)
        state.stop();
    if (now_ms < state.previous_ms || now_ms < 0 || utc_seconds < 0)
        state.stop(DownloadFailure::InvalidResult);
    else {
        state.previous_ms = now_ms;
        if (now_ms - state.started_ms >= state.limits.total_timeout_ms)
            state.stop(DownloadFailure::Timeout);
    }
    YoutubeResolverProcessUpdate process;
    try {
        process = state.process.poll(std::max(now_ms, state.previous_ms));
    } catch (...) {
        state.stop(DownloadFailure::InvalidResult);
        return state.update;
    }
    if (static_cast<quint64>(process.stdout_chunk.size()) > state.limits.max_stdout_bytes - state.stdout_bytes ||
        static_cast<quint64>(process.stderr_chunk.size()) > state.limits.max_stderr_bytes - state.stderr_bytes) {
        state.stop(DownloadFailure::LimitExceeded);
    } else {
        state.stdout_bytes += static_cast<quint64>(process.stdout_chunk.size());
        state.stderr_bytes += static_cast<quint64>(process.stderr_chunk.size());
        if (state.update.state == YoutubeResolverState::Resolving)
            state.output += process.stdout_chunk;
    }
    if (process.error != DownloadFailure::None)
        state.stop(process.error);
    if (process.state == YoutubeResolverProcessState::Running)
        return state.update;
    if (process.state != YoutubeResolverProcessState::Exited && process.state != YoutubeResolverProcessState::Failed &&
        process.state != YoutubeResolverProcessState::Cancelled) {
        state.stop(DownloadFailure::InvalidResult);
        return state.update; // An unknown enum is not native exit or pipe EOF.
    }
    if (state.update.state == YoutubeResolverState::Stopping || state.cancel_requested) {
        state.finishFailure();
        return state.update;
    }
    if (process.state != YoutubeResolverProcessState::Exited || !process.exit_code || *process.exit_code != 0) {
        state.update.error = DownloadFailure::Unavailable;
        state.finishFailure();
        return state.update;
    }
    try {
        auto decoded = decodeYoutubeAudioResolution(std::move(state.output), state.video_id);
        if (decoded.error != DownloadFailure::None || !decoded.audio) {
            state.update.error =
                decoded.error == DownloadFailure::None ? DownloadFailure::InvalidResult : decoded.error;
            state.finishFailure();
        } else if (decoded.audio->expires_at_seconds <= utc_seconds ||
                   decoded.audio->expires_at_seconds - utc_seconds < state.limits.minimum_remaining_seconds) {
            state.update.error = DownloadFailure::Expired;
            state.finishFailure();
        } else {
            state.update.audio = std::shared_ptr<const ResolvedYoutubeAudioCapability>(
                new ResolvedYoutubeAudioCapability(state.job_id, std::move(*decoded.audio)));
            state.update.state = YoutubeResolverState::Ready;
            state.output.clear();
        }
    } catch (...) {
        state.update.error = DownloadFailure::InvalidResult;
        state.finishFailure();
    }
    return state.update;
}
void YoutubeResolverSession::cancel() {
    auto& state = *impl_;
    if (terminal(state.update.state))
        return;
    if (state.update.state == YoutubeResolverState::Idle) {
        state.update.state = YoutubeResolverState::Cancelled;
        return;
    }
    state.cancel_requested = true;
    state.stop();
}
YoutubeResolverSnapshot YoutubeResolverSession::snapshot() const { return impl_->update; }
} // namespace music_player::m0
