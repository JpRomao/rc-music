#include "DirectDownloadSession.hpp"
#include <QScopedValueRollback>

namespace music_player::m0 {
namespace {
bool active(DirectDownloadState state) {
    return state == DirectDownloadState::Resolving || state == DirectDownloadState::Receiving;
}
bool elapsed(qint64 now, qint64 start, qint64 limit) {
    return static_cast<quint64>(now) - static_cast<quint64>(start) >= static_cast<quint64>(limit);
}
} // namespace

DirectDownloadSession::DirectDownloadSession(DirectDownloadIo& io, QIODevice& sink, DirectDownloadLimits limits)
    : io_(io), sink_(sink), limits_(limits) {}
DirectDownloadSession::~DirectDownloadSession() { cancel(); }
bool DirectDownloadSession::start(const QString& url, qint64 now_ms) {
    if (operating_ || snapshot_.state != DirectDownloadState::Idle)
        return false;
    const QScopedValueRollback operation(operating_, true);
    if (limits_.max_body_bytes == 0 || limits_.max_wire_bytes == 0 || limits_.max_redirects < 0 ||
        limits_.total_timeout_ms <= 0 || limits_.idle_timeout_ms <= 0 || limits_.dns_timeout_ms <= 0 ||
        !sink_.isWritable() || sink_.isSequential() || sink_.size() != 0 || sink_.pos() != 0) {
        snapshot_.state = DirectDownloadState::Failed;
        snapshot_.error = DownloadFailure::Unavailable;
        return false;
    }
    target_ = parseDownloadNetworkTarget(url);
    if (!target_) {
        snapshot_.state = DirectDownloadState::Failed;
        snapshot_.error = DownloadFailure::Restricted;
        return false;
    }
    started_ms_ = previous_ms_ = now_ms;
    return beginHop(now_ms);
}
bool DirectDownloadSession::beginHop(qint64 now_ms) {
    snapshot_.state = DirectDownloadState::Resolving;
    activity_ms_ = hop_ms_ = now_ms;
    response_ = std::make_unique<DownloadHttpResponse>(limits_.max_body_bytes);
    try {
        const auto accepted = io_.resolve(target_->host);
        if (!active(snapshot_.state))
            return false;
        if (accepted)
            return true;
    } catch (...) {
    }
    fail(DownloadFailure::Network);
    return false;
}
void DirectDownloadSession::tick(qint64 now_ms) {
    if (operating_ || !active(snapshot_.state))
        return;
    const QScopedValueRollback operation(operating_, true);
    if (now_ms < previous_ms_) {
        fail(DownloadFailure::InvalidResult);
        return;
    }
    previous_ms_ = now_ms;
    if (elapsed(now_ms, started_ms_, limits_.total_timeout_ms) ||
        elapsed(now_ms, activity_ms_, limits_.idle_timeout_ms) ||
        (snapshot_.state == DirectDownloadState::Resolving && elapsed(now_ms, hop_ms_, limits_.dns_timeout_ms))) {
        fail(DownloadFailure::Timeout);
        return;
    }
    try {
        if (snapshot_.state == DirectDownloadState::Resolving) {
            const auto resolution = io_.resolution();
            if (snapshot_.state != DirectDownloadState::Resolving)
                return;
            if (resolution.state == DownloadResolutionState::Pending)
                return;
            if (resolution.state != DownloadResolutionState::Resolved) {
                fail(DownloadFailure::Network);
                return;
            }
            const auto addresses = approveDownloadAddresses(resolution.addresses);
            if (!addresses) {
                fail(DownloadFailure::Restricted);
                return;
            }
            PinnedDownloadEndpoint endpoint;
            endpoint.address = addresses->front();
            endpoint.host = QString::fromLatin1(target_->host_header);
            endpoint.request_target = target_->request_target;
            endpoint.port = target_->https ? 443 : 80;
            endpoint.tls = target_->https;
            snapshot_.state = DirectDownloadState::Receiving;
            activity_ms_ = now_ms;
            const auto accepted = io_.open(endpoint);
            if (!active(snapshot_.state))
                return;
            if (!accepted)
                fail(DownloadFailure::Network);
            return;
        }
        const auto stream = io_.poll();
        if (snapshot_.state != DirectDownloadState::Receiving)
            return;
        if (stream.state == PinnedDownloadStreamState::Failed || stream.state == PinnedDownloadStreamState::Cancelled ||
            stream.state == PinnedDownloadStreamState::Idle) {
            fail(DownloadFailure::Network);
            return;
        }
        if (stream.state == PinnedDownloadStreamState::Connecting)
            return;
        auto bytes = io_.read(pinned_download_stream_buffer_bytes);
        if (snapshot_.state != DirectDownloadState::Receiving)
            return;
        if (bytes.size() > pinned_download_stream_buffer_bytes ||
            static_cast<quint64>(bytes.size()) > limits_.max_wire_bytes - wire_bytes_) {
            fail(DownloadFailure::LimitExceeded);
            return;
        }
        DownloadHttpUpdate update;
        if (!bytes.isEmpty()) {
            wire_bytes_ += static_cast<quint64>(bytes.size());
            activity_ms_ = now_ms;
            update = response_->feed(bytes);
        } else if (stream.state == PinnedDownloadStreamState::Finished) {
            if (!stream.authenticated_eof) {
                fail(DownloadFailure::Network);
                return;
            }
            update = response_->finishEof();
        } else
            return;
        if (update.failed) {
            fail(response_->error() == DownloadHttpError::LimitExceeded ? DownloadFailure::LimitExceeded
                                                                        : DownloadFailure::UnsupportedMedia);
            return;
        }
        if (update.metadataAvailable) {
            const auto status = response_->statusCode();
            if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308) {
                if (redirects_ >= limits_.max_redirects) {
                    fail(DownloadFailure::LimitExceeded);
                    return;
                }
                auto next = resolveDownloadRedirect(*target_, response_->header("location"));
                if (!next) {
                    fail(DownloadFailure::Restricted);
                    return;
                }
                ++redirects_;
                io_.cancel();
                if (!active(snapshot_.state))
                    return;
                target_ = std::move(next);
                static_cast<void>(beginHop(now_ms));
                return;
            }
            if (status != 200) {
                fail(status == 401 || status == 403   ? DownloadFailure::Restricted
                     : status == 404 || status == 410 ? DownloadFailure::Expired
                     : status == 206                  ? DownloadFailure::UnsupportedMedia
                                                      : DownloadFailure::Network);
                return;
            }
            snapshot_.total_bytes = response_->contentLength();
        }
        if (!update.body.isEmpty()) {
            if (!sink_.isWritable()) {
                fail(DownloadFailure::Publication);
                return;
            }
            const auto written = sink_.write(update.body);
            if (!active(snapshot_.state))
                return;
            if (written != update.body.size()) {
                fail(DownloadFailure::Publication);
                return;
            }
            snapshot_.received_bytes += static_cast<quint64>(update.body.size());
        }
        if (update.finished) {
            if (snapshot_.received_bytes == 0) {
                fail(DownloadFailure::UnsupportedMedia);
                return;
            }
            snapshot_.state = DirectDownloadState::Completed;
            io_.cancel();
        }
    } catch (...) {
        fail(DownloadFailure::Network);
    }
}
void DirectDownloadSession::cancel() {
    if (!active(snapshot_.state))
        return;
    snapshot_.state = DirectDownloadState::Cancelled;
    try {
        io_.cancel();
    } catch (...) {
    }
}
DirectDownloadSnapshot DirectDownloadSession::snapshot() const { return snapshot_; }
void DirectDownloadSession::fail(DownloadFailure error) {
    if (snapshot_.state == DirectDownloadState::Cancelled)
        return;
    snapshot_.state = DirectDownloadState::Failed;
    snapshot_.error = error;
    try {
        io_.cancel();
    } catch (...) {
    }
}
} // namespace music_player::m0
