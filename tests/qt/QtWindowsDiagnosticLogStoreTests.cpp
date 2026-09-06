#include "adapters/QtWindowsDiagnosticLogStore.hpp"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include <array>
#include <limits>
#include <stdexcept>

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

namespace music_player::m0 {
namespace {

constexpr std::int64_t initialTime = 1'700'000'000'000;

DiagnosticRecord record(std::uint64_t count = 1) {
    return {.timestamp_utc_ms = initialTime, .entry = {.count = count}};
}

QByteArray formatted(std::uint64_t count = 1) {
    return QByteArray::fromStdString(formatDiagnosticRecord(record(count)));
}

QString slotPath(const QString& root, int index) {
    return root + QStringLiteral("/diagnostics/diagnostic-%1.log").arg(index);
}

QByteArray read(const QString& path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}

bool write(const QString& path, const QByteArray& bytes) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

QStringList logs(const QString& root) {
    return QDir(root + QStringLiteral("/diagnostics"))
        .entryList({QStringLiteral("diagnostic-*.log")}, QDir::Files, QDir::Name);
}

QByteArray allLogs(const QString& root) {
    QByteArray result;
    for (int i = 0; i < 3; ++i) {
        result += read(slotPath(root, i));
    }
    return result;
}

QtWindowsDiagnosticLogStoreOptions options(std::int64_t& now) {
    return {.clock = [&now] { return now; }};
}

std::uint64_t headerSize(const QString& root) {
    return static_cast<std::uint64_t>(read(slotPath(root, 0)).indexOf('\n') + 1);
}

std::wstring native(const QString& path) { return QDir::toNativeSeparators(path).toStdWString(); }

class HeldHandle final {
  public:
    explicit HeldHandle(HANDLE value) : value_(value) {}
    ~HeldHandle() {
        if (valid()) {
            CloseHandle(value_);
        }
    }
    bool valid() const { return value_ != INVALID_HANDLE_VALUE; }

  private:
    HANDLE value_;
};

} // namespace

class QtWindowsDiagnosticLogStoreTests final : public QObject {
    Q_OBJECT

  private slots:
    void writesOnlyClosedRecordsAndPreservesUnrelatedFiles() {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        QVERIFY(write(root.filePath(QStringLiteral("preferences.json")), "private-config"));
        QVERIFY(write(root.filePath(QStringLiteral("media.mp3")), "private-media"));
        std::int64_t now = initialTime;
        QtWindowsDiagnosticLogStore store(root.path(), options(now));
        QVERIFY(!QDir(root.filePath(QStringLiteral("diagnostics"))).exists());
        QVERIFY(store.append(record()));
        const QByteArray first = read(slotPath(root.path(), 0));
        QVERIFY(first.startsWith("music-player-diagnostics-v1 "));
        QVERIFY(first.endsWith(formatted()));
        QCOMPARE(first.count('\n'), 2);
        ++now;
        QVERIFY(store.append(record(2)));
        QCOMPARE(read(slotPath(root.path(), 0)), first + formatted(2));
        QCOMPARE(logs(root.path()), QStringList{QStringLiteral("diagnostic-0.log")});
        QCOMPARE(read(root.filePath(QStringLiteral("preferences.json"))), QByteArray("private-config"));
        QCOMPARE(read(root.filePath(QStringLiteral("media.mp3"))), QByteArray("private-media"));
    }

    void includesHeaderInByteLimit_data() {
        QTest::addColumn<int>("offset");
        QTest::addColumn<int>("expectedFiles");
        QTest::newRow("below") << -1 << 2;
        QTest::newRow("exact") << 0 << 1;
        QTest::newRow("above") << 1 << 1;
    }

    void includesHeaderInByteLimit() {
        QFETCH(int, offset);
        QFETCH(int, expectedFiles);
        QTemporaryDir root;
        std::int64_t now = initialTime;
        QtWindowsDiagnosticLogStore initial(root.path(), options(now));
        QVERIFY(initial.append(record()));
        auto settings = options(now);
        settings.limits.max_file_bytes = headerSize(root.path()) + 2 * formatted().size() + offset;
        QtWindowsDiagnosticLogStore store(root.path(), settings);
        QVERIFY(store.append(record(2)));
        QCOMPARE(logs(root.path()).size(), expectedFiles);
        for (int i = 0; i < 3; ++i) {
            QVERIFY(static_cast<std::uint64_t>(read(slotPath(root.path(), i)).size()) <=
                    settings.limits.max_file_bytes);
        }
        QVERIFY(allLogs(root.path()).contains(formatted()));
        QVERIFY(allLogs(root.path()).contains(formatted(2)));
    }

    void rotatesOnlyOldestSlotAndBoundsFileCount_data() {
        QTest::addColumn<int>("limit");
        QTest::newRow("one") << 1;
        QTest::newRow("two") << 2;
        QTest::newRow("three") << 3;
    }

    void rotatesOnlyOldestSlotAndBoundsFileCount() {
        QFETCH(int, limit);
        QTemporaryDir root;
        std::int64_t now = initialTime;
        QtWindowsDiagnosticLogStore initial(root.path(), options(now));
        QVERIFY(initial.append(record()));
        auto settings = options(now);
        settings.limits.max_file_bytes = headerSize(root.path()) + formatted().size();
        settings.limits.max_files = limit;
        QtWindowsDiagnosticLogStore store(root.path(), settings);
        for (std::uint64_t i = 2; i <= 5; ++i) {
            QVERIFY(store.append(record(i)));
            QCOMPARE(logs(root.path()).size(), qMin(static_cast<int>(i), limit));
        }
        const auto contents = allLogs(root.path());
        for (std::uint64_t i = 1; i <= 5; ++i) {
            QCOMPARE(contents.contains(formatted(i)), i > 5 - static_cast<std::uint64_t>(limit));
        }
        QVERIFY(!QFile::exists(slotPath(root.path(), 3)));
    }

    void retainsCreationAgeAcrossAppends_data() {
        QTest::addColumn<qint64>("age");
        QTest::addColumn<bool>("retained");
        QTest::newRow("below") << qint64(999) << true;
        QTest::newRow("exact") << qint64(1000) << true;
        QTest::newRow("above") << qint64(1001) << false;
        QTest::newRow("future") << qint64(-1) << false;
    }

    void retainsCreationAgeAcrossAppends() {
        QFETCH(qint64, age);
        QFETCH(bool, retained);
        QTemporaryDir root;
        std::int64_t now = initialTime;
        auto settings = options(now);
        settings.limits.max_age_ms = 1000;
        QtWindowsDiagnosticLogStore store(root.path(), settings);
        QVERIFY(store.append(record()));
        now += 998;
        QVERIFY(store.append(record(2)));
        now = initialTime + age;
        QVERIFY(store.append(record(3)));
        QCOMPARE(allLogs(root.path()).contains(formatted()), retained);
        QCOMPARE(allLogs(root.path()).contains(formatted(2)), retained);
        QVERIFY(allLogs(root.path()).contains(formatted(3)));
        QCOMPARE(logs(root.path()).size(), 1);
    }

    void reducesOnlyOwnedSlotsAndPreservesExtraFiles() {
        QTemporaryDir root;
        std::int64_t now = initialTime;
        QtWindowsDiagnosticLogStore initial(root.path(), options(now));
        QVERIFY(initial.append(record()));
        auto settings = options(now);
        settings.limits.max_file_bytes = headerSize(root.path()) + formatted().size();
        QtWindowsDiagnosticLogStore three(root.path(), settings);
        QVERIFY(three.append(record(2)));
        QVERIFY(three.append(record(3)));
        QVERIFY(write(root.filePath(QStringLiteral("diagnostics/notes.txt")), "keep"));
        QVERIFY(write(root.filePath(QStringLiteral("diagnostics/diagnostic-3.log")), "also-keep"));
        settings.limits.max_files = 1;
        QtWindowsDiagnosticLogStore one(root.path(), settings);
        QVERIFY(one.append(record(4)));
        QVERIFY(!QFile::exists(slotPath(root.path(), 1)));
        QVERIFY(!QFile::exists(slotPath(root.path(), 2)));
        QVERIFY(read(slotPath(root.path(), 0)).contains(formatted(4)));
        QCOMPARE(read(root.filePath(QStringLiteral("diagnostics/notes.txt"))), QByteArray("keep"));
        QCOMPARE(read(slotPath(root.path(), 3)), QByteArray("also-keep"));
    }

    void purgesAllExpiredSlotsWithoutEnumeratingExtras() {
        QTemporaryDir root;
        std::int64_t now = initialTime;
        QtWindowsDiagnosticLogStore initial(root.path(), options(now));
        QVERIFY(initial.append(record()));
        auto settings = options(now);
        settings.limits.max_file_bytes = headerSize(root.path()) + formatted().size();
        settings.limits.max_age_ms = 1000;
        QtWindowsDiagnosticLogStore store(root.path(), settings);
        QVERIFY(store.append(record(2)));
        QVERIFY(store.append(record(3)));
        now += 1001;
        QVERIFY(store.append(record(4)));
        QCOMPARE(logs(root.path()).size(), 1);
        QVERIFY(!allLogs(root.path()).contains(formatted()));
        QVERIFY(allLogs(root.path()).contains(formatted(4)));
    }

    void validatesEveryReservedLeafBeforeChangingOwnedFiles_data() {
        QTest::addColumn<QByteArray>("collision");
        QTest::newRow("unrelated") << QByteArray("user-owned-content\n");
        QTest::newRow("future-version") << QByteArray("music-player-diagnostics-v2 created=1 sequence=1\n");
        QTest::newRow("malformed-header") << QByteArray("music-player-diagnostics-v1 created=no sequence=1\n");
    }

    void validatesEveryReservedLeafBeforeChangingOwnedFiles() {
        QFETCH(QByteArray, collision);
        QTemporaryDir root;
        std::int64_t now = initialTime;
        auto settings = options(now);
        settings.limits.max_age_ms = 1;
        QtWindowsDiagnosticLogStore store(root.path(), settings);
        QVERIFY(store.append(record()));
        const auto original = read(slotPath(root.path(), 0));
        QVERIFY(write(slotPath(root.path(), 2), collision));
        now += 2;
        QVERIFY(!store.append(record(2)));
        QCOMPARE(read(slotPath(root.path(), 0)), original);
        QCOMPARE(read(slotPath(root.path(), 2)), collision);
    }

    void failsClosedWhenReservedFileIsLocked() {
        QTemporaryDir root;
        std::int64_t now = initialTime;
        QtWindowsDiagnosticLogStore store(root.path(), options(now));
        QVERIFY(store.append(record()));
        const auto original = read(slotPath(root.path(), 0));
        {
            HeldHandle lock(CreateFileW(native(slotPath(root.path(), 0)).c_str(), GENERIC_READ, 0, nullptr,
                                        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
            QVERIFY(lock.valid());
            QVERIFY(!store.append(record(2)));
        }
        QCOMPARE(read(slotPath(root.path(), 0)), original);
    }

    void rejectsInvalidLimitsBeforeCreatingAnything_data() {
        QTest::addColumn<int>("variant");
        for (int i = 0; i < 7; ++i) {
            QTest::newRow(qPrintable(QString::number(i))) << i;
        }
    }

    void rejectsInvalidLimitsBeforeCreatingAnything() {
        QFETCH(int, variant);
        QTemporaryDir root;
        std::int64_t now = initialTime;
        auto settings = options(now);
        switch (variant) {
        case 0:
            settings.limits.max_file_bytes = 0;
            break;
        case 1:
            ++settings.limits.max_file_bytes;
            break;
        case 2:
            settings.limits.max_files = 0;
            break;
        case 3:
            ++settings.limits.max_files;
            break;
        case 4:
            settings.limits.max_age_ms = 0;
            break;
        case 5:
            ++settings.limits.max_age_ms;
            break;
        case 6:
            settings.limits.max_file_bytes = 1;
            break;
        }
        QtWindowsDiagnosticLogStore store(root.path(), settings);
        QVERIFY(!store.append(record()));
        QVERIFY(!QDir(root.filePath(QStringLiteral("diagnostics"))).exists());
    }

    void rejectsMissingOrAmbiguousRoots() {
        QTemporaryDir root;
        QVERIFY(write(root.filePath(QStringLiteral("file")), "keep"));
        std::int64_t now = initialTime;
        const QStringList paths{QString{},
                                QStringLiteral("relative"),
                                root.filePath(QStringLiteral("absent")),
                                root.filePath(QStringLiteral("file")),
                                root.path() + QStringLiteral("/../alias"),
                                QStringLiteral("\\\\.\\C:\\"),
                                root.path() + QStringLiteral(".")};
        for (const auto& path : paths) {
            QtWindowsDiagnosticLogStore store(path, options(now));
            QVERIFY(!store.append(record()));
        }
        QVERIFY(!QDir(root.filePath(QStringLiteral("absent"))).exists());
        QCOMPARE(read(root.filePath(QStringLiteral("file"))), QByteArray("keep"));
    }

    void refusesHardLinkedOwnedLog() {
        QTemporaryDir root;
        std::int64_t now = initialTime;
        QtWindowsDiagnosticLogStore store(root.path(), options(now));
        QVERIFY(store.append(record()));
        const auto original = read(slotPath(root.path(), 0));
        const auto alias = root.filePath(QStringLiteral("outside.log"));
        QVERIFY(CreateHardLinkW(native(alias).c_str(), native(slotPath(root.path(), 0)).c_str(), nullptr));
        QVERIFY(!store.append(record(2)));
        QCOMPARE(read(alias), original);
        QCOMPARE(read(slotPath(root.path(), 0)), original);
    }

    void refusesSymlinkAtEveryManagedBoundary_data() {
        QTest::addColumn<int>("boundary");
        QTest::newRow("config-root") << 0;
        QTest::newRow("ancestor") << 1;
        QTest::newRow("diagnostics-child") << 2;
        QTest::newRow("reserved-leaf") << 3;
    }

    void refusesSymlinkAtEveryManagedBoundary() {
        QFETCH(int, boundary);
        QTemporaryDir root;
        QTemporaryDir outside;
        QVERIFY(QDir().mkpath(outside.filePath(QStringLiteral("nested"))));
        QVERIFY(write(outside.filePath(QStringLiteral("target.log")), "keep"));
        QVERIFY(QDir().mkpath(root.filePath(QStringLiteral("diagnostics"))));
        QString link;
        QString target = outside.path();
        QString configured = root.path();
        DWORD flags = SYMBOLIC_LINK_FLAG_DIRECTORY | SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE;
        switch (boundary) {
        case 0:
            link = root.filePath(QStringLiteral("root-link"));
            configured = link;
            break;
        case 1:
            link = root.filePath(QStringLiteral("ancestor-link"));
            configured = link + QStringLiteral("/nested");
            break;
        case 2:
            QVERIFY(QDir(root.path()).rmdir(QStringLiteral("diagnostics")));
            link = root.filePath(QStringLiteral("diagnostics"));
            break;
        case 3:
            link = slotPath(root.path(), 0);
            target = outside.filePath(QStringLiteral("target.log"));
            flags = SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE;
            break;
        }
        if (!CreateSymbolicLinkW(native(link).c_str(), native(target).c_str(), flags)) {
            QSKIP("Host policy does not permit this synthetic symbolic link.");
        }
        std::int64_t now = initialTime;
        QtWindowsDiagnosticLogStore store(configured, options(now));
        QVERIFY(!store.append(record()));
        QCOMPARE(read(outside.filePath(QStringLiteral("target.log"))), QByteArray("keep"));
        QVERIFY(!QDir(outside.filePath(QStringLiteral("diagnostics"))).exists());
        QVERIFY(!QDir(outside.filePath(QStringLiteral("nested/diagnostics"))).exists());
        if (boundary == 3) {
            QVERIFY(DeleteFileW(native(link).c_str()));
        } else {
            QVERIFY(RemoveDirectoryW(native(link).c_str()));
        }
    }

    void treatsClockFailureAsNonfatal() {
        QTemporaryDir root;
        QtWindowsDiagnosticLogStoreOptions settings;
        settings.clock = []() -> std::int64_t { throw std::runtime_error("synthetic clock failure"); };
        QtWindowsDiagnosticLogStore store(root.path(), settings);
        QVERIFY(!store.append(record()));
        QVERIFY(!QDir(root.filePath(QStringLiteral("diagnostics"))).exists());
        settings.clock = [] { return std::int64_t{-1}; };
        QtWindowsDiagnosticLogStore invalid(root.path(), settings);
        QVERIFY(!invalid.append(record()));
        QVERIFY(!QDir(root.filePath(QStringLiteral("diagnostics"))).exists());
    }
};

} // namespace music_player::m0

QTEST_GUILESS_MAIN(music_player::m0::QtWindowsDiagnosticLogStoreTests)
#include "QtWindowsDiagnosticLogStoreTests.moc"
