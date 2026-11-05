#pragma once

#include <QByteArray>
#include <QString>
#include <QtTypes>

#include <cstddef>
#include <memory>
#include <vector>

namespace music_player::m0 {
enum class WindowsYoutubeRuntimeRole { Extractor, Node, EjsCore, EjsLibrary, Support };
struct WindowsYoutubeRuntimeSource final {
    WindowsYoutubeRuntimeRole role{};
    QString relative_path{};
    QString source_path{};
    QByteArray sha256{};
};
struct WindowsYoutubeRuntimeConfig final {
    // Trusted, reviewed C++ configuration only. Never a page, checkpoint or
    // directory-discovery manifest. Include the complete selected onedir tree.
    QString runtime_parent{};
    std::vector<WindowsYoutubeRuntimeSource> artifacts{};
};
enum class WindowsYoutubeRuntimeError { None, InvalidConfiguration, FileAccess, HashMismatch, LimitExceeded };

inline constexpr std::size_t youtube_runtime_max_files = 256;
inline constexpr qsizetype youtube_runtime_max_relative_units = 1024;
inline constexpr qsizetype youtube_runtime_max_segments = 16;
inline constexpr quint64 youtube_runtime_max_bytes = 512ULL * 1024 * 1024;

// Worker-only preparation of a private, closed inventory, reusing the download
// artifact pins. Hashes establish consistency with trusted configuration, not
// publisher trust, dependency completeness or executable compatibility.
// This lease has no process configuration or launch authority: stock yt-dlp's
// Node child remains incompatible with NoChild. A qualified broker is separate.
// Retain the lease until every future user has acknowledged native/pipe stop.
class WindowsYoutubeRuntime final {
  public:
    static std::shared_ptr<const WindowsYoutubeRuntime> prepare(const WindowsYoutubeRuntimeConfig& config,
                                                                WindowsYoutubeRuntimeError* error = nullptr);
    ~WindowsYoutubeRuntime();
    WindowsYoutubeRuntime(const WindowsYoutubeRuntime&) = delete;
    WindowsYoutubeRuntime& operator=(const WindowsYoutubeRuntime&) = delete;
    [[nodiscard]] QString directory() const;
    // Support is not a unique role and returns an empty path.
    [[nodiscard]] QString artifactPath(WindowsYoutubeRuntimeRole role) const;
    [[nodiscard]] bool unchanged() const;

  private:
    struct Impl;
    explicit WindowsYoutubeRuntime(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};
} // namespace music_player::m0
