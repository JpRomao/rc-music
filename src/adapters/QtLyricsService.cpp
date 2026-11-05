#include "QtLyricsService.hpp"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QStringDecoder>
#include <QTimer>
#include <QUrlQuery>
#include <QtConcurrent/QtConcurrentRun>

#ifdef Q_OS_WIN
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

#include <algorithm>
#include <chrono>
#include <limits>
#include <optional>
#include <utility>

namespace music_player::m0 {
namespace {
using Status = QtLyricsService::Status;
using Source = QtLyricsService::Source;
QString owned(const QString& value) { return QString(value.constData(), value.size()); }
QByteArray owned(const QByteArray& value) { return QByteArray(value.constData(), value.size()); }
bool content(const LyricsDocument& document) { return !document.lines.empty() || !document.plain_text.empty(); }
struct LocalResult final {
    quint64 generation{};
    Status status{Status::NotFound};
    LyricsDocument document;
};
LocalResult readLocal(const quint64 generation, const QString& audio_path) {
    LocalResult result{generation, Status::NotFound, {}};
    const QFileInfo audio(audio_path);
    if (audio_path.size() > 8192 || !audio.isAbsolute() || audio_path.startsWith("//") ||
        audio_path.startsWith("\\\\") || audio.completeBaseName().isEmpty())
        return result;
    const auto sidecar = audio.dir().filePath(audio.completeBaseName() + QStringLiteral(".lrc"));
    const QFileInfo information(sidecar);
    if (!information.exists())
        return result;
    if (!information.isFile() || information.isSymLink() || information.size() < 0 ||
        information.size() > QtLyricsService::maximum_local_bytes) {
        result.status = Status::Error;
        return result;
    }
#ifdef Q_OS_WIN
    const auto attributes = GetFileAttributesW(reinterpret_cast<const wchar_t*>(sidecar.utf16()));
    if (attributes == INVALID_FILE_ATTRIBUTES ||
        (attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_OFFLINE))) {
        result.status = Status::Error;
        return result;
    }
#endif
    QFile file(sidecar);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Unbuffered)) {
        result.status = Status::Error;
        return result;
    }
    const auto bytes = file.read(QtLyricsService::maximum_local_bytes + 1);
    if (file.error() != QFileDevice::NoError || bytes.size() > QtLyricsService::maximum_local_bytes) {
        result.status = Status::Error;
        return result;
    }
    auto parsed = parseLrc(std::string_view{bytes.constData(), static_cast<std::size_t>(bytes.size())});
    if (parsed.error != LyricsParseError::None) {
        result.status = Status::Error;
        return result;
    }
    result.document = std::move(parsed.document);
    result.status = content(result.document) ? Status::Ready : Status::NotFound;
    return result;
}
bool validMetadata(const QString& value) {
    if (value.isEmpty() || value.size() > 512)
        return false;
    for (const auto unit : value)
        if (unit.unicode() < 0x20 || unit.unicode() == 0x7f)
            return false;
    const auto bytes = value.toUtf8();
    return QString::fromUtf8(bytes) == value;
}
bool validClient(const QByteArray& value) {
    return !value.isEmpty() && value.size() <= 256 && std::all_of(value.begin(), value.end(), [](const auto byte) {
        const auto unit = static_cast<unsigned char>(byte);
        return unit >= 0x20 && unit < 0x7f;
    });
}
std::optional<QByteArray> lyricsField(const QJsonObject& object, const QString& key) {
    const auto value = object.value(key);
    if (value.isNull())
        return QByteArray{};
    if (!value.isString())
        return std::nullopt;
    const auto text = value.toString();
    if (text.size() > QtLyricsService::maximum_local_bytes)
        return std::nullopt;
    auto bytes = text.toUtf8();
    if (bytes.size() > QtLyricsService::maximum_local_bytes || QString::fromUtf8(bytes) != text)
        return std::nullopt;
    return bytes;
}
std::optional<LyricsDocument> plainDocument(const QByteArray& bytes) {
    QStringDecoder decoder(QStringDecoder::Utf8, QStringDecoder::Flag::Stateless);
    const QString text = decoder(bytes);
    if (decoder.hasError())
        return std::nullopt;
    for (const auto unit : text)
        if (unit.unicode() == 0 || (unit.unicode() < 0x20 && unit != u'\n' && unit != u'\r' && unit != u'\t') ||
            unit.unicode() == 0x7f)
            return std::nullopt;
    // Plain provider data is not LRC: preserve bracketed section labels and
    // timestamp-looking text, without inventing synchronized cues.
    auto normalized = text;
    normalized.replace("\r\n", "\n");
    normalized.replace('\r', '\n');
    const auto utf8 = normalized.toUtf8();
    LyricsDocument result;
    result.plain_text.assign(utf8.constData(), static_cast<std::size_t>(utf8.size()));
    return result;
}
} // namespace

struct QtLyricsService::Impl final {
    struct PendingLocal final {
        quint64 generation;
        QString path;
    };
    QtLyricsService& owner;
    QPointer<QNetworkAccessManager> network;
    QPointer<QNetworkReply> reply;
    QFutureWatcher<LocalResult> watcher;
    QTimer timeout, cooldown;
    QByteArray client, response;
    QString path, title, artist, album, message;
    qint64 duration_ms{};
    quint64 generation{};
    bool stopping{}, local_running{}, cooldown_forever{};
    std::optional<PendingLocal> pending_local;
    Status status{Status::Idle};
    Source source{Source::None};
    LyricsDocument document;

    Impl(QtLyricsService& service, QNetworkAccessManager* borrowed, QByteArray identifier, const int timeout_ms)
        : owner(service), network(borrowed ? borrowed : new QNetworkAccessManager(&service)),
          client(owned(identifier)) {
        timeout.setSingleShot(true);
        timeout.setInterval(std::clamp(timeout_ms, 1, 60'000));
        cooldown.setSingleShot(true);
        cooldown.setTimerType(Qt::PreciseTimer);
        QObject::connect(&cooldown, &QTimer::timeout, &owner, [this] {
            if (!stopping)
                emit owner.changed();
        });
        QObject::connect(&timeout, &QTimer::timeout, &owner, [this] {
            if (status == Status::Searching) {
                cancelReply();
                status = Status::Error;
                message = QStringLiteral("A busca da letra excedeu o tempo limite.");
                emit owner.changed();
            }
        });
        QObject::connect(&watcher, &QFutureWatcher<LocalResult>::finished, &owner, [this] {
            auto result = watcher.future().takeResult();
            local_running = false;
            if (!stopping && result.generation == generation && status == Status::LoadingLocal) {
                document = std::move(result.document);
                status = result.status;
                source = status == Status::Ready ? Source::Local : Source::None;
                message = status == Status::Error ? QStringLiteral("Não foi possível ler a letra local.") : QString{};
                emit owner.changed();
            }
            // At most one running read and one replacement are retained.
            if (!stopping && pending_local) {
                auto pending = std::move(*pending_local);
                pending_local.reset();
                if (pending.generation == generation && status == Status::LoadingLocal)
                    startLocal(std::move(pending));
            }
            if (stopping)
                emit owner.changed();
        });
    }
    ~Impl() {
        QObject::disconnect(&watcher, nullptr, &owner, nullptr);
        cancelReply();
        // QFutureWatcher disconnects only. The bounded reader captures data,
        // never this service or a QObject, and is not joined on the UI thread.
    }
    void cancelReply() {
        timeout.stop();
        auto previous = reply;
        reply.clear();
        response.clear();
        if (previous) {
            QObject::disconnect(previous, nullptr, &owner, nullptr);
            previous->abort();
            previous->deleteLater();
        }
    }
    void revoke() {
        ++generation;
        pending_local.reset();
        cancelReply();
    }
    void startLocal(PendingLocal pending) {
        if (local_running) {
            pending_local = std::move(pending);
            return;
        }
        local_running = true;
        watcher.setFuture(QtConcurrent::run([pending = std::move(pending)] {
            try {
                return readLocal(pending.generation, pending.path);
            } catch (...) {
                return LocalResult{pending.generation, Status::Error, {}};
            }
        }));
    }
    bool matches(const quint64 expected_generation, const QNetworkReply* expected_reply) const {
        return !stopping && generation == expected_generation && reply == expected_reply && status == Status::Searching;
    }
    void networkFailure(QString text) {
        cancelReply();
        status = Status::Error;
        message = std::move(text);
        emit owner.changed();
    }
    void rateLimit(const QByteArray& retry_after) {
        const auto header = retry_after.trimmed();
        qint64 wait_ms = 60'000;
        if (!header.isEmpty() && header.size() <= 128) {
            const bool digits =
                std::all_of(header.begin(), header.end(), [](const auto byte) { return byte >= '0' && byte <= '9'; });
            if (digits) {
                bool valid = false;
                const auto seconds = header.toLongLong(&valid);
                if (!valid || seconds > std::numeric_limits<int>::max() / 1000) {
                    // Do not shorten an unrepresentable server deadline. The
                    // library and playback continue to work for this session.
                    cooldown_forever = true;
                    cooldown.stop();
                    return;
                }
                wait_ms = seconds * 1000;
            } else {
                const auto date = QDateTime::fromString(QString::fromLatin1(header), Qt::RFC2822Date);
                if (date.isValid()) {
                    wait_ms = std::max(qint64{0}, QDateTime::currentDateTimeUtc().msecsTo(date));
                    if (wait_ms > std::numeric_limits<int>::max()) {
                        cooldown_forever = true;
                        cooldown.stop();
                        return;
                    }
                }
            }
        }
        wait_ms = std::max(wait_ms, qint64{cooldown.remainingTime()});
        if (wait_ms > 0)
            cooldown.start(static_cast<int>(wait_ms));
    }
    bool consume() {
        if (!reply)
            return false;
        const auto remaining = maximum_response_bytes - response.size();
        const auto chunk = reply->read(remaining + 1);
        if (chunk.size() > remaining || reply->bytesAvailable() > 0) {
            networkFailure(QStringLiteral("A resposta de letras excedeu o limite permitido."));
            return false;
        }
        response.append(chunk);
        return true;
    }
    void finish(const quint64 expected_generation, QNetworkReply* expected_reply) {
        if (!matches(expected_generation, expected_reply) || !consume())
            return;
        const auto http_status = expected_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (http_status == 429) {
            rateLimit(expected_reply->rawHeader("Retry-After"));
            networkFailure(QStringLiteral("O serviço limitou as consultas. Aguarde antes de buscar novamente."));
            return;
        }
        if (http_status == 404) {
            cancelReply();
            status = Status::NotFound;
            message = QStringLiteral("Letra não encontrada para esta versão da música.");
            emit owner.changed();
            return;
        }
        if (http_status != 200 || expected_reply->error() != QNetworkReply::NoError) {
            networkFailure(QStringLiteral("Não foi possível buscar a letra agora."));
            return;
        }
        QStringDecoder decoder(QStringDecoder::Utf8, QStringDecoder::Flag::Stateless);
        const QString decoded = decoder(response);
        if (decoder.hasError()) {
            networkFailure(QStringLiteral("O serviço retornou uma letra inválida."));
            return;
        }
        const auto json = QJsonDocument::fromJson(decoded.toUtf8());
        if (!json.isObject()) {
            networkFailure(QStringLiteral("O serviço retornou uma resposta inválida."));
            return;
        }
        const auto object = json.object();
        const auto synced = lyricsField(object, QStringLiteral("syncedLyrics"));
        const auto plain = lyricsField(object, QStringLiteral("plainLyrics"));
        const auto instrumental = object.value(QStringLiteral("instrumental"));
        if (!synced || !plain || !instrumental.isBool()) {
            networkFailure(QStringLiteral("O serviço retornou uma resposta inválida."));
            return;
        }
        if (instrumental.toBool()) {
            cancelReply();
            document = {};
            status = Status::Instrumental;
            source = Source::Online;
            message = QStringLiteral("Esta faixa foi identificada como instrumental.");
            emit owner.changed();
            return;
        }
        auto parsed = parseLrc(std::string_view{synced->constData(), static_cast<std::size_t>(synced->size())});
        if (parsed.error != LyricsParseError::None || parsed.document.lines.empty()) {
            const auto plain_result = plainDocument(*plain);
            if (!plain_result) {
                networkFailure(QStringLiteral("O serviço retornou uma letra inválida."));
                return;
            }
            parsed.error = LyricsParseError::None;
            parsed.document = *plain_result;
        }
        cancelReply();
        document = std::move(parsed.document);
        source = Source::Online;
        status = content(document) ? Status::Ready : Status::NotFound;
        message.clear();
        emit owner.changed();
    }
};

QtLyricsService::QtLyricsService(QNetworkAccessManager* borrowed_network, QObject* parent, QByteArray client_identifier,
                                 const int request_timeout_ms)
    : QObject(parent),
      impl_(std::make_unique<Impl>(*this, borrowed_network, std::move(client_identifier), request_timeout_ms)) {}
QtLyricsService::~QtLyricsService() = default;
void QtLyricsService::setTrack(QString path, QString title, QString artist, QString album, const qint64 duration_ms) {
    auto& value = *impl_;
    if (value.stopping)
        return;
    path = owned(path);
    title = owned(title);
    artist = owned(artist);
    album = owned(album);
    const bool same_path = path == value.path;
    const bool changed_metadata =
        title != value.title || artist != value.artist || album != value.album || duration_ms != value.duration_ms;
    value.title = std::move(title);
    value.artist = std::move(artist);
    value.album = std::move(album);
    value.duration_ms = duration_ms;
    if (same_path) {
        if (changed_metadata && value.reply) {
            value.revoke();
            value.status = content(value.document) ? Status::Ready : Status::NotFound;
            value.message.clear();
        }
        if (changed_metadata)
            emit this->changed();
        return;
    }
    value.revoke();
    value.path = std::move(path);
    value.document = {};
    value.source = Source::None;
    value.message.clear();
    value.status = value.path.isEmpty() ? Status::Idle : Status::LoadingLocal;
    const auto generation = value.generation;
    emit changed();
    if (!value.stopping && value.generation == generation && value.status == Status::LoadingLocal)
        value.startLocal({generation, owned(value.path)});
}
bool QtLyricsService::searchOnline(QString title_override, QString artist_override) {
    auto& value = *impl_;
    if (value.stopping || value.path.isEmpty() || !value.network || !validClient(value.client) ||
        value.status == Status::Searching || value.cooldown.isActive() || value.cooldown_forever)
        return false;
    const auto title = title_override.trimmed().isEmpty() ? value.title.trimmed() : title_override.trimmed();
    const auto artist = artist_override.trimmed().isEmpty() ? value.artist.trimmed() : artist_override.trimmed();
    if (!validMetadata(title) || !validMetadata(artist)) {
        value.message = QStringLiteral("Informe o título e o artista para buscar a letra.");
        emit changed();
        return false;
    }
    value.revoke();
    const auto generation = value.generation;
    QUrl endpoint(QStringLiteral("https://lrclib.net/api/get"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("track_name"), title);
    query.addQueryItem(QStringLiteral("artist_name"), artist);
    if (!value.album.trimmed().isEmpty() && validMetadata(value.album))
        query.addQueryItem(QStringLiteral("album_name"), value.album.trimmed());
    if (value.duration_ms >= 1000 && value.duration_ms <= 3'600'000)
        query.addQueryItem(QStringLiteral("duration"), QString::number(value.duration_ms / 1000));
    endpoint.setQuery(query);
    QNetworkRequest request(endpoint);
    request.setRawHeader("User-Agent", value.client);
    request.setRawHeader("Accept", "application/json");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::AuthenticationReuseAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
    request.setAttribute(QNetworkRequest::CacheSaveControlAttribute, false);
    request.setTransferTimeout(std::chrono::milliseconds(value.timeout.interval()));
    value.status = Status::Searching;
    value.message.clear();
    emit changed();
    if (value.stopping || value.generation != generation || value.status != Status::Searching)
        return false;
    value.cooldown.start(500);
    auto* reply = value.network->get(request);
    if (!reply) {
        value.networkFailure(QStringLiteral("O serviço de letras está indisponível."));
        return false;
    }
    if (value.stopping || value.generation != generation || value.status != Status::Searching) {
        reply->abort();
        reply->deleteLater();
        return false;
    }
    value.reply = reply;
    reply->setReadBufferSize(maximum_response_bytes + 1);
    connect(reply, &QNetworkReply::readyRead, this, [this, generation, reply] {
        if (impl_->matches(generation, reply))
            static_cast<void>(impl_->consume());
    });
    connect(reply, &QNetworkReply::finished, this, [this, generation, reply] { impl_->finish(generation, reply); });
    value.timeout.start();
    if (reply->isFinished())
        value.finish(generation, reply);
    return true;
}
void QtLyricsService::cancel() {
    auto& value = *impl_;
    value.revoke();
    value.document = {};
    value.source = Source::None;
    value.status = Status::Idle;
    value.message.clear();
    emit changed();
}
void QtLyricsService::beginShutdown() {
    if (impl_->stopping)
        return;
    impl_->stopping = true;
    impl_->cooldown.stop();
    cancel();
}
bool QtLyricsService::stopped() const noexcept { return impl_->stopping && !impl_->local_running && !impl_->reply; }
bool QtLyricsService::drained() const noexcept {
    return !impl_->local_running && !impl_->reply && !impl_->pending_local;
}
QtLyricsService::Status QtLyricsService::status() const noexcept { return impl_->status; }
QtLyricsService::Source QtLyricsService::source() const noexcept { return impl_->source; }
const LyricsDocument& QtLyricsService::document() const noexcept { return impl_->document; }
QString QtLyricsService::message() const { return impl_->message; }
bool QtLyricsService::canSearch() const noexcept {
    return !impl_->stopping && !impl_->path.isEmpty() && impl_->network && validClient(impl_->client) &&
           impl_->status != Status::Searching && !impl_->cooldown.isActive() && !impl_->cooldown_forever;
}
bool QtLyricsService::busy() const noexcept {
    return impl_->status == Status::LoadingLocal || impl_->status == Status::Searching;
}
} // namespace music_player::m0
