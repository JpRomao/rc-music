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

#include <Sddl.h>

#include <vector>

using namespace music_player::m0;
namespace {
QString shell_executable;
QString mp3_fixture;

// Create only a fresh child of this test's TEMP root. Never change an existing
// directory's ACL or install/register anything on the host.
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
    explicit ChildGuard(QProcess& process) : process_(process) {}
    ~ChildGuard() {
        if (process_.state() != QProcess::NotRunning) {
            process_.kill();
            static_cast<void>(process_.waitForFinished(5'000));
        }
    }

  private:
    QProcess& process_;
};
} // namespace

class DownloadsCompositionTests final : public QObject {
    Q_OBJECT
  private slots:
    void importedQueueAndCheckpointSurviveRestartWithoutAutoplayOrDuplication() {
        QTemporaryDir root{QDir::tempPath() + QStringLiteral("/rc-music-downloads-espaço-音-XXXXXX")};
        QVERIFY(root.isValid());
        const QString private_root = root.filePath(QStringLiteral("downloads"));
        QVERIFY(createPrivateDirectory(private_root));
        QVERIFY(QFile::copy(mp3_fixture, root.filePath(QStringLiteral("synthetic-input.mp3"))));

        QByteArray first_output;
        QByteArray second_output;
        for (int run = 0; run < 2; ++run) {
            QProcess child;
            ChildGuard guard{child};
            child.setProcessChannelMode(QProcess::MergedChannels);
            child.start(shell_executable, {QStringLiteral("--m0-smoke=downloads"),
                                           QStringLiteral("--m0-test-config-root=") + root.path()});
            QVERIFY(child.waitForStarted(5'000));
            QVERIFY2(child.waitForFinished(15'000), "The isolated application failed to drain and exit");
            const auto output = child.readAll();
            QVERIFY2(child.exitStatus() == QProcess::NormalExit, output.constData());
            QVERIFY2(child.exitCode() == 0, output.constData());
            QVERIFY2(output.contains("M0_DOWNLOAD_SAFE_EXIT"), output.constData());
            QVERIFY(!output.contains("fixture.invalid"));
            QVERIFY(!output.contains("synthetic-input.mp3"));
            if (run == 0) {
                first_output = output;
                QVERIFY2(output.contains("M0_DOWNLOAD_IMPORTED_WITHOUT_AUTOPLAY"), output.constData());
                QVERIFY2(output.contains("M0_DOWNLOAD_EXPLICIT_PLAYBACK"), output.constData());
            } else {
                second_output = output;
                QVERIFY2(output.contains("M0_DOWNLOAD_RESTART_NO_DUPLICATES"), output.constData());
                QVERIFY(!output.contains("M0_DOWNLOAD_EXPLICIT_PLAYBACK"));
            }

            // Independent new readers, not an in-memory model or uncertain
            // after-commit readback promoted to an admission acknowledgement.
            QtJsonWindowsSessionStore session_store{root.path()};
            const auto session = session_store.load();
            QCOMPARE(session.status, SessionLoadStatus::Loaded);
            QCOMPARE(session.snapshot.download_import_receipts.size(), std::size_t{1});
            QCOMPARE(session.snapshot.queue.items.size(), run == 0 ? std::size_t{1} : std::size_t{0});
            WindowsDownloadCheckpointStore checkpoint_store{private_root};
            const auto checkpoint = checkpoint_store.load();
            QCOMPARE(checkpoint.status, DownloadCheckpointLoadStatus::Loaded);
            QVERIFY(checkpoint.checkpoint.has_value());
            QCOMPARE(checkpoint.checkpoint->jobs.size(), std::size_t{1});
            QCOMPARE(checkpoint.checkpoint->receipts.size(), std::size_t{1});
            QCOMPARE(checkpoint.checkpoint->jobs.front().job_id,
                     session.snapshot.download_import_receipts.front().job_id);
            QCOMPARE(checkpoint.checkpoint->jobs.front().state,
                     run == 0 ? DownloadJobState::Completed : DownloadJobState::Imported);
        }
        QVERIFY(!first_output.isEmpty() && !second_output.isEmpty());
    }
};

int main(int argc, char** argv) {
    QCoreApplication application{argc, argv};
    if (application.arguments().size() != 3)
        return 1;
    shell_executable = application.arguments().at(1);
    mp3_fixture = application.arguments().at(2);
    DownloadsCompositionTests tests;
    return QTest::qExec(&tests);
}

#include "DownloadsCompositionTests.moc"
