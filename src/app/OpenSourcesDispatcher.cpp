#include "OpenSourcesDispatcher.hpp"

#include <stdexcept>
#include <utility>

namespace music_player::m0 {

OpenSourcesDispatcher::OpenSourcesDispatcher(Activate activate, ImporterBusy importer_busy, StartImport start_import,
                                             Limits limits)
    : activate_(std::move(activate)), importer_busy_(std::move(importer_busy)), start_import_(std::move(start_import)),
      limits_(limits) {
    if (!activate_ || !importer_busy_ || !start_import_)
        throw std::invalid_argument{"the open sources dispatcher requires all callbacks"};
    if (limits_.max_pending_payload_bytes < 0)
        throw std::invalid_argument{"the open sources dispatcher byte limit cannot be negative"};
}

bool OpenSourcesDispatcher::enqueue(OpenSourcesCommand command) {
    if (!accepting_)
        return false;

    const OpenSourcesEncodingResult encoded = encodeOpenSourcesCommand(command);
    if (encoded.error != OpenSourcesCommandError::None)
        return false;
    if (command.paths.isEmpty())
        return activate_();
    if (!hasCapacityFor(command, encoded.payload.size()) || !activate_())
        return false;

    pending_path_count_ += static_cast<std::size_t>(command.paths.size());
    pending_payload_bytes_ += encoded.payload.size();
    pending_.push_back({.command = std::move(command), .payload_bytes = encoded.payload.size()});
    drain();
    return true;
}

void OpenSourcesDispatcher::importerStateChanged() {
    owned_import_active_ = false;
    drain();
}

void OpenSourcesDispatcher::retryPending() { drain(); }

void OpenSourcesDispatcher::stopAccepting() noexcept { accepting_ = false; }

bool OpenSourcesDispatcher::accepting() const noexcept { return accepting_; }

std::size_t OpenSourcesDispatcher::pendingCommandCount() const noexcept { return pending_.size(); }

bool OpenSourcesDispatcher::hasDrainedAcceptedWork() const noexcept {
    return pending_.empty() && !owned_import_active_;
}

void OpenSourcesDispatcher::drain() {
    while (!owned_import_active_ && !pending_.empty() && !importer_busy_()) {
        PendingCommand& pending = pending_.front();
        if (!start_import_(pending.command.paths))
            return;
        owned_import_active_ = true;
        pending_path_count_ -= static_cast<std::size_t>(pending.command.paths.size());
        pending_payload_bytes_ -= pending.payload_bytes;
        pending_.pop_front();
        return;
    }
}

bool OpenSourcesDispatcher::hasCapacityFor(const OpenSourcesCommand& command,
                                           const qsizetype payload_bytes) const noexcept {
    const std::size_t path_count = static_cast<std::size_t>(command.paths.size());
    return pending_.size() < limits_.max_pending_commands && path_count <= limits_.max_pending_paths &&
           pending_path_count_ <= limits_.max_pending_paths - path_count &&
           payload_bytes <= limits_.max_pending_payload_bytes &&
           pending_payload_bytes_ <= limits_.max_pending_payload_bytes - payload_bytes;
}

} // namespace music_player::m0
