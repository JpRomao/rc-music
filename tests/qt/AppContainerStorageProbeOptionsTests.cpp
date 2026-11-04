#include "AppContainerStorageProbeOptions.hpp"

#include <QTest>

using Options = music_player::sandbox_probe::AppContainerStorageProbeOptions;
using music_player::sandbox_probe::appContainerStorageProbeOptions;
namespace {
const QString workspace = QStringLiteral("C:/RC Music 日本");
const QString prefix = workspace + QStringLiteral("/out/product/");
QStringList inventory(const QString& evidence) {
    return {QStringLiteral("--allow-profile-changes"), QStringLiteral("--evidence-dir"), evidence};
}
} // namespace
class AppContainerStorageProbeOptionsTests final : public QObject {
    Q_OBJECT
  private slots:
    void onlyExactInertModesAreAccepted_data() {
        QTest::addColumn<QStringList>("arguments");
        QTest::addColumn<int>("mode");
        QTest::newRow("help") << QStringList{QStringLiteral("--help")} << int(Options::Mode::Help);
        QTest::newRow("self-test") << QStringList{QStringLiteral("--self-test")} << int(Options::Mode::SelfTest);
        QTest::newRow("missing") << QStringList{} << int(Options::Mode::Refused);
        QTest::newRow("extra-after-self-test")
            << QStringList{QStringLiteral("--self-test"), QStringLiteral("x")} << int(Options::Mode::Refused);
        QTest::newRow("no-readonly-escape") << QStringList{QStringLiteral("--readonly")} << int(Options::Mode::Refused);
        QTest::newRow("no-wfp-mode") << QStringList{QStringLiteral("--allow-wfp-subscription")}
                                     << int(Options::Mode::Refused);
        QTest::newRow("no-observer") << QStringList{QStringLiteral("--external-classify-observer")}
                                     << int(Options::Mode::Refused);
    }
    void onlyExactInertModesAreAccepted() {
        QFETCH(QStringList, arguments);
        QFETCH(int, mode);
        const auto result = appContainerStorageProbeOptions(arguments, workspace);
        QCOMPARE(result.mode, static_cast<Options::Mode>(mode));
        QVERIFY(result.evidence.isEmpty());
    }
    void explicitProfileModeKeepsOnlyAnOwnedNormalizedEvidencePath() {
        QString borrowed = prefix + QStringLiteral("test-runs/probe 日本");
        auto arguments = inventory(QString::fromRawData(borrowed.constData(), borrowed.size()));
        const auto result = appContainerStorageProbeOptions(arguments, workspace);
        QCOMPARE(result.mode, Options::Mode::Inventory);
        const auto expected = QString(borrowed.constData(), borrowed.size());
        borrowed.fill('x');
        QCOMPARE(result.evidence, expected);
        const auto windows = appContainerStorageProbeOptions(inventory(QString(expected).replace('/', '\\')),
                                                             QString(workspace).replace('/', '\\'));
        QCOMPARE(windows.mode, Options::Mode::Inventory);
        QCOMPARE(windows.evidence, expected);
    }
    void exactCompiledAnchorSpellingIsRequired_data() {
        QTest::addColumn<QString>("path");
        QTest::newRow("different-case-product") << workspace + QStringLiteral("/out/PRODUCT/run");
        QTest::newRow("different-case-workspace") << QStringLiteral("C:/RC MUSIC 日本/out/product/run");
        QTest::newRow("prefix-collision") << workspace + QStringLiteral("/out/product-other/run");
        QTest::newRow("workspace-collision") << workspace + QStringLiteral("-other/out/product/run");
        QTest::newRow("outside") << QStringLiteral("C:/other/out/product/run");
        QTest::newRow("relative") << QStringLiteral("out/product/run");
        QTest::newRow("unc") << QStringLiteral("//server/share/out/product/run");
        QTest::newRow("device") << QStringLiteral("//?/C:/RC Music 日本/out/product/run");
        QTest::newRow("base-itself") << workspace + QStringLiteral("/out/product");
    }
    void exactCompiledAnchorSpellingIsRequired() {
        QFETCH(QString, path);
        const auto result = appContainerStorageProbeOptions(inventory(path), workspace);
        QCOMPARE(result.mode, Options::Mode::Refused);
        QVERIFY(result.evidence.isEmpty());
    }
    void ambiguousComponentsAreRefused_data() {
        QTest::addColumn<QString>("component");
        unsigned index = 0;
        for (const auto& name : {QString{},
                                 QStringLiteral("."),
                                 QStringLiteral(".."),
                                 QStringLiteral("a//b"),
                                 QStringLiteral("a/"),
                                 QStringLiteral("a."),
                                 QStringLiteral("a "),
                                 QStringLiteral("NUL"),
                                 QStringLiteral("con.txt"),
                                 QStringLiteral("prn"),
                                 QStringLiteral("aux"),
                                 QStringLiteral("COM1.bin"),
                                 QStringLiteral("LPT9"),
                                 QStringLiteral("COM¹"),
                                 QStringLiteral("LPT².txt"),
                                 QStringLiteral("conin$"),
                                 QStringLiteral("conout$"),
                                 QStringLiteral("a:b"),
                                 QStringLiteral("a?b"),
                                 QStringLiteral("a<b"),
                                 QStringLiteral("a>b"),
                                 QStringLiteral("a|b"),
                                 QStringLiteral("a\"b"),
                                 QStringLiteral("a*b"),
                                 QString(QChar{0xd800}),
                                 QStringLiteral("nul") + QChar{0},
                                 QStringLiteral("control") + QChar{1},
                                 QString(256, 'x')})
            QTest::newRow(qPrintable(QStringLiteral("component-%1").arg(index++))) << name;
    }
    void ambiguousComponentsAreRefused() {
        QFETCH(QString, component);
        const auto result = appContainerStorageProbeOptions(inventory(prefix + component), workspace);
        QCOMPARE(result.mode, Options::Mode::Refused);
        QVERIFY(result.evidence.isEmpty());
    }
    void boundsAreInclusiveAndApplyToTheCompletePath() {
        QString path = prefix;
        while (path.size() + 256 < Options::maximum_path_units)
            path += QString(255, 'x') + '/';
        path += QString(Options::maximum_path_units - path.size(), 'z');
        QCOMPARE(path.size(), Options::maximum_path_units);
        QCOMPARE(appContainerStorageProbeOptions(inventory(path), workspace).mode, Options::Mode::Inventory);
        QCOMPARE(appContainerStorageProbeOptions(inventory(path + 'z'), workspace).mode, Options::Mode::Refused);
    }
    void flagOrderAndCountCannotEnableOtherWork_data() {
        QTest::addColumn<QStringList>("arguments");
        const auto valid = inventory(prefix + QStringLiteral("run"));
        QTest::newRow("evidence-only") << valid.mid(1);
        QTest::newRow("flag-only") << QStringList{valid[0]};
        QTest::newRow("reordered") << QStringList{valid[1], valid[2], valid[0]};
        QTest::newRow("extra-wfp") << (valid + QStringList{QStringLiteral("--external-classify-observer")});
        QTest::newRow("profile-name-override") << (valid + QStringList{QStringLiteral("RCMusic.Media.old")});
        QTest::newRow("help-is-not-prefix") << (QStringList{QStringLiteral("--help")} + valid);
    }
    void flagOrderAndCountCannotEnableOtherWork() {
        QFETCH(QStringList, arguments);
        QCOMPARE(appContainerStorageProbeOptions(arguments, workspace).mode, Options::Mode::Refused);
    }
};
QTEST_GUILESS_MAIN(AppContainerStorageProbeOptionsTests)
#include "AppContainerStorageProbeOptionsTests.moc"
