#pragma once

#include "m0/domain/ListenerProfile.hpp"
#include "m0/ports/ProfileStore.hpp"

#include <QObject>
#include <QString>
#include <QUrl>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>

namespace music_player::m0 {
// The local listener profile for QML: first-launch setup plus the Settings section.
// Loads once at construction. Missing (or Invalid without a backup) asks for setup;
// FutureVersion and Unavailable never ask and never write, so the file is left intact.
class ProfileBinding final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool needsSetup READ needsSetup NOTIFY changed)
    Q_PROPERTY(bool available READ available NOTIFY changed)
    Q_PROPERTY(QString nickname READ nickname NOTIFY changed)
    Q_PROPERTY(QUrl avatarSource READ avatarSource NOTIFY changed)
    Q_PROPERTY(int avatarIndex READ avatarIndex NOTIFY changed)
    Q_PROPERTY(int avatarCount READ avatarCount CONSTANT)

  public:
    using RandomSource = std::function<std::uint64_t()>;
    // An empty random source draws the initial avatar from QRandomGenerator::system().
    ProfileBinding(std::shared_ptr<ProfileStore> store, QString config_directory, RandomSource random = {},
                   QObject* parent = nullptr);

    [[nodiscard]] bool needsSetup() const noexcept;
    [[nodiscard]] bool available() const noexcept;
    [[nodiscard]] QString nickname() const;
    [[nodiscard]] QUrl avatarSource() const;
    [[nodiscard]] int avatarIndex() const noexcept;
    [[nodiscard]] static int avatarCount() noexcept;
    // The listening room's view of the local listener: nullopt until the profile is set up and while
    // profile.json is unavailable.
    [[nodiscard]] std::optional<ListenerProfile> profile() const;

    Q_INVOKABLE bool acceptsNickname(const QString& nickname) const;
    Q_INVOKABLE bool setNickname(const QString& nickname);
    Q_INVOKABLE bool chooseBundledAvatar(int index);
    Q_INVOKABLE bool chooseCustomAvatar(const QUrl& file);

  signals:
    void changed();

  private:
    [[nodiscard]] bool commit(ListenerProfile candidate);

    std::shared_ptr<ProfileStore> store_;
    QString config_directory_;
    ListenerProfile profile_{};
    bool needs_setup_{false};
    bool available_{true};
};
} // namespace music_player::m0
