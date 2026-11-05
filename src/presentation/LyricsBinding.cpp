#include "LyricsBinding.hpp"
#include "adapters/QtLyricsService.hpp"

namespace music_player::m0 {
namespace {
QString text(const std::string& value) { return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size())); }
QString owned(const QString& value) { return QString(value.constData(), value.size()); }
} // namespace
LyricsBinding::LyricsBinding(QtLyricsService& service, QObject* parent) : QObject(parent), service_(service) {
    connect(&service_, &QtLyricsService::changed, this, &LyricsBinding::changed);
}
void LyricsBinding::setTrack(const QString& path, const QString& title, const QString& artist, const QString& album,
                             const qint64 duration) {
    if (shutting_down_)
        return;
    if (exit_preparing_) {
        exit_preparing_ = false;
        service_.setTrack({}, {}, {}, {}, 0);
    }
    const bool metadata_changed = title_ != title || artist_ != artist;
    if (path_ != path)
        position_ms_ = 0;
    path_ = owned(path);
    title_ = owned(title);
    artist_ = owned(artist);
    service_.setTrack(path_, title_, artist_, album, duration);
    if (metadata_changed)
        emit changed();
}
void LyricsBinding::setPosition(const qint64 position) {
    const auto before = lyricsLineAt(service_.document(), position_ms_);
    position_ms_ = position;
    if (before != lyricsLineAt(service_.document(), position_ms_))
        emit changed();
}
void LyricsBinding::beginShutdown() {
    shutting_down_ = true;
    service_.beginShutdown();
}
bool LyricsBinding::stopped() const { return service_.stopped(); }
bool LyricsBinding::prepareExit() {
    if (!exit_preparing_) {
        exit_preparing_ = true;
        service_.cancel();
        // Force the presenter through its observable preparation phase even
        // when already drained; a synchronous save refusal can then rearm us.
        return false;
    }
    return service_.drained();
}
QString LyricsBinding::title() const { return title_; }
QString LyricsBinding::artist() const { return artist_; }
QString LyricsBinding::statusText() const {
    if (!service_.message().isEmpty())
        return service_.message();
    switch (service_.status()) {
    case QtLyricsService::Status::Idle:
        return {};
    case QtLyricsService::Status::LoadingLocal:
        return tr("Carregando letra…");
    case QtLyricsService::Status::Searching:
        return tr("Buscando no LRCLIB…");
    case QtLyricsService::Status::Ready:
        if (service_.source() == QtLyricsService::Source::Online)
            return hasSyncedLyrics() ? tr("Letra sincronizada · LRCLIB") : tr("Letra sem sincronização · LRCLIB");
        return hasSyncedLyrics() ? tr("Letra sincronizada · arquivo local")
                                 : tr("Letra sem sincronização · arquivo local");
    case QtLyricsService::Status::NotFound:
        return tr("Letra não encontrada. Você pode buscar online.");
    case QtLyricsService::Status::Instrumental:
        return tr("Faixa instrumental.");
    case QtLyricsService::Status::Error:
        return tr("Não foi possível carregar a letra.");
    }
    return {};
}
QString LyricsBinding::currentLine() const {
    const auto& doc = service_.document();
    const auto at = lyricsLineAt(doc, position_ms_);
    return at ? text(doc.lines[*at].text) : QString{};
}
QString LyricsBinding::previousLine() const {
    const auto& doc = service_.document();
    const auto at = lyricsLineAt(doc, position_ms_);
    return at && *at > 0 ? text(doc.lines[*at - 1].text) : QString{};
}
QString LyricsBinding::nextLine() const {
    const auto& doc = service_.document();
    const auto at = lyricsLineAt(doc, position_ms_);
    const auto next = at ? *at + 1 : 0;
    return next < doc.lines.size() ? text(doc.lines[next].text) : QString{};
}
QString LyricsBinding::plainText() const { return text(service_.document().plain_text); }
bool LyricsBinding::hasSyncedLyrics() const { return !service_.document().lines.empty(); }
bool LyricsBinding::busy() const { return service_.busy(); }
bool LyricsBinding::canSearch() const { return !shutting_down_ && !exit_preparing_ && service_.canSearch(); }
bool LyricsBinding::search(const QString& title, const QString& artist) {
    return !shutting_down_ && !exit_preparing_ && service_.searchOnline(title, artist);
}
} // namespace music_player::m0
