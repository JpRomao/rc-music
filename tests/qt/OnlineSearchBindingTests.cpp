#include "presentation/OnlineSearchBinding.hpp"

#include <QSignalSpy>
#include <QTest>
#include <QUuid>

#include <deque>
#include <optional>

using namespace music_player::m0;
namespace {
struct SearchState final {
    std::vector<std::string> queries;
    std::optional<OnlineSearchOutcome> outcome;
    int cancels{};
    bool refuse{false};
    std::uint64_t ticket{0};
};
class FakeSearch final : public OnlineSearch {
  public:
    explicit FakeSearch(SearchState& value) : state(value) {}
    std::optional<std::uint64_t> start(const std::string& query) override {
        if (state.refuse)
            return std::nullopt;
        state.queries.push_back(query);
        return ++state.ticket;
    }
    std::optional<OnlineSearchOutcome> poll() override { return std::exchange(state.outcome, std::nullopt); }
    void cancel() override { ++state.cancels; }

  private:
    SearchState& state;
};
class FakeSubmission final : public AsyncYoutubeAudioSubmission {
  public:
    std::vector<YoutubeAudioRequest> requests;
    bool pending{false};
    std::optional<DownloadSubmissionResult> polled;
    DownloadSubmissionError error{DownloadSubmissionError::None};
    DownloadAdmissionResult begin(const YoutubeAudioRequest& request) override {
        requests.push_back(request);
        if (pending)
            return {.state = DownloadAdmissionState::Pending};
        return {.result = result()};
    }
    std::optional<DownloadSubmissionResult> poll(std::string_view) override { return std::exchange(polled, {}); }
    DownloadSubmissionResult result() const {
        if (error != DownloadSubmissionError::None)
            return {.error = error};
        return {.error = DownloadSubmissionError::None, .job_ids = {"job-1"}};
    }
};
const std::string first = "aB0-_Cd1EF2";
const std::string second = "Zz9_-Yy8Xx7";
OnlineSearchOutcome found(const std::uint64_t ticket) {
    return {.ticket = ticket,
            .results = {{first, "Canção do Mar", "Lia Campos", 217}, {second, "Sem canal", "", std::nullopt}}};
}
struct Harness final {
    SearchState state;
    FakeSubmission submission;
    OnlineSearchBinding binding;
    Harness() { binding.setBackend(std::make_unique<FakeSearch>(state), &submission); }
    void complete(OnlineSearchOutcome outcome) {
        state.outcome = std::move(outcome);
        QTRY_VERIFY_WITH_TIMEOUT(!binding.busy(), 2000);
    }
};
QVariantMap at(const OnlineSearchBinding& binding, const int index) { return binding.results().at(index).toMap(); }
} // namespace

class OnlineSearchBindingTests final : public QObject {
    Q_OBJECT
  private slots:
    void withoutABackendTheSearchIsUnavailable() {
        OnlineSearchBinding binding;
        QVERIFY(!binding.available());
        QVERIFY(!binding.search(QStringLiteral("lia")));
        QVERIFY(!binding.download(QString::fromStdString(first)));
        QVERIFY(!binding.statusText().isEmpty());
    }
    void searchingPublishesTypedResults() {
        Harness h;
        QVERIFY(h.binding.available());
        QSignalSpy changed(&h.binding, &OnlineSearchBinding::changed);
        QVERIFY(h.binding.search(QStringLiteral("  lia campos ")));
        QVERIFY(h.binding.busy());
        QCOMPARE(h.state.queries, std::vector<std::string>{"lia campos"});
        QCOMPARE(h.binding.query(), QStringLiteral("lia campos"));
        QVERIFY(!h.binding.searched());
        QVERIFY(changed.count() > 0);
        h.complete(found(1));
        QVERIFY(h.binding.searched());
        QCOMPARE(h.binding.results().size(), 2);
        const auto row = at(h.binding, 0);
        QCOMPARE(row.value("videoId").toString(), QString::fromStdString(first));
        QCOMPARE(row.value("title").toString(), QStringLiteral("Canção do Mar"));
        QCOMPARE(row.value("channel").toString(), QStringLiteral("Lia Campos"));
        QCOMPARE(row.value("durationText").toString(), QStringLiteral("3:37"));
        QCOMPARE(row.value("requested").toBool(), false);
        QCOMPARE(at(h.binding, 1).value("durationText").toString(), QString{});
        QVERIFY(h.binding.statusText().contains(QStringLiteral("2")));
    }
    void aStaleOutcomeIsIgnored() {
        Harness h;
        QVERIFY(h.binding.search(QStringLiteral("lia")));
        h.state.outcome = found(99);
        QTest::qWait(120);
        QVERIFY(h.binding.busy());
        QVERIFY(h.binding.results().isEmpty());
    }
    void emptyCancelledAndFailedSearchesHaveDistinctMessages() {
        Harness h;
        QVERIFY(h.binding.search(QStringLiteral("nada")));
        h.complete({.ticket = 1});
        QVERIFY(h.binding.searched());
        QVERIFY(h.binding.results().isEmpty());
        const auto empty = h.binding.statusText();
        QVERIFY(empty.contains(QStringLiteral("nada")));

        QVERIFY(h.binding.search(QStringLiteral("outra")));
        h.binding.cancel();
        QCOMPARE(h.state.cancels, 1);
        h.complete({.ticket = 2, .cancelled = true});
        const auto cancelled = h.binding.statusText();

        QVERIFY(h.binding.search(QStringLiteral("x")));
        h.complete({.ticket = 3, .error = OnlineSearchError::Unavailable});
        const auto unavailable = h.binding.statusText();
        QVERIFY(h.binding.search(QStringLiteral("x")));
        h.complete({.ticket = 4, .error = OnlineSearchError::TimedOut});
        const auto timed_out = h.binding.statusText();
        QVERIFY(h.binding.search(QStringLiteral("x")));
        h.complete({.ticket = 5, .error = OnlineSearchError::Failed});
        const auto failed = h.binding.statusText();
        const QStringList messages{empty, cancelled, unavailable, timed_out, failed};
        for (const auto& message : messages)
            QCOMPARE(messages.count(message), 1);
        QVERIFY(h.binding.results().isEmpty());
    }
    void blankOrRefusedQueriesDoNotStart() {
        Harness h;
        QVERIFY(!h.binding.search(QStringLiteral("   ")));
        QVERIFY(h.state.queries.empty());
        h.state.refuse = true;
        QVERIFY(!h.binding.search(QStringLiteral("a\u0001b")));
        QVERIFY(!h.binding.busy());
        QVERIFY(!h.binding.statusText().isEmpty());
    }
    void onlyOneSearchRunsAtATime() {
        Harness h;
        QVERIFY(h.binding.search(QStringLiteral("lia")));
        QVERIFY(!h.binding.search(QStringLiteral("outra")));
        QCOMPARE(h.state.queries.size(), std::size_t{1});
    }
    void downloadingSendsAnOrdinaryRequestByVideoId() {
        Harness h;
        QVERIFY(h.binding.search(QStringLiteral("lia")));
        h.complete(found(1));
        QVERIFY(!h.binding.download(QStringLiteral("notListed01")));
        QVERIFY(h.submission.requests.empty());
        QVERIFY(h.binding.download(QString::fromStdString(first)));
        QCOMPARE(h.submission.requests.size(), std::size_t{1});
        const auto& request = h.submission.requests.front();
        QVERIFY(!QUuid::fromString(QString::fromStdString(request.request_id)).isNull());
        QCOMPARE(request.request_id.size(), std::size_t{36});
        QCOMPARE(request.items.size(), std::size_t{1});
        QCOMPARE(request.items.front().video_id, first);
        QCOMPARE(request.items.front().title, std::string{"Canção do Mar"});
        // A channel is not an artist; the download keeps its own metadata.
        QVERIFY(request.items.front().artist.empty());
        QVERIFY(at(h.binding, 0).value("requested").toBool());
        QVERIFY(!at(h.binding, 1).value("requested").toBool());
        QVERIFY(h.binding.statusText().contains(QStringLiteral("Canção do Mar")));
        QVERIFY(!h.binding.download(QString::fromStdString(first)));
        QCOMPARE(h.submission.requests.size(), std::size_t{1});
    }
    void aPendingAdmissionIsPolledUntilItCompletes() {
        Harness h;
        QVERIFY(h.binding.search(QStringLiteral("lia")));
        h.complete(found(1));
        h.submission.pending = true;
        QVERIFY(h.binding.download(QString::fromStdString(second)));
        QVERIFY(h.binding.requesting());
        QVERIFY(!h.binding.download(QString::fromStdString(first)));
        h.submission.polled = DownloadSubmissionResult{.error = DownloadSubmissionError::None, .job_ids = {"job-2"}};
        QTRY_VERIFY_WITH_TIMEOUT(!h.binding.requesting(), 2000);
        QVERIFY(at(h.binding, 1).value("requested").toBool());
    }
    void aRefusedAdmissionLeavesTheResultAvailable() {
        Harness h;
        QVERIFY(h.binding.search(QStringLiteral("lia")));
        h.complete(found(1));
        h.submission.error = DownloadSubmissionError::Busy;
        QVERIFY(!h.binding.download(QString::fromStdString(first)));
        QVERIFY(!at(h.binding, 0).value("requested").toBool());
        const auto busy = h.binding.statusText();
        QVERIFY(!busy.isEmpty());
        h.submission.error = DownloadSubmissionError::NotAvailable;
        QVERIFY(!h.binding.download(QString::fromStdString(first)));
        QVERIFY(h.binding.statusText() != busy);
    }
    void revokingTheBackendStopsTheSearch() {
        Harness h;
        QVERIFY(h.binding.search(QStringLiteral("lia")));
        h.binding.setBackend(nullptr, nullptr);
        QVERIFY(!h.binding.available());
        QVERIFY(!h.binding.busy());
        QVERIFY(h.binding.results().isEmpty());
    }
};

QTEST_GUILESS_MAIN(OnlineSearchBindingTests)
#include "OnlineSearchBindingTests.moc"
