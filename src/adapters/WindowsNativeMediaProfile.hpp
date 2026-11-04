#pragma once

#include "adapters/WindowsMediaProfileOwner.hpp"

#include <functional>
#include <memory>
#include <optional>

namespace music_player::m0 {

// One native create attempt. No existing profile is adopted and destruction
// never calls a profile API. Construct and use on one trusted worker only.
class WindowsNativeMediaProfile final : public WindowsMediaProfilePlatform {
  public:
    struct Result final {
        qint32 hresult{-1};
        QByteArray sid{};
        bool sid_released{true};
    };
    struct Calls final {
        std::function<Result(const QString&)> create{};
        std::function<qint32(const QString&)> remove{};
        std::function<WindowsMediaProfileAbsence(const QString&, const QByteArray&)> observe{};
    };
    struct Report final {
        QString name{};
        QByteArray sid{};
        bool create_attempted{false};
        // Records the S_OK event, including subsequent SID/release failure.
        // False after an exception is unconfirmed, never evidence of absence.
        bool created{false};
        bool remove_attempted{false};
        bool sid_released{true};
        std::optional<qint32> create_hresult{};
        std::optional<qint32> remove_hresult{};
        WindowsMediaProfileAbsence absence{WindowsMediaProfileAbsence::Unknown};
    };

    // Missing callbacks fail closed; they never fall back to system calls.
    explicit WindowsNativeMediaProfile(Calls calls);
    ~WindowsNativeMediaProfile() override;
    WindowsNativeMediaProfile(const WindowsNativeMediaProfile&) = delete;
    WindowsNativeMediaProfile& operator=(const WindowsNativeMediaProfile&) = delete;

    // Binds native calls without invoking them, for the current desktop user
    // in a process that does not impersonate users; not a service backend.
    // The default player does not create this adapter implicitly. Tests inject
    // Calls and never use a profile.
    [[nodiscard]] static std::unique_ptr<WindowsNativeMediaProfile> system();
    [[nodiscard]] Report report() const;
    [[nodiscard]] WindowsMediaProfileCreateResult create(const QString& derived_name) override;
    [[nodiscard]] WindowsMediaProfileRemovalResult remove(const QString& derived_name,
                                                          const QByteArray& live_created_sid) override;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace music_player::m0
