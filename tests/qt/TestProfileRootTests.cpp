#include "ShellLaunchOptions.hpp"
#include "TestProfileRoot.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

#include <stdexcept>

using music_player::m0::experiment::parseShellLaunchOptions;
using music_player::m0::experiment::prepareTestProfileRoot;
using music_player::m0::experiment::takeTestProfileRoot;

namespace {

[[nodiscard]] QString rejectionOf(const QStringList& arguments) {
    try {
        static_cast<void>(takeTestProfileRoot(arguments));
    } catch (const std::invalid_argument& error) {
        return QString::fromUtf8(error.what());
    }
    return {};
}

[[nodiscard]] QString preparationRejectionOf(const QString& root) {
    try {
        static_cast<void>(prepareTestProfileRoot(root));
    } catch (const std::invalid_argument& error) {
        return QString::fromUtf8(error.what());
    }
    return {};
}

} // namespace

class TestProfileRootTests final : public QObject {
    Q_OBJECT

  private slots:
    void absentOptionLeavesEveryArgumentForTheShell() {
        const QStringList arguments{
            QStringLiteral("app"), QStringLiteral("--diagnostics"),  QStringLiteral("C:/music/a.mp3"),
            QStringLiteral("--"),  QStringLiteral("--profile-root"), QStringLiteral("C:/profiles/b")};
        const auto selection = takeTestProfileRoot(arguments);
        QVERIFY(!selection.profile_root.has_value());
        QCOMPARE(selection.remaining_arguments, arguments);
    }

    void optionAndItsValueAreRemovedAndTheRootIsCleaned() {
        const auto selection = takeTestProfileRoot(
            {QStringLiteral("app"), QStringLiteral("C:/music/a.mp3"), QStringLiteral("--profile-root"),
             QStringLiteral("C:\\profiles\\.\\guest\\..\\guest"), QStringLiteral("--diagnostics")});
        QVERIFY(selection.profile_root.has_value());
        QCOMPARE(*selection.profile_root, QStringLiteral("C:/profiles/guest"));
        QCOMPARE(selection.remaining_arguments, (QStringList{QStringLiteral("app"), QStringLiteral("C:/music/a.mp3"),
                                                             QStringLiteral("--diagnostics")}));
    }

    void argumentsAfterTheSourceSeparatorAreNeverTheOption() {
        const QStringList arguments{QStringLiteral("app"), QStringLiteral("--"), QStringLiteral("--profile-root"),
                                    QStringLiteral("C:/profiles/a")};
        QCOMPARE(takeTestProfileRoot(arguments).remaining_arguments, arguments);
    }

    void invalidSelectionsAreRejectedWithTheReason() {
        const auto app = QStringLiteral("app");
        const auto option = QStringLiteral("--profile-root");
        QVERIFY(rejectionOf({app, option}).contains(QStringLiteral("requires a directory")));
        QVERIFY(rejectionOf({app, option, QString{}}).contains(QStringLiteral("requires a directory")));
        QVERIFY(rejectionOf({app, option, QStringLiteral("profiles/a")}).contains(QStringLiteral("absolute")));
        QVERIFY(rejectionOf({app, option, QStringLiteral("C:profiles")}).contains(QStringLiteral("absolute")));
        QVERIFY(rejectionOf({app, option, QStringLiteral("\\profiles")}).contains(QStringLiteral("absolute")));
        QVERIFY(rejectionOf({app, option, QStringLiteral("--diagnostics")}).contains(QStringLiteral("absolute")));
        QVERIFY(rejectionOf({app, option, QStringLiteral("C:/a") + QChar::Null + QStringLiteral("b")})
                    .contains(QStringLiteral("absolute")));
        QVERIFY(rejectionOf({app, option, QStringLiteral("C:/a"), option, QStringLiteral("C:/b")})
                    .contains(QStringLiteral("only once")));
        QVERIFY(
            rejectionOf({app, QStringLiteral("--profile-root=C:/a")}).contains(QStringLiteral("--profile-root <dir>")));
    }

    void preparationCreatesMissingDirectoriesAndKeepsExistingOnes() {
        QTemporaryDir parent;
        QVERIFY(parent.isValid());
        const QString nested = QDir{parent.path()}.filePath(QStringLiteral("perfil convidado/日本"));
        QCOMPARE(prepareTestProfileRoot(nested), QDir::cleanPath(nested));
        QVERIFY(QFileInfo{nested}.isDir());
        QCOMPARE(prepareTestProfileRoot(nested), QDir::cleanPath(nested));
    }

    void preparationRejectsFilesAndUncreatableDirectories() {
        QTemporaryDir parent;
        QVERIFY(parent.isValid());
        const QString file_path = QDir{parent.path()}.filePath(QStringLiteral("not-a-directory"));
        QFile file{file_path};
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.close();
        QVERIFY(preparationRejectionOf(file_path).contains(QStringLiteral("not a directory")));
        QVERIFY(preparationRejectionOf(QDir{file_path}.filePath(QStringLiteral("child")))
                    .contains(QStringLiteral("cannot be created")));
        QVERIFY(preparationRejectionOf(QStringLiteral("relative")).contains(QStringLiteral("absolute")));
    }

    void withoutTheTestBuildTheOptionIsAnOrdinarySourceArgument() {
        // The product never strips --profile-root: the shell sees it as any unknown argument.
        const auto options = parseShellLaunchOptions(
            {QStringLiteral("app"), QStringLiteral("--profile-root"), QStringLiteral("C:/profiles/a")});
        QCOMPARE(options.source_paths,
                 (QStringList{QStringLiteral("--profile-root"), QStringLiteral("C:/profiles/a")}));
        QVERIFY(!options.test_config_root.has_value());
    }
};

QTEST_GUILESS_MAIN(TestProfileRootTests)
#include "TestProfileRootTests.moc"
