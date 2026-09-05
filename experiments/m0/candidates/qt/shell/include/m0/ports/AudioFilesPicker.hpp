#pragma once

#include <filesystem>
#include <functional>
#include <vector>

namespace music_player::m0 {

enum class AudioFilesSelectionStatus { Selected, Cancelled, Failed };

struct AudioFilesSelectionResult final {
    AudioFilesSelectionStatus status{AudioFilesSelectionStatus::Cancelled};
    // Complete native local-file paths, preserving selection order and duplicates.
    // Selection is not proof of successful decoding or file availability.
    std::vector<std::filesystem::path> source_paths{};
};

using AudioFilesSelectionCompletion = std::function<void(AudioFilesSelectionResult)>;

class AudioFilesPicker {
  public:
    virtual ~AudioFilesPicker() = default;
    // UI thread only. Accept at most one request and complete at most once on that thread.
    [[nodiscard]] virtual bool pick(AudioFilesSelectionCompletion completion) = 0;
    // Discard any pending completion, including when its owner shuts down.
    virtual void cancel() noexcept = 0;
};

} // namespace music_player::m0
