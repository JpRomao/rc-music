#include "adapters/WindowsDownloadCheckpointStore.hpp"

#include "adapters/DownloadCheckpointJson.hpp"
#include "adapters/WindowsPrivateDownloadDocument.hpp"

#include <utility>

namespace music_player::m0 {
namespace {
static_assert(WindowsDownloadCheckpointStore::maximum_file_size == DownloadCheckpointJson::max_document_bytes);

DownloadCheckpointLoadStatus validateCheckpoint(const QByteArrayView bytes) {
    const auto decoded = DownloadCheckpointJson::decode(bytes);
    if (decoded.status == DownloadCheckpointJsonStatus::FutureVersion)
        return DownloadCheckpointLoadStatus::FutureVersion;
    if (decoded.status == DownloadCheckpointJsonStatus::Valid && decoded.checkpoint)
        return DownloadCheckpointLoadStatus::Loaded;
    return DownloadCheckpointLoadStatus::Invalid;
}
} // namespace

WindowsDownloadCheckpointStore::WindowsDownloadCheckpointStore(QString directory,
                                                               WindowsDownloadCheckpointStoreOptions options)
    : config_directory_(std::move(directory)), options_(std::move(options)) {}

WindowsDownloadCheckpointLoadResult WindowsDownloadCheckpointStore::load() {
    try {
        WindowsPrivateDownloadDocument document(config_directory_, WindowsPrivateDownloadDocumentKind::Checkpoint,
                                                validateCheckpoint);
        auto loaded = document.load();
        if (loaded.status != DownloadCheckpointLoadStatus::Loaded)
            return {.status = loaded.status};
        auto decoded = DownloadCheckpointJson::decode(loaded.bytes);
        if (decoded.status != DownloadCheckpointJsonStatus::Valid || !decoded.checkpoint)
            return {.status = DownloadCheckpointLoadStatus::Unreadable};
        return {.checkpoint = std::move(decoded.checkpoint), .status = DownloadCheckpointLoadStatus::Loaded};
    } catch (...) {
        return {.status = DownloadCheckpointLoadStatus::Unreadable};
    }
}

WindowsDownloadCheckpointSaveStatus WindowsDownloadCheckpointStore::save(const DownloadCheckpoint& checkpoint) {
    try {
        const auto encoded = DownloadCheckpointJson::encode(checkpoint);
        if (encoded.isEmpty() || encoded.size() > maximum_file_size)
            return DownloadCheckpointSaveStatus::Failed;
        WindowsPrivateDownloadDocumentOptions options{
            .inject_fault = [fault = options_.inject_fault](const WindowsPrivateDownloadFaultPoint point) {
                if (!fault)
                    return false;
                return fault(point == WindowsPrivateDownloadFaultPoint::BeforeCommit
                                 ? WindowsDownloadCheckpointFaultPoint::BeforeCommit
                                 : WindowsDownloadCheckpointFaultPoint::AfterCommit);
            }};
        WindowsPrivateDownloadDocument document(config_directory_, WindowsPrivateDownloadDocumentKind::Checkpoint,
                                                validateCheckpoint, std::move(options));
        return document.update([&](const WindowsPrivateDownloadDocumentResult&) {
            return WindowsPrivateDownloadDocumentMutation{.status = DownloadCheckpointSaveStatus::Saved,
                                                          .bytes = encoded};
        });
    } catch (...) {
        return DownloadCheckpointSaveStatus::Failed;
    }
}

} // namespace music_player::m0
