#include "m0/application/DownloadCheckpoint.hpp"

#include <algorithm>
#include <unordered_set>

namespace music_player::m0 {
namespace {
bool asciiId(const std::string_view value) {
    return !value.empty() && value.size() <= 64 && std::ranges::all_of(value, [](const char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
    });
}
bool canonicalUuid(const std::string_view value) {
    if (value.size() != 36 || value[14] != '4' ||
        (value[19] != '8' && value[19] != '9' && value[19] != 'a' && value[19] != 'b'))
        return false;
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (value[i] != '-')
                return false;
        } else if (!((value[i] >= '0' && value[i] <= '9') || (value[i] >= 'a' && value[i] <= 'f')))
            return false;
    }
    return true;
}
bool digest(const std::string_view value) {
    return value.size() == 64 &&
           std::ranges::all_of(value, [](const char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}
bool validUtf8(const std::string_view text) {
    for (std::size_t i = 0; i < text.size();) {
        const auto lead = static_cast<unsigned char>(text[i++]);
        if (lead < 0x80) {
            if (lead == 0)
                return false;
            continue;
        }
        unsigned int count = 0;
        std::uint32_t value = 0;
        std::uint32_t minimum = 0;
        if (lead >= 0xc2 && lead <= 0xdf) {
            count = 1;
            value = lead & 0x1f;
            minimum = 0x80;
        } else if (lead >= 0xe0 && lead <= 0xef) {
            count = 2;
            value = lead & 0x0f;
            minimum = 0x800;
        } else if (lead >= 0xf0 && lead <= 0xf4) {
            count = 3;
            value = lead & 0x07;
            minimum = 0x10000;
        } else
            return false;
        if (count > text.size() - i)
            return false;
        while (count-- != 0) {
            const auto next = static_cast<unsigned char>(text[i++]);
            if ((next & 0xc0) != 0x80)
                return false;
            value = (value << 6) | (next & 0x3f);
        }
        if (value < minimum || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff))
            return false;
    }
    return true;
}
bool candidatePath(const std::filesystem::path& path) {
    const auto& text = path.native();
    if (text.empty() || text.size() > 30'000 || !path.is_absolute() || path != path.lexically_normal() ||
        std::ranges::find(text, 0) != text.end())
        return false;
    auto extension = path.extension().native();
    for (auto& c : extension)
        if (c >= 'A' && c <= 'Z')
            c = static_cast<std::filesystem::path::value_type>(c + ('a' - 'A'));
    if (extension != std::filesystem::path{".mp3"}.native())
        return false;
#ifdef _WIN32
    if (text.size() < 3 || !((text[0] >= L'a' && text[0] <= L'z') || (text[0] >= L'A' && text[0] <= L'Z')) ||
        text[1] != L':' || (text[2] != L'/' && text[2] != L'\\'))
        return false;
    for (std::size_t i = 3; i < text.size(); ++i) {
        const auto c = static_cast<std::uint32_t>(text[i]);
        if (c < 0x20 || text[i] == L':')
            return false;
        if (c >= 0xd800 && c <= 0xdbff) {
            if (++i >= text.size() || text[i] < 0xdc00 || text[i] > 0xdfff)
                return false;
        } else if (c >= 0xdc00 && c <= 0xdfff)
            return false;
    }
    return true;
#else
    return validUtf8(text);
#endif
}
bool validJob(const DownloadJobSnapshot& job) {
    if (!asciiId(job.job_id) || job.title.empty() || job.title.size() > 800 || !validUtf8(job.title) ||
        job.error < DownloadFailure::None || job.error > DownloadFailure::InvalidResult ||
        (job.total_units && job.completed_units > *job.total_units) ||
        (!job.completed_mp3.empty() && !candidatePath(job.completed_mp3)))
        return false;
    switch (job.state) {
    case DownloadJobState::Queued:
    case DownloadJobState::Downloading:
    case DownloadJobState::Converting:
    case DownloadJobState::Cancelled:
        return job.error == DownloadFailure::None && job.completed_mp3.empty();
    case DownloadJobState::Publishing:
        return job.error == DownloadFailure::None;
    case DownloadJobState::Cancelling:
        return job.completed_mp3.empty();
    case DownloadJobState::Completed:
    case DownloadJobState::Importing:
    case DownloadJobState::ImportFailed:
        return !job.completed_mp3.empty() &&
               job.error ==
                   (job.state == DownloadJobState::ImportFailed ? DownloadFailure::Import : DownloadFailure::None) &&
               (!job.total_units || job.completed_units == *job.total_units);
    case DownloadJobState::Imported:
        return !job.completed_mp3.empty() && job.error == DownloadFailure::None;
    case DownloadJobState::Failed:
        return job.error != DownloadFailure::None && job.completed_mp3.empty();
    case DownloadJobState::Interrupted:
        return job.error != DownloadFailure::None;
    }
    return false;
}
} // namespace

bool validDownloadCheckpoint(const DownloadCheckpoint& checkpoint) {
    if (checkpoint.jobs.size() > download_checkpoint_max_jobs ||
        checkpoint.receipts.size() > download_checkpoint_max_receipts)
        return false;
    std::size_t retained_bytes = 0;
    const auto retain = [&](const std::size_t bytes) {
        if (bytes > download_checkpoint_max_bytes - retained_bytes)
            return false;
        retained_bytes += bytes;
        return true;
    };
    std::unordered_set<std::string_view> job_ids;
    for (const auto& job : checkpoint.jobs) {
        if (!validJob(job) || !job_ids.insert(job.job_id).second || !retain(job.job_id.size()) ||
            !retain(job.title.size()) ||
            !retain(job.completed_mp3.native().size() * sizeof(std::filesystem::path::value_type)))
            return false;
    }
    std::unordered_set<std::string_view> request_ids;
    std::size_t original_bytes = 0;
    std::size_t job_index = 0;
    for (const auto& receipt : checkpoint.receipts) {
        if (!canonicalUuid(receipt.request_id) || !digest(receipt.fingerprint) ||
            !request_ids.insert(receipt.request_id).second || receipt.job_ids.empty() || receipt.job_ids.size() > 20 ||
            !retain(receipt.request_id.size()) || !retain(receipt.fingerprint.size()) ||
            receipt.retained_request_bytes > download_checkpoint_max_bytes - original_bytes ||
            receipt.retained_request_bytes > 36 + receipt.job_ids.size() * (8192 + 800))
            return false;
        std::size_t minimum_bytes = 36 + receipt.job_ids.size();
        for (const auto& id : receipt.job_ids) {
            // Admission order is also the one-to-one receipt/job mapping. This rejects
            // duplicates, omissions, reordered associations and orphan records.
            if (job_index >= checkpoint.jobs.size() || checkpoint.jobs[job_index].job_id != id || !retain(id.size()))
                return false;
            minimum_bytes += checkpoint.jobs[job_index++].title.size();
        }
        if (receipt.retained_request_bytes < minimum_bytes)
            return false;
        original_bytes += receipt.retained_request_bytes;
    }
    return job_index == checkpoint.jobs.size();
}
} // namespace music_player::m0
