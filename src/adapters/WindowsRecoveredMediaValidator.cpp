#include "adapters/WindowsRecoveredMediaValidator.hpp"

#include <utility>

namespace music_player::m0 {
namespace {
bool terminal(RecoveredMediaValidationState state) {
    return state == RecoveredMediaValidationState::Validated || state == RecoveredMediaValidationState::Failed ||
           state == RecoveredMediaValidationState::Cancelled;
}
bool terminal(LocalMp3ValidationState state) {
    return state == LocalMp3ValidationState::Validated || state == LocalMp3ValidationState::Failed ||
           state == LocalMp3ValidationState::Cancelled;
}
} // namespace
WindowsRecoveredMediaValidator::WindowsRecoveredMediaValidator(WindowsDownloadWorkspaceConfig workspace,
                                                               WindowsLocalMp3ValidatorConfig tools,
                                                               std::optional<WindowsMediaFoundationLaunchConfig> media)
    : workspace_(std::move(workspace)), tools_(std::move(tools)), media_(std::move(media)) {}
WindowsRecoveredMediaValidator::~WindowsRecoveredMediaValidator() = default;
bool WindowsRecoveredMediaValidator::start(const DownloadOwnershipRecord& record, qint64 now_ms) {
    if (update_.state != RecoveredMediaValidationState::Idle || now_ms < 0 || tools_.total_timeout_ms <= 0 ||
        !tools_.max_process_memory_bytes)
        return false;
    auto pin = WindowsDownloadOwnershipRecovery::pinFinal(workspace_, record);
    update_.ownership = pin.assessment;
    if (pin.assessment != DownloadOwnershipAssessment::OwnedFinalCandidate || !pin.candidate) {
        finish(RecoveredMediaValidationState::Failed, DownloadFailure::Publication);
        return false;
    }
    if (media_) {
        if (tools_.backend != WindowsLocalMediaBackend::MediaFoundation || tools_.prepared_launch) {
            finish(RecoveredMediaValidationState::Failed, DownloadFailure::InvalidResult);
            return false;
        }
        tools_.prepared_launch = WindowsMediaFoundationLaunch::prepare(*media_);
        if (!tools_.prepared_launch) {
            finish(RecoveredMediaValidationState::Failed, DownloadFailure::Unavailable);
            return false;
        }
    }
    // Keep preparation in the original deadline. Launch is deferred so a
    // cancellation/expired clock observed after filesystem proof starts no child.
    started_ms_ = previous_ms_ = now_ms;
    candidate_ = std::move(pin.candidate);
    launch_pending_ = true;
    update_.state = RecoveredMediaValidationState::Validating;
    return true;
}
void WindowsRecoveredMediaValidator::finish(RecoveredMediaValidationState state, DownloadFailure error) {
    update_.state = state;
    update_.error = error;
    if (state != RecoveredMediaValidationState::Validated) {
        validator_.reset();
        candidate_.reset();
    }
}
void WindowsRecoveredMediaValidator::stop(DownloadFailure error, bool cancelled) {
    if (terminal(update_.state) || update_.state == RecoveredMediaValidationState::Stopping)
        return;
    update_.state = RecoveredMediaValidationState::Stopping;
    update_.error = error;
    cancel_requested_ = cancelled;
    if (validator_)
        validator_->cancel();
    else {
        launch_pending_ = false;
        finish(cancelled ? RecoveredMediaValidationState::Cancelled : RecoveredMediaValidationState::Failed, error);
    }
}
void WindowsRecoveredMediaValidator::cancel() {
    if (update_.state == RecoveredMediaValidationState::Idle)
        finish(RecoveredMediaValidationState::Cancelled, DownloadFailure::None);
    else
        stop(DownloadFailure::None, true);
}
RecoveredMediaValidationUpdate WindowsRecoveredMediaValidator::poll(qint64 now_ms) {
    if (update_.state == RecoveredMediaValidationState::Idle || terminal(update_.state))
        return update_;
    if (update_.state != RecoveredMediaValidationState::Stopping) {
        if (now_ms < previous_ms_)
            stop(DownloadFailure::InvalidResult);
        else if (now_ms - started_ms_ >= tools_.total_timeout_ms)
            stop(DownloadFailure::Timeout);
        previous_ms_ = now_ms;
    }
    if (terminal(update_.state))
        return update_;
    if (launch_pending_) {
        auto remaining = tools_;
        remaining.total_timeout_ms -= now_ms - started_ms_;
        validator_ = std::make_unique<WindowsLocalMp3Validator>(std::move(remaining));
        launch_pending_ = false;
        if (tools_.backend == WindowsLocalMediaBackend::MediaFoundation) {
            auto input = candidate_->readInput();
            if (!input) {
                update_.ownership = candidate_->revalidate();
                finish(RecoveredMediaValidationState::Failed, DownloadFailure::Publication);
                return update_;
            }
            static_cast<void>(validator_->startReadInput(std::move(input), now_ms));
        } else {
            static_cast<void>(validator_->start(candidate_->pathForValidation(), now_ms));
        }
    }
    const auto result = validator_->poll(now_ms);
    if (update_.state == RecoveredMediaValidationState::Stopping) {
        if (terminal(result.state))
            finish(cancel_requested_ ? RecoveredMediaValidationState::Cancelled : RecoveredMediaValidationState::Failed,
                   update_.error);
        return update_;
    }
    switch (result.state) {
    case LocalMp3ValidationState::Validated:
        update_.ownership = candidate_->revalidate();
        if (update_.ownership == DownloadOwnershipAssessment::OwnedFinalCandidate)
            finish(RecoveredMediaValidationState::Validated, DownloadFailure::None);
        else
            finish(RecoveredMediaValidationState::Failed, DownloadFailure::Publication);
        break;
    case LocalMp3ValidationState::Failed:
        finish(RecoveredMediaValidationState::Failed,
               result.error == DownloadFailure::None ? DownloadFailure::InvalidResult : result.error);
        break;
    case LocalMp3ValidationState::Cancelled:
        finish(RecoveredMediaValidationState::Cancelled, DownloadFailure::None);
        break;
    case LocalMp3ValidationState::Stopping:
        stop(result.error);
        break;
    case LocalMp3ValidationState::Idle:
        finish(RecoveredMediaValidationState::Failed, DownloadFailure::InvalidResult);
        break;
    case LocalMp3ValidationState::Validating:
        break;
    }
    return update_;
}
const WindowsPinnedDownloadFinal* WindowsRecoveredMediaValidator::validatedCandidate() const {
    return update_.state == RecoveredMediaValidationState::Validated ? candidate_.get() : nullptr;
}
} // namespace music_player::m0
