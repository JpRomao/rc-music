#pragma once

#include "adapters/DownloadMediaProfile.hpp"
#include "adapters/WindowsDownloadAppContainer.hpp"

#include <QByteArray>
#include <QString>

#include <functional>
#include <memory>
#include <string>

namespace music_player::m0 {
enum class WindowsMediaProfileCreateStatus { Created, AlreadyExists, Failed };
struct WindowsMediaProfileCreateResult final {
    WindowsMediaProfileCreateStatus status{WindowsMediaProfileCreateStatus::Failed};
    QByteArray sid{};
};
enum class WindowsMediaProfileAbsence { Absent, Present, Unknown };
struct WindowsMediaProfileRemovalResult final {
    bool api_succeeded{false};
    WindowsMediaProfileAbsence absence{WindowsMediaProfileAbsence::Unknown};
};

// Specialized trusted worker port. Created means this exact call received
// S_OK, never an adopted/existing profile. Removal independently proves absence
// after closing handles; API success alone is not that proof. There is no
// system implementation enabled by this declaration or the default player.
class WindowsMediaProfilePlatform {
  public:
    virtual ~WindowsMediaProfilePlatform() = default;
    [[nodiscard]] virtual WindowsMediaProfileCreateResult create(const QString& derived_name) = 0;
    [[nodiscard]] virtual WindowsMediaProfileRemovalResult remove(const QString& derived_name,
                                                                  const QByteArray& live_created_sid) = 0;
};

enum class WindowsMediaProfileOwnerState {
    Idle,
    Ready,
    Draining,
    Removed,
    Abandoned,
    InvalidConfiguration,
    BlockedHistory,
    PersistenceFailed,
    CreationUnconfirmed,
    NativeUseUnconfirmed,
    RemovalUnconfirmed
};
struct WindowsMediaProfileOwnerConfig final {
    std::string job_id{};
    // Trusted test/composition seam; empty uses an internally generated UUIDv4.
    // Called only after healthy startup, never during construction.
    std::function<std::string()> operation_identity{};
};

// Synchronous single-worker owner with bounded reentrancy guards. Borrowed
// ports outlive it. Constructors/destructors perform no port calls or profile
// cleanup; destruction only revokes admission and releases the live lease.
// A loaded operation never recreates live creation/stop authority. Uncertainty
// preserves the document/profile; only explicit persistence retry writes again.
class WindowsMediaProfileOwner final {
  public:
    WindowsMediaProfileOwner(DownloadMediaProfileStore& store, WindowsMediaProfilePlatform& platform,
                             WindowsMediaProfileOwnerConfig config);
    ~WindowsMediaProfileOwner();
    WindowsMediaProfileOwner(const WindowsMediaProfileOwner&) = delete;
    WindowsMediaProfileOwner& operator=(const WindowsMediaProfileOwner&) = delete;
    [[nodiscard]] bool prepare();
    // Closes native admission first. Repeated calls observe draining but do not
    // retry an uncertain system call; close during a port callback is latched.
    [[nodiscard]] bool close();
    [[nodiscard]] bool retryPersistence();
    [[nodiscard]] bool canRetryPersistence() const;
    [[nodiscard]] WindowsMediaProfileOwnerState state() const;
    [[nodiscard]] std::shared_ptr<const WindowsDownloadAppContainer> capability() const;
    // Passive identity check for trusted composition; does not grant use or
    // establish creation/cleanup authority on its own.
    [[nodiscard]] bool usesPlatform(const WindowsMediaProfilePlatform& platform) const noexcept;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace music_player::m0
