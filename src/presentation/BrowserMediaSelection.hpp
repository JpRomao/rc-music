#pragma once

#include "adapters/BrowserMediaSnapshotCodec.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace music_player::m0 {
enum class BrowserMediaSelectionState {
    Unavailable,
    Idle,
    Detecting,
    Ready,
    Confirming,
    Confirmed,
    InvalidSnapshot,
    SourceChanged,
    Failed,
    TimedOut
};

// Single-threaded presentation state. Only the host supplies navigation events
// and operation tokens; script results contain untrusted data, never authority.
// The native adapter separately bounds outstanding ExecuteScript calls.
class BrowserMediaSelection final {
  public:
    void navigationChanged(QString trusted_page_url = {});
    [[nodiscard]] std::optional<std::uint64_t> beginDetection();
    [[nodiscard]] std::optional<std::uint64_t> beginConfirmation();
    [[nodiscard]] bool complete(std::uint64_t operation, const QByteArray& payload);
    [[nodiscard]] bool fail(std::uint64_t operation);
    [[nodiscard]] bool timeout(std::uint64_t operation);
    [[nodiscard]] bool setSelected(std::size_t index, bool selected);
    [[nodiscard]] bool canDetect() const;
    [[nodiscard]] bool canConfirm() const;
    [[nodiscard]] BrowserMediaSelectionState state() const;
    [[nodiscard]] const QString& pageUrl() const;
    [[nodiscard]] const std::optional<BrowserMediaSnapshot>& snapshot() const;
    [[nodiscard]] const std::vector<std::size_t>& selectedIndices() const;
    // A fresh confirmation can be consumed once. This does not submit a job or
    // authorize I/O; a trusted composition must perform any later admission.
    [[nodiscard]] std::vector<DownloadItem> takeConfirmedItems();

  private:
    enum class Purpose { Detection, Confirmation };
    struct Pending final {
        std::uint64_t operation;
        Purpose purpose;
    };
    [[nodiscard]] std::optional<std::uint64_t> begin(Purpose purpose);
    [[nodiscard]] bool finishFailure(std::uint64_t operation, BrowserMediaSelectionState state);
    void clearData();

    QString page_url_{};
    std::optional<BrowserMediaSnapshot> snapshot_{};
    std::vector<std::size_t> selected_{};
    std::vector<DownloadItem> confirmed_{};
    std::optional<Pending> pending_{};
    std::uint64_t serial_{0};
    BrowserMediaSelectionState state_{BrowserMediaSelectionState::Unavailable};
};
} // namespace music_player::m0
