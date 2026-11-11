#pragma once

#include "m0/ports/DownloadSubmission.hpp"

#include <QByteArray>
#include <QString>
#include <QWidget>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>

namespace music_player::m0 {
class AsyncDownloadSubmission;
// Native controls own detection and confirmation. A page supplies data only and
// never receives a QObject, selection command or download capability.
class BrowserMediaPanel final : public QWidget {
    Q_OBJECT

  public:
    using ProbeHandler = std::function<void(std::uint64_t)>;

    explicit BrowserMediaPanel(QWidget* parent = nullptr);
    ~BrowserMediaPanel() override;
    void setProbeHandler(ProbeHandler handler);
    // UI thread only. The owner outlives this panel or revokes the borrowed port.
    void setDownloadSubmission(AsyncDownloadSubmission* submission);
    void setPage(QString trusted_url);
    void resetEngine();
    void completeProbe(std::uint64_t operation, const QByteArray& payload);
    void failProbe(std::uint64_t operation);
    void expireProbe(std::uint64_t operation);
    [[nodiscard]] std::optional<std::uint64_t> pendingProbe() const;

  private:
    enum class ProbePurpose { Detection, Confirmation, Download };
    struct Impl;
    std::unique_ptr<Impl> impl_;
    void startProbe(ProbePurpose purpose);
    void beginAdmission(std::vector<DownloadItem> items);
    void pollAdmission();
    void finishAdmission(const DownloadSubmissionResult& result);
    void refresh();
};
} // namespace music_player::m0
