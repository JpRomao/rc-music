#include "QtLyricsService.hpp"
#include "StrictJson.hpp"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QSet>
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
#include <cmath>
#include <limits>
#include <map>
#include <optional>
#include <tuple>
#include <utility>

namespace music_player::m0 {
namespace {
using Status = QtLyricsService::Status;
using Source = QtLyricsService::Source;
using Provider = QtLyricsService::OnlineProvider;
using SearchError = LyricsSearchError;
QString owned(const QString& value) { return QString(value.constData(), value.size()); }
QByteArray owned(const QByteArray& value) { return QByteArray(value.constData(), value.size()); }
bool content(const LyricsDocument& document) { return !document.lines.empty() || !document.plain_text.empty(); }
struct LocalResult final {
    quint64 generation{};
    Status status{Status::NotFound};
    LyricsDocument document;
};
LocalResult readLrc(const quint64 generation, const QString& path) {
    LocalResult result{generation, Status::NotFound, {}};
    const QFileInfo information(path);
    if (path.size() > 8192 || path.contains(QChar{0}) || !information.isAbsolute() || path.startsWith("//") ||
        path.startsWith("\\\\") || information.suffix().compare(QStringLiteral("lrc"), Qt::CaseInsensitive) != 0)
        return result;
    if (!information.exists())
        return result;
    if (!information.isFile() || information.isSymLink() || information.size() < 0 ||
        information.size() > QtLyricsService::maximum_local_bytes) {
        result.status = Status::Error;
        return result;
    }
#ifdef Q_OS_WIN
    const auto attributes = GetFileAttributesW(reinterpret_cast<const wchar_t*>(path.utf16()));
    if (attributes == INVALID_FILE_ATTRIBUTES ||
        (attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_OFFLINE))) {
        result.status = Status::Error;
        return result;
    }
#endif
    QFile file(path);
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
LocalResult readLocal(const quint64 generation, const QString& audio_path) {
    const QFileInfo audio(audio_path);
    if (audio_path.size() > 8192 || !audio.isAbsolute() || audio_path.startsWith("//") ||
        audio_path.startsWith("\\\\") || audio.completeBaseName().isEmpty())
        return {generation, Status::NotFound, {}};
    return readLrc(generation, audio.dir().filePath(audio.completeBaseName() + QStringLiteral(".lrc")));
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
bool validKey(const QByteArray& key) { return validClient(key) && !key.contains(' '); }
bool validLyricFind(const LyricFindOptions& options) {
    return validKey(options.display_key) && (options.lrc_key.isEmpty() || validKey(options.lrc_key)) &&
           options.territory.size() == 2 &&
           std::all_of(options.territory.begin(), options.territory.end(),
                       [](QChar c) { return c >= u'A' && c <= u'Z'; });
}
QUrl lrclibUrl(const LyricsQuery& metadata, const bool recording_search) {
    QUrl endpoint(recording_search ? QStringLiteral("https://lrclib.net/api/search")
                                   : QStringLiteral("https://lrclib.net/api/get"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("track_name"), QString::fromLatin1(QUrl::toPercentEncoding(metadata.title)));
    if (!metadata.artist.isEmpty())
        query.addQueryItem(QStringLiteral("artist_name"),
                           QString::fromLatin1(QUrl::toPercentEncoding(metadata.artist)));
    if (!metadata.album.isEmpty())
        query.addQueryItem(QStringLiteral("album_name"), QString::fromLatin1(QUrl::toPercentEncoding(metadata.album)));
    if (!recording_search && metadata.duration_ms >= 1000 && metadata.duration_ms <= 3'600'000)
        query.addQueryItem(QStringLiteral("duration"), QString::number(metadata.duration_ms / 1000));
    endpoint.setQuery(query);
    return endpoint;
}
QByteArray lyricFindBody(const LyricFindOptions& options, const LyricsQuery& metadata) {
    auto escapeTrackId = [](QString value) { return value.replace(u':', u';').replace(u',', u';'); };
    auto track_id =
        QStringLiteral("artistname:%1,trackname:%2").arg(escapeTrackId(metadata.artist), escapeTrackId(metadata.title));
    if (!metadata.album.isEmpty())
        track_id += QStringLiteral(",albumname:") + escapeTrackId(metadata.album);
    QByteArray body = "reqtype=default&output=json&territory=" + options.territory.toLatin1() +
                      "&apikey=" + QUrl::toPercentEncoding(QString::fromLatin1(options.display_key)) +
                      "&trackid=" + QUrl::toPercentEncoding(track_id);
    if (!options.lrc_key.isEmpty())
        body += "&format=lrc&lrckey=" + QUrl::toPercentEncoding(QString::fromLatin1(options.lrc_key));
    return body;
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

// LyricFind timestamps arrive separately from the text. Do not interpret a
// bracketed phrase inside a provider verse as another time or metadata tag.
LyricsDocument lyricFindSynced(const QJsonValue& value) {
    const auto rows = value.toArray();
    if (static_cast<std::size_t>(rows.size()) > lyrics_max_cues)
        return {};
    std::map<std::int64_t, std::string> cues;
    LyricsDocument document;
    bool first_row = true;
    for (const auto& row : rows) {
        const auto object = row.toObject();
        const auto line = object.value(QStringLiteral("line"));
        if (!line.isString())
            return {};
        const auto verse = line.toString();
        const auto bytes = verse.toUtf8();
        const auto separator = first_row ? std::size_t{0} : std::size_t{1};
        if (static_cast<std::size_t>(bytes.size()) + separator > lyrics_max_input_bytes - document.plain_text.size() ||
            QString::fromUtf8(bytes) != verse || std::any_of(verse.begin(), verse.end(), [](QChar unit) {
                return (unit.unicode() < 0x20 && unit != u'\t') || unit.unicode() == 0x7f;
            }))
            return {};
        if (!first_row)
            document.plain_text.push_back('\n');
        first_row = false;
        document.plain_text.append(bytes.constData(), static_cast<std::size_t>(bytes.size()));
        if (verse.isEmpty())
            continue;

        const auto milliseconds = object.value(QStringLiteral("milliseconds")).toString();
        if (milliseconds.isEmpty() || milliseconds.size() > 9 ||
            !std::all_of(milliseconds.begin(), milliseconds.end(), [](QChar c) { return c >= u'0' && c <= u'9'; }))
            return {};
        const auto time = milliseconds.toLongLong();
        if (time > lyrics_max_timestamp_ms)
            return {};
        auto [cue, inserted] = cues.try_emplace(time);
        if (!inserted)
            cue->second.push_back('\n');
        cue->second.append(bytes.constData(), static_cast<std::size_t>(bytes.size()));
    }
    while (!document.plain_text.empty() && document.plain_text.back() == '\n')
        document.plain_text.pop_back();
    document.lines.reserve(cues.size());
    for (auto& [time, verse] : cues)
        document.lines.push_back({time, std::move(verse)});
    return document;
}
struct OnlineResult final {
    Status status{Status::Error};
    std::vector<LyricsDocument> candidates{};
    QString message{}, attribution{};
};
OnlineResult availableLyrics(LyricsDocument synced, const QByteArray& plain) {
    const auto plain_result = plainDocument(plain);
    if (!plain_result)
        return {Status::Error, {}, QStringLiteral("O serviço retornou uma letra inválida.")};
    OnlineResult result{Status::Ready};
    if (!synced.lines.empty())
        result.candidates.push_back(std::move(synced));
    // A distinct plain candidate can differ from the synchronized one.
    if (content(*plain_result) &&
        (result.candidates.empty() || result.candidates.front().plain_text != plain_result->plain_text))
        result.candidates.push_back(*plain_result);
    if (result.candidates.empty())
        result.status = Status::NotFound;
    return result;
}
OnlineResult parseLrclib(const QJsonObject& object) {
    const auto synced = lyricsField(object, QStringLiteral("syncedLyrics"));
    const auto plain = lyricsField(object, QStringLiteral("plainLyrics"));
    const auto instrumental = object.value(QStringLiteral("instrumental"));
    if (!synced || !plain || !instrumental.isBool())
        return {Status::Error, {}, QStringLiteral("O serviço retornou uma resposta inválida.")};
    if (instrumental.toBool())
        return {Status::Instrumental};
    auto parsed = parseLrc({synced->constData(), static_cast<std::size_t>(synced->size())});
    return availableLyrics(parsed.error == LyricsParseError::None ? std::move(parsed.document) : LyricsDocument{},
                           *plain);
}
std::optional<QString> recordingMetadata(const QJsonValue& value, const bool required) {
    if (!required && (value.isUndefined() || value.isNull()))
        return QString{};
    if (!value.isString())
        return std::nullopt;
    const auto text = value.toString();
    if ((!required && text.isEmpty()) || (validMetadata(text) && !text.trimmed().isEmpty()))
        return text;
    return std::nullopt;
}
std::optional<LyricsSearchCandidate> parseRecording(const QJsonObject& object) {
    const auto id_value = object.value(QStringLiteral("id"));
    const auto id = id_value.toInteger(-1);
    auto title = recordingMetadata(object.value(QStringLiteral("trackName")), true);
    auto artist = recordingMetadata(object.value(QStringLiteral("artistName")), false);
    auto album = recordingMetadata(object.value(QStringLiteral("albumName")), false);
    const auto duration = object.value(QStringLiteral("duration"));
    const auto seconds = duration.isUndefined() || duration.isNull() ? 0.0 : duration.toDouble(-1.0);
    if (!id_value.isDouble() || id <= 0 || !title || !artist || !album || !std::isfinite(seconds) || seconds < 0.0 ||
        seconds > static_cast<double>(lyrics_max_timestamp_ms) / 1000.0)
        return std::nullopt;
    auto result = parseLrclib(object);
    if (result.status == Status::Error)
        return std::nullopt;
    return LyricsSearchCandidate{
        id,
        {std::move(*title), std::move(*artist), std::move(*album), static_cast<qint64>(std::llround(seconds * 1000.0))},
        result.status == Status::Instrumental,
        std::move(result.candidates)};
}
QString comparisonText(const QString& value) {
    return value.normalized(QString::NormalizationForm_KC).toCaseFolded().simplified();
}
LyricsSearchResult parseRecordings(const QJsonArray& records, const LyricsQuery& query) {
    constexpr auto maximum_recordings = 20;
    if (records.size() > maximum_recordings)
        return {SearchError::ResponseTooLarge, {}};
    LyricsSearchResult result;
    QSet<qint64> seen;
    for (const auto& row : records) {
        if (!row.isObject())
            return {SearchError::InvalidResponse, {}};
        auto candidate = parseRecording(row.toObject());
        if (!candidate)
            return {SearchError::InvalidResponse, {}};
        if (!candidate->instrumental && candidate->formats.empty())
            continue;
        if (!seen.contains(candidate->id)) {
            seen.insert(candidate->id);
            result.candidates.push_back(std::move(*candidate));
        }
    }
    const auto title = comparisonText(query.title);
    const auto artist = comparisonText(query.artist);
    const auto album = comparisonText(query.album);
    const auto rank = [&](const LyricsSearchCandidate& candidate) {
        const auto& recording = candidate.recording;
        const auto distance = query.duration_ms <= 0       ? qint64{0}
                              : recording.duration_ms <= 0 ? std::numeric_limits<qint64>::max()
                                                           : std::abs(query.duration_ms - recording.duration_ms);
        return std::tuple{comparisonText(recording.title) != title,
                          !artist.isEmpty() && comparisonText(recording.artist) != artist,
                          !album.isEmpty() && comparisonText(recording.album) != album, distance};
    };
    std::stable_sort(result.candidates.begin(), result.candidates.end(),
                     [&](const auto& left, const auto& right) { return rank(left) < rank(right); });
    return result;
}
OnlineResult parseLyricFind(const QJsonObject& object, const bool synced_requested) {
    const auto code = object.value(QStringLiteral("response")).toObject().value(QStringLiteral("code")).toInt(-1);
    if (code == 202 || code == 203 || code == 207)
        return {Status::NotFound};
    if (code == 102)
        return {Status::Instrumental};
    if (code == 200)
        return {Status::Error, {}, QStringLiteral("O LyricFind não autorizou a chave configurada.")};
    if (code >= 204 && code <= 206)
        return {Status::Error, {}, QStringLiteral("A licença do LyricFind não permite exibir esta letra.")};
    if (code != 101 && code != 111)
        return {Status::Error, {}, QStringLiteral("O LyricFind não conseguiu concluir a consulta.")};

    const auto track = object.value(QStringLiteral("track")).toObject();
    const auto copyright = track.value(QStringLiteral("copyright")).toString().simplified();
    const auto writer = track.value(QStringLiteral("writer")).toString().simplified();
    const auto plain = track.contains(QStringLiteral("lyrics")) ? lyricsField(track, QStringLiteral("lyrics"))
                                                                : std::optional<QByteArray>{QByteArray{}};
    if (!track.value(QStringLiteral("viewable")).toBool() || !validMetadata(copyright) || !validMetadata(writer) ||
        !plain)
        return {Status::Error, {}, QStringLiteral("O LyricFind retornou uma letra sem permissão ou créditos válidos.")};
    auto result = availableLyrics(
        synced_requested ? lyricFindSynced(track.value(QStringLiteral("lrc"))) : LyricsDocument{}, *plain);
    if (result.status == Status::Ready)
        result.attribution = copyright + QLatin1Char('\n') + QStringLiteral("Composição: %1").arg(writer);
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
    QString path, title, artist, album, message, occurrence_id, attribution;
    LyricFindOptions lyricfind;
    LyricsQuery search_metadata;
    SearchCompletion recording_completion;
    qint64 duration_ms{};
    quint64 generation{}, request_revision{};
    bool stopping{}, local_running{}, cooldown_forever{}, lookup_only{}, fallback_started{};
    bool recording_independent{};
    std::optional<PendingLocal> pending_local;
    Status status{Status::Idle};
    Source source{Source::None};
    Provider active_provider{Provider::LRCLIB}, displayed_provider{Provider::LRCLIB};
    LyricsDocument document;
    std::vector<LyricsDocument> online_documents;

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
            if (recording_completion || status == Status::Searching)
                networkFailure(QStringLiteral("A busca da letra excedeu o tempo limite."), SearchError::Timeout);
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
    void revokeRequest() {
        ++request_revision;
        recording_completion = {};
        recording_independent = false;
        cancelReply();
    }
    void revoke(const bool playback_changed = false) {
        ++generation;
        pending_local.reset();
        online_documents.clear();
        if (displayed_provider == Provider::LyricFind) {
            document = {};
            source = Source::None;
            displayed_provider = Provider::LRCLIB;
            attribution.clear();
        }
        if (!playback_changed || !recording_independent)
            revokeRequest();
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
    bool matches(const quint64 expected_revision, const QNetworkReply* expected_reply) const {
        return !stopping && request_revision == expected_revision && reply == expected_reply &&
               (recording_completion || status == Status::Searching);
    }
    void completeSearch(LyricsSearchResult result) {
        auto completion = std::exchange(recording_completion, {});
        recording_independent = false;
        cancelReply();
        QPointer<QtLyricsService> alive(&owner);
        completion(std::move(result));
        if (alive)
            emit alive->changed();
    }
    void networkFailure(QString text, const SearchError error = SearchError::Network) {
        if (recording_completion) {
            completeSearch({error, {}});
            return;
        }
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
            if (!handleRateLimit(reply))
                networkFailure(QStringLiteral("A resposta de letras excedeu o limite permitido."),
                               SearchError::ResponseTooLarge);
            return false;
        }
        response.append(chunk);
        return true;
    }
    bool startRequest(const Provider provider, const quint64 expected_revision) {
        if (!network || !matches(expected_revision, nullptr))
            return false;
        active_provider = provider;
        const bool lyricfind_request = provider == Provider::LyricFind;
        const auto endpoint = lyricfind_request ? QUrl(QStringLiteral("https://api.lyricfind.com/lyric.do"))
                                                : lrclibUrl(search_metadata, bool(recording_completion));
        QNetworkRequest request(endpoint);
        request.setRawHeader("User-Agent", client);
        request.setRawHeader("Accept", "application/json");
        // Qt 6.11.2 mitigation for CVE-2026-96432.
        request.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
        request.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
        request.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
        request.setAttribute(QNetworkRequest::AuthenticationReuseAttribute, QNetworkRequest::Manual);
        request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
        request.setAttribute(QNetworkRequest::CacheSaveControlAttribute, false);
        request.setTransferTimeout(std::chrono::milliseconds(timeout.interval()));
        QNetworkReply* next{};
        if (lyricfind_request) {
            request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/x-www-form-urlencoded"));
            next = network->post(request, lyricFindBody(lyricfind, search_metadata));
        } else {
            next = network->get(request);
        }
        if (!matches(expected_revision, nullptr)) {
            if (next) {
                next->abort();
                next->deleteLater();
            }
            return false;
        }
        if (!next) {
            networkFailure(QStringLiteral("O serviço de letras está indisponível."), SearchError::Unavailable);
            return false;
        }
        reply = next;
        next->setReadBufferSize(maximum_response_bytes + 1);
        QObject::connect(next, &QNetworkReply::readyRead, &owner, [this, expected_revision, next] {
            if (matches(expected_revision, next))
                static_cast<void>(consume());
        });
        QObject::connect(next, &QNetworkReply::finished, &owner,
                         [this, expected_revision, next] { finish(expected_revision, next); });
        timeout.start();
        if (next->isFinished())
            finish(expected_revision, next);
        return true;
    }
    void applyResult(OnlineResult result, const quint64 expected_revision) {
        const auto provider = active_provider;
        cancelReply();
        if (result.status == Status::NotFound && !fallback_started && !lyricfind.display_key.isEmpty() && network) {
            fallback_started = true;
            static_cast<void>(
                startRequest(provider == Provider::LRCLIB ? Provider::LyricFind : Provider::LRCLIB, expected_revision));
            return;
        }
        status = result.status;
        message = std::move(result.message);
        if (status == Status::NotFound && message.isEmpty())
            message = QStringLiteral("Letra não encontrada para esta versão da música.");
        if (status == Status::Error || status == Status::NotFound) {
            emit owner.changed();
            return;
        }
        if (status == Status::Instrumental)
            message = QStringLiteral("Esta faixa foi identificada como instrumental.");
        online_documents = std::move(result.candidates);
        document = online_documents.empty() ? LyricsDocument{} : online_documents.front();
        source = Source::Online;
        displayed_provider = status == Status::Ready ? provider : Provider::LRCLIB;
        attribution = std::move(result.attribution);
        emit owner.changed();
    }
    bool handleRateLimit(const QNetworkReply* request) {
        const auto http_status = request->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (http_status != 429 && !(recording_completion && http_status == 503 && request->hasRawHeader("Retry-After")))
            return false;
        rateLimit(request->rawHeader("Retry-After"));
        networkFailure(QStringLiteral("O serviço limitou as consultas. Aguarde antes de buscar novamente."),
                       SearchError::RateLimited);
        return true;
    }
    void finishRecordings(const int http_status, const QNetworkReply& finished) {
        if (http_status == 404)
            return completeSearch({});
        if (http_status == 401 || http_status == 403)
            return completeSearch({SearchError::AccessDenied, {}});
        if (http_status != 200 || finished.error() != QNetworkReply::NoError)
            return completeSearch({SearchError::Network, {}});
        const auto media_type = finished.rawHeader("Content-Type").split(';').front().trimmed().toLower();
        if (media_type != "application/json" || !detail::strictJsonPreflight(response))
            return completeSearch({SearchError::InvalidResponse, {}});
        auto json = QJsonDocument::fromJson(response);
        completeSearch(json.isArray() ? parseRecordings(json.array(), search_metadata)
                                      : LyricsSearchResult{SearchError::InvalidResponse, {}});
    }
    void finish(const quint64 expected_revision, QNetworkReply* expected_reply) {
        if (!matches(expected_revision, expected_reply) || handleRateLimit(expected_reply) || !consume())
            return;
        const auto http_status = expected_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (recording_completion)
            return finishRecordings(http_status, *expected_reply);
        if (http_status == 404) {
            applyResult({Status::NotFound}, expected_revision);
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
        if (!json.isObject() || (active_provider == Provider::LyricFind && !detail::strictJsonPreflight(response, 8))) {
            networkFailure(QStringLiteral("O serviço retornou uma resposta inválida."));
            return;
        }
        auto result = active_provider == Provider::LyricFind
                          ? parseLyricFind(json.object(), !lyricfind.lrc_key.isEmpty())
                          : parseLrclib(json.object());
        applyResult(std::move(result), expected_revision);
    }
};

QtLyricsService::QtLyricsService(QNetworkAccessManager* borrowed_network, QObject* parent, QByteArray client_identifier,
                                 const int request_timeout_ms)
    : QObject(parent),
      impl_(std::make_unique<Impl>(*this, borrowed_network, std::move(client_identifier), request_timeout_ms)) {}
QtLyricsService::~QtLyricsService() = default;
std::optional<LyricsDocument> QtLyricsService::readLrcFile(const QString& path) {
    auto result = readLrc(0, path);
    if (result.status != Status::Ready)
        return std::nullopt;
    return std::move(result.document);
}
LyricFindOptions QtLyricsService::loadLyricFindOptions(const QString& private_config_root) {
    constexpr qint64 maximum_config_bytes = 4096;
    if (private_config_root.size() > 8192 || !QDir::isAbsolutePath(private_config_root) ||
        private_config_root.startsWith("//") || private_config_root.startsWith("\\\\"))
        return {};
    const auto path = QDir(private_config_root).filePath(QStringLiteral("lyricfind.json"));
    const QFileInfo info(path);
    if (info.isSymLink() || !info.isFile() || info.size() < 0 || info.size() > maximum_config_bytes)
        return {};
#ifdef Q_OS_WIN
    const auto attributes = GetFileAttributesW(reinterpret_cast<const wchar_t*>(path.utf16()));
    if (attributes == INVALID_FILE_ATTRIBUTES ||
        (attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_OFFLINE)))
        return {};
#endif
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Unbuffered))
        return {};
    const auto bytes = file.read(maximum_config_bytes + 1);
    if (file.error() != QFileDevice::NoError || bytes.size() > maximum_config_bytes ||
        !detail::strictJsonPreflight(bytes))
        return {};
    const auto object = QJsonDocument::fromJson(bytes).object();
    const auto display = object.value(QStringLiteral("displayKey"));
    const auto lrc = object.value(QStringLiteral("lrcKey"));
    const auto territory = object.value(QStringLiteral("territory"));
    const auto preferred = object.value(QStringLiteral("preferLyricFind"));
    if (!display.isString() || (!lrc.isUndefined() && !lrc.isString()) || !territory.isString() ||
        (!preferred.isUndefined() && !preferred.isBool()))
        return {};
    LyricFindOptions options{display.toString().toUtf8(), lrc.toString().toUtf8(), territory.toString(),
                             preferred.toBool()};
    return validLyricFind(options) ? options : LyricFindOptions{};
}
void QtLyricsService::configureLyricFind(LyricFindOptions options) {
    auto& value = *impl_;
    if (value.stopping || value.status == Status::Searching)
        return;
    value.lyricfind = validLyricFind(options) ? LyricFindOptions{owned(options.display_key), owned(options.lrc_key),
                                                                 owned(options.territory), options.preferred}
                                              : LyricFindOptions{};
}
bool QtLyricsService::lyricFindConfigured() const noexcept { return !impl_->lyricfind.display_key.isEmpty(); }
QtLyricsService::OnlineProvider QtLyricsService::onlineProvider() const noexcept { return impl_->displayed_provider; }
QString QtLyricsService::onlineAttribution() const { return impl_->attribution; }
void QtLyricsService::setTrack(QString path, QString title, QString artist, QString album, const qint64 duration_ms,
                               const QString& occurrence_id) {
    auto& value = *impl_;
    if (value.stopping)
        return;
    path = owned(path);
    title = owned(title);
    artist = owned(artist);
    album = owned(album);
    const bool same_path = path == value.path && !value.lookup_only;
    const bool occurrence_changed = occurrence_id != value.occurrence_id;
    const bool changed_metadata =
        title != value.title || artist != value.artist || album != value.album || duration_ms != value.duration_ms;
    value.title = std::move(title);
    value.artist = std::move(artist);
    value.album = std::move(album);
    value.duration_ms = duration_ms;
    value.lookup_only = false;
    if (occurrence_changed)
        value.occurrence_id = owned(occurrence_id);
    if (same_path) {
        const bool revoke_online = occurrence_changed && (value.status == Status::Searching ||
                                                          value.displayed_provider == Provider::LyricFind);
        const bool revoke_search = occurrence_changed && value.recording_completion && !value.recording_independent;
        if (revoke_online) {
            value.revoke(true);
            value.status = content(value.document) ? Status::Ready : Status::NotFound;
            value.message.clear();
        }
        if (revoke_search && !revoke_online)
            value.revokeRequest();
        if (changed_metadata || revoke_online || revoke_search)
            emit this->changed();
        return;
    }
    value.revoke(true);
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
void QtLyricsService::setLookupTrack(QString path, QString title, QString artist, QString album,
                                     const qint64 duration_ms) {
    auto& value = *impl_;
    if (value.stopping)
        return;
    value.revoke();
    value.path = owned(path);
    value.title = owned(title);
    value.artist = owned(artist);
    value.album = owned(album);
    value.duration_ms = duration_ms;
    value.lookup_only = true;
    value.document = {};
    value.source = Source::None;
    value.status = Status::Idle;
    value.message.clear();
    emit changed();
}
bool QtLyricsService::searchOnline(QString title_override, QString artist_override) {
    auto& value = *impl_;
    if (value.stopping || value.path.isEmpty() || !value.network || !validClient(value.client) ||
        value.status == Status::Searching || value.recording_completion || value.cooldown.isActive() ||
        value.cooldown_forever)
        return false;
    const auto title = title_override.trimmed().isEmpty() ? value.title.trimmed() : title_override.trimmed();
    const auto artist = artist_override.trimmed().isEmpty() ? value.artist.trimmed() : artist_override.trimmed();
    if (!validMetadata(title) || !validMetadata(artist)) {
        value.message = QStringLiteral("Informe o título e o artista para buscar a letra.");
        emit changed();
        return false;
    }
    value.revoke();
    const auto request_revision = value.request_revision;
    const auto album = validMetadata(value.album) ? value.album.trimmed() : QString{};
    value.search_metadata = {owned(title), owned(artist), owned(album), value.duration_ms};
    value.fallback_started = false;
    value.status = Status::Searching;
    value.message.clear();
    emit changed();
    if (!value.matches(request_revision, nullptr))
        return false;
    value.cooldown.start(500);
    const auto preferred =
        lyricFindConfigured() && value.lyricfind.preferred ? OnlineProvider::LyricFind : OnlineProvider::LRCLIB;
    return value.startRequest(preferred, request_revision);
}
bool QtLyricsService::search(const LyricsQuery& query, SearchCompletion completion,
                             const bool independent_of_playback) {
    if (!completion)
        return false;
    auto& value = *impl_;
    const auto fail = [&](const SearchError error) {
        completion({error, {}});
        return false;
    };
    if (value.stopping || (!independent_of_playback && value.path.isEmpty()) || !value.network ||
        !validClient(value.client) || value.status == Status::Searching || value.recording_completion)
        return fail(SearchError::Unavailable);
    if (value.cooldown.isActive() || value.cooldown_forever)
        return fail(SearchError::RateLimited);
    const auto title = query.title.trimmed();
    const auto artist = query.artist.trimmed();
    const auto album = query.album.trimmed();
    if (!validMetadata(title) || (!artist.isEmpty() && !validMetadata(artist)) ||
        (!album.isEmpty() && !validMetadata(album)))
        return fail(SearchError::InvalidQuery);
    value.search_metadata = {owned(title), owned(artist), owned(album),
                             std::clamp<qint64>(query.duration_ms, 0, lyrics_max_timestamp_ms)};
    value.recording_completion = std::move(completion);
    value.recording_independent = independent_of_playback;
    value.cooldown.start(500);
    const auto request_revision = ++value.request_revision;
    const QPointer<QtLyricsService> alive(this);
    emit changed();
    if (!alive || !value.matches(request_revision, nullptr))
        return false;
    return value.startRequest(Provider::LRCLIB, request_revision);
}
void QtLyricsService::cancelSearch() {
    auto& value = *impl_;
    if (!value.recording_completion)
        return;
    value.revokeRequest();
    emit changed();
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
const std::vector<LyricsDocument>& QtLyricsService::onlineDocuments() const noexcept { return impl_->online_documents; }
quint64 QtLyricsService::contextRevision() const noexcept { return impl_->generation; }
QString QtLyricsService::message() const { return impl_->message; }
bool QtLyricsService::canSearch() const noexcept {
    return !impl_->stopping && !impl_->path.isEmpty() && impl_->network && validClient(impl_->client) &&
           impl_->status != Status::Searching && !impl_->recording_completion && !impl_->cooldown.isActive() &&
           !impl_->cooldown_forever;
}
bool QtLyricsService::busy() const noexcept {
    return impl_->status == Status::LoadingLocal || impl_->status == Status::Searching || impl_->recording_completion;
}
} // namespace music_player::m0
