#pragma once

namespace music_player::m0 {

// Install before QApplication and audio libraries. The process-lifetime policy is
// never restored: diagnostics emitted during teardown must remain private.
// Failure requires startup to stop before opening user files. Stdout is untouched.
[[nodiscard]] bool installProcessDiagnosticPrivacy() noexcept;

} // namespace music_player::m0
