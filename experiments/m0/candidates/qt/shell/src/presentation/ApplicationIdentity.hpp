#pragma once

#include <QCoreApplication>
#include <QGuiApplication>
#include <QString>

namespace music_player::m0 {

class ApplicationIdentity final {
  public:
    [[nodiscard]] static QString displayName() { return QStringLiteral("RC Music"); }

    static void configureQtApplication() {
        // These persisted identities must not change without a data migration.
        QCoreApplication::setOrganizationName(QStringLiteral("MusicPlayer"));
        QCoreApplication::setApplicationName(QStringLiteral("MusicPlayer"));
        QGuiApplication::setApplicationDisplayName(displayName());
    }
};

} // namespace music_player::m0
