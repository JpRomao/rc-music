#pragma once

#include <filesystem>
#include <functional>

namespace music_player::m0 {

enum class AudioFileLocationStatus { Selected, Cancelled, Failed };

struct AudioFileLocationResult final {
    AudioFileLocationStatus status{AudioFileLocationStatus::Cancelled};
    // A Selected result carries a complete native local-file path, never a URL or relative path.
    std::filesystem::path source_path{};
};

using AudioFileLocationCompletion = std::function<void(AudioFileLocationResult)>;

class AudioFileLocator {
  public:
    virtual ~AudioFileLocator() = default;
    // Called on the UI thread. Accept at most one request; deliver its completion at most once
    // on the same thread. Selection is not proof of successful decoding or file availability.
    [[nodiscard]] virtual bool locate(const std::filesystem::path& missing_source,
                                      AudioFileLocationCompletion completion) = 0;
    // Discard the pending completion, including when the owner shuts down.
    virtual void cancel() noexcept = 0;
};

} // namespace music_player::m0
