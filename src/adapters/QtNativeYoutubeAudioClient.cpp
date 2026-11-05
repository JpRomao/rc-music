#include "adapters/QtNativeYoutubeAudioClient.hpp"

#include "adapters/NativeYoutubeAudioIpcProtocol.hpp"
#include "adapters/YoutubeAudioCodec.hpp"
#include "adapters/detail/QtNativeSubmissionClientEngine.hpp"

#include <utility>

namespace music_player::m0 {
namespace {
struct YoutubeClientProtocol final {
    using Request = YoutubeAudioRequest;
    static constexpr qsizetype response_max_payload_bytes = native_youtube_audio_ipc::response_max_payload_bytes;
    static QByteArray encode(const Request& request) { return encodeYoutubeAudioRequest(request); }
    static QByteArray preamble(const QString& origin) { return native_youtube_audio_ipc::preamble(origin); }
    static std::optional<DownloadSubmissionResult> decode(const QByteArray& payload, const std::string_view request_id,
                                                          const std::size_t item_count) {
        return decodeYoutubeAudioResponse(payload, request_id, item_count);
    }
};
using YoutubeClientEngine =
    detail::QtNativeSubmissionClientEngine<YoutubeClientProtocol, QtNativeYoutubeAudioClientOptions>;
} // namespace
struct QtNativeYoutubeAudioClient::Impl final : YoutubeClientEngine {
    using YoutubeClientEngine::YoutubeClientEngine;
};
QtNativeYoutubeAudioClient::QtNativeYoutubeAudioClient(NativeHostConfiguration configuration,
                                                       QtNativeYoutubeAudioClientOptions options, QObject* parent)
    : QObject(parent), impl_(std::make_unique<Impl>(*this, std::move(configuration), std::move(options))) {}
QtNativeYoutubeAudioClient::~QtNativeYoutubeAudioClient() { shutdown(); }
DownloadAdmissionResult QtNativeYoutubeAudioClient::begin(const YoutubeAudioRequest& request) {
    return impl_->begin(request);
}
std::optional<DownloadSubmissionResult> QtNativeYoutubeAudioClient::poll(const std::string_view id) {
    return impl_->poll(id);
}
void QtNativeYoutubeAudioClient::shutdown() { impl_->shutdown(); }
bool QtNativeYoutubeAudioClient::stopped() const { return impl_->stopped(); }
} // namespace music_player::m0
