#include "adapters/QtNativeDownloadClient.hpp"

#include "adapters/NativeDownloadCodec.hpp"
#include "adapters/NativeDownloadIpcProtocol.hpp"
#include "adapters/detail/QtNativeSubmissionClientEngine.hpp"

#include <utility>

namespace music_player::m0 {
namespace {
struct DownloadClientProtocol final {
    using Request = DownloadRequest;
    static constexpr qsizetype response_max_payload_bytes = native_download_ipc::response_max_payload_bytes;
    static QByteArray encode(const Request& request) { return encodeNativeDownloadRequest(request); }
    static QByteArray preamble(const QString& origin) { return native_download_ipc::preamble(origin); }
    static std::optional<DownloadSubmissionResult> decode(const QByteArray& payload, const std::string_view request_id,
                                                          const std::size_t item_count) {
        return decodeNativeDownloadResponse(payload, request_id, item_count);
    }
};
using DownloadClientEngine =
    detail::QtNativeSubmissionClientEngine<DownloadClientProtocol, QtNativeDownloadClientOptions>;
} // namespace
struct QtNativeDownloadClient::Impl final : DownloadClientEngine {
    using DownloadClientEngine::DownloadClientEngine;
};
QtNativeDownloadClient::QtNativeDownloadClient(NativeHostConfiguration configuration,
                                               QtNativeDownloadClientOptions options, QObject* parent)
    : QObject(parent), impl_(std::make_unique<Impl>(*this, std::move(configuration), std::move(options))) {}
QtNativeDownloadClient::~QtNativeDownloadClient() { shutdown(); }
DownloadAdmissionResult QtNativeDownloadClient::begin(const DownloadRequest& request) { return impl_->begin(request); }
std::optional<DownloadSubmissionResult> QtNativeDownloadClient::poll(const std::string_view id) {
    return impl_->poll(id);
}
void QtNativeDownloadClient::shutdown() { impl_->shutdown(); }
bool QtNativeDownloadClient::stopped() const { return impl_->stopped(); }
} // namespace music_player::m0
