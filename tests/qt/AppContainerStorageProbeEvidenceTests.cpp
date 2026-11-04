#include "AppContainerStorageProbeEvidence.hpp"
#include "adapters/WindowsDownloadMediaProfileStore.hpp"
#include "adapters/WindowsMediaProfileOwner.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <array>
#include <memory>
#include <utility>

using Evidence = music_player::sandbox_probe::AppContainerStorageProbeEvidence;
using namespace music_player::m0;
namespace {
const QByteArray payload{"{\"own_fixture\":true}\n"};
constexpr std::array leaves{Evidence::Leaf::Before, Evidence::Leaf::Profile, Evidence::Leaf::Inventory,
                            Evidence::Leaf::Close,  Evidence::Leaf::Cleanup, Evidence::Leaf::Result};
struct Fixture final {
    QTemporaryDir temp;
    QString workspace{temp.filePath(QStringLiteral("workspace espaço 音"))};
    QString product{workspace + QStringLiteral("/out/product")};
    QString evidence{product + QStringLiteral("/inventory-own-run")};
    bool initialize() const { return temp.isValid() && QDir{}.mkpath(product); }
};
QByteArray read(const QString& path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}
bool writeFixture(const QString& path, const QByteArray& bytes) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::NewOnly) && file.write(bytes) == bytes.size() && file.flush();
}
struct IoState final {
    int writes{}, flushes{}, closes{};
    bool short_write{}, fail_write{}, fail_flush{}, fail_close{};
};
Evidence::Calls calls(const std::shared_ptr<IoState>& state) {
    return {.write =
                [state](const Evidence::Handle raw, const QByteArrayView bytes) {
                    ++state->writes;
                    if (state->fail_write)
                        return Evidence::WriteResult{ERROR_WRITE_FAULT, 0};
                    const auto amount = static_cast<DWORD>(state->short_write ? bytes.size() / 2 : bytes.size());
                    DWORD used = 0;
                    const auto ok = WriteFile(reinterpret_cast<HANDLE>(raw), bytes.data(), amount, &used, nullptr);
                    return Evidence::WriteResult{ok ? ERROR_SUCCESS : GetLastError(), used};
                },
            .flush =
                [state](const Evidence::Handle raw) {
                    ++state->flushes;
                    if (state->fail_flush)
                        return DWORD{ERROR_WRITE_FAULT};
                    return FlushFileBuffers(reinterpret_cast<HANDLE>(raw)) ? DWORD{ERROR_SUCCESS} : GetLastError();
                },
            .close =
                [state](const Evidence::Handle raw) {
                    ++state->closes;
                    // Always physically close this own fixture handle. The injected
                    // error models an unconfirmed result without leaking a real FD.
                    if (!CloseHandle(reinterpret_cast<HANDLE>(raw)))
                        return GetLastError();
                    return state->fail_close ? DWORD{ERROR_ACCESS_DENIED} : DWORD{ERROR_SUCCESS};
                }};
}
struct FakePlatform final : WindowsMediaProfilePlatform {
    int creates{}, removes{};
    WindowsMediaProfileCreateResult create(const QString&) override {
        ++creates;
        QByteArray sid(40, '\0');
        sid[0] = 1;
        sid[1] = 8;
        sid[7] = 15;
        sid[8] = 2;
        sid[12] = 7;
        return {WindowsMediaProfileCreateStatus::Created, sid};
    }
    WindowsMediaProfileRemovalResult remove(const QString&, const QByteArray&) override {
        ++removes;
        return {true, WindowsMediaProfileAbsence::Absent};
    }
};
} // namespace

class AppContainerStorageProbeEvidenceTests final : public QObject {
    Q_OBJECT
  private slots:
    void constructionDestructionAndPathGettersDoNotCreateAnything() {
        Fixture f;
        QVERIFY(f.initialize());
        const auto state = std::make_shared<IoState>();
        {
            const Evidence evidence(f.workspace, f.evidence, calls(state));
            QCOMPARE(evidence.directory(), f.evidence);
            QCOMPARE(evidence.journalDirectory(), f.evidence + QStringLiteral("/profile-journal"));
            QCOMPARE(evidence.path(Evidence::Leaf::Before), f.evidence + QStringLiteral("/00-before-profile.json"));
            QVERIFY(evidence.path(static_cast<Evidence::Leaf>(99)).isEmpty());
            QVERIFY(!QFileInfo::exists(f.evidence));
        }
        QVERIFY(!QFileInfo::exists(f.evidence));
        QCOMPARE(state->writes, 0);
        QCOMPARE(state->flushes, 0);
        QCOMPARE(state->closes, 0);
    }
    void nativeWriterBindingHasAllCallbacksWithoutOpeningAFile() {
        Fixture f;
        QVERIFY(f.initialize());
        const auto native = Evidence::systemCalls();
        QVERIFY(native.write && native.flush && native.close);
        QVERIFY(!QFileInfo::exists(f.evidence));
    }
    void borrowedWorkspaceAndEvidenceStringsAreOwnedBeforeTheCallerChangesThem() {
        Fixture f;
        QVERIFY(f.initialize());
        QString workspace(f.workspace.constData(), f.workspace.size());
        QString path(f.evidence.constData(), f.evidence.size());
        Evidence evidence(QString::fromRawData(workspace.constData(), workspace.size()),
                          QString::fromRawData(path.constData(), path.size()));
        workspace.fill('x');
        path.fill('y');
        QCOMPARE(evidence.directory(), f.evidence);
        QCOMPARE(evidence.journalDirectory(), f.evidence + QStringLiteral("/profile-journal"));
        QVERIFY(evidence.create().ok);
        QVERIFY(QFileInfo::exists(f.evidence));
    }
    void createPinsAFreshEvidenceLeafAndRefusesAnotherCreationAttempt() {
        Fixture f;
        QVERIFY(f.initialize());
        Evidence evidence(f.workspace, f.evidence);
        const auto created = evidence.create();
        QVERIFY(created.ok);
        QCOMPARE(created.error, std::uint32_t{ERROR_SUCCESS});
        QCOMPARE(created.phase, Evidence::Phase::Complete);
        QVERIFY(QFileInfo(f.evidence).isDir());
        QVERIFY(!evidence.create().ok);
        QVERIFY(QDir(f.evidence).isEmpty());
    }
    void anExistingEvidenceDirectoryAndItsContentsAreNeverAdoptedOrChanged() {
        Fixture f;
        QVERIFY(f.initialize());
        QVERIFY(QDir{}.mkdir(f.evidence));
        const auto sentinel = f.evidence + QStringLiteral("/sentinel.bin");
        const QByteArray old{"existing own fixture"};
        QVERIFY(writeFixture(sentinel, old));
        Evidence evidence(f.workspace, f.evidence);
        const auto result = evidence.create();
        QVERIFY(!result.ok);
        QCOMPARE(result.error, std::uint32_t{ERROR_ALREADY_EXISTS});
        QVERIFY(!evidence.createJournal().ok);
        QVERIFY(!evidence.writeNew(Evidence::Leaf::Before, payload).ok);
        QCOMPARE(read(sentinel), old);
        QCOMPARE(QDir(f.evidence).entryList(QDir::Files), QStringList{QStringLiteral("sentinel.bin")});
    }
    void unsafeOrDifferentlySpelledRootsAreRefused_data() {
        QTest::addColumn<QString>("suffix");
        for (const auto* value : {"outside", "PRODUCT", "dot-dot", "relative", "bare-product", "drive-relative"})
            QTest::newRow(value) << QString::fromLatin1(value);
    }
    void unsafeOrDifferentlySpelledRootsAreRefused() {
        QFETCH(QString, suffix);
        Fixture f;
        QVERIFY(f.initialize());
        QString path;
        if (suffix == QStringLiteral("outside"))
            path = f.temp.filePath(QStringLiteral("foreign-own-fixture"));
        else if (suffix == QStringLiteral("PRODUCT"))
            path = f.workspace + QStringLiteral("/out/PRODUCT/refused-run");
        else if (suffix == QStringLiteral("dot-dot"))
            path = f.product + QStringLiteral("/../refused-run");
        else if (suffix == QStringLiteral("relative"))
            path = QStringLiteral("out/product/refused-run");
        else if (suffix == QStringLiteral("drive-relative"))
            path = QStringLiteral("C:refused-run");
        else
            path = f.product;
        Evidence evidence(f.workspace, path);
        QVERIFY(!evidence.create().ok);
        QVERIFY(!evidence.createJournal().ok);
        QVERIFY(!evidence.writeNew(Evidence::Leaf::Before, payload).ok);
        QVERIFY(!QFileInfo::exists(f.evidence));
        QVERIFY(!QFileInfo::exists(f.temp.filePath(QStringLiteral("foreign-own-fixture"))));
        QVERIFY(!QFileInfo::exists(f.product + QStringLiteral("/refused-run")));
        QVERIFY(!QFileInfo::exists(f.workspace + QStringLiteral("/out/refused-run")));
    }
    void journalIsAFreshFixedChildAndItsDaclIsAcceptedByTheRealStoreAndFakeOwner() {
        Fixture f;
        QVERIFY(f.initialize());
        Evidence evidence(f.workspace, f.evidence);
        QVERIFY(evidence.create().ok);
        const auto journal = evidence.createJournal();
        QVERIFY(journal.ok);
        QCOMPARE(journal.error, std::uint32_t{ERROR_SUCCESS});
        QCOMPARE(journal.phase, Evidence::Phase::Complete);
        QVERIFY(QFileInfo(evidence.journalDirectory()).isDir());
        QVERIFY(!evidence.createJournal().ok);
        WindowsDownloadMediaProfileStore store(evidence.journalDirectory());
        FakePlatform platform;
        WindowsMediaProfileOwner owner(
            store, platform, {"evidence-only", [] { return std::string{"12345678-1234-4234-8234-123456789abc"}; }});
        QVERIFY(owner.prepare());
        QCOMPARE(platform.creates, 1);
        const auto active = store.load();
        QVERIFY(active.record);
        QCOMPARE(active.record->stage, DownloadMediaProfileStage::InUse);
        QVERIFY(owner.close());
        QCOMPARE(platform.removes, 1);
        const auto removed = store.load();
        QVERIFY(removed.record);
        QCOMPARE(removed.record->stage, DownloadMediaProfileStage::Removed);
    }
    void anExistingJournalChildIsNotRepairedOrAdopted() {
        Fixture f;
        QVERIFY(f.initialize());
        Evidence evidence(f.workspace, f.evidence);
        QVERIFY(evidence.create().ok);
        QVERIFY(QDir{}.mkdir(evidence.journalDirectory()));
        const auto sentinel = evidence.journalDirectory() + QStringLiteral("/keep.bin");
        QVERIFY(writeFixture(sentinel, payload));
        const auto result = evidence.createJournal();
        QVERIFY(!result.ok);
        QCOMPARE(result.error, std::uint32_t{ERROR_ALREADY_EXISTS});
        QCOMPARE(read(sentinel), payload);
    }
    void fixedReceiptsArePublishedExactlyOnceWithoutRequiringAProfile() {
        Fixture f;
        QVERIFY(f.initialize());
        auto state = std::make_shared<IoState>();
        Evidence evidence(f.workspace, f.evidence, calls(state));
        QVERIFY(evidence.create().ok);
        for (const auto leaf : leaves) {
            const auto result = evidence.writeNew(leaf, payload);
            QVERIFY(result.ok);
            QCOMPARE(result.error, std::uint32_t{ERROR_SUCCESS});
            QCOMPARE(result.close_error, std::uint32_t{ERROR_SUCCESS});
            QCOMPARE(result.phase, Evidence::Phase::Complete);
            QCOMPARE(result.bytes_written, static_cast<std::uint32_t>(payload.size()));
            QCOMPARE(read(evidence.path(leaf)), payload);
            QVERIFY(!evidence.writeNew(leaf, QByteArrayView{"replacement"}).ok);
            QCOMPARE(read(evidence.path(leaf)), payload);
        }
        QCOMPARE(state->writes, 6);
        QCOMPARE(state->flushes, 6);
        QCOMPARE(state->closes, 6);
        QCOMPARE(QDir(f.evidence).entryList(QDir::Files).size(), qsizetype{6});
    }
    void invalidLeafEmptyPayloadAndOversizeAreRefusedBeforeNativeWriterCalls() {
        Fixture f;
        QVERIFY(f.initialize());
        auto state = std::make_shared<IoState>();
        Evidence evidence(f.workspace, f.evidence, calls(state));
        QVERIFY(!evidence.writeNew(Evidence::Leaf::Before, payload).ok);
        QVERIFY(evidence.create().ok);
        QVERIFY(!evidence.writeNew(static_cast<Evidence::Leaf>(99), payload).ok);
        QVERIFY(!evidence.writeNew(Evidence::Leaf::Before, {}).ok);
        const QByteArray large(Evidence::maximum_receipt_bytes + 1, 'x');
        const auto refused = evidence.writeNew(Evidence::Leaf::Before, large);
        QVERIFY(!refused.ok);
        QCOMPARE(refused.phase, Evidence::Phase::Input);
        QCOMPARE(state->writes, 0);
        QCOMPARE(state->flushes, 0);
        QCOMPARE(state->closes, 0);
        QVERIFY(!QFileInfo::exists(evidence.path(Evidence::Leaf::Before)));
        QVERIFY(evidence.writeNew(Evidence::Leaf::Before, payload).ok);
    }
    void inclusiveReceiptLimitIsAcceptedAndClosed() {
        Fixture f;
        QVERIFY(f.initialize());
        const QByteArray maximum(Evidence::maximum_receipt_bytes, 'x');
        Evidence evidence(f.workspace, f.evidence);
        QVERIFY(evidence.create().ok);
        const auto result = evidence.writeNew(Evidence::Leaf::Inventory, maximum);
        QVERIFY(result.ok);
        QCOMPARE(result.bytes_written, static_cast<std::uint32_t>(maximum.size()));
        QCOMPARE(result.close_error, std::uint32_t{ERROR_SUCCESS});
        QCOMPARE(read(evidence.path(Evidence::Leaf::Inventory)), maximum);
    }
    void aReceiptCollisionNeverOpensTheExistingLeafForMutation() {
        Fixture f;
        QVERIFY(f.initialize());
        auto state = std::make_shared<IoState>();
        Evidence evidence(f.workspace, f.evidence, calls(state));
        QVERIFY(evidence.create().ok);
        QVERIFY(writeFixture(evidence.path(Evidence::Leaf::Before), payload));
        const auto result = evidence.writeNew(Evidence::Leaf::Before, QByteArrayView{"replacement"});
        QVERIFY(!result.ok);
        QCOMPARE(result.error, std::uint32_t{ERROR_FILE_EXISTS});
        QCOMPARE(result.phase, Evidence::Phase::Open);
        QCOMPARE(result.close_error, Evidence::not_called);
        QCOMPARE(state->writes, 0);
        QCOMPARE(state->flushes, 0);
        QCOMPARE(state->closes, 0);
        QCOMPARE(read(evidence.path(Evidence::Leaf::Before)), payload);
    }
    void partialWriteWriteErrorFlushErrorAndCloseErrorArePreservedWithoutRetry_data() {
        QTest::addColumn<QString>("failure");
        for (const auto* value : {"partial", "write", "flush", "close"})
            QTest::newRow(value) << QString::fromLatin1(value);
    }
    void partialWriteWriteErrorFlushErrorAndCloseErrorArePreservedWithoutRetry() {
        QFETCH(QString, failure);
        Fixture f;
        QVERIFY(f.initialize());
        auto state = std::make_shared<IoState>();
        state->short_write = failure == QStringLiteral("partial");
        state->fail_write = failure == QStringLiteral("write");
        state->fail_flush = failure == QStringLiteral("flush");
        state->fail_close = failure == QStringLiteral("close");
        {
            Evidence evidence(f.workspace, f.evidence, calls(state));
            QVERIFY(evidence.create().ok);
            const auto result = evidence.writeNew(Evidence::Leaf::Before, payload);
            QVERIFY(!result.ok);
            QCOMPARE(result.phase, state->fail_close   ? Evidence::Phase::Close
                                   : state->fail_flush ? Evidence::Phase::Flush
                                                       : Evidence::Phase::Write);
            QCOMPARE(result.error, state->short_write  ? std::uint32_t{0}
                                   : state->fail_close ? std::uint32_t{ERROR_ACCESS_DENIED}
                                                       : std::uint32_t{ERROR_WRITE_FAULT});
            QCOMPARE(result.close_error,
                     state->fail_close ? std::uint32_t{ERROR_ACCESS_DENIED} : std::uint32_t{ERROR_SUCCESS});
            QCOMPARE(result.bytes_written, state->short_write  ? static_cast<std::uint32_t>(payload.size() / 2)
                                           : state->fail_write ? 0U
                                                               : static_cast<std::uint32_t>(payload.size()));
            QVERIFY(QFileInfo::exists(evidence.path(Evidence::Leaf::Before)));
            QVERIFY(!evidence.writeNew(Evidence::Leaf::Before, payload).ok);
            QCOMPARE(state->writes, 1);
            QCOMPARE(state->flushes, (state->short_write || state->fail_write) ? 0 : 1);
            QCOMPARE(state->closes, 1);
            const auto expected = state->short_write  ? payload.left(payload.size() / 2)
                                  : state->fail_write ? QByteArray{}
                                                      : payload;
            QCOMPARE(read(evidence.path(Evidence::Leaf::Before)), expected);
        }
        QCOMPARE(state->closes, 1); // Destruction cannot retry an uncertain close.
        QVERIFY(QFileInfo::exists(f.evidence + QStringLiteral("/00-before-profile.json")));
    }
    void missingWriterCallbacksAreRefusedBeforeAReceiptCanBeAcquired() {
        Fixture f;
        QVERIFY(f.initialize());
        Evidence evidence(f.workspace, f.evidence, {});
        QVERIFY(evidence.create().ok);
        const auto result = evidence.writeNew(Evidence::Leaf::Before, payload);
        QVERIFY(!result.ok);
        QCOMPARE(result.phase, Evidence::Phase::Input);
        QVERIFY(!QFileInfo::exists(evidence.path(Evidence::Leaf::Before)));
    }
    void evidenceLeafStaysPinnedUntilTheEvidenceObjectIsDestroyed() {
        Fixture f;
        QVERIFY(f.initialize());
        const auto moved = f.product + QStringLiteral("/renamed-own-run");
        {
            Evidence evidence(f.workspace, f.evidence);
            QVERIFY(evidence.create().ok);
            QVERIFY(!QDir{}.rename(f.evidence, moved));
            QVERIFY(QFileInfo(f.evidence).isDir());
        }
        QVERIFY(QDir{}.rename(f.evidence, moved));
        QVERIFY(QFileInfo(moved).isDir());
    }
};
QTEST_GUILESS_MAIN(AppContainerStorageProbeEvidenceTests)
#include "AppContainerStorageProbeEvidenceTests.moc"
