#pragma once

#include "m0/domain/LocalFileQueue.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace music_player::m0 {

inline constexpr std::size_t maximum_download_import_receipts = 128;
// Retained ID bytes + path native-code-unit bytes, excluding container overhead.
inline constexpr std::size_t maximum_download_import_receipt_bytes = 1024 * 1024;
// One ID per potential job in the bounded download checkpoint archive.
inline constexpr std::size_t maximum_retired_download_import_job_ids = 5120;

struct DownloadImportReceipt final {
    std::string job_id{};
    std::filesystem::path source_path{};

    [[nodiscard]] bool operator==(const DownloadImportReceipt&) const = default;
};

// Lexical evidence only: no file/ownership/media validation and no requirement
// that the path still exists or that the user kept its queue occurrence.
[[nodiscard]] bool isValidDownloadImportReceipts(std::span<const DownloadImportReceipt> receipts);

struct SessionSnapshot final {
    LocalFileQueueSnapshot queue{};
    std::int64_t position_ms{0};
    std::vector<DownloadImportReceipt> download_import_receipts{};
    // Acknowledged archive fence. These IDs retain no path or import authority.
    std::vector<std::string> retired_download_import_job_ids{};

    [[nodiscard]] bool operator==(const SessionSnapshot&) const = default;
};

[[nodiscard]] bool isValidSessionSnapshot(const SessionSnapshot& snapshot);

} // namespace music_player::m0
