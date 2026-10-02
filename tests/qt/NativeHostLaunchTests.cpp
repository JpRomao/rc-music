#include "adapters/NativeHostLaunch.hpp"

#include <QTest>

#include <limits>

using namespace music_player::m0;

namespace {

const QString allowed_origin = QStringLiteral("chrome-extension://abcdefghijklmnopabcdefghijklmnop/");

NativeHostConfiguration configuration() { return {allowed_origin, QStringLiteral("RCMusic_downloads-1")}; }

} // namespace

class NativeHostLaunchTests final : public QObject {
    Q_OBJECT

  private slots:
    void acceptsTrustedBoundaryConfiguration() {
        QVERIFY(isValidNativeHostConfiguration(configuration()));
        auto config = configuration();
        config.endpoint_name = QString(96, QLatin1Char('a'));
        QVERIFY(isValidNativeHostConfiguration(config));
        config.endpoint_name = QStringLiteral("x");
        QVERIFY(isValidNativeHostConfiguration(config));
        config.allowed_origin =
            QStringLiteral("chrome-extension://") + QString(32, QLatin1Char('p')) + QLatin1Char('/');
        QVERIFY(isValidNativeHostConfiguration(config));
    }

    void refusesInvalidConfiguration_data() {
        QTest::addColumn<QString>("origin");
        QTest::addColumn<QString>("endpoint");
        QTest::newRow("empty origin") << QString{} << QStringLiteral("endpoint");
        QTest::newRow("empty endpoint") << allowed_origin << QString{};
        QTest::newRow("wildcard origin") << QStringLiteral("chrome-extension://*/") << QStringLiteral("endpoint");
        QTest::newRow("short identity") << QStringLiteral("chrome-extension://") + QString(31, QLatin1Char('a')) +
                                               QLatin1Char('/')
                                        << QStringLiteral("endpoint");
        QTest::newRow("invalid identity letter")
            << QStringLiteral("chrome-extension://") + QString(31, QLatin1Char('a')) + QStringLiteral("q/")
            << QStringLiteral("endpoint");
        QTest::newRow("uppercase identity") << allowed_origin.toUpper() << QStringLiteral("endpoint");
        QTest::newRow("missing slash") << allowed_origin.chopped(1) << QStringLiteral("endpoint");
        QTest::newRow("extra path") << allowed_origin + QStringLiteral("path") << QStringLiteral("endpoint");
        QTest::newRow("query") << allowed_origin + QStringLiteral("?x=1") << QStringLiteral("endpoint");
        QTest::newRow("origin NUL") << allowed_origin + QChar{0} << QStringLiteral("endpoint");
        QTest::newRow("endpoint too long") << allowed_origin << QString(97, QLatin1Char('a'));
        QTest::newRow("endpoint path") << allowed_origin << QStringLiteral("C:/endpoint");
        QTest::newRow("endpoint pipe path") << allowed_origin << QStringLiteral("\\\\.\\pipe\\endpoint");
        QTest::newRow("endpoint dot") << allowed_origin << QStringLiteral("endpoint.v1");
        QTest::newRow("endpoint whitespace") << allowed_origin << QStringLiteral("endpoint ");
        QTest::newRow("endpoint Unicode") << allowed_origin << QStringLiteral("áudio");
        QTest::newRow("endpoint NUL") << allowed_origin << QStringLiteral("endpoint") + QChar{0};
    }

    void refusesInvalidConfiguration() {
        QFETCH(QString, origin);
        QFETCH(QString, endpoint);
        const NativeHostConfiguration config{origin, endpoint};
        QVERIFY(!isValidNativeHostConfiguration(config));
        const auto result = parseNativeHostLaunch({QStringLiteral("host.exe"), allowed_origin}, config);
        QCOMPARE(result.error, NativeHostLaunchError::InvalidConfiguration);
        QVERIFY(result.caller_origin.isEmpty());
        QVERIFY(!result.parent_window);
    }

    void missingTrustedConfigurationCannotAdoptCaller() {
        const auto result = parseNativeHostLaunch({QStringLiteral("host.exe"), allowed_origin}, {});
        QCOMPARE(result.error, NativeHostLaunchError::Disabled);
        QVERIFY(result.caller_origin.isEmpty());
        QVERIFY(!result.parent_window);
    }

    void acceptsExactCallerWithoutWindow() {
        const auto result =
            parseNativeHostLaunch({QStringLiteral("C:/RC Music/host.exe"), allowed_origin}, configuration());
        QCOMPARE(result.error, NativeHostLaunchError::None);
        QCOMPARE(result.caller_origin, allowed_origin);
        QVERIFY(!result.parent_window);
    }

    void acceptsCanonicalParentWindow_data() {
        QTest::addColumn<QString>("decimal");
        QTest::addColumn<quint64>("expected");
        QTest::newRow("service worker zero") << QStringLiteral("0") << quint64{0};
        QTest::newRow("ordinary handle") << QStringLiteral("12345") << quint64{12345};
        QTest::newRow("uint64 maximum") << QStringLiteral("18446744073709551615")
                                        << std::numeric_limits<quint64>::max();
    }

    void acceptsCanonicalParentWindow() {
        QFETCH(QString, decimal);
        QFETCH(quint64, expected);
        const auto result = parseNativeHostLaunch(
            {QStringLiteral("host.exe"), allowed_origin, QStringLiteral("--parent-window=") + decimal},
            configuration());
        QCOMPARE(result.error, NativeHostLaunchError::None);
        QCOMPARE(result.caller_origin, allowed_origin);
        QVERIFY(result.parent_window);
        QCOMPARE(*result.parent_window, expected);
    }

    void rejectsChangedCallerOrigin_data() {
        QTest::addColumn<QString>("caller");
        QTest::newRow("different extension")
            << QStringLiteral("chrome-extension://") + QString(32, QLatin1Char('a')) + QLatin1Char('/');
        QTest::newRow("extra caller path") << allowed_origin + QStringLiteral("popup.html");
        QTest::newRow("caller query") << allowed_origin + QStringLiteral("?allowed=true");
        QTest::newRow("caller without slash") << allowed_origin.chopped(1);
        QTest::newRow("uppercase caller") << allowed_origin.toUpper();
        QTest::newRow("page origin") << QStringLiteral("https://media.example.test/");
        QTest::newRow("empty caller") << QString{};
    }

    void rejectsChangedCallerOrigin() {
        QFETCH(QString, caller);
        const auto result = parseNativeHostLaunch({QStringLiteral("host.exe"), caller}, configuration());
        QCOMPARE(result.error, NativeHostLaunchError::UnauthorizedOrigin);
        QVERIFY(result.caller_origin.isEmpty());
        QVERIFY(!result.parent_window);
    }

    void rejectsUnknownOrMalformedArguments_data() {
        QTest::addColumn<QStringList>("arguments");
        QTest::newRow("no args") << QStringList{};
        QTest::newRow("no caller") << QStringList{QStringLiteral("host.exe")};
        QTest::newRow("empty executable") << QStringList{QString{}, allowed_origin};
        QTest::newRow("executable NUL") << QStringList{QStringLiteral("host") + QChar{0}, allowed_origin};
        const QStringList flags{QStringLiteral("--config=anything"),
                                QStringLiteral("--allowed-origin=") + allowed_origin,
                                QStringLiteral("--endpoint=endpoint"),
                                QStringLiteral("--parent-window="),
                                QStringLiteral("--parent-window=00"),
                                QStringLiteral("--parent-window=01"),
                                QStringLiteral("--parent-window=+1"),
                                QStringLiteral("--parent-window=-1"),
                                QStringLiteral("--parent-window=1 "),
                                QStringLiteral("--parent-window= 1"),
                                QStringLiteral("--parent-window=1.0"),
                                QStringLiteral("--parent-window=1e0"),
                                QStringLiteral("--parent-window=18446744073709551616"),
                                QStringLiteral("--parent-window=999999999999999999999"),
                                QStringLiteral("--parent-window=١"),
                                QStringLiteral("--parent-window=1") + QChar{0}};
        for (qsizetype index = 0; index < flags.size(); ++index)
            QTest::newRow(qPrintable(QStringLiteral("flag-%1").arg(index)))
                << QStringList{QStringLiteral("host.exe"), allowed_origin, flags[index]};
        QTest::newRow("duplicate window")
            << QStringList{QStringLiteral("host.exe"), allowed_origin, QStringLiteral("--parent-window=1"),
                           QStringLiteral("--parent-window=2")};
        QTest::newRow("trailing arg") << QStringList{QStringLiteral("host.exe"), allowed_origin,
                                                     QStringLiteral("--parent-window=1"), QStringLiteral("extra")};
    }

    void rejectsUnknownOrMalformedArguments() {
        QFETCH(QStringList, arguments);
        const auto result = parseNativeHostLaunch(arguments, configuration());
        QCOMPARE(result.error, NativeHostLaunchError::InvalidArguments);
        QVERIFY(result.caller_origin.isEmpty());
        QVERIFY(!result.parent_window);
    }
};

QTEST_GUILESS_MAIN(NativeHostLaunchTests)

#include "NativeHostLaunchTests.moc"
