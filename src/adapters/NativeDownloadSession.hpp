#pragma once

#include "NativeMessageFrame.hpp"
#include "m0/ports/AsyncDownloadSubmission.hpp"
#include "m0/ports/DownloadSubmission.hpp"

#include <QString>

#include <cstddef>
#include <string>

namespace music_player::m0 {

enum class NativeSessionStatus { Awaiting, Replied, Closed, Expired };

struct NativeSessionResult final {
    NativeSessionStatus status{NativeSessionStatus::Awaiting};
    QByteArray response_frame{};
};

class NativeDownloadSession final {
  public:
    // Transport metadata and trusted configuration, never fields from the page/request.
    // Single-threaded admission boundary: timeout bounds frame assembly, not a blocking submit.
    NativeDownloadSession(QString caller_origin, QString allowed_origin, DownloadSubmission& submission,
                          qint64 start_ms = 0);
    // Pending observes admission owned elsewhere. Only a completed valid result
    // becomes a v1 reply; session close does not cancel/retry the owned request.
    NativeDownloadSession(QString caller_origin, QString allowed_origin, AsyncDownloadSubmission& submission,
                          qint64 start_ms = 0);
    [[nodiscard]] NativeSessionResult receive(QByteArrayView bytes, qint64 now_ms);
    [[nodiscard]] NativeSessionResult poll(qint64 now_ms);
    [[nodiscard]] NativeSessionResult finishInput(qint64 now_ms);

  private:
    [[nodiscard]] NativeSessionStatus waitingTiming(qint64 now_ms);

    DownloadSubmission* submission_{nullptr};
    AsyncDownloadSubmission* async_submission_{nullptr};
    NativeMessageFrame frame_;
    bool authorized_{false};
    bool terminal_{false};
    bool waiting_{false};
    std::string request_id_{};
    std::size_t expected_item_count_{0};
    qint64 wait_started_ms_{0};
    qint64 wait_last_ms_{0};
};

} // namespace music_player::m0
