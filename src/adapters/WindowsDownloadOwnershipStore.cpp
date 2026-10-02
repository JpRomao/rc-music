#include "adapters/WindowsDownloadOwnershipStore.hpp"

#include "adapters/DownloadOwnershipJson.hpp"

#include <algorithm>
#include <utility>

namespace music_player::m0 {
namespace {
static_assert(WindowsDownloadOwnershipStore::maximum_file_size == DownloadOwnershipJson::maximum_file_size);

DownloadCheckpointLoadStatus validateOwnership(const QByteArrayView bytes) {
    const auto decoded = DownloadOwnershipJson::decode(bytes);
    if (decoded.status == DownloadCheckpointLoadStatus::Loaded && !decoded.journal)
        return DownloadCheckpointLoadStatus::Invalid;
    return decoded.status;
}
} // namespace

WindowsDownloadOwnershipStore::WindowsDownloadOwnershipStore(QString directory,
                                                             WindowsDownloadOwnershipStoreOptions options)
    : directory_(std::move(directory)), options_(std::move(options)) {}

DownloadOwnershipLoadResult WindowsDownloadOwnershipStore::load() {
    try {
        WindowsPrivateDownloadDocument document(directory_, WindowsPrivateDownloadDocumentKind::Ownership,
                                                validateOwnership);
        auto loaded = document.load();
        if (loaded.status != DownloadCheckpointLoadStatus::Loaded)
            return {.status = loaded.status};
        auto decoded = DownloadOwnershipJson::decode(loaded.bytes);
        return {.status = decoded.status, .journal = std::move(decoded.journal)};
    } catch (...) {
        return {.status = DownloadCheckpointLoadStatus::Unreadable};
    }
}

DownloadCheckpointSaveStatus WindowsDownloadOwnershipStore::record(const DownloadOwnershipRecord& record) {
    try {
        if (!validDownloadOwnershipRecord(record))
            return DownloadCheckpointSaveStatus::Failed;
        WindowsPrivateDownloadDocument document(directory_, WindowsPrivateDownloadDocumentKind::Ownership,
                                                validateOwnership, options_);
        return document.update([&](const WindowsPrivateDownloadDocumentResult& current) {
            DownloadOwnershipJournal journal;
            if (current.status == DownloadCheckpointLoadStatus::Loaded) {
                auto decoded = DownloadOwnershipJson::decode(current.bytes);
                if (decoded.status != DownloadCheckpointLoadStatus::Loaded || !decoded.journal)
                    return WindowsPrivateDownloadDocumentMutation{};
                journal = std::move(*decoded.journal);
            } else if (current.status != DownloadCheckpointLoadStatus::Missing) {
                return WindowsPrivateDownloadDocumentMutation{.status =
                                                                  DownloadCheckpointSaveStatus::BlockedByExistingData};
            }

            const auto existing = std::find_if(journal.records.begin(), journal.records.end(),
                                               [&](const auto& item) { return item.job_id == record.job_id; });
            if (existing == journal.records.end()) {
                if (journal.records.size() >= DownloadOwnershipJournal::maximum_records ||
                    std::any_of(journal.records.begin(), journal.records.end(),
                                [&](const auto& item) { return item.workspace_id == record.workspace_id; }))
                    return WindowsPrivateDownloadDocumentMutation{
                        .status = DownloadCheckpointSaveStatus::BlockedByExistingData};
                if (record.stage != DownloadOwnershipStage::Planned)
                    return WindowsPrivateDownloadDocumentMutation{};
                journal.records.push_back(record);
            } else {
                if (!mayReplaceDownloadOwnershipRecord(*existing, record))
                    return WindowsPrivateDownloadDocumentMutation{
                        .status = DownloadCheckpointSaveStatus::BlockedByExistingData};
                *existing = record;
            }
            // Even identical replay performs a new acknowledged write. A valid
            // readback never promotes a previous ambiguous Failed into Saved.
            return WindowsPrivateDownloadDocumentMutation{.status = DownloadCheckpointSaveStatus::Saved,
                                                          .bytes = DownloadOwnershipJson::encode(journal)};
        });
    } catch (...) {
        return DownloadCheckpointSaveStatus::Failed;
    }
}
} // namespace music_player::m0
