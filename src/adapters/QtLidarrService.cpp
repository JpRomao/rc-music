#include "QtLidarrService.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QStringDecoder>
#include <QTimer>
#include <QUrlQuery>
#include <QUuid>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <optional>
#include <utility>

namespace music_player::m0 {
namespace {
using Service = QtLidarrService;
using Status = Service::Status;
using Error = Service::Error;
using Kind = Service::Kind;
QString owned(const QString& value) { return QString(value.constData(), value.size()); }
bool validText(const QString& value, const qsizetype maximum = 512, const bool empty_allowed = false) {
    if ((!empty_allowed && value.trimmed().isEmpty()) || value.size() > maximum ||
        QString::fromUtf8(value.toUtf8()) != value)
        return false;
    return std::none_of(value.begin(), value.end(),
                        [](const QChar unit) { return unit.unicode() < 0x20 || unit.unicode() == 0x7f; });
}
bool validId(const QString& value) {
    return value.size() == 36 && !QUuid::fromString(value).isNull() &&
           QUuid::fromString(value).toString(QUuid::WithoutBraces) == value.toLower();
}
std::optional<int> integer(const QJsonValue& value, const bool optional = false) {
    if (optional && (value.isUndefined() || value.isNull()))
        return 0;
    if (!value.isDouble())
        return {};
    const auto number = value.toDouble();
    if (!std::isfinite(number) || number < 0 || number > std::numeric_limits<int>::max() ||
        std::trunc(number) != number)
        return {};
    return static_cast<int>(number);
}
std::optional<QUrl> baseAddress(const QString& address) {
    if (address.size() > 4096 || !validText(address, 4096))
        return {};
    const QUrl url(address.trimmed(), QUrl::StrictMode);
    if (!url.isValid() || (url.scheme() != QStringLiteral("http") && url.scheme() != QStringLiteral("https")) ||
        url.host().isEmpty() || !url.userInfo().isEmpty() || url.hasQuery() || url.hasFragment())
        return {};
    const auto path = url.path(QUrl::FullyDecoded);
    if (path.contains('\\') || !validText(path, 4096, true))
        return {};
    for (const auto& component : path.split('/'))
        if (component == QStringLiteral(".") || component == QStringLiteral(".."))
            return {};
    auto result = url;
    auto normalized_path = result.path();
    while (normalized_path.endsWith('/'))
        normalized_path.chop(1);
    result.setPath(normalized_path);
    return result;
}
bool validKey(const QString& key) {
    if (key.isEmpty() || key.size() > 256)
        return false;
    return std::all_of(key.begin(), key.end(), [](const QChar unit) {
        const auto value = unit.unicode();
        return value >= 0x21 && value <= 0x7e;
    });
}
bool containsProfile(const QVector<Service::Profile>& profiles, const int id) {
    return id > 0 &&
           std::any_of(profiles.begin(), profiles.end(), [id](const auto& profile) { return profile.id == id; });
}
} // namespace

struct QtLidarrService::Impl final {
    enum class Step { Status, Roots, Quality, Metadata, Lookup, Mutation };
    struct Lookup final {
        ResultItem item;
        QString foreign_id;
        QString artist_foreign_id;
    };
    QtLidarrService& owner;
    QPointer<QNetworkAccessManager> network;
    QPointer<QNetworkReply> reply;
    QTimer timeout;
    QUrl base;
    QByteArray api_key;
    QByteArray response;
    QVector<ResultItem> results;
    QVector<Lookup> lookups;
    QStringList roots;
    QVector<Profile> quality;
    QVector<Profile> metadata;
    Status status{Status::Disconnected};
    Error error{Error::None};
    Kind lookup_kind{Kind::Album};
    Step step{Step::Status};
    quint64 generation{};
    quint64 selection_generation{};
    bool connected{}, stopping{}, mutation_is_add{};
    QString mutation_foreign_id;

    Impl(QtLidarrService& q, QNetworkAccessManager* borrowed, const int timeout_ms) : owner(q), network(borrowed) {
        if (!network)
            network = new QNetworkAccessManager(&owner);
        timeout.setSingleShot(true);
        timeout.setTimerType(Qt::PreciseTimer);
        timeout.setInterval(std::clamp(timeout_ms, 1, 60'000));
        QObject::connect(&timeout, &QTimer::timeout, &owner, [this] {
            if (reply)
                fail(Error::Timeout);
        });
    }
    ~Impl() { cancelReply(); }
    bool busy() const {
        return status == Status::Connecting || status == Status::Searching || status == Status::Requesting;
    }
    void cancelReply() {
        timeout.stop();
        const auto previous = reply;
        reply.clear();
        response.clear();
        if (previous) {
            QObject::disconnect(previous, nullptr, &owner, nullptr);
            if (!previous->isFinished())
                previous->abort();
            previous->deleteLater();
        }
    }
    void revoke() {
        ++generation;
        cancelReply();
    }
    void clearResults() {
        ++selection_generation;
        results.clear();
        lookups.clear();
    }
    bool notify(const quint64 expected_generation) {
        const QPointer<QtLidarrService> alive(&owner);
        emit owner.changed();
        return alive && !stopping && generation == expected_generation;
    }
    bool matches(const quint64 expected_generation, const QNetworkReply* expected_reply) const {
        return !stopping && generation == expected_generation && reply == expected_reply && busy();
    }
    void fail(const Error failure) {
        cancelReply();
        error = failure;
        status = Status::Error;
        if (!connected) {
            roots.clear();
            quality.clear();
            metadata.clear();
        }
        emit owner.changed();
    }
    bool consume() {
        if (!reply)
            return false;
        const auto remaining = maximum_response_bytes - response.size();
        const auto chunk = reply->read(remaining + 1);
        if (chunk.size() > remaining || reply->bytesAvailable() > 0) {
            fail(Error::Limit);
            return false;
        }
        response.append(chunk);
        return true;
    }
    bool request(const Step next, const QString& endpoint, const QUrlQuery& query = {}, const QByteArray& body = {}) {
        if (stopping || !network || reply)
            return false;
        auto url = base;
        url.setPath(base.path() + QStringLiteral("/api/v1/") + endpoint);
        url.setQuery(query);
        QNetworkRequest request(url);
        request.setRawHeader("X-Api-Key", api_key);
        request.setRawHeader("Accept", "application/json");
        request.setRawHeader("User-Agent", "RCMusic/0.1");
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
        request.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
        request.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
        request.setAttribute(QNetworkRequest::AuthenticationReuseAttribute, QNetworkRequest::Manual);
        request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
        request.setAttribute(QNetworkRequest::CacheSaveControlAttribute, false);
        request.setTransferTimeout(std::chrono::milliseconds(timeout.interval()));
        if (!body.isEmpty())
            request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
        const auto operation_generation = generation;
        step = next;
        response.clear();
        const QPointer<QtLidarrService> alive(&owner);
        auto* result = body.isEmpty() ? network->get(request) : network->post(request, body);
        if (!alive || stopping || generation != operation_generation || !busy()) {
            if (result) {
                result->abort();
                result->deleteLater();
            }
            return false;
        }
        if (!result) {
            fail(Error::Network);
            return false;
        }
        reply = result;
        result->setReadBufferSize(maximum_response_bytes + 1);
        QObject::connect(result, &QNetworkReply::readyRead, &owner, [this, operation_generation, result] {
            if (matches(operation_generation, result))
                static_cast<void>(consume());
        });
        QObject::connect(result, &QNetworkReply::finished, &owner,
                         [this, operation_generation, result] { finish(operation_generation, result); });
        timeout.start();
        if (result->isFinished())
            finish(operation_generation, result);
        return true;
    }
    bool parseRoots(const QJsonDocument& document) {
        if (!document.isArray())
            return false;
        const auto array = document.array();
        if (array.size() > maximum_results) {
            error = Error::Limit;
            return false;
        }
        QStringList parsed;
        for (const auto& entry : array) {
            if (!entry.isObject() || !entry.toObject().value(QStringLiteral("path")).isString())
                return false;
            const auto path = entry.toObject().value(QStringLiteral("path")).toString();
            if (!validText(path, 4096) || parsed.contains(path))
                return false;
            parsed.push_back(owned(path));
        }
        roots = std::move(parsed);
        return true;
    }
    bool parseProfiles(const QJsonDocument& document, QVector<Profile>& target) {
        if (!document.isArray())
            return false;
        const auto array = document.array();
        if (array.size() > maximum_results) {
            error = Error::Limit;
            return false;
        }
        QVector<Profile> parsed;
        for (const auto& entry : array) {
            if (!entry.isObject())
                return false;
            const auto object = entry.toObject();
            const auto id = integer(object.value(QStringLiteral("id")));
            const auto name = object.value(QStringLiteral("name"));
            if (!id || *id <= 0 || !name.isString() || !validText(name.toString()) || containsProfile(parsed, *id))
                return false;
            parsed.push_back({*id, owned(name.toString())});
        }
        target = std::move(parsed);
        return true;
    }
    bool parseLookup(const QJsonDocument& document) {
        if (!document.isArray())
            return false;
        const auto array = document.array();
        if (array.size() > maximum_results) {
            error = Error::Limit;
            return false;
        }
        QVector<Lookup> parsed;
        QVector<ResultItem> projected;
        for (const auto& entry : array) {
            if (!entry.isObject())
                return false;
            const auto object = entry.toObject();
            const auto id = integer(object.value(QStringLiteral("id")), true);
            const auto title =
                object.value(lookup_kind == Kind::Album ? QStringLiteral("title") : QStringLiteral("artistName"));
            const auto foreign = object.value(lookup_kind == Kind::Album ? QStringLiteral("foreignAlbumId")
                                                                         : QStringLiteral("foreignArtistId"));
            if (!id || !title.isString() || !validText(title.toString()) || !foreign.isString() ||
                !validId(foreign.toString()))
                return false;
            Lookup value;
            value.item.kind = lookup_kind;
            value.item.title = owned(title.toString());
            value.item.existing_id = *id;
            value.foreign_id = owned(foreign.toString());
            if (lookup_kind == Kind::Album) {
                const auto artist = object.value(QStringLiteral("artist"));
                if (!artist.isObject())
                    return false;
                const auto name = artist.toObject().value(QStringLiteral("artistName"));
                const auto artist_foreign = artist.toObject().value(QStringLiteral("foreignArtistId"));
                if (!name.isString() || !validText(name.toString()) || !artist_foreign.isString() ||
                    !validId(artist_foreign.toString()))
                    return false;
                value.item.artist = owned(name.toString());
                value.artist_foreign_id = owned(artist_foreign.toString());
                const auto date = object.value(QStringLiteral("releaseDate"));
                if (!date.isUndefined() && !date.isNull()) {
                    if (!date.isString() || !validText(date.toString(), 64, true))
                        return false;
                    const auto year = date.toString().left(4);
                    if (year.size() == 4 &&
                        std::all_of(year.begin(), year.end(), [](const auto ch) { return ch >= '0' && ch <= '9'; }))
                        value.item.year = year;
                }
            } else {
                value.item.artist = value.item.title;
                value.artist_foreign_id = value.foreign_id;
            }
            value.item.key = QString::number(selection_generation) + QLatin1Char(':') + QString::number(parsed.size());
            projected.push_back(value.item);
            parsed.push_back(std::move(value));
        }
        lookups = std::move(parsed);
        results = std::move(projected);
        return true;
    }
    void finish(const quint64 operation_generation, QNetworkReply* expected_reply) {
        if (!matches(operation_generation, expected_reply) || !consume())
            return;
        const auto http_status = expected_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (http_status == 401 || http_status == 403) {
            fail(Error::Authentication);
            return;
        }
        if (http_status < 200 || http_status >= 300) {
            fail(http_status == 0 ? Error::Network : Error::Http);
            return;
        }
        if (expected_reply->error() != QNetworkReply::NoError) {
            fail(Error::Network);
            return;
        }
        QStringDecoder decoder(QStringDecoder::Utf8, QStringDecoder::Flag::Stateless);
        const QString decoded = decoder(response);
        if (decoder.hasError()) {
            fail(Error::Response);
            return;
        }
        QJsonParseError parse_error;
        const auto document = QJsonDocument::fromJson(decoded.toUtf8(), &parse_error);
        if (parse_error.error != QJsonParseError::NoError) {
            fail(Error::Response);
            return;
        }
        const auto completed = step;
        bool valid = false;
        switch (completed) {
        case Step::Status:
            valid = document.isObject() && document.object().value(QStringLiteral("version")).isString() &&
                    validText(document.object().value(QStringLiteral("version")).toString(), 128);
            break;
        case Step::Roots:
            valid = parseRoots(document);
            break;
        case Step::Quality:
            valid = parseProfiles(document, quality);
            break;
        case Step::Metadata:
            valid = parseProfiles(document, metadata);
            break;
        case Step::Lookup:
            valid = parseLookup(document);
            break;
        case Step::Mutation: {
            const auto object = document.object();
            const auto id = integer(object.value(QStringLiteral("id")));
            valid = document.isObject() && id && *id > 0;
            if (valid && mutation_is_add)
                valid = object.value(QStringLiteral("foreignAlbumId")).toString() == mutation_foreign_id;
            break;
        }
        }
        if (!valid) {
            fail(error == Error::Limit ? Error::Limit : Error::Response);
            return;
        }
        cancelReply();
        error = Error::None;
        if (completed == Step::Metadata) {
            connected = true;
            status = Status::Ready;
        } else if (completed == Step::Lookup) {
            status = results.isEmpty() ? Status::NoResults : Status::Ready;
        } else if (completed == Step::Mutation) {
            status = Status::Requested;
        }
        if (!notify(operation_generation))
            return;
        switch (completed) {
        case Step::Status:
            static_cast<void>(request(Step::Roots, QStringLiteral("rootfolder")));
            break;
        case Step::Roots:
            static_cast<void>(request(Step::Quality, QStringLiteral("qualityprofile")));
            break;
        case Step::Quality:
            static_cast<void>(request(Step::Metadata, QStringLiteral("metadataprofile")));
            break;
        default:
            break;
        }
    }
};

QtLidarrService::QtLidarrService(QNetworkAccessManager* borrowed_network, QObject* parent, const int request_timeout_ms)
    : QObject(parent), impl_(std::make_unique<Impl>(*this, borrowed_network, request_timeout_ms)) {}
QtLidarrService::~QtLidarrService() = default;
bool QtLidarrService::connectServer(QString address, QString key) {
    auto& value = *impl_;
    if (value.stopping)
        return false;
    const auto parsed = baseAddress(address);
    const bool key_valid = validKey(key);
    const auto detached_key = key_valid ? key.toUtf8() : QByteArray{};
    value.revoke();
    value.clearResults();
    value.connected = false;
    value.base = {};
    value.api_key.clear();
    value.roots.clear();
    value.quality.clear();
    value.metadata.clear();
    if (!parsed || !key_valid) {
        value.fail(Error::Configuration);
        return false;
    }
    value.base = *parsed;
    value.api_key = detached_key;
    value.error = Error::None;
    value.status = Status::Connecting;
    const auto generation = value.generation;
    if (!value.notify(generation))
        return false;
    return value.request(Impl::Step::Status, QStringLiteral("system/status"));
}
void QtLidarrService::disconnectServer() {
    auto& value = *impl_;
    value.revoke();
    value.clearResults();
    value.connected = false;
    value.base = {};
    value.api_key.clear();
    value.roots.clear();
    value.quality.clear();
    value.metadata.clear();
    value.error = Error::None;
    value.status = Status::Disconnected;
    emit changed();
}
bool QtLidarrService::search(QString term, const Kind kind) {
    auto& value = *impl_;
    term = owned(term.trimmed());
    if (value.stopping || !value.connected || value.busy() || !validText(term.trimmed()))
        return false;
    if (kind != Kind::Album && kind != Kind::Artist)
        return false;
    value.revoke();
    value.clearResults();
    value.lookup_kind = kind;
    value.error = Error::None;
    value.status = Status::Searching;
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("term"), term.trimmed());
    const auto generation = value.generation;
    if (!value.notify(generation))
        return false;
    return value.request(Impl::Step::Lookup,
                         kind == Kind::Album ? QStringLiteral("album/lookup") : QStringLiteral("artist/lookup"), query);
}
bool QtLidarrService::requestAlbum(const QString& result_key, const QString& root_folder, const int quality_id,
                                   const int metadata_id) {
    auto& value = *impl_;
    if (value.stopping || !value.connected || value.busy())
        return false;
    const auto found = std::find_if(value.lookups.begin(), value.lookups.end(), [&](const auto& lookup) {
        return lookup.item.key == result_key && lookup.item.kind == Kind::Album;
    });
    if (found == value.lookups.end())
        return false;
    const auto selected = *found;
    const bool existing = selected.item.existing_id > 0;
    if (!existing && (!value.roots.contains(root_folder) || !containsProfile(value.quality, quality_id) ||
                      !containsProfile(value.metadata, metadata_id)))
        return false;
    QJsonObject body;
    if (existing) {
        body = {{QStringLiteral("name"), QStringLiteral("AlbumSearch")},
                {QStringLiteral("albumIds"), QJsonArray{selected.item.existing_id}}};
    } else {
        const QJsonObject artist{{QStringLiteral("foreignArtistId"), selected.artist_foreign_id},
                                 {QStringLiteral("artistName"), selected.item.artist},
                                 {QStringLiteral("rootFolderPath"), owned(root_folder)},
                                 {QStringLiteral("qualityProfileId"), quality_id},
                                 {QStringLiteral("metadataProfileId"), metadata_id},
                                 {QStringLiteral("monitored"), true},
                                 {QStringLiteral("monitorNewItems"), QStringLiteral("none")}};
        body = {{QStringLiteral("foreignAlbumId"), selected.foreign_id},
                {QStringLiteral("title"), selected.item.title},
                {QStringLiteral("artist"), artist},
                {QStringLiteral("monitored"), true},
                {QStringLiteral("anyReleaseOk"), true},
                {QStringLiteral("addOptions"), QJsonObject{{QStringLiteral("searchForNewAlbum"), true}}}};
    }
    // Consume the handle before the POST, including ambiguous failure/cancel.
    // A fresh lookup and another explicit confirmation are required to retry.
    value.lookups.erase(found);
    value.results.erase(std::remove_if(value.results.begin(), value.results.end(),
                                       [&](const auto& result) { return result.key == selected.item.key; }),
                        value.results.end());
    value.revoke();
    value.mutation_is_add = !existing;
    value.mutation_foreign_id = selected.foreign_id;
    value.error = Error::None;
    value.status = Status::Requesting;
    const auto generation = value.generation;
    if (!value.notify(generation))
        return false;
    return value.request(Impl::Step::Mutation, existing ? QStringLiteral("command") : QStringLiteral("album"), {},
                         QJsonDocument(body).toJson(QJsonDocument::Compact));
}
void QtLidarrService::cancel() {
    auto& value = *impl_;
    if (!value.busy())
        return;
    value.revoke();
    value.error = Error::Cancelled;
    value.status = Status::Error;
    emit changed();
}
void QtLidarrService::beginShutdown() {
    if (impl_->stopping)
        return;
    impl_->stopping = true;
    disconnectServer();
}
bool QtLidarrService::drained() const noexcept { return !impl_->reply; }
bool QtLidarrService::connected() const noexcept { return impl_->connected; }
bool QtLidarrService::busy() const noexcept { return impl_->busy(); }
QtLidarrService::Status QtLidarrService::status() const noexcept { return impl_->status; }
QtLidarrService::Error QtLidarrService::error() const noexcept { return impl_->error; }
QString QtLidarrService::serverAddress() const { return impl_->base.toString(); }
const QVector<QtLidarrService::ResultItem>& QtLidarrService::results() const noexcept { return impl_->results; }
const QStringList& QtLidarrService::rootFolders() const noexcept { return impl_->roots; }
const QVector<QtLidarrService::Profile>& QtLidarrService::qualityProfiles() const noexcept { return impl_->quality; }
const QVector<QtLidarrService::Profile>& QtLidarrService::metadataProfiles() const noexcept { return impl_->metadata; }
} // namespace music_player::m0
