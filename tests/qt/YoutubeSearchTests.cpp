#include "adapters/YoutubeSearch.hpp"

#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

#include <atomic>
#include <deque>
#include <mutex>
#include <stdexcept>

using namespace music_player::m0;
namespace {
QByteArray compact(const QJsonObject& value) { return QJsonDocument(value).toJson(QJsonDocument::Compact); }
QJsonObject entry(const QString& id, const QString& title) { return {{"id", id}, {"title", title}}; }
QByteArray entries(const QJsonArray& values) { return compact({{"entries", values}}); }
const QString first = QStringLiteral("aB0-_Cd1EF2");
const QString second = QStringLiteral("Zz9_-Yy8Xx7");

class Process final : public YoutubeResolverProcess {
  public:
    int starts{}, cancels{};
    bool accepted{true};
    QString argument;
    std::deque<YoutubeResolverProcessUpdate> updates;
    bool start(const QString& value, qint64) override {
        ++starts;
        argument = value;
        return accepted;
    }
    YoutubeResolverProcessUpdate poll(qint64) override {
        if (updates.empty())
            return {};
        auto result = updates.front();
        updates.pop_front();
        return result;
    }
    void cancel() override { ++cancels; }
};
YoutubeResolverProcessUpdate exited(QByteArray bytes, int code = 0) {
    return {.state = YoutubeResolverProcessState::Exited, .stdout_chunk = std::move(bytes), .exit_code = code};
}

// Thread-safe script for the threaded runner: Running until released or
// cancelled, then exits with the given output.
struct Script final {
    std::mutex mutex;
    QByteArray output{entries({entry(first, QStringLiteral("Canção"))})};
    int exit_code{0};
    bool release{true}, refuse{false};
    std::atomic<int> created{0}, cancels{0}, destroyed{0};
    QString argument;
};
class ScriptedProcess final : public YoutubeResolverProcess {
  public:
    explicit ScriptedProcess(Script& value) : script(value) { ++script.created; }
    ~ScriptedProcess() override { ++script.destroyed; }
    bool start(const QString& value, qint64) override {
        std::lock_guard lock(script.mutex);
        script.argument = value;
        return !script.refuse;
    }
    YoutubeResolverProcessUpdate poll(qint64) override {
        std::lock_guard lock(script.mutex);
        if (cancelled)
            return {.state = YoutubeResolverProcessState::Cancelled};
        if (!script.release)
            return {};
        return exited(script.output, script.exit_code);
    }
    void cancel() override {
        ++script.cancels;
        cancelled = true;
    }

  private:
    Script& script;
    bool cancelled{false};
};
std::optional<OnlineSearchOutcome> waitOutcome(ThreadedYoutubeSearch& search) {
    QElapsedTimer elapsed;
    elapsed.start();
    while (elapsed.elapsed() < 5000) {
        if (auto outcome = search.poll())
            return outcome;
        QTest::qWait(5);
    }
    return std::nullopt;
}
} // namespace

class YoutubeSearchTests final : public QObject {
    Q_OBJECT
  private slots:
    void queriesAreCollapsedAndBounded() {
        QCOMPARE(normalizedYoutubeSearchQuery(QStringLiteral("  lia   campos　mar ")),
                 QStringLiteral("lia campos mar"));
        QVERIFY(normalizedYoutubeSearchQuery(QStringLiteral("   ")).isEmpty());
        QVERIFY(normalizedYoutubeSearchQuery(QStringLiteral("a\u0001b")).isEmpty());
        QCOMPARE(normalizedYoutubeSearchQuery(QString(400, QLatin1Char('x'))).size(), 400);
        QVERIFY(normalizedYoutubeSearchQuery(QString(401, QLatin1Char('x'))).isEmpty());
        QVERIFY(normalizedYoutubeSearchQuery(QString(201, QChar(0x00E7))).isEmpty()); // 402 UTF-8 bytes.
        // Line breaks are whitespace, so they collapse instead of failing.
        QCOMPARE(normalizedYoutubeSearchQuery(QStringLiteral("a\nb")), QStringLiteral("a b"));
    }
    void resultsDecodeOnlyTypedMetadata() {
        auto full = entry(first, QStringLiteral("Canção do Mar"));
        full.insert("channel", QStringLiteral("Lia Campos"));
        full.insert("duration", 217);
        const auto decoded = decodeYoutubeSearchResults(entries({full, entry(second, QStringLiteral("Só título"))}));
        QVERIFY(decoded);
        QCOMPARE(decoded->size(), std::size_t{2});
        QCOMPARE(decoded->at(0),
                 (YoutubeSearchResult{first, QStringLiteral("Canção do Mar"), QStringLiteral("Lia Campos"), 217}));
        QCOMPARE(decoded->at(1), (YoutubeSearchResult{second, QStringLiteral("Só título"), {}, {}}));
        const auto empty = decodeYoutubeSearchResults(entries({}));
        QVERIFY(empty);
        QVERIFY(empty->empty());
    }
    void displayTextIsSingleLine() {
        auto value = entry(first, QStringLiteral("Linha\num\ttítulo"));
        value.insert("channel", QStringLiteral(" Canal\r\n"));
        const auto decoded = decodeYoutubeSearchResults(entries({value}));
        QVERIFY(decoded);
        QCOMPARE(decoded->front().title, QStringLiteral("Linha um título"));
        QCOMPARE(decoded->front().channel, QStringLiteral("Canal"));
    }
    void malformedResultsFailTheWholeList() {
        auto with = [](const QString& key, const QJsonValue& value) {
            auto item = entry(first, QStringLiteral("x"));
            item.insert(key, value);
            return entries({item});
        };
        QJsonArray many;
        for (int index = 0; index < 13; ++index)
            many.push_back(entry(QStringLiteral("%1").arg(index, 11, 10, QLatin1Char('0')), QStringLiteral("x")));
        const QList<QByteArray> invalid{
            QByteArrayLiteral("not json"),
            QByteArrayLiteral("[]"),
            compact({}),
            compact({{"entries", QJsonArray{}}, {"extra", 1}}),
            compact({{"entries", QStringLiteral("x")}}),
            entries({QStringLiteral("x")}),
            entries({entry(QStringLiteral("bad"), QStringLiteral("x"))}),
            entries({entry(first, QStringLiteral("x")), entry(first, QStringLiteral("y"))}),
            entries({QJsonObject{{"id", first}}}),
            entries({entry(first, QStringLiteral("  "))}),
            entries({entry(first, QString(513, QLatin1Char('x')))}),
            with(QStringLiteral("url"), QStringLiteral("https://r1.googlevideo.com/")),
            with(QStringLiteral("channel"), 7),
            with(QStringLiteral("duration"), -1),
            with(QStringLiteral("duration"), 1.5),
            with(QStringLiteral("duration"), youtube_search_max_duration_seconds + 1),
            entries(many),
        };
        for (const auto& payload : invalid)
            QVERIFY2(!decodeYoutubeSearchResults(payload), payload.left(80).constData());
    }
    void sessionPublishesResultsOnlyAfterACleanExit() {
        Process process;
        YoutubeSearchSession session(process);
        QVERIFY(session.start(QStringLiteral("lia campos"), 0));
        QCOMPARE(process.argument, QStringLiteral("lia campos"));
        QVERIFY(!session.start(QStringLiteral("outra"), 1));
        process.updates.push_back({.stdout_chunk = entries({entry(first, QStringLiteral("Canção"))})});
        auto update = session.poll(1);
        QCOMPARE(update.state, YoutubeSearchState::Searching);
        QVERIFY(update.results.empty());
        process.updates.push_back(exited({}));
        update = session.poll(2);
        QCOMPARE(update.state, YoutubeSearchState::Ready);
        QCOMPARE(update.error, DownloadFailure::None);
        QCOMPARE(update.results.size(), std::size_t{1});
        QCOMPARE(update.results.front().video_id, first);
    }
    void sessionRejectsUnsearchableQueriesWithoutStartingTheProcess() {
        for (const auto& query : {QString{}, QStringLiteral(" padded "), QStringLiteral("a\u0001b")}) {
            Process process;
            YoutubeSearchSession session(process);
            QVERIFY(!session.start(query, 0));
            QCOMPARE(process.starts, 0);
        }
    }
    void sessionFailuresNeverPublishResults() {
        {
            Process process;
            YoutubeSearchSession session(process);
            QVERIFY(session.start(QStringLiteral("x"), 0));
            process.updates.push_back(exited(entries({entry(first, QStringLiteral("x"))}), 3));
            const auto update = session.poll(1);
            QCOMPARE(update.state, YoutubeSearchState::Failed);
            QCOMPARE(update.error, DownloadFailure::Unavailable);
            QVERIFY(update.results.empty());
        }
        {
            Process process;
            YoutubeSearchSession session(process);
            QVERIFY(session.start(QStringLiteral("x"), 0));
            process.updates.push_back(exited(QByteArrayLiteral("{\"entries\":\"x\"}")));
            const auto update = session.poll(1);
            QCOMPARE(update.state, YoutubeSearchState::Failed);
            QCOMPARE(update.error, DownloadFailure::InvalidResult);
        }
        {
            Process process;
            YoutubeSearchSession session(process, {.max_stdout_bytes = 8});
            QVERIFY(session.start(QStringLiteral("x"), 0));
            process.updates.push_back({.stdout_chunk = QByteArray(9, 'x')});
            QCOMPARE(session.poll(1).state, YoutubeSearchState::Stopping);
            QCOMPARE(process.cancels, 1);
            process.updates.push_back({.state = YoutubeResolverProcessState::Cancelled});
            const auto update = session.poll(2);
            QCOMPARE(update.state, YoutubeSearchState::Failed);
            QCOMPARE(update.error, DownloadFailure::LimitExceeded);
        }
        {
            Process process;
            YoutubeSearchSession session(process, {.total_timeout_ms = 100});
            QVERIFY(session.start(QStringLiteral("x"), 0));
            QCOMPARE(session.poll(100).state, YoutubeSearchState::Stopping);
            process.updates.push_back({.state = YoutubeResolverProcessState::Cancelled});
            const auto update = session.poll(101);
            QCOMPARE(update.state, YoutubeSearchState::Failed);
            QCOMPARE(update.error, DownloadFailure::Timeout);
        }
    }
    void sessionCancellationDrainsToCancelled() {
        Process process;
        YoutubeSearchSession session(process);
        QVERIFY(session.start(QStringLiteral("x"), 0));
        session.cancel();
        QCOMPARE(process.cancels, 1);
        QCOMPARE(session.snapshot().state, YoutubeSearchState::Stopping);
        process.updates.push_back(exited(entries({entry(first, QStringLiteral("x"))})));
        const auto update = session.poll(1);
        QCOMPARE(update.state, YoutubeSearchState::Cancelled);
        QVERIFY(update.results.empty());
    }
    void lookupArgumentsAreUniqueValidIdsJoinedByCommas() {
        QCOMPARE(youtubeLookupArgument({first.toStdString(), second.toStdString()}), first + "," + second);
        QVERIFY(youtubeLookupArgument({}).isEmpty());
        QVERIFY(youtubeLookupArgument({first.toStdString(), first.toStdString()}).isEmpty());
        QVERIFY(youtubeLookupArgument({"bad"}).isEmpty());
        QVERIFY(youtubeLookupArgument({"aB0-_Cd1EF2,x"}).isEmpty());
        std::vector<std::string> many;
        for (int index = 0; index < 20; ++index)
            many.push_back(QStringLiteral("%1").arg(index, 11, 10, QLatin1Char('0')).toStdString());
        QVERIFY(!youtubeLookupArgument(many).isEmpty());
        many.push_back("aB0-_Cd1EF9");
        QVERIFY(youtubeLookupArgument(many).isEmpty());
    }
    void lookupDecodesUpToTwentyEntries() {
        QJsonArray twenty;
        for (int index = 0; index < 20; ++index)
            twenty.append(entry(QStringLiteral("%1").arg(index, 11, 10, QLatin1Char('0')), QStringLiteral("Faixa")));
        QVERIFY(!decodeYoutubeSearchResults(entries(twenty)));
        QCOMPARE(decodeYoutubeSearchResults(entries(twenty), youtube_lookup_max_ids)->size(), std::size_t{20});
        twenty.append(entry(QStringLiteral("aB0-_Cd1EF9"), QStringLiteral("Demais")));
        QVERIFY(!decodeYoutubeSearchResults(entries(twenty), youtube_lookup_max_ids));
    }
    void threadedLookupUsesItsOwnProcessAndTheIdList() {
        Script searches, lookups;
        lookups.output = entries({entry(second, QStringLiteral("Segundo")), entry(first, QStringLiteral("Primeiro"))});
        ThreadedYoutubeSearch search([&] { return std::make_unique<ScriptedProcess>(searches); }, {},
                                     [&] { return std::make_unique<ScriptedProcess>(lookups); });
        QVERIFY(!search.lookup({}));
        QVERIFY(!search.lookup({"bad"}));
        const auto ticket = search.lookup({first.toStdString(), second.toStdString()});
        QVERIFY(ticket);
        const auto outcome = waitOutcome(search);
        QCOMPARE(outcome->ticket, *ticket);
        QCOMPARE(outcome->error, OnlineSearchError::None);
        QCOMPARE(outcome->results.size(), std::size_t{2});
        QCOMPARE(lookups.argument, first + "," + second);
        QCOMPARE(lookups.created.load(), 1);
        QCOMPARE(searches.created.load(), 0);
        // Without a lookup runtime, pasted links cannot be looked up.
        ThreadedYoutubeSearch searchOnly([&] { return std::make_unique<ScriptedProcess>(searches); });
        QVERIFY(!searchOnly.lookup({first.toStdString()}));
    }
    void threadedSearchRunsOneSearchAtATime() {
        Script script;
        script.release = false;
        ThreadedYoutubeSearch search([&] { return std::make_unique<ScriptedProcess>(script); });
        QVERIFY(!search.start("   "));
        const auto ticket = search.start("  lia   campos ");
        QVERIFY(ticket);
        QVERIFY(!search.start("outra"));
        QVERIFY(!search.poll());
        {
            std::lock_guard lock(script.mutex);
            script.release = true;
        }
        const auto outcome = waitOutcome(search);
        QCOMPARE(outcome->ticket, *ticket);
        QCOMPARE(outcome->error, OnlineSearchError::None);
        QVERIFY(!outcome->cancelled);
        QCOMPARE(outcome->results, (std::vector<OnlineSearchResult>{{first.toStdString(), "Canção", {}, {}}}));
        QCOMPARE(script.argument, QStringLiteral("lia campos"));
        QCOMPARE(script.destroyed.load(), 1);
        QVERIFY(!search.poll());
        const auto next = search.start("de novo");
        QVERIFY(next && *next != *ticket);
        QVERIFY(waitOutcome(search));
    }
    void threadedSearchReportsCancellationAndFailures() {
        Script script;
        script.release = false;
        ThreadedYoutubeSearch search([&] { return std::make_unique<ScriptedProcess>(script); });
        QVERIFY(search.start("x"));
        QTRY_COMPARE_WITH_TIMEOUT(script.created.load(), 1, 5000);
        search.cancel();
        auto outcome = waitOutcome(search);
        QVERIFY(outcome->cancelled);
        QVERIFY(outcome->results.empty());
        QCOMPARE(script.cancels.load(), 1);

        {
            std::lock_guard lock(script.mutex);
            script.release = true;
            script.exit_code = 5;
        }
        QVERIFY(search.start("x"));
        outcome = waitOutcome(search);
        QCOMPARE(outcome->error, OnlineSearchError::Unavailable);
        QVERIFY(!outcome->cancelled);

        {
            std::lock_guard lock(script.mutex);
            script.exit_code = 0;
            script.output = QByteArrayLiteral("{}");
        }
        QVERIFY(search.start("x"));
        QCOMPARE(waitOutcome(search)->error, OnlineSearchError::Failed);
    }
    void missingRuntimeIsUnavailable() {
        ThreadedYoutubeSearch search([]() -> std::unique_ptr<YoutubeResolverProcess> { return {}; });
        QVERIFY(search.start("x"));
        QCOMPARE(waitOutcome(search)->error, OnlineSearchError::Unavailable);
        ThreadedYoutubeSearch throwing(
            []() -> std::unique_ptr<YoutubeResolverProcess> { throw std::runtime_error("private"); });
        QVERIFY(throwing.start("x"));
        QCOMPARE(waitOutcome(throwing)->error, OnlineSearchError::Unavailable);
    }
    void destructionCancelsAndDrainsTheRunningSearch() {
        Script script;
        script.release = false;
        {
            ThreadedYoutubeSearch search([&] { return std::make_unique<ScriptedProcess>(script); });
            QVERIFY(search.start("x"));
            QTRY_COMPARE_WITH_TIMEOUT(script.created.load(), 1, 5000);
        }
        QCOMPARE(script.cancels.load(), 1);
        QCOMPARE(script.destroyed.load(), 1);
    }
};

QTEST_GUILESS_MAIN(YoutubeSearchTests)
#include "YoutubeSearchTests.moc"
