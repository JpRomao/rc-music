#include "adapters/QtJsonLyricsVariantsStore.hpp"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDeadlineTimer>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QMap>
#include <QProcess>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>
#include <limits>
#include <memory>
#include <optional>

using namespace music_player::m0;
namespace {
std::filesystem::path native(const QString& value) {
#ifdef _WIN32
    return std::filesystem::path(value.toStdWString());
#else
    return std::filesystem::path(value.toUtf8().toStdString());
#endif
}
QString slot(const QTemporaryDir& root, const QString& track) {
    return root.filePath(
        "lyrics/" + QString::fromLatin1(QCryptographicHash::hash(track.toUtf8(), QCryptographicHash::Sha256).toHex()) +
        ".json");
}
QByteArray read(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}
void write(const QString& path, const QByteArray& bytes) {
    QVERIFY(QDir{}.mkpath(QFileInfo(path).absolutePath()));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(bytes), bytes.size());
}
LyricsVariants fixture() {
    return {{.id = std::numeric_limits<std::uint64_t>::max(),
             .name = "Original 日本語",
             .language = "ja",
             .script = "Jpan",
             .source = "Imported locally",
             .content = "[00:01.001]テスト\n[00:05.000]次のテスト"},
            {.id = 9'007'199'254'740'993ULL,
             .name = "Romanizada",
             .language = "ja",
             .script = "Latn",
             .source = "User draft",
             .content = "[00:01.001]Test\n[00:05.000]Other test",
             .kind = LyricsVariantKind::Romanized,
             .generated = true}};
}
struct ChildResult final {
    bool started{}, finished{}, stopped{};
    QProcess::ExitStatus status{QProcess::CrashExit};
    int code{-1};
    QByteArray error;
};
ChildResult runChild(const QString& mode, const QString& directory, const QString& cwd) {
    auto process = std::make_unique<QProcess>();
    process->setProgram(QCoreApplication::applicationFilePath());
    process->setArguments({mode, directory});
    process->setWorkingDirectory(cwd);
    process->setProcessChannelMode(QProcess::SeparateChannels);
    QDeadlineTimer deadline(5'000);
    process->start();
    ChildResult result;
    result.started = process->waitForStarted(static_cast<int>(std::max<qint64>(1, deadline.remainingTime())));
    if (result.started) {
        process->closeWriteChannel();
        result.finished = process->state() == QProcess::NotRunning ||
                          (!deadline.hasExpired() &&
                           process->waitForFinished(static_cast<int>(std::max<qint64>(1, deadline.remainingTime()))));
    }
    if (process->state() != QProcess::NotRunning) {
        process->kill();
        static_cast<void>(process->waitForFinished(1'000));
    }
    result.stopped = process->state() == QProcess::NotRunning;
    if (result.stopped) {
        result.status = process->exitStatus();
        result.code = process->exitCode();
        result.error = process->readAllStandardError();
    } else {
        // Retain the own child handle; the caller preserves its TEMP tree.
        static_cast<void>(process.release());
    }
    return result;
}
std::optional<QMap<QString, QByteArray>> fingerprint(const QString& directory) {
    QMap<QString, QByteArray> result;
    QDirIterator files(directory, QDir::Files | QDir::Hidden | QDir::NoSymLinks, QDirIterator::Subdirectories);
    while (files.hasNext()) {
        const auto path = files.next();
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            return {};
        const auto bytes = file.readAll();
        if (file.error() != QFileDevice::NoError)
            return {};
        result.insert(QDir(directory).relativeFilePath(path),
                      QCryptographicHash::hash(bytes, QCryptographicHash::Sha256));
    }
    return result;
}
} // namespace
class QtJsonLyricsVariantsStoreTests final : public QObject {
    Q_OBJECT
  private slots:
    void passiveConstructionAndMissingLoadDoNotCreateDirectories() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const auto config = temp.filePath("missing config");
        QtJsonLyricsVariantsStore store(config);
        QVERIFY(!QFileInfo::exists(config));
        QCOMPARE(store.load(native(temp.filePath("track.mp3"))).status, LyricsVariantsLoadStatus::Missing);
        QVERIFY(!QFileInfo::exists(config));
    }
    void firstWriteAndRestartRetainEveryFormExactIdAndTimesWithoutTouchingAudio() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const auto track = temp.filePath("曲 日本語.mp3");
        write(track, "AUDIO-SENTINEL");
        const auto variants = fixture();
        {
            QtJsonLyricsVariantsStore store(temp.path());
            QCOMPARE(store.save(native(track), variants), LyricsVariantsSaveStatus::Saved);
        }
        QtJsonLyricsVariantsStore restarted(temp.path());
        const auto restored = restarted.load(native(track));
        QCOMPARE(restored.status, LyricsVariantsLoadStatus::Loaded);
        QVERIFY(restored.variants == variants);
        QVERIFY(read(slot(temp, track)).contains("18446744073709551615"));
        QCOMPARE(read(track), QByteArray("AUDIO-SENTINEL"));
        QVERIFY(!QFileInfo::exists(track + ".lrc"));
    }
    void trackIdentitiesHaveIndependentDocumentsAndCanContainOnlyOneForm() {
        QTemporaryDir temp;
        const auto a = temp.filePath("a.mp3"), b = temp.filePath("b.mp3");
        QtJsonLyricsVariantsStore store(temp.path());
        auto one = fixture();
        one.erase(one.begin());
        QCOMPARE(store.save(native(a), one), LyricsVariantsSaveStatus::Saved);
        QCOMPARE(store.load(native(b)).status, LyricsVariantsLoadStatus::Missing);
        QVERIFY(store.load(native(a)).variants == one);
        QCOMPARE(store.save(native(b), {}), LyricsVariantsSaveStatus::Saved);
        QVERIFY(store.load(native(b)).variants.empty());
        QVERIFY(store.load(native(a)).variants == one);
    }
    void schemaTwoLoadsAndRoundTripsTranslationsAlongsideExistingForms() {
        QTemporaryDir temp;
        const auto track = temp.filePath("song.mp3"), primary = slot(temp, track);
        QtJsonLyricsVariantsStore store(temp.path());
        const auto existing = fixture();
        QCOMPARE(store.save(native(track), existing), LyricsVariantsSaveStatus::Saved);
        auto root = QJsonDocument::fromJson(read(primary)).object();
        QCOMPARE(root.value("schemaVersion").toInt(), 1);
        auto versions = root.value("variants").toArray();
        versions.push_back(QJsonObject{{"id", "7"},
                                       {"name", "Tradução"},
                                       {"language", "pt"},
                                       {"script", "Latn"},
                                       {"source", "Letras"},
                                       {"content", "Linha traduzida\nOutra linha"},
                                       {"kind", "translation"},
                                       {"generated", false}});
        root.insert("schemaVersion", 2);
        root.insert("variants", versions);
        write(primary, QJsonDocument(root).toJson(QJsonDocument::Compact));

        const auto restored = store.load(native(track));
        QCOMPARE(restored.status, LyricsVariantsLoadStatus::Loaded);
        QCOMPARE(restored.variants.size(), std::size_t{3});
        QVERIFY(restored.variants.front() == existing.front());
        QVERIFY(restored.variants[1] == existing[1]);
        QCOMPARE(restored.variants.back().language, std::string{"pt"});
        QCOMPARE(restored.variants.back().content, std::string{"Linha traduzida\nOutra linha"});
        QVERIFY(!restored.variants.back().generated);

        QCOMPARE(store.save(native(track), restored.variants), LyricsVariantsSaveStatus::Saved);
        const auto rewritten = QJsonDocument::fromJson(read(primary)).object();
        QCOMPARE(rewritten.value("schemaVersion").toInt(), 2);
        QCOMPARE(rewritten.value("variants").toArray().last().toObject().value("kind").toString(), "translation");
        QtJsonLyricsVariantsStore restarted(temp.path());
        QVERIFY(restarted.load(native(track)).variants == restored.variants);
    }
    void healthyBackupRecoversCorruptionWithoutRepairingTheFiles() {
        QTemporaryDir temp;
        const auto track = temp.filePath("song.mp3"), primary = slot(temp, track);
        QtJsonLyricsVariantsStore store(temp.path());
        const auto old = fixture();
        QCOMPARE(store.save(native(track), old), LyricsVariantsSaveStatus::Saved);
        auto newer = old;
        newer[0].name = "Revised original";
        QCOMPARE(store.save(native(track), newer), LyricsVariantsSaveStatus::Saved);
        const auto backup = read(primary + ".bak");
        write(primary, "corrupt");
        const auto recovered = store.load(native(track));
        QCOMPARE(recovered.status, LyricsVariantsLoadStatus::RecoveredFromBackup);
        QVERIFY(recovered.variants == old);
        QCOMPARE(read(primary), QByteArray("corrupt"));
        QCOMPARE(read(primary + ".bak"), backup);
        QCOMPARE(store.save(native(track), newer), LyricsVariantsSaveStatus::Saved);
        QCOMPARE(read(primary + ".bak"), backup);
    }
    void futureVersionInEitherSlotBlocksEverySaveWithoutReplacingBytes_data() {
        QTest::addColumn<bool>("backup");
        QTest::newRow("primary") << false;
        QTest::newRow("backup") << true;
    }
    void futureVersionInEitherSlotBlocksEverySaveWithoutReplacingBytes() {
        QFETCH(bool, backup);
        QTemporaryDir temp;
        const auto track = temp.filePath("song.mp3"), primary = slot(temp, track);
        QtJsonLyricsVariantsStore store(temp.path());
        QCOMPARE(store.save(native(track), fixture()), LyricsVariantsSaveStatus::Saved);
        const QByteArray future = "{\"schemaVersion\":3,\"opaque\":{\"next\":[1,2,3]}}";
        write(primary + (backup ? ".bak" : ""), future);
        const auto before = read(primary), before_backup = read(primary + ".bak");
        QCOMPARE(store.load(native(track)).status, LyricsVariantsLoadStatus::FutureVersion);
        QCOMPARE(store.save(native(track), {}), LyricsVariantsSaveStatus::BlockedByFutureVersion);
        QCOMPARE(read(primary), before);
        QCOMPARE(read(primary + ".bak"), before_backup);
    }
    void malformedUnknownOrWrongIdentityDocumentsAreNotOverwritten_data() {
        QTest::addColumn<QByteArray>("bytes");
        QTest::newRow("corrupt") << QByteArray("not JSON");
        QTest::newRow("duplicate keys") << QByteArray(
            "{\"schemaVersion\":1,\"schemaVersion\":1,\"sourcePath\":TRACK,\"variants\":[]}");
        QTest::newRow("unknown field") << QByteArray(
            "{\"schemaVersion\":1,\"sourcePath\":TRACK,\"variants\":[],\"extra\":1}");
        QTest::newRow("wrong identity") << QByteArray(
            "{\"schemaVersion\":1,\"sourcePath\":\"different track\",\"variants\":[]}");
        QTest::newRow("invalid UTF8") << QByteArray(
            "{\"schemaVersion\":1,\"sourcePath\":TRACK,\"variants\":[],\"\xc0\xaf\":1}");
        const QByteArray valid_variant =
            "{\"id\":\"1\",\"name\":\"Original\",\"language\":\"ja\",\"script\":\"Jpan\",\"source\":\"User\","
            "\"content\":\"Test\",\"kind\":\"original\",\"generated\":false}";
        auto duplicate_field = valid_variant;
        duplicate_field.replace("\"name\":\"Original\"", "\"name\":\"Original\",\"name\":\"Other\"");
        QTest::newRow("duplicate variant field")
            << ("{\"schemaVersion\":1,\"sourcePath\":TRACK,\"variants\":[" + duplicate_field + "]}");
        auto invalid_id = valid_variant;
        invalid_id.replace("\"id\":\"1\"", "\"id\":\"01\"");
        QTest::newRow("invalid ID") << ("{\"schemaVersion\":1,\"sourcePath\":TRACK,\"variants\":[" + invalid_id + "]}");
        auto translation = valid_variant;
        translation.replace("\"kind\":\"original\"", "\"kind\":\"translation\"");
        QTest::newRow("translation in version one")
            << ("{\"schemaVersion\":1,\"sourcePath\":TRACK,\"variants\":[" + translation + "]}");
        auto unknown_kind = valid_variant;
        unknown_kind.replace("\"kind\":\"original\"", "\"kind\":\"unsupported\"");
        QTest::newRow("unknown kind in version two")
            << ("{\"schemaVersion\":2,\"sourcePath\":TRACK,\"variants\":[" + unknown_kind + "]}");
    }
    void malformedUnknownOrWrongIdentityDocumentsAreNotOverwritten() {
        QFETCH(QByteArray, bytes);
        QTemporaryDir temp;
        const auto track = temp.filePath("song.mp3"), primary = slot(temp, track);
        const auto encoded_track = QJsonDocument(QJsonObject{{"path", track}}).toJson(QJsonDocument::Compact);
        bytes.replace("TRACK", encoded_track.mid(8, encoded_track.size() - 9));
        write(primary, bytes);
        QtJsonLyricsVariantsStore store(temp.path());
        QCOMPARE(store.load(native(track)).status, LyricsVariantsLoadStatus::Invalid);
        QCOMPARE(store.save(native(track), fixture()), LyricsVariantsSaveStatus::Failed);
        QCOMPARE(read(primary), bytes);
    }
    void lockedOrNonregularSlotsAreUnavailableAndPreserved() {
        QTemporaryDir temp;
        const auto track = temp.filePath("song.mp3"), primary = slot(temp, track);
        QtJsonLyricsVariantsStore store(temp.path());
        QCOMPARE(store.save(native(track), fixture()), LyricsVariantsSaveStatus::Saved);
        const auto before = read(primary);
        QLockFile lock(primary + ".lock");
        QVERIFY(lock.tryLock(0));
        QCOMPARE(store.load(native(track)).status, LyricsVariantsLoadStatus::Unavailable);
        QCOMPARE(store.save(native(track), {}), LyricsVariantsSaveStatus::Failed);
        QCOMPARE(read(primary), before);
        lock.unlock();
        QVERIFY(QDir{}.mkpath(primary + ".bak"));
        QCOMPARE(store.load(native(track)).status, LyricsVariantsLoadStatus::Unavailable);
        QCOMPARE(store.save(native(track), {}), LyricsVariantsSaveStatus::Failed);
        QCOMPARE(read(primary), before);
    }
    void invalidInputCannotChangeHealthyFilesOrCreateAnAudioSidecar() {
        QTemporaryDir temp;
        const auto track = temp.filePath("song.mp3"), primary = slot(temp, track);
        QtJsonLyricsVariantsStore store(temp.path());
        QCOMPARE(store.save(native(track), fixture()), LyricsVariantsSaveStatus::Saved);
        const auto before = read(primary);
        auto bad = fixture();
        bad[0].id = 0;
        QCOMPARE(store.save(native(track), bad), LyricsVariantsSaveStatus::Failed);
        QCOMPARE(store.save(std::filesystem::path("relative.mp3"), fixture()), LyricsVariantsSaveStatus::Failed);
        QCOMPARE(store.load(std::filesystem::path("relative.mp3")).status, LyricsVariantsLoadStatus::Unavailable);
        QCOMPARE(read(primary), before);
        QVERIFY(!QFileInfo::exists(track + ".lrc"));
    }
    void failedBeforeAndAfterCommitIsNeverAcknowledgedAsSaved_data() {
        QTest::addColumn<int>("point");
        QTest::newRow("before backup") << static_cast<int>(LyricsVariantsStoreFaultPoint::BeforeBackupCommit);
        QTest::newRow("after backup") << static_cast<int>(LyricsVariantsStoreFaultPoint::AfterBackupCommit);
        QTest::newRow("before primary") << static_cast<int>(LyricsVariantsStoreFaultPoint::BeforePrimaryCommit);
        QTest::newRow("after primary") << static_cast<int>(LyricsVariantsStoreFaultPoint::AfterPrimaryCommit);
    }
    void failedBeforeAndAfterCommitIsNeverAcknowledgedAsSaved() {
        QFETCH(int, point);
        QTemporaryDir temp;
        const auto track = temp.filePath("song.mp3");
        QtJsonLyricsVariantsStore clean(temp.path());
        const auto old = fixture();
        QCOMPARE(clean.save(native(track), old), LyricsVariantsSaveStatus::Saved);
        auto candidate = old;
        candidate[0].name = "New candidate";
        QtJsonLyricsVariantsStore failing(temp.path(), {.inject_fault = [point](const auto observed) {
                                              return static_cast<int>(observed) == point;
                                          }});
        QCOMPARE(failing.save(native(track), candidate), LyricsVariantsSaveStatus::Failed);
        const auto restored = clean.load(native(track));
        QCOMPARE(restored.status, LyricsVariantsLoadStatus::Loaded);
        QVERIFY(restored.variants ==
                (point == static_cast<int>(LyricsVariantsStoreFaultPoint::AfterPrimaryCommit) ? candidate : old));
    }
    void oversizedDocumentAndConfigurationFileAreActionable() {
        QTemporaryDir temp;
        const auto track = temp.filePath("song.mp3"), primary = slot(temp, track);
        write(primary, QByteArray(QtJsonLyricsVariantsStore::maximum_file_size + 1, 'x'));
        QtJsonLyricsVariantsStore store(temp.path());
        QCOMPARE(store.load(native(track)).status, LyricsVariantsLoadStatus::Invalid);
        QCOMPARE(store.save(native(track), {}), LyricsVariantsSaveStatus::Failed);
        const auto config = temp.filePath("not-directory");
        write(config, "CONFIG-SENTINEL");
        QtJsonLyricsVariantsStore unavailable(config);
        QCOMPARE(unavailable.load(native(track)).status, LyricsVariantsLoadStatus::Unavailable);
        QCOMPARE(unavailable.save(native(track), fixture()), LyricsVariantsSaveStatus::Failed);
        QCOMPARE(read(config), QByteArray("CONFIG-SENTINEL"));
    }
    void everyVariantSurvivesTwoProcessesAndReadLeavesEveryFileUnchanged() {
        QTemporaryDir temporary(QDir::tempPath() + QStringLiteral("/RCMusic lyrics restart 日本-XXXXXX"));
        QVERIFY(temporary.isValid());
        const auto directory = temporary.filePath("saved variants 日本");
        const auto write_cwd = temporary.filePath("write cwd");
        const auto read_cwd = temporary.filePath("read cwd 日本");
        QVERIFY(QDir{}.mkpath(directory));
        QVERIFY(QDir{}.mkpath(write_cwd));
        QVERIFY(QDir{}.mkpath(read_cwd));
        const auto track = QDir(directory).filePath("曲 日本語.mp3");
        write(track, "AUDIO-SENTINEL");
        const auto written = runChild("--write", directory, write_cwd);
        if (!written.stopped)
            temporary.setAutoRemove(false);
        QVERIFY(written.started);
        QVERIFY(written.finished);
        QVERIFY(written.stopped);
        QCOMPARE(written.status, QProcess::NormalExit);
        QCOMPARE(written.code, 0);
        QVERIFY(written.error.isEmpty());
        const auto primary = QDir(directory).filePath(
            "lyrics/" +
            QString::fromLatin1(QCryptographicHash::hash(track.toUtf8(), QCryptographicHash::Sha256).toHex()) +
            ".json");
        const auto bytes = read(primary);
        QVERIFY(!bytes.isEmpty());
        const auto root = QJsonDocument::fromJson(bytes).object();
        QCOMPARE(root.value("sourcePath").toString(), track);
        const auto variants = root.value("variants").toArray();
        QCOMPARE(variants.size(), 2);
        QCOMPARE(variants[0].toObject().value("id").toString(), QStringLiteral("18446744073709551615"));
        QCOMPARE(variants[1].toObject().value("id").toString(), QStringLiteral("9007199254740993"));
        QVERIFY(!read(primary + ".bak").isEmpty());
        const auto before = fingerprint(directory);
        QVERIFY(before);
        QCOMPARE(before->size(), 3);
        const auto loaded = runChild("--read", directory, read_cwd);
        if (!loaded.stopped)
            temporary.setAutoRemove(false);
        QVERIFY(loaded.started);
        QVERIFY(loaded.finished);
        QVERIFY(loaded.stopped);
        QCOMPARE(loaded.status, QProcess::NormalExit);
        QCOMPARE(loaded.code, 0);
        QVERIFY(loaded.error.isEmpty());
        const auto after = fingerprint(directory);
        QVERIFY(after);
        QCOMPARE(*after, *before);
        QCOMPARE(read(track), QByteArray("AUDIO-SENTINEL"));
    }
};
int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);
    const auto arguments = application.arguments();
    if (arguments.size() > 1 &&
        (arguments[1] == QStringLiteral("--write") || arguments[1] == QStringLiteral("--read"))) {
        if (arguments.size() != 3 || !QFileInfo(arguments[2]).isAbsolute())
            return 2;
        const auto directory = arguments[2];
        const auto track = QDir(directory).filePath(QStringLiteral("曲 日本語.mp3"));
        if (read(track) != QByteArray("AUDIO-SENTINEL"))
            return 3;
        QtJsonLyricsVariantsStore store(directory);
        const auto expected = fixture();
        if (arguments[1] == QStringLiteral("--write")) {
            auto first = expected;
            first.pop_back();
            if (store.save(native(track), first) != LyricsVariantsSaveStatus::Saved ||
                store.save(native(track), expected) != LyricsVariantsSaveStatus::Saved)
                return 4;
        } else {
            const auto restored = store.load(native(track));
            if (restored.status != LyricsVariantsLoadStatus::Loaded || restored.variants != expected)
                return 5;
        }
        return read(track) == QByteArray("AUDIO-SENTINEL") ? 0 : 6;
    }
    QtJsonLyricsVariantsStoreTests tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "QtJsonLyricsVariantsStoreTests.moc"
