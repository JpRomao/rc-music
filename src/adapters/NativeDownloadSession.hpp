#pragma once

#include "NativeMessageFrame.hpp"
#include "m0/ports/DownloadSubmission.hpp"

#include <QString>

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
    [[nodiscard]] NativeSessionResult receive(QByteArrayView bytes, qint64 now_ms);
    [[nodiscard]] NativeSessionResult poll(qint64 now_ms);
    [[nodiscard]] NativeSessionResult finishInput(qint64 now_ms);

  private:
    DownloadSubmission& submission_;
    NativeMessageFrame frame_;
    bool authorized_{false};
    bool terminal_{false};
};

} // namespace music_player::m0
