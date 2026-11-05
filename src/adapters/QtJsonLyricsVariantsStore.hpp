#pragma once

#include "m0/ports/LyricsVariantsStore.hpp"

#include <QString>
#include <QtTypes>

#include <functional>

namespace music_player::m0 {
enum class LyricsVariantsStoreFaultPoint {
    BeforeBackupCommit,
    AfterBackupCommit,
    BeforePrimaryCommit,
    AfterPrimaryCommit
};
struct QtJsonLyricsVariantsStoreOptions final {
    std::function<bool(LyricsVariantsStoreFaultPoint)> inject_fault{};
};
// Passive construction. Documents live under config/lyrics using a SHA-256
// filename of the exact UTF-8 track path. The audio itself is never opened.
class QtJsonLyricsVariantsStore final : public LyricsVariantsStore {
  public:
    static constexpr qsizetype maximum_file_size = 10 * 1024 * 1024;
    explicit QtJsonLyricsVariantsStore(QString config_directory, QtJsonLyricsVariantsStoreOptions options = {});
    [[nodiscard]] LyricsVariantsLoadResult load(const std::filesystem::path& track) override;
    [[nodiscard]] LyricsVariantsSaveStatus save(const std::filesystem::path& track,
                                                const LyricsVariants& variants) override;

  private:
    QString config_directory_;
    QtJsonLyricsVariantsStoreOptions options_;
};
} // namespace music_player::m0
