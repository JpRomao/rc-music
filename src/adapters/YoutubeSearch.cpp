#include "YoutubeSearch.hpp"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QStringList>
#include <QThread>

#include <atomic>
#include <cmath>
#include <mutex>
#include <utility>

namespace music_player::m0 {
namespace {
bool controlCharacter(const char32_t code) {
    const auto category = QChar::category(code);
    return category == QChar::Other_Control || category == QChar::Other_Surrogate;
}
bool hasControl(const QString& text) {
    for (const auto code : text.toUcs4())
        if (controlCharacter(code))
            return true;
    return false;
}
bool validVideoId(const QString& id) {
    if (id.size() != 11)
        return false;
    for (const auto unit : id) {
        const auto c = unit.unicode();
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-'))
            return false;
    }
    return true;
}
// Single-line display text: control characters become spaces, then runs of
// whitespace collapse. The byte limit applies to the original value.
std::optional<QString> displayText(const QJsonValue& value) {
    if (!value.isString())
        return std::nullopt;
    const auto original = value.toString();
    if (original.toUtf8().size() > youtube_search_max_text_bytes)
        return std::nullopt;
    QString text;
    text.reserve(original.size());
    for (const auto unit : original.toUcs4()) {
        const auto code = static_cast<char32_t>(unit);
        if (controlCharacter(code))
            text.append(QLatin1Char(' '));
        else
            text.append(QString::fromUcs4(&code, 1));
    }
    return text.simplified();
}
bool terminal(const YoutubeSearchState state) {
    return state == YoutubeSearchState::Ready || state == YoutubeSearchState::Failed ||
           state == YoutubeSearchState::Cancelled;
}
} // namespace

QString normalizedYoutubeSearchQuery(const QString& text) {
    const auto query = text.simplified();
    if (query.isEmpty() || hasControl(query) || query.toUtf8().size() > youtube_search_max_query_bytes)
        return {};
    return query;
}

QString youtubeLookupArgument(const std::vector<std::string>& video_ids) {
    if (video_ids.empty() || video_ids.size() > youtube_lookup_max_ids)
        return {};
    QStringList ids;
    QSet<QString> seen;
    for (const auto& value : video_ids) {
        const auto id = QString::fromStdString(value);
        if (!validVideoId(id) || seen.contains(id))
            return {};
        seen.insert(id);
        ids.append(id);
    }
    return ids.join(QLatin1Char(','));
}

std::optional<std::vector<YoutubeSearchResult>> decodeYoutubeSearchResults(const QByteArray& json,
                                                                           const std::size_t max_entries) {
    QJsonParseError error{};
    const auto document = QJsonDocument::fromJson(json, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
        return std::nullopt;
    const auto root = document.object();
    if (root.size() != 1 || !root.value(QStringLiteral("entries")).isArray())
        return std::nullopt;
    const auto entries = root.value(QStringLiteral("entries")).toArray();
    if (static_cast<std::size_t>(entries.size()) > std::min(max_entries, youtube_lookup_max_ids))
        return std::nullopt;
    static const QSet<QString> allowed{QStringLiteral("id"), QStringLiteral("title"), QStringLiteral("channel"),
                                       QStringLiteral("duration")};
    std::vector<YoutubeSearchResult> results;
    QSet<QString> seen;
    for (const auto& value : entries) {
        if (!value.isObject())
            return std::nullopt;
        const auto item = value.toObject();
        for (auto key = item.begin(); key != item.end(); ++key)
            if (!allowed.contains(key.key()))
                return std::nullopt;
        const auto id = item.value(QStringLiteral("id")).toString();
        if (!item.value(QStringLiteral("id")).isString() || !validVideoId(id) || seen.contains(id))
            return std::nullopt;
        YoutubeSearchResult result{.video_id = id};
        const auto title = displayText(item.value(QStringLiteral("title")));
        if (!title || title->isEmpty())
            return std::nullopt;
        result.title = *title;
        if (item.contains(QStringLiteral("channel"))) {
            const auto channel = displayText(item.value(QStringLiteral("channel")));
            if (!channel)
                return std::nullopt;
            result.channel = *channel;
        }
        if (item.contains(QStringLiteral("duration"))) {
            const auto duration = item.value(QStringLiteral("duration"));
            const auto seconds = duration.toDouble(-1);
            if (!duration.isDouble() || seconds < 0 || seconds > youtube_search_max_duration_seconds ||
                std::floor(seconds) != seconds)
                return std::nullopt;
            result.duration_seconds = static_cast<int>(seconds);
        }
        seen.insert(id);
        results.push_back(std::move(result));
    }
    return results;
}

struct YoutubeSearchSession::Impl final {
    YoutubeResolverProcess& process;
    YoutubeSearchLimits limits;
    YoutubeSearchSnapshot update;
    QString query;
    QByteArray output;
    quint64 stdout_bytes{}, stderr_bytes{};
    qint64 started_ms{}, previous_ms{};
    bool cancel_requested{}, stop_requested{};

    Impl(YoutubeResolverProcess& source, const YoutubeSearchLimits configuration)
        : process(source), limits(configuration) {}
    void stop(const DownloadFailure error = DownloadFailure::None) {
        if (error != DownloadFailure::None && update.error == DownloadFailure::None)
            update.error = error;
        update.state = YoutubeSearchState::Stopping;
        output.clear();
        if (!stop_requested) {
            try {
                process.cancel();
                stop_requested = true;
            } catch (...) {
                // Uncertainty never acknowledges native exit; keep polling.
            }
        }
    }
    void finishFailure() {
        update.results.clear();
        update.state =
            update.error == DownloadFailure::None ? YoutubeSearchState::Cancelled : YoutubeSearchState::Failed;
        output.clear();
    }
};

YoutubeSearchSession::YoutubeSearchSession(YoutubeResolverProcess& process, const YoutubeSearchLimits limits)
    : impl_(std::make_unique<Impl>(process, limits)) {}
YoutubeSearchSession::~YoutubeSearchSession() = default;

bool YoutubeSearchSession::start(const QString& query, const qint64 now_ms) {
    auto& state = *impl_;
    if (state.update.state != YoutubeSearchState::Idle || now_ms < 0 || query.isEmpty() ||
        normalizedYoutubeSearchQuery(query) != query || state.limits.max_stdout_bytes == 0 ||
        state.limits.max_stdout_bytes > 1024 * 1024 || state.limits.max_stderr_bytes == 0 ||
        state.limits.max_stderr_bytes > 128 * 1024 || state.limits.total_timeout_ms <= 0 ||
        state.limits.max_results == 0 || state.limits.max_results > youtube_lookup_max_ids)
        return false;
    state.query = QString(query.constData(), query.size());
    state.started_ms = state.previous_ms = now_ms;
    state.update.state = YoutubeSearchState::Searching;
    try {
        if (!state.process.start(state.query, now_ms))
            state.stop(DownloadFailure::Unavailable);
    } catch (...) {
        state.stop(DownloadFailure::Unavailable);
    }
    return true; // Owns the attempt until a terminal process acknowledgement.
}

YoutubeSearchSnapshot YoutubeSearchSession::poll(const qint64 now_ms) {
    auto& state = *impl_;
    if (state.update.state == YoutubeSearchState::Idle || terminal(state.update.state))
        return state.update;
    if (state.update.state == YoutubeSearchState::Stopping && !state.stop_requested)
        state.stop();
    if (now_ms < state.previous_ms || now_ms < 0)
        state.stop(DownloadFailure::InvalidResult);
    else {
        state.previous_ms = now_ms;
        if (now_ms - state.started_ms >= state.limits.total_timeout_ms)
            state.stop(DownloadFailure::Timeout);
    }
    YoutubeResolverProcessUpdate process;
    try {
        process = state.process.poll(std::max(now_ms, state.previous_ms));
    } catch (...) {
        state.stop(DownloadFailure::InvalidResult);
        return state.update;
    }
    if (static_cast<quint64>(process.stdout_chunk.size()) > state.limits.max_stdout_bytes - state.stdout_bytes ||
        static_cast<quint64>(process.stderr_chunk.size()) > state.limits.max_stderr_bytes - state.stderr_bytes) {
        state.stop(DownloadFailure::LimitExceeded);
    } else {
        state.stdout_bytes += static_cast<quint64>(process.stdout_chunk.size());
        state.stderr_bytes += static_cast<quint64>(process.stderr_chunk.size());
        if (state.update.state == YoutubeSearchState::Searching)
            state.output += process.stdout_chunk;
    }
    if (process.error != DownloadFailure::None)
        state.stop(process.error);
    if (process.state == YoutubeResolverProcessState::Running)
        return state.update;
    if (process.state != YoutubeResolverProcessState::Exited && process.state != YoutubeResolverProcessState::Failed &&
        process.state != YoutubeResolverProcessState::Cancelled) {
        state.stop(DownloadFailure::InvalidResult);
        return state.update;
    }
    if (state.update.state == YoutubeSearchState::Stopping || state.cancel_requested) {
        state.finishFailure();
        return state.update;
    }
    if (process.state != YoutubeResolverProcessState::Exited || !process.exit_code || *process.exit_code != 0) {
        state.update.error = DownloadFailure::Unavailable;
        state.finishFailure();
        return state.update;
    }
    try {
        auto decoded = decodeYoutubeSearchResults(state.output, state.limits.max_results);
        state.output.clear();
        if (!decoded) {
            state.update.error = DownloadFailure::InvalidResult;
            state.finishFailure();
        } else {
            state.update.results = std::move(*decoded);
            state.update.state = YoutubeSearchState::Ready;
        }
    } catch (...) {
        state.update.error = DownloadFailure::InvalidResult;
        state.finishFailure();
    }
    return state.update;
}

void YoutubeSearchSession::cancel() {
    auto& state = *impl_;
    if (terminal(state.update.state))
        return;
    if (state.update.state == YoutubeSearchState::Idle) {
        state.update.state = YoutubeSearchState::Cancelled;
        return;
    }
    state.cancel_requested = true;
    state.stop();
}

YoutubeSearchSnapshot YoutubeSearchSession::snapshot() const { return impl_->update; }

struct ThreadedYoutubeSearch::Impl final {
    ProcessFactory factory, lookup_factory;
    YoutubeSearchLimits limits;
    std::mutex mutex;
    std::unique_ptr<QThread> worker;
    std::optional<OnlineSearchOutcome> finished;
    std::atomic<bool> cancel_requested{false};
    std::uint64_t serial{0};
    bool running{false};

    Impl(ProcessFactory source, const YoutubeSearchLimits configuration, ProcessFactory lookup_source)
        : factory(std::move(source)), lookup_factory(std::move(lookup_source)), limits(configuration) {}
    void publish(OnlineSearchOutcome outcome) {
        std::lock_guard lock(mutex);
        finished = std::move(outcome);
    }
    static OnlineSearchError mapped(const DownloadFailure error) {
        switch (error) {
        case DownloadFailure::Timeout:
            return OnlineSearchError::TimedOut;
        case DownloadFailure::InvalidResult:
        case DownloadFailure::LimitExceeded:
            return OnlineSearchError::Failed;
        default:
            return OnlineSearchError::Unavailable;
        }
    }
    // Worker thread. The process and session live and die here, after the
    // session reached a terminal state that acknowledges the native exit.
    void run(const QString& query, const std::uint64_t ticket, const ProcessFactory& source,
             const YoutubeSearchLimits session_limits) {
        OnlineSearchOutcome outcome{.ticket = ticket};
        std::unique_ptr<YoutubeResolverProcess> process;
        try {
            if (source)
                process = source();
        } catch (...) {
            process.reset();
        }
        if (!process) {
            outcome.error = OnlineSearchError::Unavailable;
            publish(std::move(outcome));
            return;
        }
        YoutubeSearchSnapshot snapshot;
        {
            YoutubeSearchSession session(*process, session_limits);
            QElapsedTimer clock;
            clock.start();
            if (!session.start(query, 0)) {
                outcome.error = OnlineSearchError::Failed;
            } else {
                bool cancel_sent{false};
                for (;;) {
                    if (!cancel_sent && cancel_requested.load()) {
                        session.cancel();
                        cancel_sent = true;
                    }
                    QCoreApplication::processEvents();
                    snapshot = session.poll(clock.elapsed());
                    if (terminal(snapshot.state))
                        break;
                    QThread::msleep(5);
                }
                if (snapshot.state == YoutubeSearchState::Cancelled)
                    outcome.cancelled = true;
                else if (snapshot.state == YoutubeSearchState::Failed)
                    outcome.error = mapped(snapshot.error);
                for (const auto& result : snapshot.results)
                    outcome.results.push_back({result.video_id.toStdString(), result.title.toStdString(),
                                               result.channel.toStdString(), result.duration_seconds});
            }
        }
        process.reset();
        publish(std::move(outcome));
    }
    void join() {
        if (worker) {
            worker->wait();
            worker.reset();
        }
    }
    std::optional<std::uint64_t> begin(const QString& argument, const bool lookup) {
        if (running || argument.isEmpty() || (lookup && !lookup_factory))
            return std::nullopt;
        join();
        const auto ticket = ++serial;
        {
            std::lock_guard lock(mutex);
            finished.reset();
        }
        cancel_requested = false;
        running = true;
        auto session_limits = limits;
        if (lookup)
            session_limits.max_results = youtube_lookup_max_ids;
        worker.reset(QThread::create([this, argument, ticket, lookup, session_limits] {
            run(argument, ticket, lookup ? lookup_factory : factory, session_limits);
        }));
        worker->start();
        return ticket;
    }
};

ThreadedYoutubeSearch::ThreadedYoutubeSearch(ProcessFactory factory, const YoutubeSearchLimits limits,
                                             ProcessFactory lookup_factory)
    : impl_(std::make_unique<Impl>(std::move(factory), limits, std::move(lookup_factory))) {}

std::optional<std::uint64_t> ThreadedYoutubeSearch::lookup(const std::vector<std::string>& video_ids) {
    return impl_->begin(youtubeLookupArgument(video_ids), true);
}

ThreadedYoutubeSearch::~ThreadedYoutubeSearch() {
    impl_->cancel_requested = true;
    impl_->join();
}

std::optional<std::uint64_t> ThreadedYoutubeSearch::start(const std::string& text) {
    if (impl_->running)
        return std::nullopt;
    return impl_->begin(normalizedYoutubeSearchQuery(QString::fromStdString(text)), false);
}

std::optional<OnlineSearchOutcome> ThreadedYoutubeSearch::poll() {
    auto& state = *impl_;
    std::optional<OnlineSearchOutcome> outcome;
    {
        std::lock_guard lock(state.mutex);
        outcome = std::exchange(state.finished, std::nullopt);
    }
    if (outcome) {
        state.join();
        state.running = false;
    }
    return outcome;
}

void ThreadedYoutubeSearch::cancel() {
    if (impl_->running)
        impl_->cancel_requested = true;
}
} // namespace music_player::m0
