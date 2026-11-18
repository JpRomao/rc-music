#include "adapters/WindowsRoomEnvelopeCipher.hpp"

#include <QTest>

#include <array>
#include <cstdint>

using namespace music_player::m0;
namespace {
std::array<std::uint8_t, 16> sequence(const std::uint8_t start) {
    std::array<std::uint8_t, 16> bytes{};
    for (std::size_t i = 0; i < bytes.size(); ++i)
        bytes[i] = static_cast<std::uint8_t>(start + i * 11);
    return bytes;
}
QByteArray failingHkdf(QByteArrayView, QByteArrayView, QByteArrayView, qsizetype) { return {}; }
QByteArray shortHkdf(QByteArrayView, QByteArrayView, QByteArrayView, qsizetype) { return QByteArray(16, '\x07'); }
QByteArray zeroHkdf(QByteArrayView, QByteArrayView, QByteArrayView, const qsizetype length) {
    return QByteArray(length, '\0');
}
} // namespace

class WindowsRoomEnvelopeCipherTests final : public QObject {
    Q_OBJECT
  private slots:
    void hkdfMatchesRfc5869TestCase1() {
        const QByteArray ikm(22, '\x0b');
        const auto salt = QByteArray::fromHex("000102030405060708090a0b0c");
        const auto info = QByteArray::fromHex("f0f1f2f3f4f5f6f7f8f9");
        QCOMPARE(hkdfSha256(ikm, salt, info, 42).toHex(),
                 QByteArray("3cb25f25faacd57a90434f64d0362f2a2d2d0a90cf1a5a4c5db02d56ecc4c5bf34007208d5b887185865"));
    }
    void hkdfRejectsImpossibleLengths() {
        QVERIFY(hkdfSha256("k", {}, {}, 0).isEmpty());
        QVERIFY(hkdfSha256("k", {}, {}, 255 * 32 + 1).isEmpty());
        QCOMPARE(hkdfSha256("k", {}, {}, 255 * 32).size(), 255 * 32);
    }
    void sealAndOpenRoundTrip() {
        const WindowsRoomEnvelopeCipher cipher(sequence(1), sequence(2));
        for (const QByteArray plain :
             {QByteArray(), QByteArray("x"), QByteArray(R"({"sdp":"v=0"})"), QByteArray(70000, 'a')}) {
            const auto sealed = cipher.seal(plain);
            QCOMPARE(sealed.size(),
                     plain.size() + WindowsRoomEnvelopeCipher::nonce_size + WindowsRoomEnvelopeCipher::tag_size);
            QVERIFY(!sealed.contains(plain) || plain.size() < 2);
            const auto opened = cipher.open(sealed);
            QVERIFY(opened.has_value());
            QCOMPARE(*opened, plain);
            // A second instance with the same secret and room opens it too.
            QCOMPARE(WindowsRoomEnvelopeCipher(sequence(1), sequence(2)).open(sealed).value_or("-"), plain);
        }
    }
    void twoSealsOfTheSameTextDiffer() {
        const WindowsRoomEnvelopeCipher cipher(sequence(1), sequence(2));
        const auto a = cipher.seal("offer");
        const auto b = cipher.seal("offer");
        QVERIFY(a != b);
        QVERIFY(a.left(WindowsRoomEnvelopeCipher::nonce_size) != b.left(WindowsRoomEnvelopeCipher::nonce_size));
    }
    void anyChangedByteIsRejected() {
        const WindowsRoomEnvelopeCipher cipher(sequence(1), sequence(2));
        const auto sealed = cipher.seal("candidate:1 1 udp 2122260223 192.0.2.1 54400 typ host");
        for (qsizetype i = 0; i < sealed.size(); ++i) {
            auto altered = sealed;
            altered[i] = static_cast<char>(altered[i] ^ 0x01);
            QVERIFY2(!cipher.open(altered).has_value(), qPrintable(QString::number(i)));
        }
        QVERIFY(!cipher.open(sealed + 'x').has_value());
        QVERIFY(!cipher.open(sealed.chopped(1)).has_value());
    }
    void anotherSecretOrRoomCannotOpen() {
        const auto sealed = WindowsRoomEnvelopeCipher(sequence(1), sequence(2)).seal("answer");
        QVERIFY(!WindowsRoomEnvelopeCipher(sequence(3), sequence(2)).open(sealed).has_value());
        QVERIFY(!WindowsRoomEnvelopeCipher(sequence(1), sequence(3)).open(sealed).has_value());
    }
    void inputShorterThanNonceAndTagIsRejected() {
        const WindowsRoomEnvelopeCipher cipher(sequence(1), sequence(2));
        for (qsizetype size = 0; size < 28; ++size)
            QVERIFY(!cipher.open(QByteArray(size, '\0')).has_value());
    }
    void productionCipherUsesHkdfSha256AndIsValid() {
        const WindowsRoomEnvelopeCipher cipher(sequence(1), sequence(2));
        QVERIFY(cipher.isValid());
        const WindowsRoomEnvelopeCipher seam(sequence(1), sequence(2), &hkdfSha256);
        QVERIFY(seam.isValid());
        QCOMPARE(seam.open(cipher.seal("offer")).value_or("-"), QByteArray("offer"));
    }
    void aFailedKeyDerivationFailsClosed() {
        // A known all-zero key is exactly what a fail-open cipher would fall back to.
        const WindowsRoomEnvelopeCipher zero(sequence(1), sequence(2), &zeroHkdf);
        const auto zero_sealed = zero.seal("candidate:1 1 udp 2122260223 192.0.2.1 54400 typ host");
        QVERIFY(!zero_sealed.isEmpty());
        for (const auto derive : {&failingHkdf, &shortHkdf}) {
            const WindowsRoomEnvelopeCipher broken(sequence(1), sequence(2), derive);
            QVERIFY(!broken.isValid());
            QVERIFY(broken.seal("offer").isEmpty());
            QVERIFY(broken.seal({}).isEmpty());
            QVERIFY(!broken.open(zero_sealed).has_value());
        }
    }
};

QTEST_GUILESS_MAIN(WindowsRoomEnvelopeCipherTests)
#include "WindowsRoomEnvelopeCipherTests.moc"
