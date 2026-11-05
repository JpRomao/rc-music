#include "adapters/QtNativeDownloadServer.hpp"

#include "adapters/NativeDownloadIpcProtocol.hpp"
#include "adapters/NativeDownloadSession.hpp"
#include "adapters/detail/QtNativeSubmissionServerEngine.hpp"

#include <utility>

namespace music_player::m0 {
namespace {
struct Protocol final {
    using Request = DownloadRequest;
    using Submission = AsyncDownloadSubmission;
    using Session = NativeDownloadSession;
    using Options = QtNativeDownloadServerOptions;
    static constexpr qsizetype max_payload_bytes = native_message_max_payload_bytes;
    static constexpr qsizetype preamble_bytes = native_download_ipc::preamble_bytes;
    static constexpr qsizetype response_max_payload_bytes = native_download_ipc::response_max_payload_bytes;
    static QByteArray preamble(const QString& origin) { return native_download_ipc::preamble(origin); }
};
} // namespace
struct QtNativeDownloadServer::Impl final : native_submission_server_detail::QtNativeSubmissionServerEngine<Protocol> {
    using QtNativeSubmissionServerEngine::QtNativeSubmissionServerEngine;
};
QtNativeDownloadServer::QtNativeDownloadServer(NativeHostConfiguration configuration,
                                               AsyncDownloadSubmission& submission,
                                               QtNativeDownloadServerOptions options, QObject* parent)
    : QObject(parent), impl_(std::make_unique<Impl>(*this, std::move(configuration), submission, std::move(options))) {}
QtNativeDownloadServer::~QtNativeDownloadServer() { impl_->shutdown(); }
bool QtNativeDownloadServer::start() { return impl_->start(); }
void QtNativeDownloadServer::poll() { impl_->poll(); }
void QtNativeDownloadServer::beginShutdown() { impl_->beginShutdown(); }
bool QtNativeDownloadServer::stopped() const { return impl_->stopped(); }
qsizetype QtNativeDownloadServer::activeConnectionCount() const { return impl_->activeConnectionCount(); }
} // namespace music_player::m0
