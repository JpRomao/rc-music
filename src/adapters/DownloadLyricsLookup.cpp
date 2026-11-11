#include "DownloadLyricsLookup.hpp"
#include "QtLyricsService.hpp"

#include <QDir>
#include <QFileInfo>
#include <QUrl>

#include <algorithm>
#include <utility>

namespace music_player::m0 {
namespace {
QString owned(const QString& value) { return QString(value.constData(), value.size()); }

bool text(const QString& value, const qsizetype maximum, const bool optional) {
    if (value.size() > maximum)
        return false;
    const auto bytes = value.toUtf8();
    if (bytes.size() > maximum || QString::fromUtf8(bytes) != value)
        return false;
    if (value.trimmed().isEmpty())
        return optional;
    return std::none_of(value.begin(), value.end(),
                        [](const QChar unit) { return unit.unicode() < 0x20 || unit.unicode() == 0x7f; });
}

bool validPage(const QString& page) {
    if (page.isEmpty())
        return true;
    if (!text(page, 2048, false) || page.contains('\\'))
        return false;
    const QUrl url(page, QUrl::StrictMode);
    return url.isValid() && !url.isRelative() && url.scheme() == QStringLiteral("https") &&
           (url.host() == QStringLiteral("letras.mus.br") || url.host() == QStringLiteral("www.letras.mus.br")) &&
           url.userName().isEmpty() && url.password().isEmpty() && (url.port(-1) == -1 || url.port(-1) == 443) &&
           !url.hasQuery() && !url.hasFragment() && url.path().size() > 1;
}

PublishedLyricsRequest retained(const PublishedLyricsRequest& request) {
    return {request.job_id,        owned(request.final_mp3),   owned(request.title),
            owned(request.artist), owned(request.lyrics_page), owned(request.album),
            request.duration_ms};
}

QString providerNotice() {
    return DownloadLyricsLookup::tr("Letras indisponível nesta versão; busca alternativa no LRCLIB.");
}
} // namespace

bool DownloadLyricsLookup::validPublication(const PublishedLyricsRequest& request) {
    if (request.job_id.empty() || request.job_id.size() > 64 ||
        !std::all_of(request.job_id.begin(), request.job_id.end(),
                     [](const char unit) {
                         return (unit >= 'a' && unit <= 'z') || (unit >= 'A' && unit <= 'Z') ||
                                (unit >= '0' && unit <= '9') || unit == '-' || unit == '_';
                     }) ||
        !text(request.final_mp3, 8192, false) || request.final_mp3.startsWith("//") ||
        request.final_mp3.startsWith("\\\\") || !QFileInfo(request.final_mp3).isAbsolute() ||
        QFileInfo(request.final_mp3).suffix().compare(QStringLiteral("mp3"), Qt::CaseInsensitive) != 0 ||
        !text(request.title, 512, true) || !text(request.artist, 512, true) || !text(request.album, 512, true) ||
        !validPage(request.lyrics_page))
        return false;
    const auto path = QDir::fromNativeSeparators(request.final_mp3);
    return QDir::cleanPath(path) == path;
}

struct DownloadLyricsLookup::Impl final {
    DownloadLyricsLookup& owner;
    QtLyricsService& service;
    State state{State::Idle};
    QString message;
    std::optional<Result> result;
    std::optional<PublishedLyricsRequest> current;
    quint64 generation{};
    bool active_online{}, search_started{}, preparing{}, stopping{};

    Impl(DownloadLyricsLookup& observer, QtLyricsService& source) : owner(observer), service(source) {
        QObject::connect(&service, &QtLyricsService::changed, &owner, [this] { observe(); });
    }

    bool matches(const quint64 expected) const {
        return generation == expected && !preparing && !stopping && current.has_value();
    }

    quint64 reset(const State next) {
        ++generation;
        active_online = false;
        search_started = false;
        result.reset();
        current.reset();
        state = next;
        message.clear();
        return generation;
    }

    void notify(const quint64 expected) {
        if (generation == expected)
            emit owner.changed();
    }

    void observe() {
        if (!active_online || !current || stopping || preparing)
            return;
        const auto expected = generation;
        message = providerNotice();
        switch (service.status()) {
        case QtLyricsService::Status::Searching:
            state = State::Searching;
            break;
        case QtLyricsService::Status::Ready:
            if (service.source() != QtLyricsService::Source::Online || service.onlineDocuments().empty()) {
                active_online = false;
                state = State::Error;
                message += QStringLiteral(" ") + DownloadLyricsLookup::tr("O serviço retornou uma letra inválida.");
                break;
            }
            result = Result{generation, current->job_id, owned(current->final_mp3), service.onlineDocuments(),
                            QStringLiteral("LRCLIB")};
            state = State::Ready;
            active_online = false;
            break;
        case QtLyricsService::Status::NotFound:
            state = State::NotFound;
            active_online = false;
            break;
        case QtLyricsService::Status::Instrumental:
            state = State::Instrumental;
            active_online = false;
            break;
        case QtLyricsService::Status::Error:
            state = State::Error;
            active_online = false;
            if (!service.message().isEmpty())
                message += QStringLiteral(" ") + service.message();
            break;
        case QtLyricsService::Status::Idle:
        case QtLyricsService::Status::LoadingLocal:
            return;
        }
        notify(expected);
    }

    bool request(const PublishedLyricsRequest& input) {
        if (preparing || stopping)
            return false;
        // Bound and own every retained field before cancellation or notification
        // can invoke application callbacks and mutate the caller's storage.
        const bool valid = validPublication(input);
        auto candidate = valid ? std::optional<PublishedLyricsRequest>{retained(input)} : std::nullopt;
        const auto expected = reset(valid ? State::Idle : State::Error);
        current = std::move(candidate);
        service.cancel();
        if (generation != expected || preparing || stopping)
            return false;
        if (!current) {
            message = DownloadLyricsLookup::tr("Confira a faixa publicada e os limites dos metadados da letra.");
            notify(expected);
            return false;
        }
        service.setLookupTrack(current->final_mp3, current->title, current->artist, current->album,
                               current->duration_ms);
        if (!matches(expected))
            return false;
        message = providerNotice();
        if (current->title.trimmed().isEmpty() || current->artist.trimmed().isEmpty()) {
            state = State::NeedsMetadata;
            message += QStringLiteral(" ") + DownloadLyricsLookup::tr("Informe título e artista para buscar a letra.");
            notify(expected);
            return matches(expected);
        }
        if (!service.canSearch()) {
            state = State::Error;
            message += QStringLiteral(" ") + DownloadLyricsLookup::tr("Aguarde antes de tentar buscar novamente.");
            notify(expected);
            return matches(expected);
        }
        active_online = true;
        state = State::Searching;
        // searchOnline notifies Searching before issuing HTTP. Reentrant clear,
        // replacement or exit revokes that generation before its request escapes.
        const bool started = service.searchOnline();
        if (!matches(expected))
            return false;
        search_started = started;
        if (!started && active_online) {
            active_online = false;
            state = State::Error;
            message = providerNotice() + QStringLiteral(" ") +
                      DownloadLyricsLookup::tr("Não foi possível iniciar a busca da letra.");
            notify(expected);
        }
        return matches(expected);
    }

    void clear() {
        if (stopping)
            return;
        const auto expected = reset(State::Cancelled);
        service.cancel();
        notify(expected);
    }
};

DownloadLyricsLookup::DownloadLyricsLookup(QtLyricsService& service, QObject* parent)
    : QObject(parent), impl_(std::make_unique<Impl>(*this, service)) {}
DownloadLyricsLookup::~DownloadLyricsLookup() {
    impl_->stopping = true;
    impl_->reset(State::Cancelled);
    QObject::disconnect(&impl_->service, nullptr, this, nullptr);
    impl_->service.cancel();
}
bool DownloadLyricsLookup::request(const PublishedLyricsRequest& request) { return impl_->request(request); }
bool DownloadLyricsLookup::retry(QString title, QString artist) {
    auto& value = *impl_;
    if (value.preparing || value.stopping || !value.current)
        return false;
    auto next = retained(*value.current);
    next.title = std::move(title);
    next.artist = std::move(artist);
    return value.request(next) && value.search_started;
}
void DownloadLyricsLookup::clear() { impl_->clear(); }
bool DownloadLyricsLookup::prepareExit() {
    if (!impl_->preparing) {
        impl_->preparing = true;
        impl_->clear();
        return false;
    }
    return drained();
}
void DownloadLyricsLookup::resume() {
    if (impl_->stopping || !impl_->preparing)
        return;
    impl_->preparing = false;
    const auto expected = impl_->reset(State::Idle);
    impl_->notify(expected);
}
void DownloadLyricsLookup::beginShutdown() {
    if (impl_->stopping)
        return;
    impl_->stopping = true;
    impl_->preparing = true;
    const auto expected = impl_->reset(State::Cancelled);
    impl_->service.beginShutdown();
    impl_->notify(expected);
}
bool DownloadLyricsLookup::drained() const { return impl_->service.drained(); }
DownloadLyricsLookup::State DownloadLyricsLookup::state() const { return impl_->state; }
QString DownloadLyricsLookup::message() const { return impl_->message; }
const std::optional<DownloadLyricsLookup::Result>& DownloadLyricsLookup::result() const { return impl_->result; }
} // namespace music_player::m0
