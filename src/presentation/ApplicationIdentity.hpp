#pragma once

#include <QCoreApplication>
#include <QGuiApplication>
#include <QIcon>
#include <QString>

namespace music_player::m0 {

class ApplicationIdentity final {
  public:
    [[nodiscard]] static QString displayName() { return QStringLiteral("RC Music"); }

    [[nodiscard]] static QIcon icon() { return QIcon{QStringLiteral(":/m0/assets/cover-track.svg")}; }

    static void configureQtApplication() {
        // These persisted identities must not change without a data migration.
        QCoreApplication::setOrganizationName(QStringLiteral("MusicPlayer"));
        QCoreApplication::setApplicationName(QStringLiteral("MusicPlayer"));
        QGuiApplication::setApplicationDisplayName(displayName());
        QGuiApplication::setWindowIcon(icon());
    }
};

} // namespace music_player::m0
