#include "adapters/QtJsonPlaylistStore.hpp"
#include "presentation/PlaylistsBinding.hpp"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDeadlineTimer>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QProcess>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>

#include <algorithm>
#include <cstdio>
#include <memory>
#include <optional>

using namespace music_player::m0;
namespace {
constexpr qint64 child_timeout_ms = 5'000;
constexpr qsizetype maximum_child_output_bytes = 64 * 1024;

struct ChildResult final {
    bool started{}, finished{}, stopped{};
    QProcess::ExitStatus status{QProcess::CrashExit};
    int code{-1};
    QByteArray output, error;
};
ChildResult runChild(const QString& mode, const QString& directory, const QString& cwd) {
    auto process = std::make_unique<QProcess>();
    process->setProgram(QCoreApplication::applicationFilePath());
    process->setArguments({mode, directory});
    process->setWorkingDirectory(cwd);
    process->setProcessChannelMode(QProcess::SeparateChannels);
    QDeadlineTimer deadline(child_timeout_ms);
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
        // Only this QProcess's own child is stopped. A missing acknowledgement
        // preserves the handle and causes the driver to retain its TEMP tree.
        process->kill();
        static_cast<void>(process->waitForFinished(1'000));
    }
    result.stopped = process->state() == QProcess::NotRunning;
    if (result.stopped) {
        result.status = process->exitStatus();
        result.code = process->exitCode();
        result.output = process->readAllStandardOutput();
        result.error = process->readAllStandardError();
    } else {
        static_cast<void>(process.release());
    }
    return result;
}

bool writeFixture(const QString& path, const QByteArray& bytes) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::NewOnly) && file.write(bytes) == bytes.size() && file.flush();
}
std::optional<QByteArray> readFixture(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    const auto bytes = file.read(QtJsonPlaylistStore::maximum_file_size + 1);
    if (file.error() != QFileDevice::NoError || bytes.size() > QtJsonPlaylistStore::maximum_file_size)
        return {};
    return bytes;
}
std::optional<QMap<QString, QByteArray>> fingerprint(const QString& directory) {
    QMap<QString, QByteArray> result;
    const QDir folder(directory);
    for (const auto& leaf :
         folder.entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System, QDir::Name)) {
        const auto path = folder.filePath(leaf);
        const QFileInfo info(path);
        if (!info.isFile() || info.isSymLink())
            return {};
        const auto bytes = readFixture(path);
        if (!bytes)
            return {};
        result.insert(leaf, QCryptographicHash::hash(*bytes, QCryptographicHash::Sha256));
    }
    return result;
}
std::optional<QJsonObject> receipt(const QByteArray& output) {
    if (output.isEmpty() || output.size() > maximum_child_output_bytes)
        return {};
    QJsonParseError failure;
    const auto parsed = QJsonDocument::fromJson(output, &failure);
    if (failure.error != QJsonParseError::NoError || !parsed.isObject())
        return {};
    return parsed.object();
}

QString initialName() { return QStringLiteral("Aberturas 日本"); }
QString finalName() { return QStringLiteral("Aberturas 日本 — favoritas"); }
QString firstAudio(const QString& directory) { return QDir(directory).filePath(QStringLiteral("Abertura 日本.mp3")); }
QString secondAudio(const QString& directory) { return QDir(directory).filePath(QStringLiteral("Segunda faixa.flac")); }
std::filesystem::path sourcePath(const QString& text) {
#ifdef Q_OS_WIN
    return std::filesystem::path{text.toStdWString()};
#else
    const auto bytes = text.toUtf8();
    return std::filesystem::path{std::string(bytes.constData(), static_cast<std::size_t>(bytes.size()))};
#endif
}
PlaylistsBinding::Paths initialQueue(const QString& directory) {
    return {sourcePath(firstAudio(directory)), sourcePath(secondAudio(directory)), sourcePath(firstAudio(directory))};
}
bool ownedTemporaryDirectory(const QString& directory) {
    const QFileInfo folder(directory);
    const QFileInfo root(folder.absolutePath());
    return folder.isAbsolute() && folder.isDir() && !folder.isSymLink() && root.isDir() && !root.isSymLink() &&
           folder.fileName() == QStringLiteral("playlists data 日本") &&
           root.fileName().startsWith(QStringLiteral("RCMusic playlists restart 日本-")) &&
           QDir(root.absolutePath()).canonicalPath() == QDir(QDir::tempPath()).canonicalPath();
}
template <class Condition> bool await(Condition condition, QDeadlineTimer& deadline) {
    while (!condition()) {
        if (deadline.hasExpired())
            return false;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        QThread::msleep(1);
    }
    return true;
}
bool loaded(PlaylistsBinding& binding, QDeadlineTimer& deadline) {
    return binding.retryLoad() && await([&] { return !binding.busy(); }, deadline) && binding.ready();
}
bool saved(PlaylistsBinding& binding, QDeadlineTimer& deadline) {
    return await([&] { return !binding.busy(); }, deadline) && !binding.hasPendingChange();
}
bool drained(PlaylistsBinding& binding, QDeadlineTimer& deadline) {
    if (binding.prepareExit())
        return false; // Preparation has its own observable first phase.
    if (!await([&] { return binding.prepareExit(); }, deadline))
        return false;
    binding.beginShutdown();
    return !binding.interactionAllowed() && !binding.busy() && !binding.hasPendingChange();
}
std::optional<QJsonObject> observedPlaylist(const PlaylistsBinding& binding, const QString& directory) {
    const auto playlists = binding.playlists();
    if (playlists.size() != 1)
        return {};
    const auto row = playlists.first().toMap();
    const auto id = row.value("id").toString();
    const auto entries = binding.entries();
    if (id.isEmpty() || row.value("name").toString() != finalName() || row.value("count").toInt() != 3 ||
        binding.selectedPlaylistId() != id || entries.size() != 3)
        return {};
    QJsonArray entry_ids, paths;
    QStringList seen;
    for (const auto& entry : entries) {
        const auto fields = entry.toMap();
        const auto entry_id = fields.value("id").toString();
        bool valid{};
        const auto numeric_id = entry_id.toULongLong(&valid);
        if (!valid || numeric_id == 0 || QString::number(numeric_id) != entry_id || seen.contains(entry_id))
            return {};
        seen.push_back(entry_id);
        entry_ids.push_back(entry_id);
        paths.push_back(fields.value("path").toString());
    }
    if (paths != QJsonArray({firstAudio(directory), firstAudio(directory), secondAudio(directory)}))
        return {};
    return QJsonObject{{"playlist_id", id},
                       {"playlist_name", row.value("name").toString()},
                       {"entry_ids", entry_ids},
                       {"paths", paths}};
}
int printReceipt(QJsonObject result, const int reads, const int appends, const bool unchanged) {
    result.insert("queue_reader_calls", reads);
    result.insert("queue_appender_calls", appends);
    result.insert("queue_unchanged", unchanged);
    result.insert("drained", true);
    result.insert("working_directory", QDir::currentPath());
    const auto bytes = QJsonDocument(result).toJson(QJsonDocument::Compact) + '\n';
    const auto written = std::fwrite(bytes.constData(), 1, static_cast<std::size_t>(bytes.size()), stdout);
    return written == static_cast<std::size_t>(bytes.size()) && std::fflush(stdout) == 0 ? 0 : 10;
}
int childMode(const QString& mode, const QString& directory) {
    if (!ownedTemporaryDirectory(directory))
        return 2;
    const auto primary = QDir(directory).filePath(QStringLiteral("playlists.json"));
    const auto backup = QDir(directory).filePath(QStringLiteral("playlists.json.bak"));
    if (mode == QStringLiteral("--write") && (QFileInfo::exists(primary) || QFileInfo::exists(backup)))
        return 2; // The fixture writer never replaces an existing document.
    QDeadlineTimer deadline(4'000);
    auto store = std::make_shared<QtJsonPlaylistStore>(directory);
    auto queue = initialQueue(directory);
    const auto unchanged_queue = queue;
    int reads{}, appends{};
    PlaylistsBinding binding(
        store,
        [&] {
            ++reads;
            return queue;
        },
        [&](auto paths) {
            ++appends;
            queue.insert(queue.end(), paths.begin(), paths.end());
            return true;
        });
    if (!loaded(binding, deadline))
        return 3;
    if (reads != 0 || appends != 0 || !binding.selectedPlaylistId().isEmpty())
        return 4;
    if (mode == QStringLiteral("--write")) {
        if (!binding.playlists().isEmpty() || !binding.saveQueueAsPlaylist(initialName()) || !saved(binding, deadline))
            return 5;
        const auto rows = binding.playlists();
        if (rows.size() != 1 || reads != 1 || appends != 0)
            return 5;
        const auto id = rows.first().toMap().value("id").toString();
        const auto original_entries = binding.entries();
        if (original_entries.size() != 3)
            return 5;
        const auto moved_id = original_entries.last().toMap().value("id").toString();
        if (!binding.renamePlaylist(id, finalName()) || !saved(binding, deadline))
            return 6;
        if (!binding.moveEntry(id, moved_id, 0) || !saved(binding, deadline))
            return 7;
        const auto after = binding.entries();
        if (after.size() != 3 || after[0].toMap().value("id") != original_entries[2].toMap().value("id") ||
            after[1].toMap().value("id") != original_entries[0].toMap().value("id") ||
            after[2].toMap().value("id") != original_entries[1].toMap().value("id"))
            return 7;
    } else {
        const auto rows = binding.playlists();
        if (rows.size() != 1 || !binding.selectPlaylist(rows.first().toMap().value("id").toString()))
            return 8;
        if (reads != 0 || appends != 0)
            return 8; // Loading and selecting never invoke playback callbacks.
    }
    const auto observed = observedPlaylist(binding, directory);
    if (!observed || queue != unchanged_queue)
        return 9;
    if (!drained(binding, deadline))
        return 9;
    return printReceipt(*observed, reads, appends, queue == unchanged_queue);
}
} // namespace

class PlaylistsRestartTests final : public QObject {
    Q_OBJECT
  private slots:
    void namedPlaylistsSurviveTwoProcessesWithoutAutomaticQueueActions() {
        QTemporaryDir temporary(QDir::tempPath() + QStringLiteral("/RCMusic playlists restart 日本-XXXXXX"));
        QVERIFY(temporary.isValid());
        const auto directory = temporary.filePath(QStringLiteral("playlists data 日本"));
        const auto write_cwd = temporary.filePath(QStringLiteral("write cwd"));
        const auto read_cwd = temporary.filePath(QStringLiteral("read cwd 日本"));
        QVERIFY(QDir().mkpath(directory));
        QVERIFY(QDir().mkpath(write_cwd));
        QVERIFY(QDir().mkpath(read_cwd));
        const auto first_audio = QDir(directory).filePath(QStringLiteral("Abertura 日本.mp3"));
        const auto second_audio = QDir(directory).filePath(QStringLiteral("Segunda faixa.flac"));
        QVERIFY(writeFixture(first_audio, "synthetic audio-path sentinel A"));
        QVERIFY(writeFixture(second_audio, "synthetic audio-path sentinel B"));

        const auto written = runChild(QStringLiteral("--write"), directory, write_cwd);
        if (!written.stopped)
            temporary.setAutoRemove(false);
        QVERIFY(written.started);
        QVERIFY(written.finished);
        QVERIFY(written.stopped);
        QCOMPARE(written.status, QProcess::NormalExit);
        QCOMPARE(written.code, 0);
        QVERIFY(written.error.isEmpty());
        const auto write_receipt = receipt(written.output);
        QVERIFY(write_receipt);
        QCOMPARE(write_receipt->value("queue_reader_calls").toInt(), 1);
        QCOMPARE(write_receipt->value("queue_appender_calls").toInt(), 0);
        QCOMPARE(write_receipt->value("queue_unchanged").toBool(), true);
        QCOMPARE(write_receipt->value("drained").toBool(), true);
        QCOMPARE(write_receipt->value("working_directory").toString(), QDir(write_cwd).absolutePath());

        const auto before = fingerprint(directory);
        QVERIFY(before);
        QVERIFY(before->contains(QStringLiteral("playlists.json")));
        QVERIFY(before->contains(QStringLiteral("playlists.json.bak")));
        QVERIFY(!before->contains(QStringLiteral("playlists.json.lock")));
        const auto primary = readFixture(QDir(directory).filePath(QStringLiteral("playlists.json")));
        QVERIFY(primary);
        const auto document = QJsonDocument::fromJson(*primary).object();
        QCOMPARE(document.value("schemaVersion").toInt(), 1);
        const auto playlists = document.value("playlists").toArray();
        QCOMPARE(playlists.size(), 1);
        const auto saved = playlists.first().toObject();
        QCOMPARE(saved.value("id"), write_receipt->value("playlist_id"));
        QCOMPARE(saved.value("name"), write_receipt->value("playlist_name"));
        const auto saved_entries = saved.value("entries").toArray();
        const auto entry_ids = write_receipt->value("entry_ids").toArray();
        const auto paths = write_receipt->value("paths").toArray();
        QCOMPARE(saved_entries.size(), 3);
        QCOMPARE(entry_ids.size(), 3);
        QCOMPARE(paths, QJsonArray({first_audio, first_audio, second_audio}));
        for (qsizetype index = 0; index < saved_entries.size(); ++index) {
            const auto entry = saved_entries.at(index).toObject();
            QCOMPARE(entry.value("id"), entry_ids.at(index));
            QCOMPARE(entry.value("sourcePath"), paths.at(index));
        }
        QVERIFY(entry_ids[0] != entry_ids[1]);

        const auto read = runChild(QStringLiteral("--read"), directory, read_cwd);
        if (!read.stopped)
            temporary.setAutoRemove(false);
        QVERIFY(read.started);
        QVERIFY(read.finished);
        QVERIFY(read.stopped);
        QCOMPARE(read.status, QProcess::NormalExit);
        QCOMPARE(read.code, 0);
        QVERIFY(read.error.isEmpty());
        const auto read_receipt = receipt(read.output);
        QVERIFY(read_receipt);
        for (const auto* field : {"playlist_id", "playlist_name", "entry_ids", "paths"})
            QCOMPARE(read_receipt->value(field), write_receipt->value(field));
        QCOMPARE(read_receipt->value("queue_reader_calls").toInt(), 0);
        QCOMPARE(read_receipt->value("queue_appender_calls").toInt(), 0);
        QCOMPARE(read_receipt->value("queue_unchanged").toBool(), true);
        QCOMPARE(read_receipt->value("drained").toBool(), true);
        QCOMPARE(read_receipt->value("working_directory").toString(), QDir(read_cwd).absolutePath());
        QVERIFY(read_receipt->value("working_directory") != write_receipt->value("working_directory"));
        const auto after = fingerprint(directory);
        QVERIFY(after);
        QCOMPARE(*after, *before);
    }
};

int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);
    const auto arguments = application.arguments();
    if (arguments.size() > 1 &&
        (arguments[1] == QStringLiteral("--write") || arguments[1] == QStringLiteral("--read"))) {
        if (arguments.size() != 3 || !QFileInfo(arguments[2]).isAbsolute())
            return 2;
        return childMode(arguments[1], arguments[2]);
    }
    PlaylistsRestartTests tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "PlaylistsRestartTests.moc"
