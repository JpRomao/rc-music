#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace music_player::m0 {

struct AudioOutputDevice final {
    std::string id;
    std::string description;
};

class AudioOutputSelection {
  public:
    virtual ~AudioOutputSelection() = default;

    [[nodiscard]] virtual std::vector<AudioOutputDevice> audioOutputs() const = 0;
    // An empty ID follows the system default. A disconnected explicit ID remains selected.
    [[nodiscard]] virtual std::string selectedAudioOutputId() const = 0;
    virtual bool selectAudioOutput(std::string_view id) = 0;
    // Called after the available outputs or selected ID change.
    virtual void setAudioOutputsChangedCallback(std::function<void()> callback) = 0;
};

} // namespace music_player::m0
