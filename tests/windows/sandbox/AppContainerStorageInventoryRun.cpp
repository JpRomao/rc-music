#include "AppContainerStorageInventoryRun.hpp"

#include <utility>

namespace music_player::sandbox_probe {
AppContainerStorageInventoryRun::Result
AppContainerStorageInventoryRun::execute(Inventory::Source& source, const Recorder& recorder,
                                         const Inventory::Limits limits) noexcept {
    Result result;
    for (std::size_t index = 0; index < result.report.roots.size(); ++index)
        result.report.roots[index].root = static_cast<Inventory::Root>(index);
    if (!recorder) {
        result.record_state = RecordState::Missing;
        return result;
    }
    result.inspection_called = true;
    try {
        auto captured = Inventory::inspect(source, limits);
        result.report = std::move(captured.report);
        result.lease = std::move(captured.lease);
    } catch (...) {
        result.report.reason = Inventory::Reason::Exception;
    }
    result.record_called = true;
    try {
        result.record_state = recorder(result.report) ? RecordState::Saved : RecordState::Failed;
    } catch (...) {
        result.record_state = RecordState::Exception;
    }
    if (result.lease) {
        // A refused source is not permission to make further observations.
        // Its transferred reading resources still require explicit closure.
        if (result.report.provenance == Inventory::Provenance::LiveCreated && result.report.begin_error == 0) {
            result.post_validation_called = true;
            result.post_validation = result.lease->revalidate();
        }
        result.close_called = true;
        result.closure = result.lease->close();
    }
    result.observed_and_revalidated =
        result.report.status == Inventory::Status::Observed && result.report.reason == Inventory::Reason::None &&
        result.report.provenance == Inventory::Provenance::LiveCreated && result.report.begin_error == 0 &&
        result.report.validation.state == Inventory::Validation::Unchanged && result.report.validation.error == 0 &&
        result.post_validation_called && result.post_validation.state == Inventory::Validation::Unchanged &&
        result.post_validation.error == 0;
    result.read_resources_closed =
        result.close_called && result.closure.state == Inventory::CloseState::Closed && result.closure.error == 0;
    return result;
}
} // namespace music_player::sandbox_probe
