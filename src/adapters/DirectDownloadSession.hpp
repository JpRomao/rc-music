#pragma once

#include "DownloadHttpResponse.hpp"
#include "DownloadNetworkPolicy.hpp"
#include "PinnedDownloadStream.hpp"
#include "m0/ports/DownloadExecution.hpp"

#include <QIODevice>
#include <functional>
#include <memory>

namespace music_player::m0 {

enum class DownloadResolutionState { Pending, Resolved, Failed };
struct DownloadResolution final {
    DownloadResolutionState state{DownloadResolutionState::Pending};
    QList<QHostAddress> addresses{};
};

struct DirectDownloadIoStopUpdate final {
    bool stopped{false};
    DownloadFailure error{DownloadFailure::None};
};

// Trusted adapter seam. Neither endpoints nor resolver answers come directly from a page.
// Methods return promptly; the owning event loop advances DNS and socket I/O between ticks.
class DirectDownloadIo {
  public:
    virtual ~DirectDownloadIo() = default;
    virtual bool resolve(const QString& host) = 0;
    virtual DownloadResolution resolution() const = 0;
    virtual bool open(const PinnedDownloadEndpoint& endpoint) = 0;
    virtual PinnedDownloadStreamUpdate poll() const = 0;
    virtual QByteArray read(qint64 maximum) = 0;
    // Last typed failure already observed by resolve/open/resolution/poll/read.
    // This getter must not advance IO. Read it before cancel may reset state.
    virtual DownloadFailure failure() const = 0;
    virtual void cancel() = 0;
    // Poll only after cancel(). stopped acknowledges all owned native work,
    // including resolver/process termination and completion of every pipe.
    // An error or exception alone does not acknowledge stopping. No default
    // success: callback invalidation/abortHostLookup is not native DNS proof.
    virtual DirectDownloadIoStopUpdate pollStop() = 0;
};

enum class DirectDownloadState { Idle, Resolving, Receiving, Completed, Failed, Cancelled };
struct DirectDownloadSnapshot final {
    DirectDownloadState state{DirectDownloadState::Idle};
    DownloadFailure error{DownloadFailure::None};
    quint64 received_bytes{0};
    std::optional<quint64> total_bytes{};
};
enum class DirectDownloadRestriction { Target, Dns, Redirect, Http401, Http403, Io };
struct DirectDownloadLimits final {
    quint64 max_body_bytes{512 * 1024 * 1024};
    quint64 max_wire_bytes{514 * 1024 * 1024};
    int max_redirects{5};
    qint64 total_timeout_ms{30 * 60 * 1000};
    qint64 idle_timeout_ms{15 * 1000};
    qint64 dns_timeout_ms{10 * 1000};
    // Trusted, optional observer: closed code only, no remote data or text.
    // Must not reenter or destroy the session. Exceptions are contained.
    std::function<void(DirectDownloadRestriction)> on_restricted{};
    // Trusted composition only; pages cannot enable resolver-header forwarding.
    bool forward_youtube_user_agent{false};
    // Inclusion attempted at open(), not proof of server receipt. Same observer
    // lifetime/reentrancy restrictions as on_restricted; exceptions are contained.
    std::function<void(bool)> on_request_user_agent{};
};

// Caller-driven transport session, NOT a published/validated MP3. Sink is a fresh empty
// job-owned device; its owner closes/flushes/removes partials and validates media afterwards.
// Run on a worker: sink writes may block. No timers, processes, paths or playback in this class.
class DirectDownloadSession final {
  public:
    DirectDownloadSession(DirectDownloadIo& io, QIODevice& sink, DirectDownloadLimits limits = {});
    ~DirectDownloadSession();
    bool start(const QString& url, qint64 now_ms);
    // Resolver capability is never minted from a page flag. UTC epoch is
    // explicitly supplied by trusted composition; ticks remain monotonic.
    bool startResolvedYoutube(std::shared_ptr<const ResolvedYoutubeAudioCapability> capability, qint64 now_ms,
                              qint64 trusted_epoch_seconds);
    void tick(qint64 now_ms);
    void cancel();
    DirectDownloadSnapshot snapshot() const;

  private:
    bool startImpl(const QString& url, qint64 now_ms, std::shared_ptr<const ResolvedYoutubeAudioCapability> capability,
                   qint64 trusted_epoch_seconds);
    DownloadFailure ioFailure() const noexcept;
    void fail(DownloadFailure error, DirectDownloadRestriction reason = DirectDownloadRestriction::Io);
    void observeRestriction(DirectDownloadRestriction reason) noexcept;
    bool beginHop(qint64 now_ms);
    DirectDownloadIo& io_;
    QIODevice& sink_;
    DirectDownloadLimits limits_;
    DirectDownloadSnapshot snapshot_{};
    std::optional<DownloadNetworkTarget> target_{};
    std::shared_ptr<const ResolvedYoutubeAudioCapability> youtube_capability_{};
    qint64 youtube_epoch_start_{};
    qint64 youtube_cap_budget_ms_{};
    std::optional<qint64> youtube_target_budget_ms_{};
    std::unique_ptr<DownloadHttpResponse> response_{};
    qint64 started_ms_{0};
    qint64 activity_ms_{0};
    qint64 previous_ms_{0};
    qint64 hop_ms_{0};
    quint64 wire_bytes_{0};
    int redirects_{0};
    bool operating_{false};
    bool restriction_observed_{false};
};

} // namespace music_player::m0
