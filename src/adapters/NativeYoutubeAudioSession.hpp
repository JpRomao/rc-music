#pragma once

#include "NativeDownloadSession.hpp"
#include "m0/ports/AsyncYoutubeAudioSubmission.hpp"

#include <memory>

namespace music_player::m0 {
// A separate, one-request observer for trusted YouTube Native Messaging.
// Origins are supplied by transport/configuration, never the JSON request.
// Pending has no wire ACK; closure/EOF/timeout only close this observer and
// never cancel, retransmit or steal ownership of an admitted batch.
class NativeYoutubeAudioSession final {
  public:
    NativeYoutubeAudioSession(QString caller_origin, QString allowed_origin, AsyncYoutubeAudioSubmission& submission,
                              qint64 start_ms = 0);
    ~NativeYoutubeAudioSession();
    [[nodiscard]] NativeSessionResult receive(QByteArrayView bytes, qint64 now_ms);
    [[nodiscard]] NativeSessionResult poll(qint64 now_ms);
    [[nodiscard]] NativeSessionResult finishInput(qint64 now_ms);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace music_player::m0
