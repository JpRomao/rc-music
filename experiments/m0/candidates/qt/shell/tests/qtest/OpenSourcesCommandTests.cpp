#include "OpenSourcesCommand.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

using music_player::m0::decodeOpenSourcesCommand;
using music_player::m0::encodeOpenSourcesCommand;
using music_player::m0::open_sources_max_path_count;
using music_player::m0::open_sources_max_payload_bytes;
using music_player::m0::OpenSourcesCommand;
using music_player::m0::OpenSourcesCommandError;

namespace {

[[nodiscard]] QString absolutePath(const QString& suffix) {
#ifdef Q_OS_WIN
    return QStringLiteral("C:/music/") + suffix;
#else
    return QStringLiteral("/music/") + suffix;
#endif
}

[[nodiscard]] QByteArray jsonPayload(QJsonObject object) {
    return QJsonDocument{std::move(object)}.toJson(QJsonDocument::Compact);
}

} // namespace

class OpenSourcesCommandTests final : public QObject {
    Q_OBJECT

  private slots:
    void roundTripsZeroOneAndOneThousandPaths_data() {
        QTest::addColumn<QStringList>("paths");
        QTest::newRow("activate only") << QStringList{};
        QTest::newRow("one hostile Unicode path")
            << QStringList{absolutePath(QStringLiteral("Música 日本/faixa 'aspas' &; $().flac"))};

        QStringList maximum;
        maximum.reserve(open_sources_max_path_count);
        for (qsizetype index = 0; index < open_sources_max_path_count; ++index)
            maximum.push_back(absolutePath(QStringLiteral("faixa %1.mp3").arg(index)));
        QTest::newRow("one thousand ordered paths") << maximum;
    }

    void roundTripsZeroOneAndOneThousandPaths() {
        QFETCH(QStringList, paths);

        const auto encoded = encodeOpenSourcesCommand(OpenSourcesCommand{paths});
        QCOMPARE(encoded.error, OpenSourcesCommandError::None);
        QVERIFY(encoded.payload.size() <= open_sources_max_payload_bytes);

        const auto decoded = decodeOpenSourcesCommand(encoded.payload);
        QCOMPARE(decoded.error, OpenSourcesCommandError::None);
        QCOMPARE(decoded.command.paths, paths);
    }

    void rejectsOneThousandAndOnePathsBeforeSerialization() {
        QStringList paths;
        paths.reserve(open_sources_max_path_count + 1);
        for (qsizetype index = 0; index <= open_sources_max_path_count; ++index)
            paths.push_back(absolutePath(QStringLiteral("faixa-%1.mp3").arg(index)));

        const auto result = encodeOpenSourcesCommand(OpenSourcesCommand{paths});
        QCOMPARE(result.error, OpenSourcesCommandError::TooManyPaths);
        QVERIFY(result.payload.isEmpty());
    }

    void enforcesTheInclusiveOneMebibyteSerializedLimit() {
        const QString seed = absolutePath(QStringLiteral("x"));
        const auto seed_result = encodeOpenSourcesCommand(OpenSourcesCommand{{seed}});
        QCOMPARE(seed_result.error, OpenSourcesCommandError::None);
        QVERIFY(seed_result.payload.size() < open_sources_max_payload_bytes);

        const qsizetype padding = open_sources_max_payload_bytes - seed_result.payload.size();
        const QString exact_path = seed + QString(padding, QLatin1Char('a'));
        const auto exact = encodeOpenSourcesCommand(OpenSourcesCommand{{exact_path}});
        QCOMPARE(exact.error, OpenSourcesCommandError::None);
        QCOMPARE(exact.payload.size(), open_sources_max_payload_bytes);
        QCOMPARE(decodeOpenSourcesCommand(exact.payload).error, OpenSourcesCommandError::None);

        const auto above = encodeOpenSourcesCommand(OpenSourcesCommand{{exact_path + QLatin1Char('b')}});
        QCOMPARE(above.error, OpenSourcesCommandError::PayloadTooLarge);
        QVERIFY(above.payload.isEmpty());

        const QByteArray oversized(open_sources_max_payload_bytes + 1, 'x');
        QCOMPARE(decodeOpenSourcesCommand(oversized).error, OpenSourcesCommandError::PayloadTooLarge);
    }

    void rejectsInvalidPathsAsData_data() {
        QTest::addColumn<QString>("path");
        QTest::newRow("empty") << QString{};
        QTest::newRow("relative") << QStringLiteral("relative/song.mp3");
        QTest::newRow("embedded nul") << (absolutePath(QStringLiteral("song")) + QChar{0} + QStringLiteral(".mp3"));
    }

    void rejectsInvalidPathsAsData() {
        QFETCH(QString, path);
        QCOMPARE(encodeOpenSourcesCommand(OpenSourcesCommand{{path}}).error, OpenSourcesCommandError::InvalidPath);
    }

    void rejectsMalformedOrUnexpectedSchemas_data() {
        QTest::addColumn<QByteArray>("payload");
        QTest::addColumn<OpenSourcesCommandError>("error");
        QTest::newRow("empty") << QByteArray{} << OpenSourcesCommandError::MalformedPayload;
        QTest::newRow("invalid json") << QByteArray{"{"} << OpenSourcesCommandError::MalformedPayload;
        QTest::newRow("array root") << QByteArray{"[]"} << OpenSourcesCommandError::InvalidSchema;
        QTest::newRow("unknown field") << jsonPayload({{QStringLiteral("version"), 1},
                                                       {QStringLiteral("command"), QStringLiteral("openSources")},
                                                       {QStringLiteral("paths"), QJsonArray{}},
                                                       {QStringLiteral("execute"), QStringLiteral("calc.exe")}})
                                       << OpenSourcesCommandError::InvalidSchema;
        QTest::newRow("future version") << jsonPayload({{QStringLiteral("version"), 2},
                                                        {QStringLiteral("command"), QStringLiteral("openSources")},
                                                        {QStringLiteral("paths"), QJsonArray{}}})
                                        << OpenSourcesCommandError::UnsupportedVersion;
        QTest::newRow("other command") << jsonPayload({{QStringLiteral("version"), 1},
                                                       {QStringLiteral("command"), QStringLiteral("execute")},
                                                       {QStringLiteral("paths"), QJsonArray{}}})
                                       << OpenSourcesCommandError::UnsupportedCommand;
        QTest::newRow("paths is not an array")
            << jsonPayload({{QStringLiteral("version"), 1},
                            {QStringLiteral("command"), QStringLiteral("openSources")},
                            {QStringLiteral("paths"), QStringLiteral("C:/song.mp3")}})
            << OpenSourcesCommandError::InvalidSchema;
    }

    void rejectsMalformedOrUnexpectedSchemas() {
        QFETCH(QByteArray, payload);
        QFETCH(OpenSourcesCommandError, error);
        QCOMPARE(decodeOpenSourcesCommand(payload).error, error);
    }
};

QTEST_GUILESS_MAIN(OpenSourcesCommandTests)

#include "OpenSourcesCommandTests.moc"
