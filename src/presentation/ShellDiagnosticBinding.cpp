#include "presentation/ShellDiagnosticBinding.hpp"

#include "presentation/PlayerPresenter.hpp"

#include <array>
#include <cstddef>

namespace music_player::m0 {
namespace {

DiagnosticCode sourceCode(const QString& code) noexcept {
    if (code == QStringLiteral("source_unavailable"))
        return DiagnosticCode::SourceUnavailable;
    if (code == QStringLiteral("source_volume_removed"))
        return DiagnosticCode::SourceVolumeRemoved;
    if (code == QStringLiteral("invalid_media"))
        return DiagnosticCode::InvalidMedia;
    if (code == QStringLiteral("access_denied"))
        return DiagnosticCode::AccessDenied;
    if (code == QStringLiteral("backend_failure"))
        return DiagnosticCode::BackendFailure;
    if (code == QStringLiteral("audio_output_unavailable"))
        return DiagnosticCode::AudioOutputUnavailable;
    if (code == QStringLiteral("audio_output_changed"))
        return DiagnosticCode::AudioOutputChanged;
    return DiagnosticCode::Unknown;
}

DiagnosticCode persistenceCode(const PlayerPresenter::PreferenceWarning warning) noexcept {
    switch (warning) {
    case PlayerPresenter::PreferenceWarning::RecoveredFromBackup:
    case PlayerPresenter::PreferenceWarning::UsingDefaults:
        return DiagnosticCode::CorruptState;
    case PlayerPresenter::PreferenceWarning::FutureVersion:
        return DiagnosticCode::UnsupportedVersion;
    case PlayerPresenter::PreferenceWarning::SaveFailed:
        return DiagnosticCode::WriteFailed;
    case PlayerPresenter::PreferenceWarning::NoPreferenceWarning:
        return DiagnosticCode::None;
    }
    return DiagnosticCode::Unknown;
}

} // namespace

ShellDiagnosticBinding::ShellDiagnosticBinding(PlayerPresenter& presenter, DiagnosticSink& sink, QObject* parent)
    : QObject(parent), presenter_(&presenter), sink_(sink) {
    connect(&presenter, &PlayerPresenter::stateChanged, this, [this] { refresh(); });
    connect(&presenter, &PlayerPresenter::preferencePersistenceChanged, this, [this] { refreshPersistenceWarning(); });
    connect(&presenter, &PlayerPresenter::occurrenceAutomaticallySkipped, this,
            [this](const QString&, const QString& code) {
                static_cast<void>(sink_.submit({.severity = DiagnosticSeverity::Warning,
                                                .event = DiagnosticEvent::SourceSkipped,
                                                .code = sourceCode(code)}));
            });
    connect(&presenter, &PlayerPresenter::seekRejected, this, [this] {
        // This signal does not expose a cause; do not infer one from later state.
        static_cast<void>(sink_.submit({.severity = DiagnosticSeverity::Warning,
                                        .event = DiagnosticEvent::SeekRejected,
                                        .code = DiagnosticCode::Unknown}));
    });
    refresh();
}

void ShellDiagnosticBinding::refresh() {
    const QPointer<ShellDiagnosticBinding> guard(this);
    refreshSourceError();
    if (guard)
        refreshPersistenceWarning();
}

void ShellDiagnosticBinding::refreshSourceError() {
    if (!presenter_)
        return;
    if (!presenter_->hasError()) {
        last_error_.reset();
        return;
    }
    const auto occurrence = presenter_->currentOccurrenceId();
    const ErrorIdentity identity{static_cast<int>(presenter_->playbackState()),
                                 occurrence.isEmpty() ? presenter_->contextOccurrenceId() : occurrence,
                                 presenter_->errorCode(), presenter_->errorMessageKey()};
    if (last_error_ == identity)
        return;
    // IDs and presentation keys are only an in-memory deduplication identity.
    // Update before submission so a reentrant refresh cannot emit it twice.
    last_error_ = identity;
    static_cast<void>(sink_.submit({.severity = DiagnosticSeverity::Error,
                                    .event = DiagnosticEvent::SourceOpenFailed,
                                    .code = sourceCode(identity.code)}));
}

void ShellDiagnosticBinding::refreshPersistenceWarning() {
    if (!presenter_)
        return;
    const auto warning = presenter_->preferenceWarning();
    const auto scope = presenter_->persistenceWarningScope();
    const bool has_warning = warning != PlayerPresenter::PreferenceWarning::NoPreferenceWarning;
    const auto preferences_warning =
        has_warning && (scope == PlayerPresenter::PersistenceWarningScope::PreferencePersistence ||
                        scope == PlayerPresenter::PersistenceWarningScope::CombinedPersistence)
            ? std::optional{static_cast<int>(warning)}
            : std::nullopt;
    const auto session_warning =
        has_warning && (scope == PlayerPresenter::PersistenceWarningScope::SessionPersistence ||
                        scope == PlayerPresenter::PersistenceWarningScope::CombinedPersistence)
            ? std::optional{static_cast<int>(warning)}
            : std::nullopt;
    if (preferences_warning == last_preferences_warning_ && session_warning == last_session_warning_)
        return;

    std::array<DiagnosticEntry, 2> pending{};
    std::size_t count = 0;
    if (preferences_warning && preferences_warning != last_preferences_warning_)
        pending[count++] = {.severity = DiagnosticSeverity::Warning,
                            .event = DiagnosticEvent::PreferencesWarning,
                            .code = persistenceCode(warning)};
    if (session_warning && session_warning != last_session_warning_)
        pending[count++] = {.severity = DiagnosticSeverity::Warning,
                            .event = DiagnosticEvent::SessionWarning,
                            .code = persistenceCode(warning)};
    last_preferences_warning_ = preferences_warning;
    last_session_warning_ = session_warning;
    const auto revision = ++persistence_revision_;
    const QPointer<ShellDiagnosticBinding> guard(this);
    for (std::size_t index = 0; index < count; ++index) {
        static_cast<void>(sink_.submit(pending[index]));
        if (!guard || !presenter_ || revision != persistence_revision_)
            return;
    }
}

} // namespace music_player::m0
