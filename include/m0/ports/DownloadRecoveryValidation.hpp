#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace music_player::m0 {
enum class DownloadRecoveryValidationCommand { Accepted, Busy, Closed, InvalidRequest };
enum class DownloadRecoveryValidationOutcome {
    Validated,
    ImportReady,
    PublicationUnconfirmed,
    Released,
    Unverified,
    OwnedPartial,
    Missing,
    Conflict,
    Unreadable,
    HistoryMissing,
    HistoryInvalid,
    HistoryFuture,
    InvalidMedia,
    Timeout
};
struct DownloadRecoveryValidationResult final {
    std::uint64_t operation_id{};
    std::string job_id{};
    DownloadRecoveryValidationOutcome outcome{DownloadRecoveryValidationOutcome::Unreadable};
    // Trusted application-side handoff only, never a QML/remote path ingress.
    // Present exclusively for ImportReady; the worker still owns the proof.
    std::filesystem::path trusted_mp3{};
    bool operator==(const DownloadRecoveryValidationResult&) const = default;
};

// Prompt application-thread port, never a path/handle/URL ingress. One slot
// includes unobserved completions. Accepted IDs are positive and strictly
// increasing; wrong polls consume nothing. Validated retains the private
// capability even after its result is consumed: it is not publication/import
// authority. prepareImport is explicit, only after a retained completion was
// consumed; a new acknowledged Published write and fresh proof can return
// ImportReady with a trusted C++ path while retaining that same capability.
// Failed/ambiguous writes never become ready by readback or automatic retry.
// Only release of the matching ID revokes it. Release also cancels
// an in-flight request and invalidates unobserved Validated/ImportReady; Released
// acknowledges physical drain AND private task/capability destruction. Repeated
// release while matching release is pending is harmless; wrong/inactive IDs
// return false without work. Other terminal results release the slot only when
// consumed by a matching poll. shutdown closes admission promptly; stopped
// additionally acknowledges all worker/factory/capture teardown. Owners must
// observe that acknowledgement before destruction on the application thread.
class DownloadRecoveryValidation {
  public:
    virtual ~DownloadRecoveryValidation() = default;
    [[nodiscard]] virtual DownloadRecoveryValidationCommand startValidation(std::uint64_t operation_id,
                                                                            std::string_view job_id) = 0;
    [[nodiscard]] virtual DownloadRecoveryValidationCommand prepareImport(std::uint64_t) {
        return DownloadRecoveryValidationCommand::InvalidRequest;
    }
    [[nodiscard]] virtual bool release(std::uint64_t operation_id) = 0;
    [[nodiscard]] virtual std::optional<DownloadRecoveryValidationResult> poll(std::uint64_t operation_id) = 0;
    virtual void shutdown() = 0;
    [[nodiscard]] virtual bool stopped() const = 0;
};
} // namespace music_player::m0
