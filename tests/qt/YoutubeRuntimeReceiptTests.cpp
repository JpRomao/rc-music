#include "support/YoutubeRuntimeReceipt.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>
#include <QTest>

#include <algorithm>
#include <array>
#include <utility>

using namespace music_player::m0;
using namespace music_player::m0::test_support;
namespace {
const QString hash{QStringLiteral("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef")};
const QString root{QStringLiteral("C:/RC Music own synthetic/未実在 runtime")};
const std::array<std::pair<const char*, const char*>, 11> pins{{{"python", "3.13.1"},
                                                                {"node", "24.20.0"},
                                                                {"altgraph", "0.17.5"},
                                                                {"packaging", "26.3"},
                                                                {"pefile", "2024.8.26"},
                                                                {"pyinstaller", "6.22.3"},
                                                                {"pyinstaller-hooks-contrib", "2026.9"},
                                                                {"pywin32-ctypes", "0.2.3"},
                                                                {"setuptools", "84.2.1"},
                                                                {"yt-dlp", "2026.3.17"},
                                                                {"yt-dlp-ejs", "0.8.0"}}};
const std::array<const char*, 6> projects{{"entrypoint", "offline-entry", "fixture", "spec", "hook", "build-lock"}};
const std::array<const char*, 5> notices{{"notices/python-LICENSE.txt", "notices/node-LICENSE.txt",
                                          "notices/yt-dlp-LICENSE.txt", "notices/ejs-LICENSE.txt",
                                          "notices/pyinstaller-COPYING.txt"}};
const std::array<const char*, 4> roles{{"Extractor", "Node", "EjsCore", "EjsLibrary"}};
const std::array<const char*, 4> paths{{"extractor/yt-dlp.exe", "javascript/node.exe",
                                        "extractor/_internal/yt_dlp_ejs/yt/solver/core.min.js",
                                        "extractor/_internal/yt_dlp_ejs/yt/solver/lib.min.js"}};
QJsonObject source(const QString& name, const QString& version) {
    return {{"name", name},
            {"version", version},
            {"path", QStringLiteral("C:/reviewed sources/") + name + ".input"},
            {"sha256", hash},
            {"provenance", QStringLiteral("explicit reviewed offline observation")}};
}
QJsonObject artifact(const QString& role, const QString& path, const qint64 bytes = 1) {
    return {{"role", role}, {"path", path}, {"bytes", bytes}, {"sha256", hash}};
}
QJsonObject fixture() {
    QJsonArray sources, artifacts;
    for (const auto& [name, version] : pins)
        sources.append(source(QLatin1String(name), QLatin1String(version)));
    for (const auto name : projects)
        sources.append(source(QLatin1String(name), QStringLiteral("project")));
    for (std::size_t index = 0; index < roles.size(); ++index)
        artifacts.append(
            artifact(QLatin1String(roles[index]), QLatin1String(paths[index]), static_cast<qint64>(index + 1)));
    for (const auto path : notices)
        artifacts.append(artifact(QStringLiteral("Support"), QLatin1String(path)));
    return {{"schemaVersion", 1},
            {"purpose", QStringLiteral("offline-synthetic-v1")},
            {"artifact_root", root},
            {"sources", sources},
            {"artifacts", artifacts}};
}
QByteArray encode(const QJsonObject& object) { return QJsonDocument(object).toJson(QJsonDocument::Compact); }
YoutubeRuntimeReceiptResult decode(const QJsonObject& object) { return decodeYoutubeRuntimeReceipt(encode(object)); }
void changeRecord(QJsonObject& object, const QString& collection, const qsizetype index, const QString& field,
                  const QJsonValue& value) {
    auto values = object.value(collection).toArray();
    auto record = values.at(index).toObject();
    record.insert(field, value);
    values[index] = record;
    object.insert(collection, values);
}
QJsonObject expatFixture() {
    auto object = fixture();
    changeRecord(object, "sources", 0, "version", QStringLiteral("3.14.10"));
    auto sources = object.value("sources").toArray();
    auto artifacts = object.value("artifacts").toArray();
    const auto addModule = [&sources, &artifacts](const QString& name, const QString& file, const QString& pin) {
        auto observation = source(name, QStringLiteral("2.10.0"));
        observation.insert("sha256", pin);
        sources.append(observation);
        auto module = artifact(QStringLiteral("Support"), QStringLiteral("extractor/_internal/") + file);
        module.insert("sha256", pin);
        artifacts.append(module);
    };
    addModule(QStringLiteral("pyexpat"), QStringLiteral("pyexpat.pyd"), QString(64, u'a'));
    addModule(QStringLiteral("elementtree"), QStringLiteral("_elementtree.pyd"), QString(64, u'b'));
    object.insert("sources", sources);
    object.insert("artifacts", artifacts);
    return object;
}
QString relativeUnits(const qsizetype units) {
    QStringList parts{QStringLiteral("notices")};
    for (int index = 0; index < 14; ++index)
        parts.append(QString(64, QLatin1Char('a')));
    const auto prefix = parts.join('/') + '/';
    return prefix + QString(units - prefix.size(), QLatin1Char('b'));
}
QString relativeDepth(const qsizetype count) {
    QStringList parts{QStringLiteral("notices")};
    while (parts.size() < count - 1)
        parts.append(QStringLiteral("level%1").arg(parts.size()));
    parts.append(QStringLiteral("own.txt"));
    return parts.join('/');
}
} // namespace

class YoutubeRuntimeReceiptTests final : public QObject {
    Q_OBJECT
  private slots:
    void decodesOwnedSourcesAndConfigWithoutTouchingDisk() {
        auto object = fixture();
        const auto result = decode(object);
        QVERIFY(result.receipt);
        QCOMPARE(result.error, YoutubeRuntimeReceiptError::None);
        const auto& receipt = *result.receipt;
        QCOMPARE(receipt.artifact_root, root);
        QCOMPARE(receipt.sources.size(), std::size_t{17});
        QCOMPARE(receipt.config.artifacts.size(), std::size_t{9});
        QCOMPARE(receipt.declared_sizes.size(), receipt.config.artifacts.size());
        QVERIFY(receipt.config.runtime_parent.isEmpty());
        QCOMPARE(receipt.total_bytes, quint64{15});
        for (std::size_t index = 0; index < receipt.config.artifacts.size(); ++index) {
            const auto& entry = receipt.config.artifacts[index];
            QCOMPARE(entry.source_path, root + '/' + entry.relative_path);
            QCOMPARE(entry.sha256, hash.toLatin1());
            const auto expected = object.value("artifacts").toArray();
            const auto found = std::find_if(expected.begin(), expected.end(), [&entry](const auto& value) {
                return value.toObject().value("path").toString() == entry.relative_path;
            });
            QVERIFY(found != expected.end());
            QCOMPARE(receipt.declared_sizes[index],
                     static_cast<quint64>((*found).toObject().value("bytes").toInteger()));
        }
        for (const auto& [name, version] : pins) {
            const auto found = std::find_if(receipt.sources.begin(), receipt.sources.end(),
                                            [name](const auto& row) { return row.name == QLatin1String(name); });
            QVERIFY(found != receipt.sources.end());
            QCOMPARE(found->version, QLatin1String(version));
            QCOMPARE(found->provenance, QStringLiteral("explicit reviewed offline observation"));
        }
        object.insert("artifact_root", QStringLiteral("C:/changed"));
        QCOMPARE(receipt.artifact_root, root);
    }
    void bothWindowsSeparatorFormsAreAcceptedAndOwned() {
        auto object = fixture();
        object.insert("artifact_root", QStringLiteral("C:\\RC Music own synthetic\\未実在 runtime"));
        changeRecord(object, "sources", 0, "path", QStringLiteral("C:\\reviewed sources\\python.input"));
        const auto result = decode(object);
        QVERIFY(result.receipt);
        QCOMPARE(result.receipt->artifact_root, root);
        const auto found = std::find_if(result.receipt->sources.begin(), result.receipt->sources.end(),
                                        [](const auto& row) { return row.name == QStringLiteral("python"); });
        QVERIFY(found != result.receipt->sources.end());
        QCOMPARE(found->path, QStringLiteral("C:/reviewed sources/python.input"));
    }
    void acceptsOnlyExactOfflinePurposeAndSchema_data() {
        QTest::addColumn<QString>("field");
        QTest::addColumn<QJsonValue>("value");
        QTest::newRow("purpose prod") << QStringLiteral("purpose") << QJsonValue("production-candidate-v1");
        QTest::newRow("purpose smoke") << QStringLiteral("purpose") << QJsonValue("youtube-offline-smoke-v1");
        QTest::newRow("purpose wrong type") << QStringLiteral("purpose") << QJsonValue(1);
        QTest::newRow("schema false") << QStringLiteral("schemaVersion") << QJsonValue(false);
        QTest::newRow("schema string") << QStringLiteral("schemaVersion") << QJsonValue("1");
        QTest::newRow("schema other") << QStringLiteral("schemaVersion") << QJsonValue(2);
        QTest::newRow("schema fraction") << QStringLiteral("schemaVersion") << QJsonValue(1.5);
    }
    void acceptsOnlyExactOfflinePurposeAndSchema() {
        QFETCH(QString, field);
        QFETCH(QJsonValue, value);
        auto object = fixture();
        object.insert(field, value);
        QVERIFY(!decode(object).receipt);
    }
    void everyTopLevelFieldIsRequiredAndExtrasAreRejected() {
        const auto before = fixture();
        for (const auto& key : before.keys()) {
            auto object = before;
            object.remove(key);
            QVERIFY2(!decode(object).receipt, qPrintable(key));
        }
        auto object = before;
        object.insert("configuration", QJsonObject{});
        QVERIFY(!decode(object).receipt);
    }
    void strictUtf8JsonRejectsDuplicateEscapedKeysAndMalformedTokens() {
        const auto good = encode(fixture());
        const std::array<QByteArray, 9> malformed{
            {QByteArray{}, QByteArrayLiteral("[]"), good + "x", good + good,
             QByteArrayLiteral("{\"schemaVersion\":1,\"schema\\u0056ersion\":1}"),
             QByteArrayLiteral("{\"x\":\"\\ud800\"}"), QByteArrayLiteral("{\"x\":NaN}"),
             QByteArrayLiteral("{\"x\":\"\\q\"}"), good + QByteArray::fromHex("ff")}};
        for (const auto& bytes : malformed)
            QVERIFY(!decodeYoutubeRuntimeReceipt(bytes).receipt);
        auto nestedDuplicate = good;
        nestedDuplicate.replace("\"role\":\"Extractor\"", "\"role\":\"Extractor\",\"ro\\u006ce\":\"Extractor\"");
        QVERIFY(!decodeYoutubeRuntimeReceipt(nestedDuplicate).receipt);
    }
    void payloadByteLimitIsInclusive() {
        auto bytes = encode(fixture());
        bytes.append(youtube_runtime_receipt_max_bytes - bytes.size(), ' ');
        QVERIFY(decodeYoutubeRuntimeReceipt(bytes).receipt);
        bytes.append(' ');
        const auto rejected = decodeYoutubeRuntimeReceipt(bytes);
        QVERIFY(!rejected.receipt);
        QCOMPARE(rejected.error, YoutubeRuntimeReceiptError::LimitExceeded);
    }
    void nestingBeyondEightIsRejectedBeforeQtDecoding() {
        auto bytes = encode(fixture());
        bytes.chop(1);
        bytes += ",\"deep\":[[[[[[[[0]]]]]]]]}";
        QVERIFY(!decodeYoutubeRuntimeReceipt(bytes).receipt);
    }
    void rootAndSourcePathsMustBeExplicitLocalWindowsPaths_data() {
        QTest::addColumn<QString>("path");
        const QStringList bad{QString{},
                              QStringLiteral("relative/file"),
                              QStringLiteral("/rooted/file"),
                              QStringLiteral("C:relative"),
                              QStringLiteral("//server/share/file"),
                              QStringLiteral("\\\\?\\C:\\device"),
                              QStringLiteral("https://example.invalid/file"),
                              QStringLiteral("C:/a/../b"),
                              QStringLiteral("C:/a/./b"),
                              QStringLiteral("C:/a//b"),
                              QStringLiteral("C:/a/CON.txt"),
                              QStringLiteral("C:/a/b."),
                              QStringLiteral("C:/a/b "),
                              QStringLiteral("C:/a/stream:fork"),
                              QStringLiteral("C:/a/b\nfile"),
                              QStringLiteral("C:/a/") + QChar(0) + QStringLiteral("b")};
        for (qsizetype index = 0; index < bad.size(); ++index)
            QTest::newRow(qPrintable(QString::number(index))) << bad.at(index);
    }
    void rootAndSourcePathsMustBeExplicitLocalWindowsPaths() {
        QFETCH(QString, path);
        auto object = fixture();
        object.insert("artifact_root", path);
        QVERIFY(!decode(object).receipt);
        object = fixture();
        changeRecord(object, "sources", 0, "path", path);
        QVERIFY(!decode(object).receipt);
    }
    void rootAndCollectionsHaveExactTypes() {
        for (const auto& key :
             {QStringLiteral("artifact_root"), QStringLiteral("sources"), QStringLiteral("artifacts")}) {
            for (const QJsonValue value : {QJsonValue{}, QJsonValue(false), QJsonValue(7), QJsonValue(QJsonObject{})}) {
                auto object = fixture();
                object.insert(key, value);
                QVERIFY(!decode(object).receipt);
            }
        }
    }
    void everyPinnedSourceAndProjectInputIsRequiredOnce() {
        const auto base = fixture();
        const auto sources = base.value("sources").toArray();
        for (qsizetype index = 0; index < sources.size(); ++index) {
            auto object = base;
            auto missing = sources;
            missing.removeAt(index);
            object.insert("sources", missing);
            QVERIFY(!decode(object).receipt);
        }
        auto duplicate = sources;
        duplicate.append(sources.first());
        auto object = base;
        object.insert("sources", duplicate);
        QVERIFY(!decode(object).receipt);
    }
    void sourceVersionPinsCannotDriftOrUseRuntimeVersionSpelling() {
        for (qsizetype index = 0; index < static_cast<qsizetype>(pins.size()); ++index) {
            auto object = fixture();
            changeRecord(object, "sources", index, "version", QStringLiteral("0.0.unreviewed"));
            QVERIFY(!decode(object).receipt);
        }
        auto object = fixture();
        changeRecord(object, "sources", 9, "version", QStringLiteral("2026.03.17"));
        QVERIFY(!decode(object).receipt);
        for (qsizetype index = static_cast<qsizetype>(pins.size());
             index < static_cast<qsizetype>(pins.size() + projects.size()); ++index) {
            object = fixture();
            changeRecord(object, "sources", index, "version", QStringLiteral("1.0"));
            QVERIFY(!decode(object).receipt);
        }
    }
    void reviewedPythonVersionsArePreservedExactly() {
        for (const auto version : {"3.13.1", "3.14.10"}) {
            auto object = fixture();
            changeRecord(object, "sources", 0, "version", QString::fromLatin1(version));
            const auto result = decode(object);
            QVERIFY(result.receipt);
            QCOMPARE(result.error, YoutubeRuntimeReceiptError::None);
            QCOMPARE(result.receipt->sources.front().name, QStringLiteral("python"));
            QCOMPARE(result.receipt->sources.front().version, QLatin1String(version));
        }
        for (const auto version : {"3.13.17", "3.14.9", "3.14.10 "}) {
            auto object = fixture();
            changeRecord(object, "sources", 0, "version", QString::fromLatin1(version));
            const auto result = decode(object);
            QVERIFY(!result.receipt);
            QCOMPARE(result.error, YoutubeRuntimeReceiptError::InvalidSource);
        }
        auto object = fixture();
        changeRecord(object, "sources", 1, "version", QStringLiteral("3.14.10"));
        QCOMPARE(decode(object).error, YoutubeRuntimeReceiptError::InvalidSource);
    }
    void acceptsCurrentAndHistoricalNodeReceiptsWithoutAcceptingOtherVersions() {
        auto current = expatFixture();
        changeRecord(current, "sources", 1, "version", QStringLiteral("24.22.3"));
        const auto result = decode(current);
        QVERIFY(result.receipt);
        QCOMPARE(result.receipt->sources.at(1).version, QStringLiteral("24.22.3"));
        QVERIFY(decode(fixture()).receipt);
        changeRecord(current, "sources", 1, "version", QStringLiteral("24.22.2"));
        QCOMPARE(decode(current).error, YoutubeRuntimeReceiptError::InvalidSource);
    }
    void acceptsCurrentAndHistoricalResolverReceiptsWithoutAcceptingOtherVersions() {
        auto current = expatFixture();
        changeRecord(current, "sources", 9, "version", QStringLiteral("2026.10.26"));
        const auto result = decode(current);
        QVERIFY(result.receipt);
        QCOMPARE(result.receipt->sources.at(9).version, QStringLiteral("2026.10.26"));
        QVERIFY(decode(fixture()).receipt);
        changeRecord(current, "sources", 9, "version", QStringLiteral("2026.10.21"));
        QCOMPARE(decode(current).error, YoutubeRuntimeReceiptError::InvalidSource);
    }
    void acceptsBothReviewedExpatModulesWithMatchingArtifactPins() {
        const auto result = decode(expatFixture());
        QVERIFY(result.receipt);
        QCOMPARE(result.error, YoutubeRuntimeReceiptError::None);
        QCOMPARE(result.receipt->sources.size(), std::size_t{19});
        QCOMPARE(result.receipt->config.artifacts.size(), std::size_t{11});
    }
    void reviewedExpatModulesRequireThePairAndReviewedVersions() {
        const auto before = expatFixture();
        auto object = before;
        changeRecord(object, "sources", 0, "version", QStringLiteral("3.13.1"));
        QCOMPARE(decode(object).error, YoutubeRuntimeReceiptError::InvalidSource);
        const auto sources = before.value("sources").toArray();
        for (const auto index : {sources.size() - 2, sources.size() - 1}) {
            object = before;
            changeRecord(object, "sources", index, "version", QStringLiteral("2.9.0"));
            QCOMPARE(decode(object).error, YoutubeRuntimeReceiptError::InvalidSource);
            auto incomplete = sources;
            incomplete.removeAt(index);
            object = before;
            object.insert("sources", incomplete);
            QCOMPARE(decode(object).error, YoutubeRuntimeReceiptError::InvalidSource);
        }
    }
    void reviewedExpatModulesRequireExactArtifactPathsAndPins() {
        const auto before = expatFixture();
        const auto artifacts = before.value("artifacts").toArray();
        for (const auto index : {artifacts.size() - 2, artifacts.size() - 1}) {
            auto object = before;
            changeRecord(object, "artifacts", index, "sha256", hash);
            QCOMPARE(decode(object).error, YoutubeRuntimeReceiptError::InvalidArtifact);
            object = before;
            changeRecord(object, "artifacts", index, "path", QStringLiteral("extractor/_internal/other.pyd"));
            QCOMPARE(decode(object).error, YoutubeRuntimeReceiptError::InvalidArtifact);
            auto incomplete = artifacts;
            incomplete.removeAt(index);
            object = before;
            object.insert("artifacts", incomplete);
            QCOMPARE(decode(object).error, YoutubeRuntimeReceiptError::InvalidArtifact);
        }
    }
    void sourceMetadataRequiresExactBoundedFields() {
        const auto before = fixture();
        const auto row = before.value("sources").toArray().first().toObject();
        for (const auto& key : row.keys()) {
            auto object = before;
            changeRecord(object, "sources", 0, key, QJsonValue{});
            QVERIFY(!decode(object).receipt);
        }
        const std::array<std::pair<QString, QJsonValue>, 8> bad{{{"name", "unknown-dependency"},
                                                                 {"name", "Python"},
                                                                 {"provenance", ""},
                                                                 {"provenance", "unreviewed\ncontrol"},
                                                                 {"provenance", QString(2049, QLatin1Char('a'))},
                                                                 {"sha256", hash.toUpper()},
                                                                 {"sha256", QString(63, QLatin1Char('a'))},
                                                                 {"extra", true}}};
        for (const auto& [key, value] : bad) {
            auto object = before;
            changeRecord(object, "sources", 0, key, value);
            QVERIFY(!decode(object).receipt);
        }
        auto values = before.value("sources").toArray();
        values[0] = QStringLiteral("not a record");
        auto object = before;
        object.insert("sources", values);
        QVERIFY(!decode(object).receipt);
    }
    void optionalLicenseMetadataRemainsDataAndUsesProjectVersion() {
        auto object = fixture();
        auto values = object.value("sources").toArray();
        for (const auto name :
             {"python-license", "node-license", "yt-dlp-license", "ejs-license", "pyinstaller-license"})
            values.append(source(QLatin1String(name), QStringLiteral("project")));
        object.insert("sources", values);
        const auto result = decode(object);
        QVERIFY(result.receipt);
        QCOMPARE(result.receipt->sources.size(), std::size_t{22});
        changeRecord(object, "sources", 17, "version", QStringLiteral("3.13.1"));
        QVERIFY(!decode(object).receipt);
    }
    void requiredRolesAndTheirFixedPathsAreNotInterchangeable() {
        const auto before = fixture();
        for (qsizetype index = 0; index < 4; ++index) {
            auto object = before;
            auto values = object.value("artifacts").toArray();
            values.removeAt(index);
            object.insert("artifacts", values);
            QVERIFY(!decode(object).receipt);
            object = before;
            changeRecord(object, "artifacts", index, "path", QStringLiteral("notices/other.bin"));
            QVERIFY(!decode(object).receipt);
            object = before;
            changeRecord(object, "artifacts", index, "role", QStringLiteral("Support"));
            QVERIFY(!decode(object).receipt);
        }
        auto object = before;
        auto values = object.value("artifacts").toArray();
        values.append(values.first());
        object.insert("artifacts", values);
        QVERIFY(!decode(object).receipt);
        object = before;
        changeRecord(object, "artifacts", 0, "role", QStringLiteral("Unknown"));
        QVERIFY(!decode(object).receipt);
    }
    void everyRequiredNoticeMustRemainInTheClosedInventory() {
        const auto before = fixture();
        for (qsizetype index = 4; index < 9; ++index) {
            auto object = before;
            auto values = object.value("artifacts").toArray();
            values.removeAt(index);
            object.insert("artifacts", values);
            QVERIFY(!decode(object).receipt);
        }
    }
    void artifactRecordsRequireExactFieldsAndTypes() {
        const auto before = fixture();
        const auto row = before.value("artifacts").toArray().first().toObject();
        for (const auto& key : row.keys()) {
            auto object = before;
            changeRecord(object, "artifacts", 0, key, QJsonValue{});
            QVERIFY(!decode(object).receipt);
        }
        const std::array<std::pair<QString, QJsonValue>, 7> bad{{{"sha256", hash.toUpper()},
                                                                 {"sha256", QString(65, QLatin1Char('0'))},
                                                                 {"bytes", "1"},
                                                                 {"bytes", true},
                                                                 {"bytes", 0.5},
                                                                 {"path", 7},
                                                                 {"extra", true}}};
        for (const auto& [key, value] : bad) {
            auto object = before;
            changeRecord(object, "artifacts", 0, key, value);
            QVERIFY(!decode(object).receipt);
        }
        auto object = before;
        auto values = object.value("artifacts").toArray();
        values[0] = QJsonArray{};
        object.insert("artifacts", values);
        QVERIFY(!decode(object).receipt);
    }
    void supportPathsRejectTraversalDeviceNamesAndOtherRoots_data() {
        QTest::addColumn<QString>("path");
        const QStringList bad{QString{},
                              QStringLiteral("/notices/file"),
                              QStringLiteral("C:/file"),
                              QStringLiteral("notices\\file"),
                              QStringLiteral("notices/../file"),
                              QStringLiteral("notices/./file"),
                              QStringLiteral("notices//file"),
                              QStringLiteral("notices/file."),
                              QStringLiteral("notices/file "),
                              QStringLiteral("notices/CON.txt"),
                              QStringLiteral("notices/LPT1"),
                              QStringLiteral("notices/file:stream"),
                              QStringLiteral("notices/日本.txt"),
                              QStringLiteral("javascript/extra.exe"),
                              QStringLiteral("extractor/other.exe"),
                              QStringLiteral("extractor/_internality/file"),
                              QStringLiteral("notices/") + QString(256, QLatin1Char('a')),
                              relativeDepth(17),
                              relativeUnits(1025)};
        for (qsizetype index = 0; index < bad.size(); ++index)
            QTest::newRow(qPrintable(QString::number(index))) << bad.at(index);
    }
    void supportPathsRejectTraversalDeviceNamesAndOtherRoots() {
        QFETCH(QString, path);
        auto object = fixture();
        auto values = object.value("artifacts").toArray();
        values.append(artifact(QStringLiteral("Support"), path));
        object.insert("artifacts", values);
        QVERIFY(!decode(object).receipt);
    }
    void exactRelativePathUnitAndDepthLimitsAreAccepted() {
        for (const auto& relative : {relativeUnits(1024), relativeDepth(16)}) {
            auto object = fixture();
            auto values = object.value("artifacts").toArray();
            values.append(artifact(QStringLiteral("Support"), relative));
            object.insert("artifacts", values);
            QVERIFY(decode(object).receipt);
        }
    }
    void filePathsAreUniqueCaseInsensitivelyAndNeverAncestors() {
        const auto before = fixture();
        for (const auto& path :
             {QStringLiteral("notices/NODE-license.TXT"), QStringLiteral("notices/node-LICENSE.txt/child"),
              QStringLiteral("extractor/_internal/yt_dlp_ejs")}) {
            auto object = before;
            auto values = object.value("artifacts").toArray();
            values.append(artifact(QStringLiteral("Support"), path));
            object.insert("artifacts", values);
            QVERIFY(!decode(object).receipt);
        }
    }
    void perArtifactBytesHaveAnInclusive128MiBLimit() {
        auto object = fixture();
        changeRecord(object, "artifacts", 0, "bytes", qint64{128} * 1024 * 1024);
        QVERIFY(decode(object).receipt);
        for (const auto bytes : {qint64{0}, qint64{-1}, qint64{128} * 1024 * 1024 + 1}) {
            object = fixture();
            changeRecord(object, "artifacts", 0, "bytes", bytes);
            QVERIFY(!decode(object).receipt);
        }
    }
    void totalBytesHaveAnInclusive512MiBLimit() {
        auto object = fixture();
        for (qsizetype index = 0; index < 4; ++index)
            changeRecord(object, "artifacts", index, "bytes", qint64{128} * 1024 * 1024 - (index == 3 ? 5 : 0));
        const auto result = decode(object);
        QVERIFY(result.receipt);
        QCOMPARE(result.receipt->total_bytes, youtube_runtime_max_bytes);
        changeRecord(object, "artifacts", 3, "bytes", qint64{128} * 1024 * 1024 - 4);
        const auto rejected = decode(object);
        QVERIFY(!rejected.receipt);
        QCOMPARE(rejected.error, YoutubeRuntimeReceiptError::LimitExceeded);
    }
    void fileCountHasAnInclusive256Limit() {
        auto object = fixture();
        auto values = object.value("artifacts").toArray();
        while (values.size() < static_cast<qsizetype>(youtube_runtime_max_files))
            values.append(artifact(QStringLiteral("Support"), QStringLiteral("notices/own-%1.bin").arg(values.size())));
        object.insert("artifacts", values);
        const auto result = decode(object);
        QVERIFY(result.receipt);
        QCOMPARE(result.receipt->config.artifacts.size(), youtube_runtime_max_files);
        values.append(artifact(QStringLiteral("Support"), QStringLiteral("notices/one-extra.bin")));
        object.insert("artifacts", values);
        const auto rejected = decode(object);
        QVERIFY(!rejected.receipt);
        QCOMPARE(rejected.error, YoutubeRuntimeReceiptError::LimitExceeded);
    }
};
QTEST_GUILESS_MAIN(YoutubeRuntimeReceiptTests)
#include "YoutubeRuntimeReceiptTests.moc"
