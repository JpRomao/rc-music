#include "presentation/LibraryListModel.hpp"

#include <QElapsedTimer>
#include <QPersistentModelIndex>
#include <QSignalSpy>
#include <QTest>

#include <algorithm>
#include <string>
#include <vector>

using namespace music_player::m0;

namespace {
LibraryRecord record(std::uint64_t id, const std::string& title, const std::string& artist = {}) {
    return {id, std::filesystem::path{"C:/Music/track-" + std::to_string(id) + ".mp3"}, title, artist, "", 1000, true};
}

std::vector<LibraryRecord> numbered(std::uint64_t count) {
    std::vector<LibraryRecord> records;
    for (std::uint64_t id = 1; id <= count; ++id)
        records.push_back(record(id, "Faixa " + std::to_string(id), id % 4 == 0 ? "Mar Azul" : "Rio"));
    return records;
}

QStringList ids(const LibraryListModel& model) {
    QStringList result;
    for (int row = 0; row < model.rowCount(); ++row)
        result.append(model.data(model.index(row, 0), LibraryListModel::RecordIdRole).toString());
    return result;
}

// Watches every structural signal at once.
struct Spies final {
    explicit Spies(LibraryListModel& model)
        : reset(&model, &QAbstractItemModel::modelAboutToBeReset), removed(&model, &QAbstractItemModel::rowsRemoved),
          inserted(&model, &QAbstractItemModel::rowsInserted), changed(&model, &QAbstractItemModel::dataChanged),
          count(&model, &LibraryListModel::countChanged) {}
    QSignalSpy reset, removed, inserted, changed, count;
};
} // namespace

class LibraryListModelTests final : public QObject {
    Q_OBJECT
  private slots:
    void unchangedRecordsEmitNothing() {
        LibraryListModel model;
        model.setRecords(numbered(5));
        Spies spies(model);
        model.setRecords(numbered(5));
        QCOMPARE(spies.reset.count(), 0);
        QCOMPARE(spies.removed.count(), 0);
        QCOMPARE(spies.inserted.count(), 0);
        QCOMPARE(spies.changed.count(), 0);
        QCOMPARE(spies.count.count(), 0);
    }
    void aChangedTagUpdatesOnlyItsRow() {
        LibraryListModel model;
        model.setRecords(numbered(5));
        Spies spies(model);
        auto records = numbered(5);
        records[2].title = "Outro nome";
        model.setRecords(records);
        QCOMPARE(spies.reset.count(), 0);
        QCOMPARE(spies.removed.count(), 0);
        QCOMPARE(spies.inserted.count(), 0);
        QCOMPARE(spies.count.count(), 0);
        QCOMPARE(spies.changed.count(), 1);
        QCOMPARE(spies.changed.first().at(0).toModelIndex().row(), 2);
        QCOMPARE(spies.changed.first().at(1).toModelIndex().row(), 2);
        QCOMPARE(model.data(model.index(2, 0), LibraryListModel::TitleRole).toString(), QStringLiteral("Outro nome"));
    }
    void aNewestRecordEntersTheRecentListWithoutAReset() {
        // The recent list holds the newest twelve, newest first.
        const auto newest = [](std::uint64_t top) {
            std::vector<LibraryRecord> records;
            for (std::uint64_t id = top; id > top - 12; --id)
                records.push_back(record(id, "Faixa " + std::to_string(id)));
            return records;
        };
        LibraryListModel model;
        model.setRecords(newest(20));
        QPersistentModelIndex fifth(model.index(5, 0));
        const auto fifthId = fifth.data(LibraryListModel::RecordIdRole).toString();
        Spies spies(model);
        model.setRecords(newest(21));
        QCOMPARE(spies.reset.count(), 0);
        QCOMPARE(spies.removed.count(), 1);
        QCOMPARE(spies.removed.first().at(1).toInt(), 11);
        QCOMPARE(spies.removed.first().at(2).toInt(), 11);
        QCOMPARE(spies.inserted.count(), 1);
        QCOMPARE(spies.inserted.first().at(1).toInt(), 0);
        QCOMPARE(spies.inserted.first().at(2).toInt(), 0);
        QCOMPARE(spies.count.count(), 0);
        QCOMPARE(model.rowCount(), 12);
        QCOMPARE(fifth.row(), 6);
        QCOMPARE(fifth.data(LibraryListModel::RecordIdRole).toString(), fifthId);
        QCOMPARE(model.data(model.index(0, 0), LibraryListModel::RecordIdRole).toString(), QStringLiteral("21"));
    }
    void filteringRemovesAndRestoresRowsWithoutAReset() {
        LibraryListModel model;
        model.setRecords(numbered(1000));
        // Row 3 (id 4) has the artist "Mar Azul" and matches.
        QPersistentModelIndex matching(model.index(3, 0));
        QCOMPARE(matching.data(LibraryListModel::RecordIdRole).toString(), QStringLiteral("4"));
        Spies spies(model);
        model.setFilter(QStringLiteral("mar"));
        QCOMPARE(model.rowCount(), 250);
        QCOMPARE(spies.reset.count(), 0);
        QVERIFY(spies.removed.count() > 0);
        QCOMPARE(spies.inserted.count(), 0);
        QCOMPARE(spies.count.count(), 1);
        QVERIFY(matching.isValid());
        QCOMPARE(matching.row(), 0);
        QCOMPARE(matching.data(LibraryListModel::RecordIdRole).toString(), QStringLiteral("4"));

        const auto removedCalls = spies.removed.count();
        model.setFilter(QString{});
        QCOMPARE(model.rowCount(), 1000);
        QCOMPARE(spies.reset.count(), 0);
        QCOMPARE(spies.removed.count(), removedCalls);
        QVERIFY(spies.inserted.count() > 0);
        QCOMPARE(spies.count.count(), 2);
        QVERIFY(matching.isValid());
        QCOMPARE(matching.row(), 3);
        QCOMPARE(matching.data(LibraryListModel::RecordIdRole).toString(), QStringLiteral("4"));
    }
    void aReorderFallsBackToAReset() {
        LibraryListModel model;
        model.setRecords(numbered(5));
        Spies spies(model);
        auto records = numbered(5);
        std::swap(records[1], records[3]);
        model.setRecords(records);
        QCOMPARE(spies.reset.count(), 1);
        QCOMPARE((ids(model)), (QStringList{"1", "4", "3", "2", "5"}));
    }
    void aMixedChangeKeepsTheFinalContentRight() {
        LibraryListModel model;
        model.setRecords(numbered(8));
        Spies spies(model);
        auto records = numbered(8);
        records.erase(records.begin() + 5); // 6 leaves.
        records.erase(records.begin() + 1); // 2 leaves.
        records.insert(records.begin() + 3, record(20, "Nova"));
        records.push_back(record(21, "Ultima"));
        records[0].artist = "Mudou";
        model.setRecords(records);
        QCOMPARE(spies.reset.count(), 0);
        QCOMPARE((ids(model)), (QStringList{"1", "3", "4", "20", "5", "7", "8", "21"}));
        QCOMPARE(model.data(model.index(0, 0), LibraryListModel::ArtistRole).toString(), QStringLiteral("Mudou"));
        QCOMPARE(spies.count.count(), 0);
    }
    // A first letter that matches one row in four splits 20 000 rows into 5 000 removal blocks: row signals
    // would shift the rows after each block and hand the views 5 000 changes, so the model resets once instead.
    // Measured on this machine (Release): about 17 ms for the filter and its clearing with resets, 60 to 70 ms
    // with row signals; the views add about 45 ms to a reset and 60 to 110 ms to the row signals.
    void aScatteredFilterOnALargeLibraryResetsOnce() {
        LibraryListModel model;
        model.setRecords(numbered(20000));
        Spies spies(model);
        QElapsedTimer timer;
        timer.start();
        model.setFilter(QStringLiteral("mar"));
        QCOMPARE(model.rowCount(), 5000);
        model.setFilter(QString{});
        const auto elapsed = timer.elapsed();
        QCOMPARE(model.rowCount(), 20000);
        QCOMPARE(spies.reset.count(), 2);
        QCOMPARE(spies.removed.count(), 0);
        QCOMPARE(spies.inserted.count(), 0);
        QCOMPARE(spies.changed.count(), 0);
        QCOMPARE(spies.count.count(), 2);
        QVERIFY2(elapsed < 100, qPrintable(QStringLiteral("filter and clear took %1 ms").arg(elapsed)));
    }
    // After a reset the view finds its current record again by id.
    void rowOfFindsAVisibleRecord() {
        LibraryListModel model;
        model.setRecords(numbered(20000));
        model.setFilter(QStringLiteral("mar"));
        QCOMPARE(model.rowOf(QStringLiteral("4000")), 999);
        QCOMPARE(model.rowOf(QStringLiteral("4")), 0);
        QCOMPARE(model.rowOf(QStringLiteral("3")), -1);
        QCOMPARE(model.rowOf(QString{}), -1);
    }
    void aLargeLibraryIsDiffedInLinearTime() {
        LibraryListModel model;
        model.setRecords(numbered(20000));
        auto records = numbered(20001);
        Spies spies(model);
        QElapsedTimer timer;
        timer.start();
        model.setRecords(records);
        QVERIFY2(timer.elapsed() < 2000, "diff should be linear");
        QCOMPARE(spies.reset.count(), 0);
        QCOMPARE(model.rowCount(), 20001);
    }
};

QTEST_GUILESS_MAIN(LibraryListModelTests)
#include "LibraryListModelTests.moc"
