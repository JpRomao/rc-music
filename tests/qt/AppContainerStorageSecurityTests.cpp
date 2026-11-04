#include "../windows/sandbox/AppContainerStorageSecurity.hpp"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

using Security = music_player::sandbox_probe::AppContainerStorageSecurity;
namespace {
constexpr Security::Handle fake_handle = 17;
constexpr std::uint16_t self_relative = 0x8000, dacl_present = 4, dacl_protected = 0x1000;
QByteArray owner() { return QByteArray::fromHex("010100000000000515000000"); }
QByteArray group() { return QByteArray::fromHex("010100000000000520000000"); }
void put16(QByteArray& bytes, const qsizetype offset, const std::uint16_t value) {
    bytes[offset] = static_cast<char>(value);
    bytes[offset + 1] = static_cast<char>(value >> 8);
}
void put32(QByteArray& bytes, const qsizetype offset, const std::uint32_t value) {
    for (int i = 0; i < 4; ++i)
        bytes[offset + i] = static_cast<char>(value >> (8 * i));
}
std::uint32_t get32(const QByteArray& bytes, const qsizetype offset) {
    std::uint32_t result = 0;
    for (int i = 0; i < 4; ++i)
        result |= static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[offset + i])) << (8 * i);
    return result;
}
QByteArray acl(const bool opaque = false) {
    QByteArray ace(opaque ? 8 : 8 + owner().size(), '\0');
    ace[0] = opaque ? static_cast<char>(0x7e) : 0; // Unknown ACE payload is opaque, not an ACL policy approval.
    put16(ace, 2, static_cast<std::uint16_t>(ace.size()));
    put32(ace, 4, 0x001f01ff);
    if (!opaque)
        std::memcpy(ace.data() + 8, owner().constData(), static_cast<std::size_t>(owner().size()));
    QByteArray result(8, '\0');
    result[0] = 2;
    put16(result, 2, static_cast<std::uint16_t>(8 + ace.size()));
    put16(result, 4, 1);
    result += ace;
    return result;
}
QByteArray descriptor(const Security::Dacl kind = Security::Dacl::Present, const bool opaque = false,
                      const bool with_owner = true, const bool with_group = true) {
    QByteArray result(20, '\0');
    result[0] = 1;
    put16(result, 2, self_relative | dacl_protected | (kind == Security::Dacl::Absent ? 0 : dacl_present));
    if (with_owner) {
        put32(result, 4, static_cast<std::uint32_t>(result.size()));
        result += owner();
    }
    if (with_group) {
        put32(result, 8, static_cast<std::uint32_t>(result.size()));
        result += group();
    }
    if (kind == Security::Dacl::Present) {
        put32(result, 16, static_cast<std::uint32_t>(result.size()));
        result += acl(opaque);
    }
    return result;
}
struct Fake final {
    QByteArray raw{descriptor()};
    unsigned queries{}, reads{};
    Security::Object object{Security::Object::File};
    Security::Handle handle{};
    std::size_t buffer_size{};
    std::uint32_t query_error{ERROR_INSUFFICIENT_BUFFER}, read_error{};
    std::optional<std::uint32_t> required{}, used{};
    bool throw_query{false}, throw_read{false};
};
Security::Calls calls(const std::shared_ptr<Fake>& fake) {
    return {
        .size_query =
            [fake](const Security::Object object, const Security::Handle handle) {
                ++fake->queries;
                fake->object = object;
                fake->handle = handle;
                if (fake->throw_query)
                    throw std::runtime_error("size query failed");
                return Security::NativeResult{fake->query_error,
                                              fake->required.value_or(static_cast<std::uint32_t>(fake->raw.size()))};
            },
        .read =
            [fake](const Security::Object object, const Security::Handle handle, const std::span<std::uint8_t> buffer) {
                ++fake->reads;
                fake->object = object;
                fake->handle = handle;
                fake->buffer_size = buffer.size();
                if (fake->throw_read)
                    throw std::runtime_error("read failed");
                const auto copied = std::min(buffer.size(), static_cast<std::size_t>(fake->raw.size()));
                std::memcpy(buffer.data(), fake->raw.constData(), copied);
                return Security::NativeResult{fake->read_error,
                                              fake->used.value_or(static_cast<std::uint32_t>(fake->raw.size()))};
            }};
}
void noPublishedData(const Security::Result& result) {
    QVERIFY(result.raw_sd.isEmpty());
    QVERIFY(result.owner_sid.isEmpty());
    QCOMPARE(result.dacl, Security::Dacl::Unknown);
    QVERIFY(result.sacl_not_queried);
}
struct OwnedHandle final {
    HANDLE value{INVALID_HANDLE_VALUE};
    ~OwnedHandle() {
        if (value != INVALID_HANDLE_VALUE)
            CloseHandle(value);
    }
};
} // namespace

class AppContainerStorageSecurityTests final : public QObject {
    Q_OBJECT
  private slots:
    void constructionAndSystemBindingAreInert() {
        const auto native = Security::systemCalls();
        QVERIFY(native.size_query && native.read);
        auto fake = std::make_shared<Fake>();
        {
            const Security security(calls(fake));
        }
        QCOMPARE(fake->queries, 0U);
        QCOMPARE(fake->reads, 0U);
    }
    void fileAndKeyReadsPreserveBoundedRawAndOwnerWithoutPolicyApproval_data() {
        QTest::addColumn<bool>("key");
        QTest::newRow("file") << false;
        QTest::newRow("key fake only") << true;
    }
    void fileAndKeyReadsPreserveBoundedRawAndOwnerWithoutPolicyApproval() {
        QFETCH(bool, key);
        auto fake = std::make_shared<Fake>();
        Security security(calls(fake));
        const auto object = key ? Security::Object::Key : Security::Object::File;
        const auto result = security.read(object, fake_handle, 4096);
        QCOMPARE(result.error, Security::Error::None);
        QCOMPARE(result.query_error, static_cast<std::uint32_t>(ERROR_INSUFFICIENT_BUFFER));
        QCOMPARE(result.read_error, 0U);
        QCOMPARE(result.raw_sd, fake->raw);
        QCOMPARE(result.owner_sid, owner());
        QCOMPARE(result.dacl, Security::Dacl::Present);
        QVERIFY(result.dacl_protected && result.sacl_not_queried);
        QCOMPARE(result.required_bytes, static_cast<std::uint32_t>(fake->raw.size()));
        QCOMPARE(result.used_bytes, result.required_bytes);
        QCOMPARE(fake->queries, 1U);
        QCOMPARE(fake->reads, 1U);
        QCOMPARE(fake->object, object);
        QCOMPARE(fake->handle, fake_handle);
        QCOMPARE(fake->buffer_size, static_cast<std::size_t>(fake->raw.size()));
    }
    void absentNullEmptyAndOpaqueDaclsRemainDistinct_data() {
        QTest::addColumn<int>("kind");
        for (int kind = 0; kind < 6; ++kind)
            QTest::newRow(qPrintable(QString::number(kind))) << kind;
    }
    void absentNullEmptyAndOpaqueDaclsRemainDistinct() {
        QFETCH(int, kind);
        auto fake = std::make_shared<Fake>();
        auto expected = Security::Dacl::Present;
        if (kind == 0)
            expected = Security::Dacl::Absent;
        if (kind == 1)
            expected = Security::Dacl::Null;
        fake->raw = descriptor(expected, kind == 3, kind != 4, kind != 4);
        if (kind == 2) {
            const auto offset = get32(fake->raw, 16);
            fake->raw.resize(static_cast<qsizetype>(offset) + 8);
            put16(fake->raw, offset + 2, 8);
            put16(fake->raw, offset + 4, 0);
        }
        if (kind == 5)
            put16(fake->raw, 2, self_relative | dacl_present | 0x10); // SACL bit, no queried SACL bytes.
        Security security(calls(fake));
        const auto result = security.read(Security::Object::Key, fake_handle, 4096);
        QCOMPARE(result.error, Security::Error::None);
        QCOMPARE(result.dacl, expected);
        QCOMPARE(result.raw_sd, fake->raw);
        QCOMPARE(result.owner_sid, kind == 4 ? QByteArray{} : owner());
        QCOMPARE(result.dacl_protected, kind != 5);
        QVERIFY(result.sacl_not_queried);
    }
    void ownerAndGroupMayAliasTheSameBoundedSid() {
        auto fake = std::make_shared<Fake>();
        fake->raw = descriptor(Security::Dacl::Absent, false, true, false);
        put32(fake->raw, 8, get32(fake->raw, 4));
        Security security(calls(fake));
        const auto result = security.read(Security::Object::File, fake_handle, 4096);
        QCOMPARE(result.error, Security::Error::None);
        QCOMPARE(result.owner_sid, owner());
        QCOMPARE(result.raw_sd, fake->raw);
    }
    void invalidInputsAndMissingCallsDoNotInvokeThePort_data() {
        QTest::addColumn<int>("kind");
        for (int kind = 0; kind < 5; ++kind)
            QTest::newRow(qPrintable(QString::number(kind))) << kind;
    }
    void invalidInputsAndMissingCallsDoNotInvokeThePort() {
        QFETCH(int, kind);
        auto fake = std::make_shared<Fake>();
        auto injected = calls(fake);
        if (kind == 2)
            injected.size_query = {};
        if (kind == 3)
            injected.read = {};
        Security security(std::move(injected));
        const auto result = security.read(kind == 4 ? static_cast<Security::Object>(99) : Security::Object::File,
                                          kind == 0   ? 0
                                          : kind == 1 ? std::numeric_limits<std::uintptr_t>::max()
                                                      : fake_handle,
                                          4096);
        QCOMPARE(result.error, kind < 2   ? Security::Error::InvalidHandle
                               : kind < 4 ? Security::Error::MissingCall
                                          : Security::Error::InvalidObject);
        QCOMPARE(fake->queries, 0U);
        QCOMPARE(fake->reads, 0U);
        noPublishedData(result);
    }
    void sizeAndReadFailuresPreserveNativeCodesWithoutRetry_data() {
        QTest::addColumn<int>("kind");
        for (int kind = 0; kind < 5; ++kind)
            QTest::newRow(qPrintable(QString::number(kind))) << kind;
    }
    void sizeAndReadFailuresPreserveNativeCodesWithoutRetry() {
        QFETCH(int, kind);
        auto fake = std::make_shared<Fake>();
        if (kind == 0)
            fake->query_error = ERROR_ACCESS_DENIED;
        if (kind == 1)
            fake->query_error = ERROR_SUCCESS; // A zero-buffer success is not the size-query contract.
        if (kind == 2)
            fake->read_error = ERROR_ACCESS_DENIED;
        if (kind == 3) {
            fake->read_error = ERROR_INSUFFICIENT_BUFFER;
            fake->used = 8192;
        }
        if (kind == 4)
            fake->read_error = Security::not_called;
        Security security(calls(fake));
        const auto result = security.read(Security::Object::Key, fake_handle, 4096);
        QCOMPARE(result.error, kind < 2 ? Security::Error::SizeQuery : Security::Error::Read);
        QCOMPARE(result.query_error, fake->query_error);
        QCOMPARE(result.read_error, kind < 2 ? Security::not_called : fake->read_error);
        QCOMPARE(fake->queries, 1U);
        QCOMPARE(fake->reads, kind < 2 ? 0U : 1U);
        noPublishedData(result);
    }
    void capsAreCheckedBeforeAllocatingTheReadResponse_data() {
        QTest::addColumn<int>("kind");
        for (int kind = 0; kind < 5; ++kind)
            QTest::newRow(qPrintable(QString::number(kind))) << kind;
    }
    void capsAreCheckedBeforeAllocatingTheReadResponse() {
        QFETCH(int, kind);
        auto fake = std::make_shared<Fake>();
        std::uint64_t budget = 4096;
        if (kind == 0)
            budget = 0;
        if (kind == 1)
            budget = 19;
        if (kind == 2)
            fake->required = 0;
        if (kind == 3)
            fake->required = static_cast<std::uint32_t>(Security::maximum_sd_bytes + 1);
        if (kind == 4)
            budget = static_cast<std::uint64_t>(fake->raw.size() - 1);
        Security security(calls(fake));
        const auto result = security.read(Security::Object::File, fake_handle, budget);
        QCOMPARE(result.error, Security::Error::Bounds);
        QCOMPARE(fake->queries, kind < 2 ? 0U : 1U);
        QCOMPARE(fake->reads, 0U);
        noPublishedData(result);
    }
    void ownerCopyIsIncludedInTheBudgetBeforeItIsPublished_data() {
        QTest::addColumn<bool>("enough");
        QTest::newRow("raw alone is insufficient") << false;
        QTest::newRow("exact raw plus owner") << true;
    }
    void ownerCopyIsIncludedInTheBudgetBeforeItIsPublished() {
        QFETCH(bool, enough);
        auto fake = std::make_shared<Fake>();
        Security security(calls(fake));
        const auto cap = static_cast<std::uint64_t>(fake->raw.size() + (enough ? owner().size() : 0));
        const auto result = security.read(Security::Object::File, fake_handle, cap);
        QCOMPARE(result.error, enough ? Security::Error::None : Security::Error::Bounds);
        QCOMPARE(fake->queries, 1U);
        QCOMPARE(fake->reads, 1U);
        if (enough) {
            QCOMPARE(result.raw_sd, fake->raw);
            QCOMPARE(result.owner_sid, owner());
        } else
            noPublishedData(result);
    }
    void hardRawLimitIsInclusiveAndOnlyUsedBytesAreCopied() {
        auto fake = std::make_shared<Fake>();
        fake->raw.resize(static_cast<qsizetype>(Security::maximum_sd_bytes), '\0');
        Security security(calls(fake));
        const auto result =
            security.read(Security::Object::File, fake_handle, Security::maximum_sd_bytes + owner().size());
        QCOMPARE(result.error, Security::Error::None);
        QCOMPARE(result.raw_sd.size(), static_cast<qsizetype>(Security::maximum_sd_bytes));
        QCOMPARE(result.owner_sid, owner());
        fake->raw = descriptor();
        fake->required = static_cast<std::uint32_t>(fake->raw.size() + 16);
        const auto short_result = security.read(Security::Object::Key, fake_handle, 4096);
        QCOMPARE(short_result.error, Security::Error::None);
        QCOMPARE(short_result.raw_sd, fake->raw);
        QCOMPARE(short_result.used_bytes, static_cast<std::uint32_t>(fake->raw.size()));
    }
    void ownerIsAnIndependentCopyRatherThanABorrowedViewIntoRawSD() {
        auto fake = std::make_shared<Fake>();
        Security security(calls(fake));
        auto result = security.read(Security::Object::Key, fake_handle, 4096);
        QCOMPARE(result.error, Security::Error::None);
        QCOMPARE(result.owner_sid, owner());
        result.raw_sd.fill('\0');
        fake->raw.fill('x');
        QCOMPARE(result.owner_sid, owner());
    }
    void malformedSelfRelativeStructuresAreRejectedBeforeAnyGetter_data() {
        QTest::addColumn<int>("kind");
        for (int kind = 0; kind < 20; ++kind)
            QTest::newRow(qPrintable(QString::number(kind))) << kind;
    }
    void malformedSelfRelativeStructuresAreRejectedBeforeAnyGetter() {
        QFETCH(int, kind);
        auto fake = std::make_shared<Fake>();
        const auto owner_offset = get32(fake->raw, 4);
        const auto group_offset = get32(fake->raw, 8);
        const auto acl_offset = get32(fake->raw, 16);
        switch (kind) {
        case 0:
            fake->used = 19;
            break;
        case 1:
            fake->used = static_cast<std::uint32_t>(fake->raw.size() + 1);
            break;
        case 2:
            fake->raw[0] = 2;
            break;
        case 3:
            put16(fake->raw, 2, dacl_present);
            break;
        case 4:
            put32(fake->raw, 4, 4);
            break;
        case 5:
            put32(fake->raw, 4, 0xfffffff0U);
            break;
        case 6:
            put32(fake->raw, 4, owner_offset + 1);
            break;
        case 7:
            fake->raw[owner_offset] = 2;
            break;
        case 8:
            fake->raw[owner_offset + 1] = 16;
            break;
        case 9:
            put32(fake->raw, 8, static_cast<std::uint32_t>(fake->raw.size() - 4));
            break;
        case 10:
            fake->raw[group_offset] = 0;
            break;
        case 11:
            put32(fake->raw, 12, acl_offset);
            break; // SACL bytes were not requested.
        case 12:
            put16(fake->raw, 2, self_relative);
            break; // Nonzero DACL offset without PRESENT.
        case 13:
            put32(fake->raw, 16, 8);
            break;
        case 14:
            put16(fake->raw, acl_offset + 2, 7);
            break;
        case 15:
            put16(fake->raw, acl_offset + 2, 0xffff);
            break;
        case 16:
            put16(fake->raw, acl_offset + 4, 2);
            break;
        case 17:
            put16(fake->raw, acl_offset + 8 + 2, 0);
            break;
        case 18:
            put16(fake->raw, acl_offset + 8 + 2, 0xffff);
            break;
        case 19:
            fake->raw[acl_offset] = 0;
            break;
        }
        Security security(calls(fake));
        const auto result = security.read(Security::Object::Key, fake_handle, 4096);
        QCOMPARE(result.error, Security::Error::InvalidSD);
        QCOMPARE(fake->queries, 1U);
        QCOMPARE(fake->reads, 1U);
        noPublishedData(result);
    }
    void exceptionsNeverRetryOrPublishPartialDescriptors_data() {
        QTest::addColumn<bool>("duringRead");
        QTest::newRow("size") << false;
        QTest::newRow("read") << true;
    }
    void exceptionsNeverRetryOrPublishPartialDescriptors() {
        QFETCH(bool, duringRead);
        auto fake = std::make_shared<Fake>();
        fake->throw_query = !duringRead;
        fake->throw_read = duringRead;
        Security security(calls(fake));
        const auto result = security.read(Security::Object::File, fake_handle, 4096);
        QCOMPARE(result.error, Security::Error::Exception);
        QCOMPARE(fake->queries, 1U);
        QCOMPARE(fake->reads, duringRead ? 1U : 0U);
        noPublishedData(result);
    }
    void nativeFileAndDirectorySecurityAreReadWithoutClosingBorrowedHandles_data() {
        QTest::addColumn<bool>("directory");
        QTest::newRow("owned TEMP file") << false;
        QTest::newRow("owned TEMP directory") << true;
    }
    void nativeFileAndDirectorySecurityAreReadWithoutClosingBorrowedHandles() {
        QFETCH(bool, directory);
        QTemporaryDir temporary(QDir::tempPath() + QStringLiteral("/RC Music Storage Security 日本-XXXXXX"));
        QVERIFY(temporary.isValid());
        const auto path = directory ? temporary.filePath(QStringLiteral("directory"))
                                    : temporary.filePath(QStringLiteral("file.bin"));
        const QByteArray contents{"untouched owned fixture"};
        if (directory)
            QVERIFY(QDir{}.mkdir(path));
        else {
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::NewOnly));
            QCOMPARE(file.write(contents), contents.size());
        }
        const auto native = QDir::toNativeSeparators(path).toStdWString();
        const OwnedHandle handle{
            CreateFileW(native.c_str(), READ_CONTROL | FILE_READ_ATTRIBUTES,
                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                        FILE_FLAG_OPEN_REPARSE_POINT | (directory ? FILE_FLAG_BACKUP_SEMANTICS : 0), nullptr)};
        QVERIFY2(handle.value != INVALID_HANDLE_VALUE, qPrintable(QString::number(GetLastError())));
        FILE_ID_INFO before{};
        QVERIFY(GetFileInformationByHandleEx(handle.value, FileIdInfo, &before, sizeof(before)));
        Security::Result result;
        {
            Security security(Security::systemCalls());
            result = security.read(Security::Object::File, reinterpret_cast<Security::Handle>(handle.value),
                                   Security::maximum_sd_bytes + Security::maximum_sid_bytes);
        }
        QCOMPARE(result.error, Security::Error::None);
        QCOMPARE(result.query_error, static_cast<std::uint32_t>(ERROR_INSUFFICIENT_BUFFER));
        QCOMPARE(result.read_error, 0U);
        QVERIFY(!result.raw_sd.isEmpty() && !result.owner_sid.isEmpty());
        QVERIFY(result.dacl != Security::Dacl::Unknown && result.sacl_not_queried);
        FILE_ID_INFO after{};
        QVERIFY(GetFileInformationByHandleEx(handle.value, FileIdInfo, &after, sizeof(after)));
        QCOMPARE(before.VolumeSerialNumber, after.VolumeSerialNumber);
        QCOMPARE(std::memcmp(before.FileId.Identifier, after.FileId.Identifier, sizeof(before.FileId.Identifier)), 0);
        if (directory)
            QVERIFY(QDir(path).entryList(QDir::NoDotAndDotDot | QDir::AllEntries).isEmpty());
        else {
            QFile file(path);
            QVERIFY(file.open(QIODevice::ReadOnly));
            QCOMPARE(file.readAll(), contents);
        }
    }
};

QTEST_GUILESS_MAIN(AppContainerStorageSecurityTests)
#include "AppContainerStorageSecurityTests.moc"
