#include "NativeHostBuildConfiguration.hpp"
#include "adapters/NativeDownloadCodec.hpp"
#include "adapters/QtJsonWindowsSessionStore.hpp"
#include "adapters/WindowsDownloadArtifactPins.hpp"
#include "adapters/WindowsDownloadCheckpointStore.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QTest>
#include <QtEndian>

#include <Sddl.h>

#include <vector>

using namespace music_player::m0;
namespace {
QString shell_executable;
QString host_executable;
QString mp3_fixture;
constexpr std::string_view request_id = "11111111-1111-4111-8111-111111111111";

bool createPrivateDirectory(const QString& directory) {
    HANDLE raw_token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &raw_token))
        return false;
    const download_artifacts::Handle token{raw_token};
    DWORD bytes = 0;
    static_cast<void>(GetTokenInformation(token.get(), TokenUser, nullptr, 0, &bytes));
    std::vector<unsigned char> user(bytes);
    if (user.empty() || !GetTokenInformation(token.get(), TokenUser, user.data(), bytes, &bytes))
        return false;
    LPWSTR sid = nullptr;
    if (!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(user.data())->User.Sid, &sid))
        return false;
    const auto release_sid = qScopeGuard([&] { LocalFree(sid); });
    const QString sddl = QStringLiteral("D:P(A;;FA;;;SY)(A;;FA;;;") + QString::fromWCharArray(sid) + QLatin1Char(')');
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.toStdWString().c_str(), SDDL_REVISION_1, &descriptor,
                                                              nullptr))
        return false;
    const auto release_descriptor = qScopeGuard([&] { LocalFree(descriptor); });
    SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES), descriptor, FALSE};
    return CreateDirectoryW(download_artifacts::nativePath(directory).c_str(), &attributes) != FALSE;
}

class ChildGuard final {
  public:
    explicit ChildGuard(QProcess& child) : child_(child) {}
    ~ChildGuard() {
        if (child_.state() != QProcess::NotRunning) {
            child_.kill();
            static_cast<void>(child_.waitForFinished(5'000));
        }
    }

  private:
    QProcess& child_;
};

QByteArray requestFrame() {
    const QByteArray payload =
        R"({"items":[{"kind":"audio","title":"Áudio sintético","url":"https://fixture.invalid/synthetic.mp3"}],"requestId":"11111111-1111-4111-8111-111111111111","schemaVersion":1,"type":"download.request"})";
    QByteArray length(4, Qt::Uninitialized);
    qToLittleEndian<quint32>(static_cast<quint32>(payload.size()), length.data());
    return length + payload;
}
} // namespace

class NativeDownloadsCompositionTests final : public QObject {
    Q_OBJECT
  private slots:
    void realHostOwnerUiAndDurableReceiptsSurviveReplayAndRestart() {
        QTemporaryDir root{QDir::tempPath() + QStringLiteral("/rc-music-native-espaço-音-XXXXXX")};
        QVERIFY(root.isValid());
        const auto private_root = root.filePath(QStringLiteral("downloads"));
        QVERIFY(createPrivateDirectory(private_root));
        QVERIFY(QFile::copy(mp3_fixture, root.filePath(QStringLiteral("synthetic-input.mp3"))));
        const auto exit_marker = root.filePath(QStringLiteral("m0-native-downloads-exit"));
        std::vector<std::string> original_job_ids;
        for (int run = 0; run < 2; ++run) {
            if (run != 0)
                QVERIFY(QFile::remove(exit_marker));
            QProcess app;
            ChildGuard app_guard{app};
            app.setProcessChannelMode(QProcess::MergedChannels);
            QByteArray app_output;
            bool output_overflow = false;
            const auto collect_output = [&] {
                constexpr qsizetype limit = 64 * 1024;
                if (app_output.size() <= limit)
                    app_output += app.read(limit + 1 - app_output.size());
                output_overflow = output_overflow || app_output.size() > limit || app.bytesAvailable() > 0;
            };
            QObject::connect(&app, &QProcess::readyReadStandardOutput, &app, collect_output);
            app.start(shell_executable, {QStringLiteral("--m0-smoke=native-downloads"),
                                         QStringLiteral("--m0-test-config-root=") + root.path()});
            QVERIFY(app.waitForStarted(5'000));
            QTRY_VERIFY_WITH_TIMEOUT(app_output.contains("M0_NATIVE_DOWNLOAD_READY") ||
                                         app.state() == QProcess::NotRunning || output_overflow,
                                     10'000);
            QVERIFY2(app_output.contains("M0_NATIVE_DOWNLOAD_READY"), app_output.constData());
            QVERIFY(!output_overflow);
            for (int replay = 0; replay < 2; ++replay) {
                QProcess host;
                ChildGuard host_guard{host};
                host.setProcessChannelMode(QProcess::SeparateChannels);
                host.start(host_executable,
                           {nativeHostBuildConfiguration().allowed_origin, QStringLiteral("--parent-window=0")});
                QVERIFY(host.waitForStarted(3'000));
                QCOMPARE(host.write(requestFrame()), requestFrame().size());
                // Stdin stays open throughout Pending. The driver is neither
                // the owner nor a fake submission port, only a browser peer.
                QVERIFY(host.waitForFinished(10'000));
                QCOMPARE(host.exitStatus(), QProcess::NormalExit);
                QCOMPARE(host.exitCode(), 0);
                const auto wire = host.readAllStandardOutput();
                QVERIFY(wire.size() >= 5 && wire.size() <= 4100);
                QCOMPARE(qFromLittleEndian<quint32>(wire.constData()), static_cast<quint32>(wire.size() - 4));
                const auto response = decodeNativeDownloadResponse(wire.sliced(4), request_id, 1);
                QVERIFY(response);
                QCOMPARE(response->error, DownloadSubmissionError::None);
                QCOMPARE(response->job_ids.size(), std::size_t{1});
                if (run == 0 && replay == 0)
                    original_job_ids = response->job_ids;
                QCOMPARE(response->job_ids, original_job_ids);
                QVERIFY(host.readAllStandardError().isEmpty());
                // An independent store must already contain the accepted IDs
                // when the browser receives ACK, even on idempotent replay.
                WindowsDownloadCheckpointStore store{private_root};
                const auto checkpoint = store.load();
                QCOMPARE(checkpoint.status, DownloadCheckpointLoadStatus::Loaded);
                QVERIFY(checkpoint.checkpoint);
                QCOMPARE(checkpoint.checkpoint->jobs.size(), std::size_t{1});
                QCOMPARE(checkpoint.checkpoint->receipts.size(), std::size_t{1});
                QCOMPARE(checkpoint.checkpoint->jobs.front().job_id, original_job_ids.front());
                QCOMPARE(checkpoint.checkpoint->receipts.front().request_id, std::string{request_id});
            }
            const QByteArray expected =
                run == 0 ? "M0_DOWNLOAD_EXPLICIT_PLAYBACK" : "M0_DOWNLOAD_RESTART_NO_DUPLICATES";
            QTRY_VERIFY_WITH_TIMEOUT(
                app_output.contains(expected) || app.state() == QProcess::NotRunning || output_overflow, 10'000);
            QVERIFY2(app_output.contains(expected), app_output.constData());
            // A closed, instrumented-only lifecycle marker, never a wire
            // command, requests exit after both real host replies have ended.
            QFile exit_signal{exit_marker};
            QVERIFY(exit_signal.open(QIODevice::WriteOnly | QIODevice::NewOnly));
            exit_signal.close();
            QVERIFY2(app.waitForFinished(10'000), app_output.constData());
            collect_output();
            QVERIFY(!output_overflow);
            QCOMPARE(app.exitStatus(), QProcess::NormalExit);
            QVERIFY2(app.exitCode() == 0, app_output.constData());
            QVERIFY2(app_output.contains("M0_DOWNLOAD_SAFE_EXIT"), app_output.constData());
            QVERIFY(!app_output.contains("fixture.invalid"));
            QVERIFY(!app_output.contains("synthetic-input.mp3"));
            QVERIFY(!app_output.contains("M0_DOWNLOAD_COMPOSITION_FAILED"));
            if (run == 0) {
                QVERIFY(app_output.contains("M0_DOWNLOAD_IMPORTED_WITHOUT_AUTOPLAY"));
                QVERIFY(app_output.contains("M0_NATIVE_DURABLE_BEFORE_EXECUTION"));
            } else {
                QVERIFY(!app_output.contains("M0_DOWNLOAD_EXPLICIT_PLAYBACK"));
                QVERIFY(!app_output.contains("M0_NATIVE_DURABLE_BEFORE_EXECUTION"));
            }
            QtJsonWindowsSessionStore session_store{root.path()};
            const auto session = session_store.load();
            QCOMPARE(session.status, SessionLoadStatus::Loaded);
            QCOMPARE(session.snapshot.download_import_receipts.size(), std::size_t{1});
            QCOMPARE(session.snapshot.queue.items.size(), run == 0 ? std::size_t{1} : std::size_t{0});
            QCOMPARE(session.snapshot.download_import_receipts.front().job_id, original_job_ids.front());
            WindowsDownloadCheckpointStore checkpoint_store{private_root};
            const auto checkpoint = checkpoint_store.load();
            QCOMPARE(checkpoint.status, DownloadCheckpointLoadStatus::Loaded);
            QVERIFY(checkpoint.checkpoint);
            QCOMPARE(checkpoint.checkpoint->jobs.size(), std::size_t{1});
            QCOMPARE(checkpoint.checkpoint->receipts.size(), std::size_t{1});
            QCOMPARE(checkpoint.checkpoint->jobs.front().state,
                     run == 0 ? DownloadJobState::Completed : DownloadJobState::Imported);
        }
    }
};

int main(int argc, char** argv) {
    QCoreApplication application{argc, argv};
    if (application.arguments().size() != 4)
        return 1;
    shell_executable = application.arguments().at(1);
    host_executable = application.arguments().at(2);
    mp3_fixture = application.arguments().at(3);
    NativeDownloadsCompositionTests tests;
    return QTest::qExec(&tests);
}

#include "NativeDownloadsCompositionTests.moc"
