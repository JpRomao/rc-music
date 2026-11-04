#include "presentation/BrowserMediaSelection.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

#include <algorithm>

using namespace music_player::m0;

namespace {
const QString page_url = QStringLiteral("https://page.example.test/listen?view=audio#current");

QJsonObject media(const QString& suffix = QStringLiteral("first"), const QString& title = QStringLiteral("Áudio"),
                  const QString& kind = QStringLiteral("audio")) {
    return {{QStringLiteral("url"), QStringLiteral("https://media.example.test/") + suffix + QStringLiteral(".mp3")},
            {QStringLiteral("title"), title},
            {QStringLiteral("kind"), kind}};
}
QByteArray snapshot(const QJsonArray& candidates = {media()}, const QString& page = page_url,
                    const bool truncated = false) {
    return QJsonDocument{QJsonObject{{QStringLiteral("pageUrl"), page},
                                     {QStringLiteral("candidates"), candidates},
                                     {QStringLiteral("truncated"), truncated}}}
        .toJson(QJsonDocument::Compact);
}
bool detect(BrowserMediaSelection& selection, const QJsonArray& candidates = {media()}) {
    selection.navigationChanged(page_url);
    const auto operation = selection.beginDetection();
    return operation && selection.complete(*operation, snapshot(candidates));
}
} // namespace

class BrowserMediaSelectionTests final : public QObject {
    Q_OBJECT
  private slots:
    void navigationDoesNotCollectOrAuthorizeAnything() {
        BrowserMediaSelection selection;
        QCOMPARE(selection.state(), BrowserMediaSelectionState::Unavailable);
        QVERIFY(!selection.canDetect());
        QVERIFY(!selection.beginDetection());
        QVERIFY(!selection.beginConfirmation());
        QVERIFY(selection.takeConfirmedItems().empty());
        selection.navigationChanged(page_url);
        QCOMPARE(selection.state(), BrowserMediaSelectionState::Idle);
        QCOMPARE(selection.pageUrl(), page_url);
        QVERIFY(selection.canDetect());
        QVERIFY(!selection.canConfirm());
        QVERIFY(!selection.snapshot());
        QVERIFY(selection.selectedIndices().empty());
        QVERIFY(!selection.setSelected(0, true));
        QVERIFY(selection.takeConfirmedItems().empty());
    }
    void unsupportedHostPagesCannotBeginDetection_data() {
        QTest::addColumn<QString>("page");
        QTest::newRow("closed") << QString{};
        QTest::newRow("http") << QStringLiteral("http://page.example.test/");
        QTest::newRow("youtube") << QStringLiteral("https://www.youtube.com/watch?v=synthetic");
        QTest::newRow("local") << QStringLiteral("https://127.0.0.1/private");
        QTest::newRow("credentials") << QStringLiteral("https://person:secret@page.example.test/");
        QTest::newRow("file") << QStringLiteral("file:///C:/private.mp3");
    }
    void unsupportedHostPagesCannotBeginDetection() {
        QFETCH(QString, page);
        BrowserMediaSelection selection;
        selection.navigationChanged(page);
        QCOMPARE(selection.state(), BrowserMediaSelectionState::Unavailable);
        QVERIFY(!selection.canDetect());
        QVERIFY(!selection.beginDetection());
        QVERIFY(!selection.canConfirm());
    }
    void detectionIsExplicitAndRetainsOnlyItsMatchingCompletion() {
        BrowserMediaSelection selection;
        selection.navigationChanged(page_url);
        const auto operation = selection.beginDetection();
        QVERIFY(operation);
        QVERIFY(*operation > 0);
        QCOMPARE(selection.state(), BrowserMediaSelectionState::Detecting);
        QVERIFY(!selection.beginDetection());
        QVERIFY(!selection.beginConfirmation());
        QVERIFY(!selection.setSelected(0, true));
        QVERIFY(!selection.complete(*operation + 1, snapshot()));
        QCOMPARE(selection.state(), BrowserMediaSelectionState::Detecting);
        QVERIFY(selection.complete(*operation, snapshot({media()}, page_url, true)));
        QCOMPARE(selection.state(), BrowserMediaSelectionState::Ready);
        QVERIFY(selection.canDetect());
        QVERIFY(selection.snapshot());
        QCOMPARE(selection.snapshot()->page_url, page_url);
        QCOMPARE(selection.snapshot()->candidates.size(), std::size_t{1});
        QVERIFY(selection.snapshot()->truncated);
        QVERIFY(selection.selectedIndices().empty());
        QVERIFY(!selection.canConfirm());
        QVERIFY(!selection.complete(*operation, snapshot({media(QStringLiteral("second"))})));
        QCOMPARE(selection.snapshot()->candidates[0].url, std::string{"https://media.example.test/first.mp3"});
    }
    void selectionsAreBoundedAndKeepSnapshotOrder() {
        QJsonArray candidates;
        for (int index = 0; index < 21; ++index)
            candidates.append(media(QString::number(index)));
        BrowserMediaSelection selection;
        QVERIFY(detect(selection, candidates));
        for (int index = 19; index >= 0; --index)
            QVERIFY(selection.setSelected(static_cast<std::size_t>(index), true));
        QCOMPARE(selection.selectedIndices().size(), std::size_t{20});
        QVERIFY(std::ranges::is_sorted(selection.selectedIndices()));
        QVERIFY(selection.canConfirm());
        QVERIFY(!selection.setSelected(20, true));
        QVERIFY(!selection.setSelected(21, false));
        QCOMPARE(selection.selectedIndices().size(), std::size_t{20});
        QVERIFY(selection.setSelected(0, true));
        QCOMPARE(selection.selectedIndices().size(), std::size_t{20});
        QVERIFY(selection.setSelected(0, false));
        QVERIFY(selection.setSelected(0, false));
        QVERIFY(selection.setSelected(20, true));
        QCOMPARE(selection.selectedIndices().front(), std::size_t{1});
        QCOMPARE(selection.selectedIndices().back(), std::size_t{20});
    }
    void selectedItemsRequireFreshVerificationAndCanBeConsumedOnlyOnce() {
        const auto first = media();
        const auto second = media(QStringLiteral("second"), QStringLiteral("Vídeo"), QStringLiteral("video"));
        BrowserMediaSelection selection;
        QVERIFY(detect(selection, {first, second}));
        QVERIFY(selection.setSelected(1, true));
        const auto expected = selection.snapshot()->candidates[1];
        QVERIFY(selection.takeConfirmedItems().empty());
        const auto operation = selection.beginConfirmation();
        QVERIFY(operation);
        QCOMPARE(selection.state(), BrowserMediaSelectionState::Confirming);
        QVERIFY(!selection.beginConfirmation());
        QVERIFY(!selection.beginDetection());
        QVERIFY(!selection.setSelected(0, true));
        QVERIFY(selection.takeConfirmedItems().empty());
        QVERIFY(selection.complete(*operation, snapshot({first, second})));
        QCOMPARE(selection.state(), BrowserMediaSelectionState::Confirmed);
        const auto items = selection.takeConfirmedItems();
        QVERIFY(items == std::vector<DownloadItem>{expected});
        QCOMPARE(selection.state(), BrowserMediaSelectionState::Ready);
        QVERIFY(selection.takeConfirmedItems().empty());
        QVERIFY(!selection.complete(*operation, snapshot({first, second})));
        QVERIFY(selection.takeConfirmedItems().empty());
        QVERIFY(selection.canConfirm());
    }
    void unselectedChangesAndCandidateReorderingDoNotRevokeConsent() {
        BrowserMediaSelection selection;
        const auto selected = media(QStringLiteral("selected"));
        QVERIFY(detect(selection, {media(QStringLiteral("unselected")), selected}));
        QVERIFY(selection.setSelected(1, true));
        const auto expected = selection.snapshot()->candidates[1];
        const auto operation = selection.beginConfirmation();
        QVERIFY(operation);
        QVERIFY(selection.complete(*operation, snapshot({selected, media(QStringLiteral("new"))})));
        QVERIFY(selection.takeConfirmedItems() == std::vector<DownloadItem>{expected});
    }
    void changedSelectedTupleRequiresNewDetection_data() {
        QTest::addColumn<QString>("change");
        for (const auto* name : {"url", "title", "kind", "removed"})
            QTest::newRow(name) << QString::fromLatin1(name);
    }
    void changedSelectedTupleRequiresNewDetection() {
        QFETCH(QString, change);
        BrowserMediaSelection selection;
        QVERIFY(detect(selection));
        QVERIFY(selection.setSelected(0, true));
        const auto operation = selection.beginConfirmation();
        QVERIFY(operation);
        auto item = media();
        if (change == QStringLiteral("url"))
            item[change] = QStringLiteral("https://media.example.test/replaced.mp3");
        else if (change == QStringLiteral("kind"))
            item[change] = QStringLiteral("video");
        else
            item[change] = QStringLiteral("Outro título");
        const QJsonArray fresh = change == QStringLiteral("removed") ? QJsonArray{} : QJsonArray{item};
        QVERIFY(!selection.complete(*operation, snapshot(fresh)));
        QCOMPARE(selection.state(), BrowserMediaSelectionState::SourceChanged);
        QVERIFY(!selection.snapshot());
        QVERIFY(selection.selectedIndices().empty());
        QVERIFY(!selection.canConfirm());
        QVERIFY(selection.canDetect());
        QVERIFY(selection.takeConfirmedItems().empty());
    }
    void navigationInvalidatesPendingCompletionEvenForTheSameUrl() {
        BrowserMediaSelection selection;
        selection.navigationChanged(page_url);
        const auto old = selection.beginDetection();
        QVERIFY(old);
        selection.navigationChanged(page_url);
        const auto current = selection.beginDetection();
        QVERIFY(current);
        QVERIFY(*current > *old);
        QVERIFY(!selection.complete(*old, snapshot()));
        QVERIFY(!selection.fail(*old));
        QVERIFY(!selection.timeout(*old));
        QCOMPARE(selection.state(), BrowserMediaSelectionState::Detecting);
        QVERIFY(selection.complete(*current, snapshot()));
        QVERIFY(selection.setSelected(0, true));
        const auto confirmation = selection.beginConfirmation();
        QVERIFY(confirmation);
        selection.navigationChanged();
        QCOMPARE(selection.state(), BrowserMediaSelectionState::Unavailable);
        QVERIFY(!selection.complete(*confirmation, snapshot()));
        QVERIFY(selection.takeConfirmedItems().empty());
        QVERIFY(!selection.snapshot());
        QVERIFY(!selection.canConfirm());
    }
    void malformedDataAndMismatchedPageCannotCreateAList_data() {
        QTest::addColumn<QByteArray>("payload");
        QTest::addColumn<BrowserMediaSelectionState>("expected");
        QTest::newRow("malformed") << QByteArray{"{broken"} << BrowserMediaSelectionState::InvalidSnapshot;
        QTest::newRow("different page") << snapshot({}, QStringLiteral("https://other.example.test/"))
                                        << BrowserMediaSelectionState::SourceChanged;
        QTest::newRow("different fragment")
            << snapshot({}, page_url + QStringLiteral("-changed")) << BrowserMediaSelectionState::SourceChanged;
    }
    void malformedDataAndMismatchedPageCannotCreateAList() {
        QFETCH(QByteArray, payload);
        QFETCH(BrowserMediaSelectionState, expected);
        BrowserMediaSelection selection;
        selection.navigationChanged(page_url);
        const auto operation = selection.beginDetection();
        QVERIFY(operation);
        QVERIFY(!selection.complete(*operation, payload));
        QCOMPARE(selection.state(), expected);
        QVERIFY(!selection.snapshot());
        QVERIFY(!selection.canConfirm());
        QVERIFY(selection.takeConfirmedItems().empty());
    }
    void failureAndTimeoutRevokePendingAndPreviouslySelectedData_data() {
        QTest::addColumn<bool>("expire");
        QTest::newRow("failure") << false;
        QTest::newRow("timeout") << true;
    }
    void failureAndTimeoutRevokePendingAndPreviouslySelectedData() {
        QFETCH(bool, expire);
        BrowserMediaSelection selection;
        QVERIFY(detect(selection));
        QVERIFY(selection.setSelected(0, true));
        const auto operation = selection.beginConfirmation();
        QVERIFY(operation);
        QVERIFY(expire ? selection.timeout(*operation) : selection.fail(*operation));
        QCOMPARE(selection.state(), expire ? BrowserMediaSelectionState::TimedOut : BrowserMediaSelectionState::Failed);
        QVERIFY(!selection.snapshot());
        QVERIFY(selection.selectedIndices().empty());
        QVERIFY(selection.takeConfirmedItems().empty());
        QVERIFY(!selection.complete(*operation, snapshot()));
        QVERIFY(!selection.fail(*operation));
        const auto next = selection.beginDetection();
        QVERIFY(next);
        QVERIFY(*next > *operation);
        QVERIFY(selection.complete(*next, snapshot()));
    }
    void refreshingAndChangingSelectionRevokeAnUnconsumedConfirmation() {
        BrowserMediaSelection selection;
        QVERIFY(detect(selection, {media(), media(QStringLiteral("second"))}));
        QVERIFY(selection.setSelected(0, true));
        auto operation = selection.beginConfirmation();
        QVERIFY(operation);
        QVERIFY(selection.complete(*operation, snapshot({media(), media(QStringLiteral("second"))})));
        QVERIFY(selection.setSelected(1, true));
        QCOMPARE(selection.state(), BrowserMediaSelectionState::Ready);
        QVERIFY(selection.takeConfirmedItems().empty());
        operation = selection.beginConfirmation();
        QVERIFY(operation);
        QVERIFY(selection.complete(*operation, snapshot({media(), media(QStringLiteral("second"))})));
        const auto refresh = selection.beginDetection();
        QVERIFY(refresh);
        QCOMPARE(selection.state(), BrowserMediaSelectionState::Detecting);
        QVERIFY(!selection.snapshot());
        QVERIFY(selection.selectedIndices().empty());
        QVERIFY(selection.takeConfirmedItems().empty());
    }
    void emptySnapshotIsUsefulButCannotBeConfirmed() {
        BrowserMediaSelection selection;
        QVERIFY(detect(selection, {}));
        QCOMPARE(selection.state(), BrowserMediaSelectionState::Ready);
        QVERIFY(selection.snapshot());
        QVERIFY(selection.snapshot()->candidates.empty());
        QVERIFY(!selection.setSelected(0, true));
        QVERIFY(!selection.canConfirm());
        QVERIFY(!selection.beginConfirmation());
    }
};

QTEST_GUILESS_MAIN(BrowserMediaSelectionTests)
#include "BrowserMediaSelectionTests.moc"
