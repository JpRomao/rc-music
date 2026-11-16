#pragma once

#include "m0/domain/LocalLibrary.hpp"

#include <QObject>
#include <QString>

#include <filesystem>
#include <memory>
#include <vector>

namespace music_player::m0 {
class LibraryStore;

// ADR-0019: owns the permanent "Minha música" catalog for the UI thread. Store
// I/O runs on a worker. Admissions arrive at any time; the confirmed catalog
// only advances when the store reports Saved, and later admissions are saved
// in a follow-up write. A failed save keeps the change pending without
// blocking playback or exit.
class LibraryBinding final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool ready READ ready NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(int count READ count NOTIFY changed)
    Q_PROPERTY(bool hasPendingChange READ hasPendingChange NOTIFY changed)
    Q_PROPERTY(bool canRetrySave READ canRetrySave NOTIFY changed)
    Q_PROPERTY(QString statusText READ statusText NOTIFY changed)
  public:
    using Paths = std::vector<std::filesystem::path>;
    // Passive construction; start() begins loading.
    explicit LibraryBinding(std::shared_ptr<LibraryStore> store, QObject* parent = nullptr);
    ~LibraryBinding() override;
    [[nodiscard]] bool start();
    // Explicit local ingress (open, add, drop, forwarding, playlist). Idempotent by exact path.
    void admit(Paths paths);
    // First-use seed from existing queue and playlist references. Ignored once
    // library.json exists, so a removed record never comes back this way.
    void seed(Paths paths);
    [[nodiscard]] bool ready() const;
    [[nodiscard]] bool busy() const;
    // Confirmed (Saved) records only.
    [[nodiscard]] int count() const;
    [[nodiscard]] bool hasPendingChange() const;
    [[nodiscard]] bool canRetrySave() const;
    [[nodiscard]] QString statusText() const;
    [[nodiscard]] LibrarySnapshot confirmedSnapshot() const;
    // True while a write is running or queued; a failed save does not hold exit.
    [[nodiscard]] bool savingForExit() const;
    Q_INVOKABLE bool retrySave();
    Q_INVOKABLE bool retryLoad();
    void beginShutdown();
  signals:
    void changed();

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace music_player::m0
