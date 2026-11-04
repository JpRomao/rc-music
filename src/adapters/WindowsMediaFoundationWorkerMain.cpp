#include "adapters/MediaFoundationWorkerProtocol.hpp"
#include "adapters/WindowsMediaFoundationCodec.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <optional>
#include <string_view>

namespace {
using namespace music_player::m0;
constexpr std::size_t io_chunk_bytes = 64 * 1024;
constexpr std::uint64_t maximum_progress_blocks = 60'000;
constexpr std::size_t maximum_progress_record_bytes = 64;
static_assert((maximum_progress_blocks + 1) * maximum_progress_record_bytes < 4 * 1024 * 1024);

bool pipeHandle(HANDLE handle) {
    return handle && handle != INVALID_HANDLE_VALUE && GetFileType(handle) == FILE_TYPE_PIPE;
}

bool writeAll(HANDLE handle, const std::span<const std::uint8_t> bytes) {
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const auto count = static_cast<DWORD>(std::min(io_chunk_bytes, bytes.size() - offset));
        DWORD written = 0;
        if (!WriteFile(handle, bytes.data() + offset, count, &written, nullptr) || written == 0 || written > count)
            return false;
        offset += written;
    }
    return true;
}

std::optional<MediaFoundationWorkerRequest> readRequest(HANDLE input) {
    std::vector<std::uint8_t> bytes;
    bytes.reserve(media_foundation_worker_max_request_bytes + 1);
    std::array<std::uint8_t, 4096> buffer{};
    for (;;) {
        const auto count =
            static_cast<DWORD>(std::min(buffer.size(), media_foundation_worker_max_request_bytes + 1 - bytes.size()));
        DWORD received = 0;
        if (!ReadFile(input, buffer.data(), count, &received, nullptr)) {
            if (GetLastError() == ERROR_BROKEN_PIPE)
                break;
            return std::nullopt;
        }
        if (received == 0)
            break;
        if (received > count)
            return std::nullopt;
        bytes.insert(bytes.end(), buffer.begin(), buffer.begin() + received);
        if (bytes.size() > media_foundation_worker_max_request_bytes)
            return std::nullopt;
    }
    return decodeMediaFoundationWorkerRequest(bytes);
}

bool writeProgress(HANDLE errors, const std::uint64_t processed_us, const bool ended) {
    std::array<char, maximum_progress_record_bytes> record{};
    constexpr std::string_view prefix = "out_time_us=";
    const std::string_view suffix = ended ? "\nprogress=end\n" : "\nprogress=continue\n";
    std::copy(prefix.begin(), prefix.end(), record.begin());
    const auto converted =
        std::to_chars(record.data() + prefix.size(), record.data() + record.size() - suffix.size(), processed_us);
    if (converted.ec != std::errc{})
        return false;
    std::copy(suffix.begin(), suffix.end(), converted.ptr);
    const auto size = static_cast<std::size_t>(converted.ptr - record.data()) + suffix.size();
    return writeAll(errors, {reinterpret_cast<const std::uint8_t*>(record.data()), size});
}

MediaFoundationWorkerExit run(const MediaFoundationWorkerRequest& request, HANDLE output, HANDLE errors) {
    using Clock = std::chrono::steady_clock;
    const auto interval =
        std::chrono::microseconds(std::max<std::uint64_t>(250'000, request.max_duration_us / 60'000 + 1));
    std::optional<Clock::time_point> last_progress;
    std::uint64_t progress_blocks = 0;
    std::uint64_t last_processed_us = 0;
    std::uint64_t output_bytes = 0;
    bool io_failed = false;
    bool limit_exceeded = false;
    const MediaFoundationCodecCallbacks callbacks{.write =
                                                      [&](const std::span<const std::uint8_t> bytes) {
                                                          if (io_failed || limit_exceeded)
                                                              return false;
                                                          if (request.mode != MediaFoundationWorkerMode::Convert) {
                                                              io_failed = !bytes.empty();
                                                              return !io_failed;
                                                          }
                                                          if (bytes.size() > request.max_output_bytes - output_bytes) {
                                                              limit_exceeded = true;
                                                              return false;
                                                          }
                                                          if (!writeAll(output, bytes)) {
                                                              io_failed = true;
                                                              return false;
                                                          }
                                                          output_bytes += bytes.size();
                                                          return true;
                                                      },
                                                  .progress =
                                                      [&](const std::uint64_t processed_us) {
                                                          if (io_failed || limit_exceeded)
                                                              return false;
                                                          if (processed_us > request.max_duration_us) {
                                                              limit_exceeded = true;
                                                              return false;
                                                          }
                                                          if (processed_us < last_processed_us) {
                                                              io_failed = true;
                                                              return false;
                                                          }
                                                          last_processed_us = processed_us;
                                                          const auto now = Clock::now();
                                                          if (progress_blocks >= maximum_progress_blocks ||
                                                              (last_progress && now - *last_progress < interval))
                                                              return true;
                                                          if (!writeProgress(errors, processed_us, false)) {
                                                              io_failed = true;
                                                              return false;
                                                          }
                                                          ++progress_blocks;
                                                          last_progress = now;
                                                          return true;
                                                      }};
    const auto result = runWindowsMediaFoundationCodec(request, callbacks);
    if (limit_exceeded)
        return MediaFoundationWorkerExit::LimitExceeded;
    if (io_failed)
        return MediaFoundationWorkerExit::Output;
    if (result == MediaFoundationWorkerExit::Success && !writeProgress(errors, last_processed_us, true))
        return MediaFoundationWorkerExit::Output;
    return result;
}
} // namespace

int main(int argc, char**) {
    using music_player::m0::MediaFoundationWorkerExit;
    try {
        // The codec loads MF entry points explicitly from System32 and borrows
        // one parent-selected inherited read handle. No media path, Qt,
        // environment path, command argument or page can configure this worker.
        if (!SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32))
            return static_cast<int>(MediaFoundationWorkerExit::Unavailable);
        const auto input = GetStdHandle(STD_INPUT_HANDLE);
        const auto output = GetStdHandle(STD_OUTPUT_HANDLE);
        const auto errors = GetStdHandle(STD_ERROR_HANDLE);
        if (argc != 1 || !pipeHandle(input) || !pipeHandle(output) || !pipeHandle(errors))
            return static_cast<int>(MediaFoundationWorkerExit::InvalidRequest);
        const auto request = readRequest(input);
        if (!request)
            return static_cast<int>(MediaFoundationWorkerExit::InvalidRequest);
        return static_cast<int>(run(*request, output, errors));
    } catch (...) {
        // Never emit free text, a path, codec diagnostics or exception content.
        return static_cast<int>(MediaFoundationWorkerExit::Unavailable);
    }
}
