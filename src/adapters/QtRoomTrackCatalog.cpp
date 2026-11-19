#include "adapters/QtRoomTrackCatalog.hpp"

#include "m0/domain/ListeningRoomLimits.hpp"
#include "m0/domain/LocalAudioFilePolicy.hpp"
#include "m0/domain/MetadataSanitization.hpp"

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QUtf8StringView>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace music_player::m0 {
namespace {

using Description = std::optional<RoomTrackDescription>;
using Summary = std::optional<RoomTrackSummary>;

// The peer's codec accepts a text only when sanitizeMetadataText leaves it unchanged.
std::string displayText(const std::optional<std::string>& value) {
    if (!value)
        return {};
    const auto sanitized = sanitizeMetadataText(*value);
    if (sanitized.disposition == MetadataTextDisposition::InvalidEncoding ||
        sanitizeMetadataText(sanitized.text).disposition != MetadataTextDisposition::Unchanged)
        return {};
    return sanitized.text;
}

std::string chosenLyrics(LyricsVariantsStore& store, const std::filesystem::path& source) {
    const auto loaded = store.load(source);
    if (loaded.status != LyricsVariantsLoadStatus::Loaded &&
        loaded.status != LyricsVariantsLoadStatus::RecoveredFromBackup)
        return {};
    const auto& variants = loaded.variants;
    if (variants.empty() || loaded.selection.kind == LyricsSelectionKind::Hidden ||
        loaded.selection.kind == LyricsSelectionKind::Source)
        return {};
    auto chosen = variants.begin();
    if (loaded.selection.kind == LyricsSelectionKind::Variant) {
        const auto found = std::find_if(variants.begin(), variants.end(), [&](const LyricsVariant& variant) {
            return variant.id == loaded.selection.variant_id;
        });
        if (found != variants.end())
            chosen = found;
    }
    const auto& content = chosen->content;
    if (content.size() > maximum_room_lyrics_bytes || !QUtf8StringView{content}.isValidUtf8())
        return {};
    return content;
}

Description describeTrack(const MetadataExtractor& metadata, LyricsVariantsStore* lyrics,
                          const std::filesystem::path& source) {
    try {
        QFile file{source};
        if (!file.open(QIODevice::ReadOnly))
            return std::nullopt;
        QCryptographicHash hash{QCryptographicHash::Sha256};
        if (!hash.addData(&file))
            return std::nullopt;
        RoomTrackDescription description;
        auto& item = description.item;
        item.origin = RoomItemOrigin::HostTrack;
        item.content_sha256 = hash.result().toHex().toStdString();
        item.size_bytes = static_cast<std::uint64_t>(file.size());
        file.close();

        try {
            const auto read = metadata.read(source);
            if (const auto* track = std::get_if<TrackMetadata>(&read)) {
                item.summary.title = displayText(track->tags.title);
                item.summary.artist = displayText(track->tags.artist);
                item.summary.album = displayText(track->tags.album);
                item.summary.duration_ms = std::max<std::int64_t>(track->audio.duration_ms.value_or(0), 0);
            }
        } catch (...) {
            item.summary = {}; // Unreadable tags: the file is still playable and shared without them.
        }
        if (lyrics != nullptr) {
            try {
                description.lyrics_lrc = chosenLyrics(*lyrics, source);
            } catch (...) {
                description.lyrics_lrc.clear();
            }
        }
        return description;
    } catch (...) {
        return std::nullopt;
    }
}

// The extension a local import would need for the container the extractor found ("" for any other).
std::string_view extensionFor(const std::string_view container) {
    if (container == "MP3")
        return ".mp3";
    if (container == "WAV")
        return ".wav";
    if (container == "FLAC")
        return ".flac";
    if (container == "ISO MP4/M4A")
        return ".m4a";
    if (container == "Ogg")
        return ".ogg";
    return {};
}

Summary probeUpload(const MetadataExtractor& metadata, const std::filesystem::path& file) {
    try {
        if (!QFileInfo{file}.isFile())
            return std::nullopt;
        const auto read = metadata.read(file);
        const auto* track = std::get_if<TrackMetadata>(&read);
        if (track == nullptr)
            return std::nullopt;
        const auto extension = extensionFor(track->audio.container);
        if (extension.empty() || !LocalAudioFilePolicy::isCandidate(std::filesystem::path{"upload"}.concat(extension)))
            return std::nullopt;
        RoomTrackSummary summary;
        summary.title = displayText(track->tags.title);
        summary.artist = displayText(track->tags.artist);
        summary.album = displayText(track->tags.album);
        summary.duration_ms = std::max<std::int64_t>(track->audio.duration_ms.value_or(0), 0);
        return summary;
    } catch (...) {
        return std::nullopt;
    }
}

} // namespace

QtRoomTrackCatalog::QtRoomTrackCatalog(std::shared_ptr<const MetadataExtractor> metadata,
                                       std::shared_ptr<LyricsVariantsStore> lyrics)
    : metadata_(std::move(metadata)), lyrics_(std::move(lyrics)) {}

QtRoomTrackCatalog::~QtRoomTrackCatalog() = default;

void QtRoomTrackCatalog::describe(std::filesystem::path source,
                                  std::function<void(std::optional<RoomTrackDescription>)> done) {
    if (!metadata_) {
        if (done)
            done(std::nullopt);
        return;
    }
    auto* watcher = new QFutureWatcher<Description>(&context_);
    QObject::connect(watcher, &QFutureWatcher<Description>::finished, &context_, [watcher, done = std::move(done)] {
        auto result = watcher->future().resultCount() > 0 ? watcher->result() : Description{};
        watcher->deleteLater();
        if (done)
            done(std::move(result));
    });
    watcher->setFuture(QtConcurrent::run([metadata = metadata_, lyrics = lyrics_, source = std::move(source)] {
        return describeTrack(*metadata, lyrics.get(), source);
    }));
}

void QtRoomTrackCatalog::probe(std::filesystem::path file, std::function<void(std::optional<RoomTrackSummary>)> done) {
    if (!metadata_) {
        if (done)
            done(std::nullopt);
        return;
    }
    auto* watcher = new QFutureWatcher<Summary>(&context_);
    QObject::connect(watcher, &QFutureWatcher<Summary>::finished, &context_, [watcher, done = std::move(done)] {
        auto result = watcher->future().resultCount() > 0 ? watcher->result() : Summary{};
        watcher->deleteLater();
        if (done)
            done(std::move(result));
    });
    watcher->setFuture(
        QtConcurrent::run([metadata = metadata_, file = std::move(file)] { return probeUpload(*metadata, file); }));
}

} // namespace music_player::m0
