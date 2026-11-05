#include "WindowsYoutubeResolverProcess.hpp"

#include <QElapsedTimer>
#include <QThread>

#include <utility>

namespace music_player::m0 {
namespace {
bool validVideoId(const QString& value) {
    if (value.size() != 11)
        return false;
    for (const auto unit : value)
        if (!((unit >= u'a' && unit <= u'z') || (unit >= u'A' && unit <= u'Z') || (unit >= u'0' && unit <= u'9') ||
              unit == u'_' || unit == u'-'))
            return false;
    return true;
}
QString owned(const QString& value) { return QString(value.constData(), value.size()); }
WindowsDownloadProcessConfig copiedConfiguration(const WindowsDownloadProcessConfig& source) {
    auto result = source;
    result.executable = owned(source.executable);
    result.working_directory = owned(source.working_directory);
    result.arguments.clear();
    for (const auto& argument : source.arguments)
        result.arguments.push_back(owned(argument));
    result.standard_input = QByteArray(source.standard_input.constData(), source.standard_input.size());
    if (source.environment) {
        QProcessEnvironment environment;
        for (const auto& key : source.environment->keys())
            environment.insert(owned(key), owned(source.environment->value(key)));
        result.environment = std::move(environment);
    }
    return result;
}
DownloadFailure failure(const WindowsDownloadProcessError error) {
    switch (error) {
    case WindowsDownloadProcessError::InvalidConfiguration:
    case WindowsDownloadProcessError::Io:
        return DownloadFailure::InvalidResult;
    case WindowsDownloadProcessError::Isolation:
        return DownloadFailure::Restricted;
    case WindowsDownloadProcessError::LimitExceeded:
        return DownloadFailure::LimitExceeded;
    case WindowsDownloadProcessError::Timeout:
        return DownloadFailure::Timeout;
    case WindowsDownloadProcessError::Start:
    case WindowsDownloadProcessError::None:
        return DownloadFailure::Unavailable;
    }
    return DownloadFailure::InvalidResult;
}
} // namespace
struct WindowsYoutubeResolverProcess::Impl final {
    Prepare prepare;
    std::optional<WindowsYoutubeResolverPreparedProcess> retained;
    std::unique_ptr<WindowsDownloadProcess> process;
    YoutubeResolverProcessUpdate update{.state = YoutubeResolverProcessState::Failed,
                                        .error = DownloadFailure::Unavailable};
    std::optional<DownloadFailure> forced_failure;
    qint64 last_now{};
    bool attempted{}, acknowledged{true}, refused_start{};
    explicit Impl(Prepare source) : prepare(std::move(source)) {}
    void noNativeFailure(const DownloadFailure error) {
        update = {.state = YoutubeResolverProcessState::Failed, .error = error};
        acknowledged = true;
        retained.reset();
    }
    void requestStop() noexcept {
        if (acknowledged || !process)
            return;
        try {
            process->cancel();
        } catch (...) {
            forced_failure = DownloadFailure::InvalidResult;
        }
    }
    YoutubeResolverProcessUpdate sample(const qint64 now_ms) {
        update.stdout_chunk.clear();
        update.stderr_chunk.clear();
        if (acknowledged)
            return update;
        last_now = now_ms;
        WindowsDownloadProcessUpdate native;
        try {
            native = process->poll(now_ms);
        } catch (...) {
            forced_failure = DownloadFailure::InvalidResult;
            requestStop();
            return {.state = YoutubeResolverProcessState::Running};
        }
        update.stdout_chunk = std::move(native.stdout_chunk);
        update.stderr_chunk = std::move(native.stderr_chunk);
        const auto state = native.state;
        if (state != WindowsDownloadProcessState::Exited && state != WindowsDownloadProcessState::Failed &&
            state != WindowsDownloadProcessState::Cancelled) {
            update.state = YoutubeResolverProcessState::Running;
            update.error = DownloadFailure::None;
            update.exit_code.reset();
            return update;
        }
        // Only this terminal observation acknowledges both native exit and all
        // owned pipe EOF. A false/throwing start or cancellation did not do so.
        acknowledged = true;
        update.exit_code = native.exit_code;
        if (forced_failure) {
            update.state = YoutubeResolverProcessState::Failed;
            update.error = *forced_failure;
        } else if (state == WindowsDownloadProcessState::Failed) {
            update.state = YoutubeResolverProcessState::Failed;
            update.error = failure(native.error);
        } else if (refused_start) {
            update.state = YoutubeResolverProcessState::Failed;
            update.error = DownloadFailure::Unavailable;
        } else if (state == WindowsDownloadProcessState::Cancelled) {
            update.state = YoutubeResolverProcessState::Cancelled;
            update.error = DownloadFailure::None;
        } else if (native.exit_code && *native.exit_code == 0) {
            update.state = YoutubeResolverProcessState::Exited;
            update.error = DownloadFailure::None;
        } else {
            update.state = YoutubeResolverProcessState::Failed;
            update.error = native.exit_code ? DownloadFailure::Unavailable : DownloadFailure::InvalidResult;
        }
        process.reset();
        retained.reset();
        return update;
    }
    bool drainForDestruction() noexcept {
        if (acknowledged)
            return true;
        requestStop();
        try {
            QElapsedTimer elapsed;
            elapsed.start();
            do {
                static_cast<void>(sample(last_now));
                if (acknowledged)
                    return true;
                QThread::msleep(1);
            } while (elapsed.elapsed() < 1000);
        } catch (...) {
        }
        requestStop();
        return acknowledged;
    }
};
WindowsYoutubeResolverProcess::WindowsYoutubeResolverProcess(Prepare prepare)
    : impl_(std::make_unique<Impl>(std::move(prepare))) {}
WindowsYoutubeResolverProcess::~WindowsYoutubeResolverProcess() {
    if (impl_ && !impl_->drainForDestruction()) {
        // Contract misuse: best-effort teardown is not an ACK. Retain the
        // already-cancelled process and authenticated runtime lease together,
        // rather than releasing files/profile authority while state is unsure.
        // The owner must preserve its workspace unless it witnessed terminal.
        static_cast<void>(impl_.release());
    }
}
bool WindowsYoutubeResolverProcess::start(const QString& video_id, const qint64 now_ms) {
    auto& value = *impl_;
    if (value.attempted)
        return false;
    value.attempted = true;
    if (now_ms < 0 || !validVideoId(video_id)) {
        value.noNativeFailure(DownloadFailure::InvalidResult);
        return false;
    }
    try {
        auto prepare = std::move(value.prepare);
        if (!prepare) {
            value.noNativeFailure(DownloadFailure::Unavailable);
            return false;
        }
        auto prepared = prepare(owned(video_id));
        if (!prepared || !prepared->lifetime || !prepared->config.environment) {
            value.noNativeFailure(DownloadFailure::Unavailable);
            return false;
        }
        prepared->config = copiedConfiguration(prepared->config);
        value.retained = std::move(prepared);
        value.process = std::make_unique<WindowsDownloadProcess>();
    } catch (...) {
        // No native start has been attempted; this is explicit no-child proof.
        value.noNativeFailure(DownloadFailure::Unavailable);
        return false;
    }
    value.acknowledged = false;
    value.last_now = now_ms;
    value.update = {.state = YoutubeResolverProcessState::Running};
    try {
        const auto accepted = value.process->start(value.retained->config, now_ms);
        value.refused_start = !accepted;
        if (!accepted)
            value.requestStop();
        return accepted;
    } catch (...) {
        value.refused_start = true;
        value.forced_failure = DownloadFailure::Unavailable;
        value.requestStop();
        return false;
    }
}
YoutubeResolverProcessUpdate WindowsYoutubeResolverProcess::poll(const qint64 now_ms) { return impl_->sample(now_ms); }
void WindowsYoutubeResolverProcess::cancel() {
    if (!impl_->attempted) {
        impl_->attempted = true;
        impl_->prepare = {};
        impl_->update = {.state = YoutubeResolverProcessState::Cancelled};
        return;
    }
    impl_->requestStop();
}
} // namespace music_player::m0
