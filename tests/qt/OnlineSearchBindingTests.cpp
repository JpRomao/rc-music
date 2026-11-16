#include "presentation/OnlineSearchBinding.hpp"

#include <QDesktopServices>
#include <QSignalSpy>
#include <QTest>
#include <QUuid>

#include <deque>
#include <optional>

using namespace music_player::m0;
namespace {
struct SearchState final {
    std::vector<std::string> queries;
    std::vector<std::vector<std::string>> lookups;
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
    std::optional<std::uint64_t> lookup(const std::vector<std::string>& ids) override {
        if (state.refuse)
            return std::nullopt;
        state.lookups.push_back(ids);
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
        // One job per item of the last request.
        std::vector<std::string> jobs;
        for (std::size_t index = 0; index < requests.back().items.size(); ++index)
            jobs.push_back("job-" + std::to_string(index + 1));
        return {.error = DownloadSubmissionError::None, .job_ids = jobs};
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
// Stands in for the browser: QDesktopServices hands https links to it.
class UrlRecorder final : public QObject {
    Q_OBJECT
  public:
    QList<QUrl> urls;
  public slots:
    void open(const QUrl& url) { urls.append(url); }
};
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
    void pastedLinksAreLookedUpAndKeepTheirLetrasPages() {
        Harness h;
        const auto pasted = QStringLiteral("https://youtu.be/aB0-_Cd1EF2 https://www.letras.mus.br/lia/mar/\n"
                                           "https://www.youtube.com/watch?v=Zz9_-Yy8Xx7\n"
                                           "https://www.youtube.com/watch?v=Aa1_-Bb2Cc3\n"
                                           "texto qualquer\n"
                                           "https://www.letras.mus.br/sozinha/\n");
        QVERIFY(h.binding.lookupLinks(pasted));
        QVERIFY(h.binding.busy());
        QVERIFY(h.binding.linksMode());
        QCOMPARE(h.state.lookups, (std::vector<std::vector<std::string>>{{first, second, "Aa1_-Bb2Cc3"}}));
        QVERIFY(h.state.queries.empty());
        // Problems are listed per line while the lookup runs.
        QCOMPARE(h.binding.linkIssues(),
                 (QStringList{QStringLiteral("Linha 4: não é um link do YouTube nem do Letras."),
                              QStringLiteral("Linha 5: falta o link do YouTube para esta página do Letras.")}));
        // The third video is not found (private or removed).
        h.complete(found(1));
        QCOMPARE(h.binding.results().size(), 2);
        QVERIFY(at(h.binding, 0).value("hasLyricsPage").toBool());
        QVERIFY(!at(h.binding, 1).value("hasLyricsPage").toBool());
        QCOMPARE(h.binding.linkIssues().last(),
                 QStringLiteral("Linha 3: vídeo não encontrado no YouTube (privado ou removido)."));
        QCOMPARE(h.binding.statusText(), QStringLiteral("2 links prontos para baixar."));
        QVERIFY(h.binding.canDownloadAll());

        // One link alone keeps its Letras page too.
        QVERIFY(h.binding.download(QString::fromStdString(first)));
        QCOMPARE(h.submission.requests.back().items.front().lyrics_page,
                 std::string{"https://www.letras.mus.br/lia/mar/"});
        QVERIFY(h.binding.downloadAll());
        const auto& all = h.submission.requests.back();
        QCOMPARE(all.items.size(), std::size_t{1}); // Only what was not requested yet.
        QCOMPARE(all.items.front().video_id, second);
        QCOMPARE(all.items.front().title, std::string{"Sem canal"});
        QVERIFY(at(h.binding, 1).value("requested").toBool());
        QVERIFY(!h.binding.canDownloadAll());
        QVERIFY(!h.binding.downloadAll());
    }
    void downloadAllSendsOneRequestWithEveryLink() {
        Harness h;
        QVERIFY(h.binding.lookupLinks(QStringLiteral("https://youtu.be/aB0-_Cd1EF2\nhttps://youtu.be/Zz9_-Yy8Xx7")));
        QVERIFY(h.binding.linkIssues().isEmpty());
        h.complete(found(1));
        QVERIFY(h.binding.downloadAll());
        QCOMPARE(h.submission.requests.size(), std::size_t{1});
        QCOMPARE(h.submission.requests.front().items.size(), std::size_t{2});
        QVERIFY(at(h.binding, 0).value("requested").toBool());
        QVERIFY(at(h.binding, 1).value("requested").toBool());
        QCOMPARE(h.binding.statusText(), QStringLiteral("2 áudios foram para os downloads. Acompanhe em Atividade."));
    }
    void pastingNothingUsableDoesNotLookUp() {
        Harness h;
        QVERIFY(!h.binding.lookupLinks(QStringLiteral("nada aqui")));
        QVERIFY(h.state.lookups.empty());
        QCOMPARE(h.binding.statusText(), QStringLiteral("Nenhum link do YouTube encontrado."));
        QCOMPARE(h.binding.linkIssues().size(), 1);
        QVERIFY(!h.binding.lookupLinks(QStringLiteral("  ")));
        QCOMPARE(h.binding.statusText(), QStringLiteral("Cole um ou mais links do YouTube, um por linha."));
        // A normal search leaves the link mode.
        QVERIFY(h.binding.lookupLinks(QStringLiteral("https://youtu.be/aB0-_Cd1EF2")));
        h.complete(found(1));
        QVERIFY(h.binding.search(QStringLiteral("lia")));
        QVERIFY(!h.binding.linksMode());
        QVERIFY(h.binding.linkIssues().isEmpty());
        QVERIFY(!h.binding.downloadAll());
    }
    void previewOpensOnlyListedVideosInTheBrowser() {
        UrlRecorder browser;
        QDesktopServices::setUrlHandler(QStringLiteral("https"), &browser, "open");
        Harness h;
        QVERIFY(!h.binding.openPreview(QString::fromStdString(first)));
        QVERIFY(h.binding.search(QStringLiteral("lia")));
        h.complete(found(1));
        QVERIFY(!h.binding.openPreview(QStringLiteral("notListed01")));
        QVERIFY(!h.binding.openPreview(QStringLiteral("https://evil.example/")));
        QVERIFY(browser.urls.isEmpty());
        QVERIFY(h.binding.openPreview(QString::fromStdString(second)));
        QDesktopServices::unsetUrlHandler(QStringLiteral("https"));
        QCOMPARE(browser.urls, QList<QUrl>{QUrl(QStringLiteral("https://www.youtube.com/watch?v=") +
                                                QString::fromStdString(second))});
        // Previewing never sends a download request.
        QVERIFY(h.submission.requests.empty());
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
