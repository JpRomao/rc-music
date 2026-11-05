#include "adapters/QtNativeYoutubeAudioServer.hpp"

#include "adapters/NativeYoutubeAudioIpcProtocol.hpp"
#include "adapters/NativeYoutubeAudioSession.hpp"
#include "adapters/YoutubeAudioCodec.hpp"
#include "adapters/detail/QtNativeSubmissionServerEngine.hpp"

#include <utility>

namespace music_player::m0 {
namespace {
struct Protocol final {
    using Request = YoutubeAudioRequest;
    using Submission = AsyncYoutubeAudioSubmission;
    using Session = NativeYoutubeAudioSession;
    using Options = QtNativeYoutubeAudioServerOptions;
    static constexpr qsizetype max_payload_bytes = static_cast<qsizetype>(youtube_audio_max_payload_bytes);
    static constexpr qsizetype preamble_bytes = native_youtube_audio_ipc::preamble_bytes;
    static constexpr qsizetype response_max_payload_bytes = native_youtube_audio_ipc::response_max_payload_bytes;
    static QByteArray preamble(const QString& origin) { return native_youtube_audio_ipc::preamble(origin); }
};
} // namespace
struct QtNativeYoutubeAudioServer::Impl final
    : native_submission_server_detail::QtNativeSubmissionServerEngine<Protocol> {
    using QtNativeSubmissionServerEngine::QtNativeSubmissionServerEngine;
};
QtNativeYoutubeAudioServer::QtNativeYoutubeAudioServer(NativeHostConfiguration configuration,
                                                       AsyncYoutubeAudioSubmission& submission,
                                                       QtNativeYoutubeAudioServerOptions options, QObject* parent)
    : QObject(parent), impl_(std::make_unique<Impl>(*this, std::move(configuration), submission, std::move(options))) {}
QtNativeYoutubeAudioServer::~QtNativeYoutubeAudioServer() { impl_->shutdown(); }
bool QtNativeYoutubeAudioServer::start() { return impl_->start(); }
void QtNativeYoutubeAudioServer::poll() { impl_->poll(); }
void QtNativeYoutubeAudioServer::beginShutdown() { impl_->beginShutdown(); }
bool QtNativeYoutubeAudioServer::stopped() const { return impl_->stopped(); }
qsizetype QtNativeYoutubeAudioServer::activeConnectionCount() const { return impl_->activeConnectionCount(); }
} // namespace music_player::m0
