#include "m0/adapters/fixture/FixtureRepository.hpp"
#include "m0/presentation/QueueListModel.hpp"

#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaType>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>

namespace music_player::m0::test {

class FixtureRepositoryTests final : public QObject {
    Q_OBJECT

  private slots:
    void loadsFrozenFixtureAndQueueReadOnly();
    void preservesHostileFixedCases();
    void queueModelExposesTypedRolesInSourceOrder();
    void rejectsQueueCountDrift();
    void rejectsHostileCaseDrift();
    void rejectsDriftInOtherwiseIgnoredFixtureFields();
    void rejectsIntegerNarrowingAliases();
    void rejectsIntegerOutsideSigned64BitRange();
};

[[nodiscard]] QByteArray readBytes(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return file.readAll();
}

[[nodiscard]] const QueueItem& itemById(const FixtureBundle& bundle, const QString& occurrence_id) {
    const auto found = std::find_if(bundle.queue.cbegin(), bundle.queue.cend(),
                                    [&](const QueueItem& item) { return item.occurrence_id == occurrence_id; });
    if (found == bundle.queue.cend()) {
        throw FixtureValidationError(QStringLiteral("missing test item"));
    }
    return *found;
}

void FixtureRepositoryTests::loadsFrozenFixtureAndQueueReadOnly() {
    const QString fixture_path = QString::fromUtf8(M0_TEST_FIXTURE_FILE);
    const QString queue_path = QString::fromUtf8(M0_TEST_QUEUE_FILE);
    const QByteArray fixture_before = QCryptographicHash::hash(readBytes(fixture_path), QCryptographicHash::Sha256);
    const QByteArray queue_before = QCryptographicHash::hash(readBytes(queue_path), QCryptographicHash::Sha256);

    const FixtureBundle bundle = FixtureRepository{}.load(fixture_path, queue_path);

    QCOMPARE(bundle.schema_id, QStringLiteral("m0.shell-fixture.v1"));
    QCOMPARE(bundle.schema_version, 1);
    QCOMPARE(bundle.fixture_id, QStringLiteral("M0-SHELL-F1"));
    QCOMPARE(bundle.locale, QStringLiteral("pt-BR"));
    QCOMPARE(bundle.default_state_id, QStringLiteral("empty"));
    QCOMPARE(bundle.queue_schema_id, QStringLiteral("m0.shell-queue.v1"));
    QCOMPARE(bundle.queue_id, QStringLiteral("M0-SHELL-Q1000"));
    QCOMPARE(bundle.states.size(), 3);
    QCOMPARE(bundle.controls.size(), 11);
    QCOMPARE(bundle.queue.size(), 1000);
    QCOMPARE(bundle.defaults.theme, QStringLiteral("system"));
    QCOMPARE(bundle.defaults.motion, QStringLiteral("system"));
    QCOMPARE(bundle.defaults.volume_percent, 80);
    QCOMPARE(bundle.defaults.muted, false);
    QCOMPARE(bundle.defaults.skip_backward_seconds, 5);
    QCOMPARE(bundle.defaults.skip_forward_seconds, 5);
    QCOMPARE(bundle.defaults.close_behavior, QStringLiteral("minimize_to_tray"));
    QCOMPARE(bundle.defaults.restore_last_session, true);
    QCOMPARE(bundle.defaults.start_minimized, false);
    QCOMPARE(bundle.defaults.queue_end_behavior, QStringLiteral("stop_keep_selection"));
    QCOMPARE(QCryptographicHash::hash(readBytes(fixture_path), QCryptographicHash::Sha256), fixture_before);
    QCOMPARE(QCryptographicHash::hash(readBytes(queue_path), QCryptographicHash::Sha256), queue_before);
}

void FixtureRepositoryTests::preservesHostileFixedCases() {
    const FixtureBundle bundle =
        FixtureRepository{}.load(QString::fromUtf8(M0_TEST_FIXTURE_FILE), QString::fromUtf8(M0_TEST_QUEUE_FILE));
    QCOMPARE(bundle.queue.size(), 1000);
    const QueueItem& duplicate_a = itemById(bundle, QStringLiteral("queue-0007"));
    const QueueItem& duplicate_b = itemById(bundle, QStringLiteral("queue-0707"));
    QCOMPARE_NE(duplicate_a.occurrence_id, duplicate_b.occurrence_id);
    QCOMPARE(duplicate_a.semantic_key, duplicate_b.semantic_key);
    QCOMPARE(duplicate_a.title, duplicate_b.title);

    const QueueItem& nfc = itemById(bundle, QStringLiteral("queue-0017"));
    const QueueItem& nfd = itemById(bundle, QStringLiteral("queue-0018"));
    QCOMPARE(nfc.presentation_form, QStringLiteral("nfc"));
    QCOMPARE(nfd.presentation_form, QStringLiteral("nfd"));
    QCOMPARE_NE(nfc.title, nfd.title);
    QCOMPARE(nfc.title.normalized(QString::NormalizationForm_C), nfd.title.normalized(QString::NormalizationForm_C));

    const QueueItem& non_bmp = itemById(bundle, QStringLiteral("queue-0023"));
    QVERIFY(non_bmp.title.contains(QString::fromUcs4(U"🎧")));
    QCOMPARE(itemById(bundle, QStringLiteral("queue-0042")).title,
             QStringLiteral("<b>Não interpretar</b> & \"aspas\" 'simples'"));
    QCOMPARE(itemById(bundle, QStringLiteral("queue-0512")).title.toUcs4().size(), 512);

    const QueueItem& unavailable = itemById(bundle, QStringLiteral("queue-0999"));
    QCOMPARE(unavailable.available, false);
    QVERIFY(!unavailable.duration_ms.has_value());
    QCOMPARE(unavailable.error_code, QStringLiteral("fixture_source_unavailable"));
}

void FixtureRepositoryTests::queueModelExposesTypedRolesInSourceOrder() {
    const FixtureBundle bundle =
        FixtureRepository{}.load(QString::fromUtf8(M0_TEST_FIXTURE_FILE), QString::fromUtf8(M0_TEST_QUEUE_FILE));
    const QueueListModel model(bundle.queue);

    QCOMPARE(model.rowCount(), 1000);
    QCOMPARE(model.roleNames().value(QueueListModel::OccurrenceIdRole), QByteArray("occurrenceId"));
    QCOMPARE(model.roleNames().value(QueueListModel::AvailableRole), QByteArray("available"));
    const QModelIndex first = model.index(0, 0);
    QCOMPARE(model.data(first, QueueListModel::OrderIndexRole).metaType().id(), QMetaType::Int);
    QCOMPARE(model.data(first, QueueListModel::OccurrenceIdRole).metaType().id(), QMetaType::QString);
    QCOMPARE(model.data(first, QueueListModel::AvailableRole).metaType().id(), QMetaType::Bool);
    QCOMPARE(model.data(first, QueueListModel::OrderIndexRole).toInt(), 0);
    QCOMPARE(model.data(first, QueueListModel::OccurrenceIdRole).toString(), QStringLiteral("queue-0001"));
    QCOMPARE(model.data(model.index(16, 0), QueueListModel::OccurrenceIdRole).toString(), QStringLiteral("queue-0017"));
    QCOMPARE(model.data(model.index(999, 0), QueueListModel::OccurrenceIdRole).toString(),
             QStringLiteral("queue-1000"));
    const QModelIndex unavailable = model.index(998, 0);
    QCOMPARE(model.data(unavailable, QueueListModel::AvailableRole).toBool(), false);
    QVERIFY(!model.data(unavailable, QueueListModel::DurationMsRole).isValid());
}

void FixtureRepositoryTests::rejectsQueueCountDrift() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    QJsonDocument queue_document = QJsonDocument::fromJson(readBytes(QString::fromUtf8(M0_TEST_QUEUE_FILE)));
    QJsonObject queue_object = queue_document.object();
    queue_object.insert(QStringLiteral("item_count"), 999);
    QFile mutated(temporary.filePath(QStringLiteral("queue.v1.json")));
    QVERIFY(mutated.open(QIODevice::WriteOnly | QIODevice::Truncate));
    const QByteArray mutated_bytes = QJsonDocument(queue_object).toJson(QJsonDocument::Compact);
    QCOMPARE(mutated.write(mutated_bytes), static_cast<qint64>(mutated_bytes.size()));
    mutated.close();

    QVERIFY_THROWS_EXCEPTION(FixtureValidationError, static_cast<void>(FixtureRepository{}.load(
                                                         QString::fromUtf8(M0_TEST_FIXTURE_FILE), mutated.fileName())));
}

void FixtureRepositoryTests::rejectsHostileCaseDrift() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    QJsonDocument queue_document = QJsonDocument::fromJson(readBytes(QString::fromUtf8(M0_TEST_QUEUE_FILE)));
    QJsonObject queue_object = queue_document.object();
    QJsonArray items = queue_object.value(QStringLiteral("items")).toArray();
    QJsonObject item = items.at(41).toObject();
    QJsonObject metadata = item.value(QStringLiteral("metadata")).toObject();
    metadata.insert(QStringLiteral("title"), QStringLiteral("interpreted"));
    item.insert(QStringLiteral("metadata"), metadata);
    items.replace(41, item);
    queue_object.insert(QStringLiteral("items"), items);
    QFile mutated(temporary.filePath(QStringLiteral("queue.v1.json")));
    QVERIFY(mutated.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QVERIFY(mutated.write(QJsonDocument(queue_object).toJson(QJsonDocument::Compact)) > 0);
    mutated.close();

    QVERIFY_THROWS_EXCEPTION(FixtureValidationError, static_cast<void>(FixtureRepository{}.load(
                                                         QString::fromUtf8(M0_TEST_FIXTURE_FILE), mutated.fileName())));
}

void FixtureRepositoryTests::rejectsDriftInOtherwiseIgnoredFixtureFields() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    QJsonDocument fixture_document = QJsonDocument::fromJson(readBytes(QString::fromUtf8(M0_TEST_FIXTURE_FILE)));
    QJsonObject fixture_object = fixture_document.object();
    QJsonObject window = fixture_object.value(QStringLiteral("window")).toObject();
    window.insert(QStringLiteral("client_width_px"), 961);
    fixture_object.insert(QStringLiteral("window"), window);
    QFile mutated(temporary.filePath(QStringLiteral("fixture.v1.json")));
    QVERIFY(mutated.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QVERIFY(mutated.write(QJsonDocument(fixture_object).toJson(QJsonDocument::Compact)) > 0);
    mutated.close();

    QVERIFY_THROWS_EXCEPTION(FixtureValidationError, static_cast<void>(FixtureRepository{}.load(
                                                         mutated.fileName(), QString::fromUtf8(M0_TEST_QUEUE_FILE))));
}

void FixtureRepositoryTests::rejectsIntegerNarrowingAliases() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());

    QJsonDocument fixture_document = QJsonDocument::fromJson(readBytes(QString::fromUtf8(M0_TEST_FIXTURE_FILE)));
    QJsonObject fixture_object = fixture_document.object();
    fixture_object.insert(QStringLiteral("schema_version"), 4'294'967'297.0);
    QFile fixture_mutated(temporary.filePath(QStringLiteral("fixture.v1.json")));
    QVERIFY(fixture_mutated.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QVERIFY(fixture_mutated.write(QJsonDocument(fixture_object).toJson(QJsonDocument::Compact)) > 0);
    fixture_mutated.close();

    QVERIFY_THROWS_EXCEPTION(
        FixtureValidationError,
        static_cast<void>(FixtureRepository{}.load(fixture_mutated.fileName(), QString::fromUtf8(M0_TEST_QUEUE_FILE))));

    QJsonDocument queue_document = QJsonDocument::fromJson(readBytes(QString::fromUtf8(M0_TEST_QUEUE_FILE)));
    QJsonObject queue_object = queue_document.object();
    QJsonArray items = queue_object.value(QStringLiteral("items")).toArray();
    QJsonObject first = items.at(0).toObject();
    first.insert(QStringLiteral("order_index"), 4'294'967'296.0);
    items.replace(0, first);
    queue_object.insert(QStringLiteral("items"), items);
    QFile queue_mutated(temporary.filePath(QStringLiteral("queue.v1.json")));
    QVERIFY(queue_mutated.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QVERIFY(queue_mutated.write(QJsonDocument(queue_object).toJson(QJsonDocument::Compact)) > 0);
    queue_mutated.close();

    QVERIFY_THROWS_EXCEPTION(
        FixtureValidationError,
        static_cast<void>(FixtureRepository{}.load(QString::fromUtf8(M0_TEST_FIXTURE_FILE), queue_mutated.fileName())));
}

void FixtureRepositoryTests::rejectsIntegerOutsideSigned64BitRange() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    QJsonDocument queue_document = QJsonDocument::fromJson(readBytes(QString::fromUtf8(M0_TEST_QUEUE_FILE)));
    QJsonObject queue_object = queue_document.object();
    QJsonArray items = queue_object.value(QStringLiteral("items")).toArray();
    QJsonObject first = items.at(0).toObject();
    first.insert(QStringLiteral("order_index"), 9'223'372'036'854'775'808.0);
    items.replace(0, first);
    queue_object.insert(QStringLiteral("items"), items);
    QFile mutated(temporary.filePath(QStringLiteral("queue.v1.json")));
    QVERIFY(mutated.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QVERIFY(mutated.write(QJsonDocument(queue_object).toJson(QJsonDocument::Compact)) > 0);
    mutated.close();

    QVERIFY_THROWS_EXCEPTION(FixtureValidationError, static_cast<void>(FixtureRepository{}.load(
                                                         QString::fromUtf8(M0_TEST_FIXTURE_FILE), mutated.fileName())));
}

} // namespace music_player::m0::test

QTEST_GUILESS_MAIN(music_player::m0::test::FixtureRepositoryTests)

#include "FixtureRepositoryTests.moc"
