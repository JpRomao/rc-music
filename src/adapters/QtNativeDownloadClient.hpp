#pragma once

#include "adapters/NativeHostLaunch.hpp"
#include "m0/ports/AsyncDownloadSubmission.hpp"

#include <QObject>

#include <functional>
#include <memory>

namespace music_player::m0 {
struct QtNativeDownloadClientOptions final {
    // Prompt clock observation; never wait or perform I/O. Synchronous object
    // destruction from this callback is outside the borrowed lifetime contract.
    std::function<qint64()> now_ms{};
};

// One prompt native connection attempt/request, with no busy wait, retry or app
// launch. After bytes may have been sent, malformed/disconnected/timed-out
// exchanges throw on observation; they cannot declare an uncertain request rejected.
class QtNativeDownloadClient final : public QObject, public AsyncDownloadSubmission {
    Q_OBJECT
  public:
    explicit QtNativeDownloadClient(NativeHostConfiguration configuration, QtNativeDownloadClientOptions options = {},
                                    QObject* parent = nullptr);
    ~QtNativeDownloadClient() override;
    [[nodiscard]] DownloadAdmissionResult begin(const DownloadRequest& request) override;
    [[nodiscard]] std::optional<DownloadSubmissionResult> poll(std::string_view request_id) override;
    void shutdown();
    [[nodiscard]] bool stopped() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace music_player::m0
