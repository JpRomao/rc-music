#include "DirectDownloadSession.hpp"
#include "YoutubeAudioResolution.hpp"
#include <QScopedValueRollback>

#include <algorithm>
#include <limits>
#include <utility>

namespace music_player::m0 {
namespace {
bool active(DirectDownloadState state) {
    return state == DirectDownloadState::Resolving || state == DirectDownloadState::Receiving;
}
bool elapsed(qint64 now, qint64 start, qint64 limit) {
    return static_cast<quint64>(now) - static_cast<quint64>(start) >= static_cast<quint64>(limit);
}
std::optional<qint64> expiryBudget(const qint64 expires_at, const qint64 epoch_start) {
    if (epoch_start < 0 || expires_at <= epoch_start)
        return {};
    const auto seconds = expires_at - epoch_start;
    return seconds > std::numeric_limits<qint64>::max() / 1000 ? std::numeric_limits<qint64>::max() : seconds * 1000;
}
struct ContentRange final {
    quint64 first{0};
    quint64 last{0};
    quint64 total{0};
};
std::optional<quint64> decimal(QByteArrayView text) {
    if (text.isEmpty() || text.size() > 19)
        return {};
    quint64 value = 0;
    for (const char c : text) {
        if (c < '0' || c > '9')
            return {};
        value = value * 10 + static_cast<quint64>(c - '0');
    }
    return value;
}
// Strict `bytes first-last/total`; an unknown total ("*") cannot be planned.
std::optional<ContentRange> contentRange(const QByteArray& header) {
    if (!header.startsWith("bytes "))
        return {};
    const auto spec = QByteArrayView{header}.sliced(6);
    const auto dash = spec.indexOf('-');
    const auto slash = spec.indexOf('/');
    if (dash <= 0 || slash <= dash + 1)
        return {};
    const auto first = decimal(spec.first(dash));
    const auto last = decimal(spec.sliced(dash + 1, slash - dash - 1));
    const auto total = decimal(spec.sliced(slash + 1));
    if (!first || !last || !total || *first > *last || *last >= *total)
        return {};
    return ContentRange{*first, *last, *total};
}
} // namespace

DirectDownloadSession::DirectDownloadSession(DirectDownloadIo& io, QIODevice& sink, DirectDownloadLimits limits)
    : io_(io), sink_(sink), limits_(limits) {}
DirectDownloadSession::~DirectDownloadSession() { cancel(); }
bool DirectDownloadSession::startResolvedYoutube(std::shared_ptr<const ResolvedYoutubeAudioCapability> capability,
                                                 const qint64 now_ms, const qint64 trusted_epoch_seconds) {
    return startImpl({}, now_ms, std::move(capability), trusted_epoch_seconds);
}
bool DirectDownloadSession::start(const QString& url, qint64 now_ms) { return startImpl(url, now_ms, {}, 0); }
bool DirectDownloadSession::startImpl(const QString& url, const qint64 now_ms,
                                      std::shared_ptr<const ResolvedYoutubeAudioCapability> capability,
                                      const qint64 trusted_epoch_seconds) {
    if (operating_ || snapshot_.state != DirectDownloadState::Idle)
        return false;
    const QScopedValueRollback operation(operating_, true);
    if (limits_.max_body_bytes == 0 || limits_.max_wire_bytes == 0 || limits_.max_redirects < 0 ||
        limits_.total_timeout_ms <= 0 || limits_.idle_timeout_ms <= 0 || limits_.dns_timeout_ms <= 0 ||
        limits_.max_resumes < 0 || !sink_.isWritable() || sink_.isSequential() || sink_.size() != 0 ||
        sink_.pos() != 0) {
        snapshot_.state = DirectDownloadState::Failed;
        snapshot_.error = DownloadFailure::Unavailable;
        return false;
    }
    if (capability && (now_ms < 0 || trusted_epoch_seconds < 0)) {
        snapshot_.state = DirectDownloadState::Failed;
        snapshot_.error = DownloadFailure::InvalidResult;
        return false;
    }
    target_ = capability ? parseResolvedYoutubeNetworkTarget(*capability) : parseDownloadNetworkTarget(url);
    if (!target_) {
        snapshot_.state = DirectDownloadState::Failed;
        snapshot_.error = DownloadFailure::Restricted;
        observeRestriction(DirectDownloadRestriction::Target);
        return false;
    }
    if (capability) {
        const auto budget = expiryBudget(capability->expiresAtSeconds(), trusted_epoch_seconds);
        if (!budget) {
            snapshot_.state = DirectDownloadState::Failed;
            snapshot_.error = DownloadFailure::Expired;
            return false;
        }
        youtube_capability_ = std::move(capability);
        youtube_epoch_start_ = trusted_epoch_seconds;
        youtube_cap_budget_ms_ = *budget;
        ranged_ = limits_.youtube_range_bytes > 0;
    }
    started_ms_ = previous_ms_ = now_ms;
    return beginHop(now_ms);
}
bool DirectDownloadSession::beginHop(qint64 now_ms) {
    if (youtube_capability_) {
        const auto expires_at = youtubeAudioStreamExpiry(target_->url);
        const auto budget = expires_at ? expiryBudget(*expires_at, youtube_epoch_start_) : std::nullopt;
        if (!budget || elapsed(now_ms, started_ms_, std::min(youtube_cap_budget_ms_, *budget))) {
            fail(DownloadFailure::Expired);
            return false;
        }
        youtube_target_budget_ms_ = std::min(youtube_cap_budget_ms_, *budget);
    }
    snapshot_.state = DirectDownloadState::Resolving;
    activity_ms_ = hop_ms_ = now_ms;
    response_ = std::make_unique<DownloadHttpResponse>(limits_.max_body_bytes);
    partial_hop_ = false;
    hop_body_bytes_ = hop_expected_bytes_ = 0;
    if (ranged_) {
        // Each block starts at the last written byte, so a resume never repeats or skips audio.
        const auto first = snapshot_.received_bytes;
        auto last = first + std::min(limits_.youtube_range_bytes - 1, std::numeric_limits<quint64>::max() - first);
        if (range_total_)
            last = std::min(last, *range_total_ - 1);
        request_range_ = PinnedDownloadByteRange{first, last};
    }
    try {
        const auto accepted = io_.resolve(target_->host);
        if (!active(snapshot_.state))
            return false;
        if (accepted)
            return true;
    } catch (...) {
    }
    fail(ioFailure());
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
    if (youtube_target_budget_ms_ && elapsed(now_ms, started_ms_, *youtube_target_budget_ms_)) {
        fail(DownloadFailure::Expired);
        return;
    }
    if (elapsed(now_ms, started_ms_, limits_.total_timeout_ms)) {
        fail(DownloadFailure::Timeout);
        return;
    }
    if (elapsed(now_ms, activity_ms_, limits_.idle_timeout_ms) ||
        (snapshot_.state == DirectDownloadState::Resolving && elapsed(now_ms, hop_ms_, limits_.dns_timeout_ms))) {
        // Only a stalled transfer resumes; a DNS answer that never comes is not retried.
        if (snapshot_.state != DirectDownloadState::Receiving || !resume(DownloadFailure::Timeout, now_ms))
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
                fail(ioFailure());
                return;
            }
            const auto addresses = approveDownloadAddresses(resolution.addresses);
            if (!addresses) {
                fail(DownloadFailure::Restricted, DirectDownloadRestriction::Dns);
                return;
            }
            PinnedDownloadEndpoint endpoint;
            endpoint.address = addresses->front();
            endpoint.host = QString::fromLatin1(target_->host_header);
            endpoint.request_target = target_->request_target;
            endpoint.port = target_->https ? 443 : 80;
            endpoint.tls = target_->https;
            if (limits_.forward_youtube_user_agent && youtube_capability_)
                endpoint.user_agent = youtube_capability_->data().user_agent_hint;
            endpoint.byte_range = request_range_;
            snapshot_.state = DirectDownloadState::Receiving;
            activity_ms_ = now_ms;
            if (limits_.on_request_user_agent) {
                try {
                    limits_.on_request_user_agent(!endpoint.user_agent.isEmpty());
                } catch (...) {
                    // Diagnostic observers cannot change transport outcomes.
                }
            }
            if (snapshot_.state != DirectDownloadState::Receiving)
                return;
            const auto accepted = io_.open(endpoint);
            if (!active(snapshot_.state))
                return;
            if (!accepted)
                fail(ioFailure());
            return;
        }
        const auto stream = io_.poll();
        if (snapshot_.state != DirectDownloadState::Receiving)
            return;
        if (stream.state == PinnedDownloadStreamState::Failed || stream.state == PinnedDownloadStreamState::Cancelled ||
            stream.state == PinnedDownloadStreamState::Idle) {
            const auto error = ioFailure();
            if (!resume(error, now_ms))
                fail(error);
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
                if (!resume(DownloadFailure::Network, now_ms))
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
                auto next = youtube_capability_ ? resolveYoutubeAudioRedirect(*target_, response_->header("location"),
                                                                              *youtube_capability_)
                                                : resolveDownloadRedirect(*target_, response_->header("location"));
                if (!next) {
                    fail(DownloadFailure::Restricted, DirectDownloadRestriction::Redirect);
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
            if (status == 206 && request_range_) {
                if (!acceptPartialContent()) {
                    fail(DownloadFailure::UnsupportedMedia);
                    return;
                }
            } else if (status == 200 && request_range_ && snapshot_.received_bytes != 0) {
                // The whole file again cannot be spliced after bytes already written.
                fail(DownloadFailure::UnsupportedMedia);
                return;
            } else if (status != 200) {
                if (status == 401 || status == 403)
                    fail(DownloadFailure::SourceRejected,
                         status == 401 ? DirectDownloadRestriction::Http401 : DirectDownloadRestriction::Http403);
                else
                    fail(status == 404 || status == 410 ? DownloadFailure::Expired
                         : status == 206                ? DownloadFailure::UnsupportedMedia
                                                        : DownloadFailure::Network);
                return;
            } else
                snapshot_.total_bytes = response_->contentLength();
        }
        if (!update.body.isEmpty()) {
            if (partial_hop_ && static_cast<quint64>(update.body.size()) > hop_expected_bytes_ - hop_body_bytes_) {
                fail(DownloadFailure::UnsupportedMedia);
                return;
            }
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
            hop_body_bytes_ += static_cast<quint64>(update.body.size());
        }
        if (update.finished && partial_hop_) {
            if (hop_body_bytes_ != hop_expected_bytes_) {
                fail(DownloadFailure::UnsupportedMedia);
                return;
            }
            if (snapshot_.received_bytes == *range_total_) {
                snapshot_.state = DirectDownloadState::Completed;
                io_.cancel();
                return;
            }
            io_.cancel();
            if (!active(snapshot_.state))
                return;
            static_cast<void>(beginHop(now_ms));
            return;
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
        fail(ioFailure());
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
bool DirectDownloadSession::acceptPartialContent() {
    const auto range = contentRange(response_->header("content-range"));
    if (!range || range->first != request_range_->first || range->last > request_range_->last ||
        range->total > limits_.max_body_bytes || (range_total_ && *range_total_ != range->total))
        return false;
    const auto expected = range->last - range->first + 1;
    if (const auto length = response_->contentLength(); length && *length != expected)
        return false;
    range_total_ = range->total;
    snapshot_.total_bytes = range->total;
    partial_hop_ = true;
    hop_expected_bytes_ = expected;
    return true;
}
bool DirectDownloadSession::resume(DownloadFailure error, qint64 now_ms) {
    if (!ranged_ || resumes_ >= limits_.max_resumes ||
        (error != DownloadFailure::Network && error != DownloadFailure::Timeout))
        return false;
    ++resumes_;
    try {
        io_.cancel();
    } catch (...) {
        return false;
    }
    if (active(snapshot_.state))
        static_cast<void>(beginHop(now_ms));
    return true;
}
DownloadFailure DirectDownloadSession::ioFailure() const noexcept {
    // Capture the adapter's already observed error before fail() cancels it.
    // Missing reports keep the conservative Network fallback; unknown values
    // cannot escape the closed public failure contract.
    try {
        const auto error = io_.failure();
        switch (error) {
        case DownloadFailure::None:
            return DownloadFailure::Network;
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
        case DownloadFailure::SourceRejected:
            return error;
        default:
            return DownloadFailure::InvalidResult;
        }
    } catch (...) {
        return DownloadFailure::Network;
    }
}
void DirectDownloadSession::observeRestriction(DirectDownloadRestriction reason) noexcept {
    if (restriction_observed_)
        return;
    restriction_observed_ = true;
    if (limits_.on_restricted) {
        try {
            limits_.on_restricted(reason);
        } catch (...) {
            // Observability never replaces the original failure or stop policy.
        }
    }
}
void DirectDownloadSession::fail(DownloadFailure error, DirectDownloadRestriction reason) {
    if (snapshot_.state == DirectDownloadState::Cancelled)
        return;
    snapshot_.state = DirectDownloadState::Failed;
    snapshot_.error = error;
    try {
        io_.cancel();
    } catch (...) {
    }
    if (error == DownloadFailure::Restricted || error == DownloadFailure::SourceRejected)
        observeRestriction(reason);
}
} // namespace music_player::m0
