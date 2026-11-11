#include "m0/domain/SessionSnapshot.hpp"

#include <algorithm>
#include <string_view>
#include <unordered_set>

namespace music_player::m0 {
namespace {

bool validJobId(const std::string_view id) noexcept {
    return !id.empty() && id.size() <= 64 && std::ranges::all_of(id, [](const char unit) {
        return (unit >= 'a' && unit <= 'z') || (unit >= 'A' && unit <= 'Z') || (unit >= '0' && unit <= '9') ||
               unit == '_' || unit == '-';
    });
}

bool validNativeText(const std::filesystem::path::string_type& text) noexcept {
#ifdef _WIN32
    for (std::size_t index = 0; index < text.size(); ++index) {
        const auto unit = static_cast<std::uint16_t>(text[index]);
        if (unit == 0 || (unit >= 0xdc00 && unit <= 0xdfff))
            return false;
        if (unit >= 0xd800 && unit <= 0xdbff) {
            if (++index == text.size())
                return false;
            const auto next = static_cast<std::uint16_t>(text[index]);
            if (next < 0xdc00 || next > 0xdfff)
                return false;
        }
    }
#else
    for (std::size_t index = 0; index < text.size();) {
        const auto lead = static_cast<unsigned char>(text[index++]);
        if (lead == 0)
            return false;
        if (lead <= 0x7f)
            continue;
        std::uint32_t scalar;
        std::size_t continuation;
        std::uint32_t minimum;
        if (lead >= 0xc2 && lead <= 0xdf) {
            scalar = lead & 0x1f;
            continuation = 1;
            minimum = 0x80;
        } else if (lead >= 0xe0 && lead <= 0xef) {
            scalar = lead & 0x0f;
            continuation = 2;
            minimum = 0x800;
        } else if (lead >= 0xf0 && lead <= 0xf4) {
            scalar = lead & 0x07;
            continuation = 3;
            minimum = 0x10000;
        } else {
            return false;
        }
        if (continuation > text.size() - index)
            return false;
        while (continuation-- != 0) {
            const auto next = static_cast<unsigned char>(text[index++]);
            if (next < 0x80 || next > 0xbf)
                return false;
            scalar = (scalar << 6) | (next & 0x3f);
        }
        if (scalar < minimum || scalar > 0x10ffff || (scalar >= 0xd800 && scalar <= 0xdfff))
            return false;
    }
#endif
    return true;
}

bool mp3Suffix(const std::filesystem::path& path) {
    const auto extension = path.extension().native();
    return extension.size() == 4 && extension[0] == '.' && (extension[1] == 'm' || extension[1] == 'M') &&
           (extension[2] == 'p' || extension[2] == 'P') && extension[3] == '3';
}

} // namespace

bool isValidDownloadImportReceipts(const std::span<const DownloadImportReceipt> receipts) {
    if (receipts.size() > maximum_download_import_receipts)
        return false;
    std::size_t remaining_bytes = maximum_download_import_receipt_bytes;
    for (std::size_t index = 0; index < receipts.size(); ++index) {
        const auto& receipt = receipts[index];
        if (!validJobId(receipt.job_id) || receipt.job_id.size() > remaining_bytes)
            return false;
        remaining_bytes -= receipt.job_id.size();
        const auto& native = receipt.source_path.native();
        if (native.size() > remaining_bytes / sizeof(std::filesystem::path::value_type))
            return false;
        remaining_bytes -= native.size() * sizeof(std::filesystem::path::value_type);
        if (!validNativeText(native) || !receipt.source_path.is_absolute() ||
            receipt.source_path != receipt.source_path.lexically_normal() || !mp3Suffix(receipt.source_path))
            return false;
        for (std::size_t previous = 0; previous < index; ++previous) {
            if (receipts[previous].job_id == receipt.job_id)
                return false;
        }
    }
    return true;
}

bool isValidSessionSnapshot(const SessionSnapshot& snapshot) {
    if (!isValidLocalFileQueueSnapshot(snapshot.queue) || snapshot.position_ms < 0 ||
        (!snapshot.queue.current_occurrence_id && snapshot.position_ms != 0) ||
        snapshot.retired_download_import_job_ids.size() > maximum_retired_download_import_job_ids ||
        !isValidDownloadImportReceipts(snapshot.download_import_receipts))
        return false;
    if (snapshot.retired_download_import_job_ids.empty())
        return true;

    std::unordered_set<std::string_view> ids;
    ids.reserve(snapshot.download_import_receipts.size() + snapshot.retired_download_import_job_ids.size());
    for (const auto& receipt : snapshot.download_import_receipts)
        ids.insert(receipt.job_id);
    for (const auto& id : snapshot.retired_download_import_job_ids)
        if (!validJobId(id) || !ids.insert(id).second)
            return false;
    return true;
}

} // namespace music_player::m0
