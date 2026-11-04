#include "adapters/WindowsDownloadMediaProfileStore.hpp"

#include "adapters/DownloadMediaProfileJson.hpp"

#include <utility>

namespace music_player::m0 {
namespace {
static_assert(WindowsDownloadMediaProfileStore::maximum_file_size == DownloadMediaProfileJson::maximum_file_size);

DownloadCheckpointLoadStatus validateProfile(const QByteArrayView bytes) {
    const auto decoded = DownloadMediaProfileJson::decode(bytes);
    if (decoded.status == DownloadCheckpointLoadStatus::Loaded && !decoded.record)
        return DownloadCheckpointLoadStatus::Invalid;
    return decoded.status;
}
} // namespace

WindowsDownloadMediaProfileStore::WindowsDownloadMediaProfileStore(QString directory,
                                                                   WindowsDownloadMediaProfileStoreOptions options)
    : directory_(std::move(directory)), options_(std::move(options)) {}
std::unique_ptr<DownloadMediaProfileLease> WindowsDownloadMediaProfileStore::acquireLease() {
    const auto loaded = load();
    if (loaded.status != DownloadCheckpointLoadStatus::Missing && loaded.status != DownloadCheckpointLoadStatus::Loaded)
        return {};
    return WindowsPrivateDownloadDocument::acquireMediaProfileLease(directory_);
}
DownloadMediaProfileLoadResult WindowsDownloadMediaProfileStore::load() {
    try {
        WindowsPrivateDownloadDocument document(directory_, WindowsPrivateDownloadDocumentKind::MediaProfile,
                                                validateProfile);
        auto loaded = document.load();
        if (loaded.status != DownloadCheckpointLoadStatus::Loaded)
            return {.status = loaded.status};
        auto decoded = DownloadMediaProfileJson::decode(loaded.bytes);
        return {.status = decoded.status, .record = std::move(decoded.record)};
    } catch (...) {
        return {.status = DownloadCheckpointLoadStatus::Unreadable};
    }
}
DownloadCheckpointSaveStatus WindowsDownloadMediaProfileStore::record(const DownloadMediaProfileRecord& value) {
    try {
        if (!validDownloadMediaProfileRecord(value))
            return DownloadCheckpointSaveStatus::Failed;
        WindowsPrivateDownloadDocument document(directory_, WindowsPrivateDownloadDocumentKind::MediaProfile,
                                                validateProfile, options_);
        return document.update([&](const WindowsPrivateDownloadDocumentResult& current) {
            if (current.status == DownloadCheckpointLoadStatus::Loaded) {
                const auto decoded = DownloadMediaProfileJson::decode(current.bytes);
                if (decoded.status != DownloadCheckpointLoadStatus::Loaded || !decoded.record)
                    return WindowsPrivateDownloadDocumentMutation{};
                if (!mayReplaceDownloadMediaProfileRecord(*decoded.record, value))
                    return WindowsPrivateDownloadDocumentMutation{
                        .status = DownloadCheckpointSaveStatus::BlockedByExistingData};
            } else if (current.status == DownloadCheckpointLoadStatus::Missing) {
                if (value.stage != DownloadMediaProfileStage::Planned)
                    return WindowsPrivateDownloadDocumentMutation{};
            } else {
                return WindowsPrivateDownloadDocumentMutation{.status =
                                                                  DownloadCheckpointSaveStatus::BlockedByExistingData};
            }
            // Identical replay still requires a new acknowledged write. A
            // successful load never upgrades an earlier ambiguous Failed.
            return WindowsPrivateDownloadDocumentMutation{.status = DownloadCheckpointSaveStatus::Saved,
                                                          .bytes = DownloadMediaProfileJson::encode(value)};
        });
    } catch (...) {
        return DownloadCheckpointSaveStatus::Failed;
    }
}
} // namespace music_player::m0
