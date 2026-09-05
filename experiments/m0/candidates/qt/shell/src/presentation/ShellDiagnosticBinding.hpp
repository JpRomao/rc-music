#pragma once

#include "m0/ports/DiagnosticLog.hpp"

#include <QObject>
#include <QPointer>
#include <QString>

#include <cstdint>
#include <optional>

namespace music_player::m0 {

class PlayerPresenter;

// UI-thread projection of closed diagnostic entries only. The sink must outlive
// this binding and remain nonblocking; rejection never changes player behavior.
class ShellDiagnosticBinding final : public QObject {
  public:
    ShellDiagnosticBinding(PlayerPresenter& presenter, DiagnosticSink& sink, QObject* parent = nullptr);
    void refresh();

  private:
    struct ErrorIdentity final {
        int state;
        QString occurrence;
        QString code;
        QString message_key;

        friend bool operator==(const ErrorIdentity&, const ErrorIdentity&) = default;
    };

    void refreshSourceError();
    void refreshPersistenceWarning();

    QPointer<PlayerPresenter> presenter_;
    DiagnosticSink& sink_;
    std::optional<ErrorIdentity> last_error_;
    std::optional<int> last_preferences_warning_;
    std::optional<int> last_session_warning_;
    std::uint64_t persistence_revision_{0};
};

} // namespace music_player::m0
