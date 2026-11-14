#include "adapters/WindowsDownloadCheckpointStore.hpp"

#include "adapters/DownloadCheckpointJson.hpp"
#include "adapters/WindowsPrivateDownloadDocument.hpp"

#include <QCryptographicHash>
#include <QScopeGuard>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <wincrypt.h>

#include <utility>

namespace music_player::m0 {
namespace {
constexpr QByteArrayView protected_magic{"RCMusic.DownloadCheckpoint.DPAPI.v1\n"};
constexpr auto digest_algorithm = QCryptographicHash::Sha256;
constexpr qsizetype digest_size = 32;

// Win32's input blobs are non-const even though their contents are input-only.
DATA_BLOB inputBlob(const QByteArrayView bytes) {
    return {static_cast<DWORD>(bytes.size()), reinterpret_cast<BYTE*>(const_cast<char*>(bytes.data()))};
}

QByteArray protectCheckpoint(QByteArray json) {
    const auto clear_json = qScopeGuard([&] { SecureZeroMemory(json.data(), static_cast<SIZE_T>(json.size())); });
    // Verify the recovered payload itself in addition to DPAPI's integrity check.
    auto plaintext = QCryptographicHash::hash(json, digest_algorithm);
    plaintext.append(json);
    const auto clear_payload =
        qScopeGuard([&] { SecureZeroMemory(plaintext.data(), static_cast<SIZE_T>(plaintext.size())); });
    auto input = inputBlob(plaintext);
    auto entropy = inputBlob(protected_magic);
    DATA_BLOB ciphertext{};
    const auto release = qScopeGuard([&] { LocalFree(ciphertext.pbData); });
    if (!CryptProtectData(&input, nullptr, &entropy, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &ciphertext) ||
        !ciphertext.pbData || ciphertext.cbData == 0 ||
        ciphertext.cbData > WindowsDownloadCheckpointStore::maximum_file_size - protected_magic.size())
        return {};
    auto result = protected_magic.toByteArray();
    result.append(reinterpret_cast<const char*>(ciphertext.pbData), ciphertext.cbData);
    return result;
}

DownloadCheckpointJsonResult unprotectCheckpoint(const QByteArrayView bytes) {
    if (bytes.isEmpty() || bytes.size() > WindowsDownloadCheckpointStore::maximum_file_size - protected_magic.size())
        return {};
    auto input = inputBlob(bytes);
    auto entropy = inputBlob(protected_magic);
    DATA_BLOB plaintext{};
    const auto clear_and_release = qScopeGuard([&] {
        if (plaintext.pbData) {
            SecureZeroMemory(plaintext.pbData, plaintext.cbData);
            LocalFree(plaintext.pbData);
        }
    });
    if (!CryptUnprotectData(&input, nullptr, &entropy, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &plaintext) ||
        !plaintext.pbData || plaintext.cbData <= digest_size ||
        plaintext.cbData > DownloadCheckpointJson::max_document_bytes + digest_size)
        return {};
    const QByteArrayView payload{reinterpret_cast<const char*>(plaintext.pbData), plaintext.cbData};
    const auto json = payload.sliced(digest_size);
    if (payload.first(digest_size) != QCryptographicHash::hash(json, digest_algorithm))
        return {};
    return DownloadCheckpointJson::decode(json);
}

DownloadCheckpointJsonResult decodeOnDisk(const QByteArrayView bytes) {
    const bool protected_format = bytes.startsWith(protected_magic);
    auto decoded = protected_format ? unprotectCheckpoint(bytes.sliced(protected_magic.size()))
                                    : DownloadCheckpointJson::decode(bytes);
    if (decoded.checkpoint) {
        const bool has_origins = !decoded.checkpoint->resumable_jobs.empty();
        if (has_origins != protected_format)
            return {};
    }
    return decoded;
}

DownloadCheckpointLoadStatus validatedStatus(const DownloadCheckpointJsonResult& decoded) {
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
        DownloadCheckpointJsonResult decoded;
        WindowsPrivateDownloadDocument document(config_directory_, WindowsPrivateDownloadDocumentKind::Checkpoint,
                                                [&](const QByteArrayView bytes) {
                                                    decoded = decodeOnDisk(bytes);
                                                    return validatedStatus(decoded);
                                                });
        auto loaded = document.load();
        if (loaded.status != DownloadCheckpointLoadStatus::Loaded)
            return {.status = loaded.status};
        return {.checkpoint = std::move(decoded.checkpoint), .status = DownloadCheckpointLoadStatus::Loaded};
    } catch (...) {
        return {.status = DownloadCheckpointLoadStatus::Unreadable};
    }
}

WindowsDownloadCheckpointSaveStatus WindowsDownloadCheckpointStore::save(const DownloadCheckpoint& checkpoint) {
    try {
        auto encoded = DownloadCheckpointJson::encode(checkpoint);
        if (!encoded.isEmpty() && !checkpoint.resumable_jobs.empty())
            encoded = protectCheckpoint(std::move(encoded));
        if (encoded.isEmpty())
            return DownloadCheckpointSaveStatus::Failed;
        WindowsPrivateDownloadDocumentOptions options{
            .inject_fault = [fault = options_.inject_fault](const WindowsPrivateDownloadFaultPoint point) {
                if (!fault)
                    return false;
                return fault(point == WindowsPrivateDownloadFaultPoint::BeforeCommit
                                 ? WindowsDownloadCheckpointFaultPoint::BeforeCommit
                                 : WindowsDownloadCheckpointFaultPoint::AfterCommit);
            }};
        WindowsPrivateDownloadDocument document(
            config_directory_, WindowsPrivateDownloadDocumentKind::Checkpoint,
            [](const QByteArrayView bytes) { return validatedStatus(decodeOnDisk(bytes)); }, std::move(options));
        return document.update([&](const WindowsPrivateDownloadDocumentResult&) {
            return WindowsPrivateDownloadDocumentMutation{.status = DownloadCheckpointSaveStatus::Saved,
                                                          .bytes = encoded};
        });
    } catch (...) {
        return DownloadCheckpointSaveStatus::Failed;
    }
}

} // namespace music_player::m0
