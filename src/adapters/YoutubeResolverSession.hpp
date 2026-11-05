#pragma once

#include "YoutubeAudioResolution.hpp"

#include <QByteArray>
#include <memory>
#include <optional>

namespace music_player::m0 {
enum class YoutubeResolverProcessState { Running, Exited, Failed, Cancelled };
struct YoutubeResolverProcessUpdate final {
    YoutubeResolverProcessState state{YoutubeResolverProcessState::Running};
    QByteArray stdout_chunk{}, stderr_chunk{};
    std::optional<int> exit_code{};
    DownloadFailure error{DownloadFailure::None};
};
// Worker-only trusted seam. Every terminal update acknowledges native exit and
// EOF of all owned pipes/children. False/throwing start is uncertain: the owner
// cancels and polls to terminal. No browser-supplied executable or arguments.
class YoutubeResolverProcess {
  public:
    virtual ~YoutubeResolverProcess() = default;
    [[nodiscard]] virtual bool start(const QString& video_id, qint64 now_ms) = 0;
    [[nodiscard]] virtual YoutubeResolverProcessUpdate poll(qint64 now_ms) = 0;
    virtual void cancel() = 0;
};
struct YoutubeResolverLimits final {
    quint64 max_stdout_bytes{1024 * 1024};
    quint64 max_stderr_bytes{128 * 1024};
    qint64 total_timeout_ms{60 * 1000};
    qint64 minimum_remaining_seconds{30};
};
enum class YoutubeResolverState { Idle, Resolving, Stopping, Ready, Failed, Cancelled };
struct YoutubeResolverSnapshot final {
    YoutubeResolverState state{YoutubeResolverState::Idle};
    DownloadFailure error{DownloadFailure::None};
    std::shared_ptr<const ResolvedYoutubeAudioCapability> audio{};
};
// One-shot worker session. JSON alone cannot mint acquisition authority: a
// matching successful process + stopped pipes must be recognized first.
// UTC seconds are explicitly supplied for expiry; monotonic ms bound duration.
// The owner must drain to terminal before destroying this object/process.
class YoutubeResolverSession final {
  public:
    explicit YoutubeResolverSession(YoutubeResolverProcess& process, YoutubeResolverLimits limits = {});
    ~YoutubeResolverSession();
    [[nodiscard]] bool start(std::string job_id, QString video_id, qint64 now_ms, qint64 utc_seconds);
    [[nodiscard]] YoutubeResolverSnapshot poll(qint64 now_ms, qint64 utc_seconds);
    void cancel();
    [[nodiscard]] YoutubeResolverSnapshot snapshot() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace music_player::m0
