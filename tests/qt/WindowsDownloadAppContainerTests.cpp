#include "adapters/WindowsDownloadAppContainer.hpp"

#include <QTest>

using namespace music_player::m0;
namespace {
// A synthetic SID-shaped value for parsing/ACL tests only. It does not name a
// provisioned profile and must never be used by an ordinary launch test.
QByteArray syntheticProfileSid() {
    QByteArray bytes(40, '\0');
    bytes[0] = 1;
    bytes[1] = 8;
    bytes[7] = 15;
    bytes[8] = 2;
    for (int index = 0; index < 7; ++index)
        bytes[12 + 4 * index] = static_cast<char>(index + 1);
    return bytes;
}
} // namespace

class WindowsDownloadAppContainerTests final : public QObject {
    Q_OBJECT
  private slots:
    void acceptsAndRetainsExactBinaryProfileSid() {
        const auto bytes = syntheticProfileSid();
        const auto identity = WindowsDownloadAppContainer::fromTrustedSid(bytes);
        QVERIFY(identity);
        QCOMPARE(identity->sidBytes(), bytes);
    }
    void ownsBorrowedByteArrayStorage() {
        auto storage = syntheticProfileSid();
        const QByteArray expected{storage.constData(), storage.size()};
        const auto borrowed = QByteArray::fromRawData(storage.constData(), storage.size());
        const auto identity = WindowsDownloadAppContainer::fromTrustedSid(borrowed);
        QVERIFY(identity);
        storage[12] = 99;
        QCOMPARE(identity->sidBytes(), expected);
        QCOMPARE(borrowed.at(12), char{99});
    }
    void rejectsOtherSidKindsAndMalformedBinary_data() {
        QTest::addColumn<QByteArray>("bytes");
        QTest::newRow("empty") << QByteArray{};
        QTest::newRow("SID text is not binary") << QByteArrayLiteral("S-1-15-2-1-2-3-4-5-6-7");
        const auto valid = syntheticProfileSid();
        QTest::newRow("short header") << valid.first(7);
        QTest::newRow("truncated subauthority") << valid.first(valid.size() - 1);
        QTest::newRow("trailing data") << valid + '\0';
        QTest::newRow("oversized") << QByteArray(65'536, '\0');
        for (const auto revision : {0, 2}) {
            auto bytes = valid;
            bytes[0] = static_cast<char>(revision);
            QTest::newRow(qPrintable(QStringLiteral("revision-%1").arg(revision))) << bytes;
        }
        for (const auto count : {0, 7, 9, 255}) {
            auto bytes = valid;
            bytes[1] = static_cast<char>(count);
            QTest::newRow(qPrintable(QStringLiteral("subauthority-count-%1").arg(count))) << bytes;
        }
        auto authority = valid;
        authority[7] = 5;
        QTest::newRow("NT authority") << authority;
        authority = valid;
        authority[2] = 1;
        QTest::newRow("different high authority byte") << authority;
        for (const auto base : {0, 1, 3}) {
            auto bytes = valid;
            bytes[8] = static_cast<char>(base);
            QTest::newRow(qPrintable(QStringLiteral("not-profile-base-%1").arg(base))) << bytes;
        }
    }
    void rejectsOtherSidKindsAndMalformedBinary() {
        QFETCH(QByteArray, bytes);
        QVERIFY(!WindowsDownloadAppContainer::fromTrustedSid(bytes));
    }
};

QTEST_GUILESS_MAIN(WindowsDownloadAppContainerTests)
#include "WindowsDownloadAppContainerTests.moc"
