#include "../windows/sandbox/AppContainerStorageInventoryJson.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QTest>

#include <array>
#include <cstdint>
#include <limits>

using Codec = music_player::sandbox_probe::AppContainerStorageInventoryJson;
using Inventory = Codec::Inventory;
namespace {
Inventory::Report emptyReport() {
    Inventory::Report report;
    for (std::size_t index = 0; index < report.roots.size(); ++index)
        report.roots[index].root = static_cast<Inventory::Root>(index);
    return report;
}
Inventory::Object object() {
    return {.root = Inventory::Root::PackageRoot,
            .parent = {1},
            .name = QStringLiteral("settings 日本.bin"),
            .depth = 1,
            .metadata = {.ref = {2},
                         .identity = {7, 2},
                         .presence = Inventory::Presence::Present,
                         .type = Inventory::Type::File,
                         .error = 0,
                         .link_count = 1,
                         .owner_sid = QByteArray::fromHex("010100000000000515000000"),
                         .raw_sd = QByteArray::fromHex("00112233445500ff"),
                         .dacl = Inventory::Dacl::Present,
                         .dacl_protected = true}};
}
std::uint64_t payload(const Inventory::Report& report) {
    std::uint64_t result = 0;
    for (const auto& entry : report.objects)
        result += static_cast<std::uint64_t>(entry.metadata.raw_sd.size() + entry.metadata.owner_sid.size()) +
                  2 * static_cast<std::uint64_t>(entry.name.size());
    return result;
}
Inventory::Report oneObject() {
    auto report = emptyReport();
    report.objects.push_back(object());
    report.total_bytes = payload(report);
    return report;
}
Codec::Result encode(const Inventory::Report& report) {
    return Codec::encode(report, {Inventory::Validation::Unknown, Inventory::not_called},
                         {Inventory::CloseState::NotAttempted, Inventory::not_called});
}
QJsonObject json(const Codec::Result& result) {
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(result.bytes, &error);
    return error.error == QJsonParseError::NoError && document.isObject() ? document.object() : QJsonObject{};
}
QJsonObject firstMetadata(const QJsonObject& document) {
    return document.value("snapshot")
        .toObject()
        .value("objects")
        .toArray()
        .first()
        .toObject()
        .value("metadata")
        .toObject();
}
void refused(const Codec::Result& result, const Codec::Error expected) {
    QCOMPARE(result.error, expected);
    QVERIFY(result.bytes.isEmpty());
    QCOMPARE(result.payload_bytes, std::uint64_t{});
}
} // namespace

class AppContainerStorageInventoryJsonTests final : public QObject {
    Q_OBJECT
  private slots:
    void emptyPartialReportKeepsFourFixedRootsAndRawNotCalledErrors() {
        const auto result = encode(emptyReport());
        QCOMPARE(result.error, Codec::Error::None);
        QCOMPARE(result.payload_bytes, std::uint64_t{});
        const auto document = json(result);
        QCOMPARE(document.value("schema").toInt(), 1);
        QCOMPARE(document.value("kind").toString(), QStringLiteral("appcontainer-storage-inventory"));
        QVERIFY(document.value("diagnosticOnly").toBool());
        const auto snapshot = document.value("snapshot").toObject();
        QCOMPARE(snapshot.value("status").toString(), QStringLiteral("incomplete"));
        QCOMPARE(snapshot.value("provenance").toString(), QStringLiteral("unknown"));
        QCOMPARE(snapshot.value("beginError").toInteger(), qint64{Inventory::not_called});
        const auto roots = snapshot.value("roots").toArray();
        QCOMPARE(roots.size(), qsizetype{4});
        const std::array<const char*, 4> names{"package-root", "ac", "mappings-sid", "storage-sid"};
        for (qsizetype index = 0; index < roots.size(); ++index) {
            const auto root = roots[index].toObject();
            QCOMPARE(root.value("root").toString(), QString::fromLatin1(names[static_cast<std::size_t>(index)]));
            QCOMPARE(root.value("metadataError").toInteger(), qint64{Inventory::not_called});
            QVERIFY(root.value("objectIndex").isNull());
            const auto lookup = root.value("lookup").toObject();
            QCOMPARE(lookup.value("ref").toString(), QStringLiteral("0"));
            QCOMPARE(lookup.value("error").toInteger(), qint64{Inventory::not_called});
            QCOMPARE(lookup.value("presence").toString(), QStringLiteral("unknown"));
        }
        QVERIFY(snapshot.value("objects").toArray().isEmpty());
    }
    void boundedMetadataPreservesEveryFieldWithoutPolicyInterpretation() {
        auto report = oneObject();
        auto& metadata = report.objects.front().metadata;
        metadata.error = 5;
        metadata.link_count = 2;
        metadata.attributes = 0x400;
        metadata.reparse_tag = 0xa0000003U;
        metadata.reparse = true;
        metadata.registry_link = true;
        metadata.sacl_not_queried = false;
        const auto result = encode(report);
        QCOMPARE(result.error, Codec::Error::None);
        QCOMPARE(result.payload_bytes, payload(report));
        const auto document = json(result);
        const auto entry = document.value("snapshot").toObject().value("objects").toArray().first().toObject();
        QCOMPARE(entry.value("root").toString(), QStringLiteral("package-root"));
        QCOMPARE(entry.value("parentRef").toString(), QStringLiteral("1"));
        QCOMPARE(entry.value("name").toString(), report.objects.front().name);
        QCOMPARE(entry.value("depth").toInteger(), qint64{1});
        const auto encoded = entry.value("metadata").toObject();
        QCOMPARE(encoded.value("ref").toString(), QStringLiteral("2"));
        QCOMPARE(encoded.value("presence").toString(), QStringLiteral("present"));
        QCOMPARE(encoded.value("type").toString(), QStringLiteral("file"));
        QCOMPARE(encoded.value("error").toInteger(), qint64{5});
        QCOMPARE(encoded.value("linkCount").toInteger(), qint64{2});
        QCOMPARE(encoded.value("attributes").toInteger(), qint64{0x400});
        QCOMPARE(encoded.value("reparseTag").toInteger(), qint64{0xa0000003U});
        QVERIFY(encoded.value("reparse").toBool());
        QVERIFY(encoded.value("registryLink").toBool());
        QVERIFY(!encoded.value("saclNotQueried").toBool(true));
        QCOMPARE(encoded.value("ownerSidHex").toString(), QString::fromLatin1(metadata.owner_sid.toHex()));
        QCOMPARE(encoded.value("rawSdBase64").toString(), QString::fromLatin1(metadata.raw_sd.toBase64()));
        QVERIFY(encoded.value("daclProtected").toBool());
    }
    void missingRootAndItsPartialMetadataErrorRemainDifferentObservations() {
        auto report = emptyReport();
        report.reason = Inventory::Reason::Root;
        report.provenance = Inventory::Provenance::LiveCreated;
        report.begin_error = 0;
        auto& root = report.roots[3];
        root.result = {{99}, 2, Inventory::Presence::Missing};
        root.presence = Inventory::Presence::Unknown;
        root.metadata_error = 5;
        const auto result = encode(report);
        QCOMPARE(result.error, Codec::Error::None);
        const auto snapshot = json(result).value("snapshot").toObject();
        QCOMPARE(snapshot.value("reason").toString(), QStringLiteral("root"));
        QCOMPARE(snapshot.value("provenance").toString(), QStringLiteral("live-created"));
        const auto encoded = snapshot.value("roots").toArray().at(3).toObject();
        QCOMPARE(encoded.value("presence").toString(), QStringLiteral("unknown"));
        QCOMPARE(encoded.value("metadataError").toInteger(), qint64{5});
        QCOMPARE(encoded.value("lookup").toObject().value("presence").toString(), QStringLiteral("missing"));
        QCOMPARE(encoded.value("lookup").toObject().value("error").toInteger(), qint64{2});
    }
    void snapshotAndLaterValidationAndClosureAreIndependent() {
        auto report = emptyReport();
        report.status = Inventory::Status::Observed;
        report.validation = {Inventory::Validation::Unchanged, 0};
        const auto result =
            Codec::encode(report, {Inventory::Validation::Changed, 5}, {Inventory::CloseState::Unknown, 170});
        QCOMPARE(result.error, Codec::Error::None);
        const auto document = json(result);
        const auto snapshot = document.value("snapshot").toObject();
        QCOMPARE(snapshot.value("status").toString(), QStringLiteral("observed"));
        QCOMPARE(snapshot.value("validation").toObject().value("state").toString(), QStringLiteral("unchanged"));
        QCOMPARE(document.value("postValidation").toObject().value("state").toString(), QStringLiteral("changed"));
        QCOMPARE(document.value("postValidation").toObject().value("error").toInteger(), qint64{5});
        QCOMPARE(document.value("closure").toObject().value("state").toString(), QStringLiteral("unknown"));
        QCOMPARE(document.value("closure").toObject().value("error").toInteger(), qint64{170});
    }
    void observedAndClosedNeverGrantPolicyOrCleanupAuthority() {
        auto report = emptyReport();
        report.status = Inventory::Status::Observed;
        const auto result =
            Codec::encode(report, {Inventory::Validation::Unchanged, 0}, {Inventory::CloseState::Closed, 0});
        QCOMPARE(result.error, Codec::Error::None);
        const auto qualification = json(result).value("qualification").toObject();
        QCOMPARE(qualification.value("identityScope").toString(), QStringLiteral("session-local"));
        for (const auto* name : {"securityPolicyApproved", "mandatoryLabelProven", "ordinaryRegistryKeyTypeProven",
                                 "atomicSnapshotProven", "persistentObjectIdentityProven", "cleanupAuthorityGranted"}) {
            QVERIFY(qualification.value(QLatin1StringView{name}).isBool());
            QVERIFY(!qualification.value(QLatin1StringView{name}).toBool(true));
        }
    }
    void daclKindsRemainDistinct_data() {
        QTest::addColumn<int>("kind");
        QTest::addColumn<QString>("text");
        QTest::newRow("unknown") << int(Inventory::Dacl::Unknown) << QStringLiteral("unknown");
        QTest::newRow("absent") << int(Inventory::Dacl::Absent) << QStringLiteral("absent");
        QTest::newRow("null") << int(Inventory::Dacl::Null) << QStringLiteral("null");
        QTest::newRow("present") << int(Inventory::Dacl::Present) << QStringLiteral("present");
    }
    void daclKindsRemainDistinct() {
        QFETCH(int, kind);
        QFETCH(QString, text);
        auto report = oneObject();
        report.objects.front().metadata.dacl = static_cast<Inventory::Dacl>(kind);
        const auto result = encode(report);
        QCOMPARE(result.error, Codec::Error::None);
        QCOMPARE(firstMetadata(json(result)).value("dacl").toString(), text);
    }
    void localUint64TokensAreDecimalStringsWithoutRounding() {
        auto report = oneObject();
        const auto high = std::numeric_limits<std::uint64_t>::max();
        report.roots[0].result.ref = {high};
        report.objects.front().parent = {high};
        report.objects.front().metadata.ref = {high};
        report.objects.front().metadata.identity = {high, high - 1};
        const auto result = encode(report);
        QCOMPARE(result.error, Codec::Error::None);
        const auto document = json(result);
        const auto snapshot = document.value("snapshot").toObject();
        QCOMPARE(
            snapshot.value("roots").toArray().first().toObject().value("lookup").toObject().value("ref").toString(),
            QStringLiteral("18446744073709551615"));
        const auto encoded = firstMetadata(document);
        QCOMPARE(encoded.value("ref").toString(), QStringLiteral("18446744073709551615"));
        QCOMPARE(encoded.value("localIdentity").toObject().value("domain").toString(),
                 QStringLiteral("18446744073709551615"));
        QCOMPARE(encoded.value("localIdentity").toObject().value("object").toString(),
                 QStringLiteral("18446744073709551614"));
    }
    void hardLimitsAreExplicitAndDoNotClaimAppliedPerRunLimits() {
        const auto result = encode(emptyReport());
        QCOMPARE(result.error, Codec::Error::None);
        const auto limits = json(result).value("hardLimits").toObject();
        QCOMPARE(limits.value("objects").toInteger(), qint64{512});
        QCOMPARE(limits.value("depth").toInteger(), qint64{16});
        QCOMPARE(limits.value("rawSdBytes").toString(), QStringLiteral("65536"));
        QCOMPARE(limits.value("ownerSidBytes").toString(), QStringLiteral("68"));
        QCOMPARE(limits.value("nameUtf16Units").toInteger(), qint64{255});
        QCOMPARE(limits.value("payloadBytes").toString(), QStringLiteral("4194304"));
        QCOMPARE(limits.value("jsonBytes").toString(), QStringLiteral("12582912"));
    }
    void inclusiveObjectDepthNameSdAndOwnerLimitsAreAccepted() {
        auto report = emptyReport();
        report.objects.assign(Inventory::maximum_objects, object());
        auto& first = report.objects.front();
        first.depth = Inventory::maximum_depth;
        first.name = QString(Inventory::maximum_name_units, QChar('x'));
        first.metadata.raw_sd = QByteArray(Inventory::maximum_sd_bytes, '\xff');
        first.metadata.owner_sid = QByteArray(Codec::maximum_owner_sid_bytes, '\x01');
        report.total_bytes = payload(report);
        const auto result = encode(report);
        QCOMPARE(result.error, Codec::Error::None);
        QCOMPARE(result.payload_bytes, report.total_bytes);
        QVERIFY(static_cast<std::uint64_t>(result.bytes.size()) <= Codec::maximum_json_bytes);
        QCOMPARE(json(result).value("snapshot").toObject().value("objects").toArray().size(), qsizetype{512});
    }
    void inclusiveAggregatePayloadIsAcceptedAndBoundedAfterBase64() {
        auto report = emptyReport();
        Inventory::Object entry;
        entry.metadata.raw_sd = QByteArray(Inventory::maximum_sd_bytes, '\x01');
        report.objects.assign(64, entry);
        report.total_bytes = Inventory::maximum_total_bytes;
        const auto result = encode(report);
        QCOMPARE(result.error, Codec::Error::None);
        QCOMPARE(result.payload_bytes, Inventory::maximum_total_bytes);
        QVERIFY(static_cast<std::uint64_t>(result.bytes.size()) <= Codec::maximum_json_bytes);
    }
    void excessivePayloadsAreRejectedWholeWithoutTruncation_data() {
        QTest::addColumn<QString>("mode");
        for (const auto* mode : {"objects", "depth", "name", "sd", "owner", "reported-total", "actual-total"})
            QTest::newRow(mode) << QString::fromLatin1(mode);
    }
    void excessivePayloadsAreRejectedWholeWithoutTruncation() {
        QFETCH(QString, mode);
        auto report = oneObject();
        auto& first = report.objects.front();
        if (mode == QStringLiteral("objects"))
            report.objects.assign(Inventory::maximum_objects + 1, object());
        else if (mode == QStringLiteral("depth"))
            first.depth = Inventory::maximum_depth + 1;
        else if (mode == QStringLiteral("name"))
            first.name = QString(Inventory::maximum_name_units + 1, QChar('x'));
        else if (mode == QStringLiteral("sd"))
            first.metadata.raw_sd = QByteArray(Inventory::maximum_sd_bytes + 1, '\x01');
        else if (mode == QStringLiteral("owner"))
            first.metadata.owner_sid = QByteArray(Codec::maximum_owner_sid_bytes + 1, '\x01');
        else if (mode == QStringLiteral("reported-total"))
            report.total_bytes = Inventory::maximum_total_bytes + 1;
        else {
            report.objects.assign(65, object());
            for (auto& entry : report.objects)
                entry.metadata.raw_sd = QByteArray(Inventory::maximum_sd_bytes, '\x01');
            report.total_bytes = 0; // A caller's low counter cannot bypass the actual payload bound.
        }
        refused(encode(report), Codec::Error::Bounds);
    }
    void malformedRelativeNamesAreRejected_data() {
        QTest::addColumn<QString>("name");
        QTest::newRow("nul") << QStringLiteral("bad") + QChar{};
        QTest::newRow("unpaired-surrogate") << QString(QChar{0xd800});
        QTest::newRow("absolute-drive") << QStringLiteral("C:\\Users\\private");
        QTest::newRow("absolute-root") << QStringLiteral("/private");
        QTest::newRow("relative-path") << QStringLiteral("child\\nested");
        QTest::newRow("dot-dot") << QStringLiteral("..");
    }
    void malformedRelativeNamesAreRejected() {
        QFETCH(QString, name);
        auto report = oneObject();
        report.objects.front().name = name;
        refused(encode(report), Codec::Error::InvalidReport);
    }
    void invalidEnumsAreRejectedInsteadOfInventingLabels_data() {
        QTest::addColumn<QString>("field");
        for (const auto* field :
             {"status", "reason", "provenance", "snapshot-validation", "post-validation", "close", "root",
              "root-lookup-presence", "root-presence", "object-root", "metadata-presence", "metadata-type", "dacl"})
            QTest::newRow(field) << QString::fromLatin1(field);
    }
    void invalidEnumsAreRejectedInsteadOfInventingLabels() {
        QFETCH(QString, field);
        auto report = oneObject();
        auto post = Inventory::ValidationResult{};
        auto close = Inventory::CloseResult{};
        if (field == QStringLiteral("status"))
            report.status = static_cast<Inventory::Status>(99);
        else if (field == QStringLiteral("reason"))
            report.reason = static_cast<Inventory::Reason>(99);
        else if (field == QStringLiteral("provenance"))
            report.provenance = static_cast<Inventory::Provenance>(99);
        else if (field == QStringLiteral("snapshot-validation"))
            report.validation.state = static_cast<Inventory::Validation>(99);
        else if (field == QStringLiteral("post-validation"))
            post.state = static_cast<Inventory::Validation>(99);
        else if (field == QStringLiteral("close"))
            close.state = static_cast<Inventory::CloseState>(99);
        else if (field == QStringLiteral("root"))
            report.roots[0].root = static_cast<Inventory::Root>(99);
        else if (field == QStringLiteral("root-lookup-presence"))
            report.roots[0].result.presence = static_cast<Inventory::Presence>(99);
        else if (field == QStringLiteral("root-presence"))
            report.roots[0].presence = static_cast<Inventory::Presence>(99);
        else if (field == QStringLiteral("object-root"))
            report.objects[0].root = static_cast<Inventory::Root>(99);
        else if (field == QStringLiteral("metadata-presence"))
            report.objects[0].metadata.presence = static_cast<Inventory::Presence>(99);
        else if (field == QStringLiteral("metadata-type"))
            report.objects[0].metadata.type = static_cast<Inventory::Type>(99);
        else
            report.objects[0].metadata.dacl = static_cast<Inventory::Dacl>(99);
        refused(Codec::encode(report, post, close), Codec::Error::InvalidEnum);
    }
    void fixedRootOrderAndObjectIndicesMustRemainUnambiguous() {
        auto report = oneObject();
        report.roots[0].root = Inventory::Root::StorageSID;
        refused(encode(report), Codec::Error::InvalidReport);
        report = oneObject();
        report.roots[0].object_index = 1;
        refused(encode(report), Codec::Error::InvalidReport);
    }
    void reportCountersArePreservedButThePayloadIsRecomputed() {
        auto report = oneObject();
        report.total_bytes = 0;
        const auto result = encode(report);
        QCOMPARE(result.error, Codec::Error::None);
        QCOMPARE(result.payload_bytes, payload(report));
        const auto document = json(result);
        QCOMPARE(document.value("snapshot").toObject().value("reportedPayloadBytes").toString(), QStringLiteral("0"));
        QCOMPARE(document.value("encodedPayloadBytes").toString(), QString::number(result.payload_bytes));
    }
    void encodingDoesNotRetainBorrowedQtBuffersOrMutateInput() {
        auto report = oneObject();
        QString name = report.objects.front().name;
        QByteArray sd = report.objects.front().metadata.raw_sd, owner = report.objects.front().metadata.owner_sid;
        report.objects.front().name = QString::fromRawData(name.constData(), name.size());
        report.objects.front().metadata.raw_sd = QByteArray::fromRawData(sd.constData(), sd.size());
        report.objects.front().metadata.owner_sid = QByteArray::fromRawData(owner.constData(), owner.size());
        const auto result = encode(report);
        QCOMPARE(result.error, Codec::Error::None);
        const auto before = result.bytes;
        name.fill('x');
        sd.fill('x');
        owner.fill('x');
        QCOMPARE(result.bytes, before);
        const auto encoded = firstMetadata(json(result));
        QCOMPARE(encoded.value("rawSdBase64").toString(), QStringLiteral("ABEiM0RVAP8="));
        QCOMPARE(encoded.value("ownerSidHex").toString(), QStringLiteral("010100000000000515000000"));
        QVERIFY(!result.bytes.contains("nativeHandle"));
        QVERIFY(!result.bytes.contains("packagePath"));
    }
};
QTEST_GUILESS_MAIN(AppContainerStorageInventoryJsonTests)
#include "AppContainerStorageInventoryJsonTests.moc"
