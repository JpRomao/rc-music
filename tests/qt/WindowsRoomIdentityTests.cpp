#include "adapters/WindowsRoomIdentity.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

#include <map>

using namespace music_player::m0;
namespace {
const QStringList names = {"room-identity-cert.pem", "room-identity-key.pem", "room-identity-pass.bin"};
QString cert(const QString& d) { return QDir(d).filePath(names[0]); }
QString key(const QString& d) { return QDir(d).filePath(names[1]); }
QString pass(const QString& d) { return QDir(d).filePath(names[2]); }
bool write(const QString& path, const QByteArray& bytes) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(bytes) == bytes.size();
}
QByteArray read(const QString& path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}
std::map<QString, QByteArray> snapshot(const QString& d) {
    std::map<QString, QByteArray> files;
    for (const auto& name : QDir(d).entryList(QDir::Files))
        files[name] = read(QDir(d).filePath(name));
    return files;
}
QByteArray pemBody(const QByteArray& pem) {
    QByteArray body;
    for (const auto& line : pem.split('\n'))
        if (!line.trimmed().startsWith("-----"))
            body += line.trimmed();
    return QByteArray::fromBase64(body);
}
QByteArray flipped(QByteArray bytes, const qsizetype at) {
    bytes[at] = static_cast<char>(bytes[at] ^ 0x04);
    return bytes;
}
// Flip one base64 character in the middle of a PEM body so the structure changes but stays base64.
QByteArray flippedPem(QByteArray pem) {
    const auto at = pem.size() / 2;
    pem[at] = pem[at] == 'A' ? 'B' : 'A';
    return pem;
}
// DER prefixes of an unencrypted EC key: SEC1 ECPrivateKey (version 1, 32-byte scalar) and the
// PKCS#8 PrivateKeyInfo header (version 0, id-ecPublicKey).
const QByteArray sec1Prefix = QByteArray::fromHex("020101"
                                                  "0420");
const QByteArray pkcs8Prefix = QByteArray::fromHex("020100"
                                                   "3013"
                                                   "0607"
                                                   "2a8648ce3d0201");
const QByteArray prime256v1 = QByteArray::fromHex("06082a8648ce3d030107");
} // namespace

class WindowsRoomIdentityTests final : public QObject {
    Q_OBJECT
  private slots:
    void createsTheIdentityAndReloadsTheSameOne() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const auto config = dir.path() + "/config/rc-music";
        const auto first = loadOrCreateRoomIdentity(config);
        QVERIFY(first.has_value());
        for (const auto& name : names)
            QVERIFY2(QFile::exists(QDir(config).filePath(name)), qPrintable(name));
        QVERIFY(QFileInfo(first->certificate_file).isAbsolute());
        QCOMPARE(QFileInfo(first->certificate_file).canonicalFilePath(), QFileInfo(cert(config)).canonicalFilePath());
        QCOMPARE(QFileInfo(first->key_file).canonicalFilePath(), QFileInfo(key(config)).canonicalFilePath());
        QCOMPARE(QByteArray::fromStdString(first->certificate_pem), read(cert(config)));
        QCOMPARE(QByteArray::fromStdString(first->key_pem), read(key(config)));
        QVERIFY(first->certificate_pem.starts_with("-----BEGIN CERTIFICATE-----"));
        QVERIFY(first->key_pem.starts_with("-----BEGIN ENCRYPTED PRIVATE KEY-----"));
        QCOMPARE(first->key_passphrase.size(), std::size_t{43});
        QCOMPARE(QByteArray::fromBase64(QByteArray::fromStdString(first->key_passphrase),
                                        QByteArray::Base64UrlEncoding | QByteArray::AbortOnBase64DecodingErrors)
                     .size(),
                 32);

        // The fingerprint is the SHA-256 of the certificate DER, an ECDSA P-256 certificate.
        const auto der = pemBody(read(cert(config)));
        QVERIFY(der.contains(prime256v1));
        const auto digest = QCryptographicHash::hash(der, QCryptographicHash::Sha256);
        QCOMPARE(QByteArray(reinterpret_cast<const char*>(first->fingerprint.data()), 32), digest);

        const auto files = snapshot(config);
        const auto second = loadOrCreateRoomIdentity(config);
        QVERIFY(second.has_value());
        QCOMPARE(second->fingerprint, first->fingerprint);
        QCOMPARE(second->certificate_pem, first->certificate_pem);
        QCOMPARE(second->key_pem, first->key_pem);
        QCOMPARE(second->key_passphrase, first->key_passphrase);
        QCOMPARE(snapshot(config), files);
    }
    void anotherDirectoryGetsAnotherIdentity() {
        QTemporaryDir a, b;
        const auto x = loadOrCreateRoomIdentity(a.path());
        const auto y = loadOrCreateRoomIdentity(b.path());
        QVERIFY(x && y);
        QVERIFY(x->fingerprint != y->fingerprint);
        QVERIFY(x->key_passphrase != y->key_passphrase);
    }
    void noFileHoldsThePrivateKeyInClear() {
        QTemporaryDir dir;
        const auto identity = loadOrCreateRoomIdentity(dir.path());
        QVERIFY(identity.has_value());
        const auto encrypted = pemBody(read(key(dir.path())));
        // EncryptedPrivateKeyInfo: PBES2 / PBKDF2 / hmacWithSHA256 / aes256-CBC.
        QVERIFY(encrypted.contains(QByteArray::fromHex("06092a864886f70d01050d")));
        QVERIFY(encrypted.contains(QByteArray::fromHex("06092a864886f70d01050c")));
        QVERIFY(encrypted.contains(QByteArray::fromHex("06082a864886f70d0209")));
        QVERIFY(encrypted.contains(QByteArray::fromHex("060960864801650304012a")));
        const auto passphrase =
            QByteArray::fromBase64(QByteArray::fromStdString(identity->key_passphrase), QByteArray::Base64UrlEncoding);
        for (const auto& [name, bytes] : snapshot(dir.path())) {
            QVERIFY2(!bytes.contains("PRIVATE KEY-----") || bytes.contains("BEGIN ENCRYPTED PRIVATE KEY"),
                     qPrintable(name));
            QVERIFY2(!bytes.contains("BEGIN EC PRIVATE KEY"), qPrintable(name));
            QVERIFY2(!bytes.contains(identity->key_passphrase.c_str()), qPrintable(name));
            QVERIFY2(!bytes.contains(passphrase), qPrintable(name));
            for (const auto& candidate : {bytes, pemBody(bytes)}) {
                QVERIFY2(!candidate.contains(sec1Prefix), qPrintable(name));
                QVERIFY2(!candidate.contains(pkcs8Prefix), qPrintable(name));
            }
        }
        QVERIFY(read(pass(dir.path())).size() > 32);
    }
    void corruptFilesAreRejectedAndNotOverwritten_data() {
        QTest::addColumn<int>("file");
        QTest::addColumn<int>("damage");
        const char* files[] = {"certificate", "key", "passphrase"};
        const char* damages[] = {"garbage", "flipped", "truncated", "empty", "swapped"};
        for (int f = 0; f < 3; ++f)
            for (int d = 0; d < 5; ++d)
                QTest::newRow(qPrintable(QString("%1 %2").arg(files[f], damages[d]))) << f << d;
    }
    void corruptFilesAreRejectedAndNotOverwritten() {
        QFETCH(int, file);
        QFETCH(int, damage);
        QTemporaryDir dir, other;
        QVERIFY(loadOrCreateRoomIdentity(dir.path()).has_value());
        QVERIFY(loadOrCreateRoomIdentity(other.path()).has_value());
        const auto path = QDir(dir.path()).filePath(names[file]);
        const auto good = read(path);
        QByteArray bad;
        switch (damage) {
        case 0:
            bad = "not an identity file\n";
            break;
        case 1:
            bad = file == 2 ? flipped(good, good.size() / 2) : flippedPem(good);
            break;
        case 2:
            bad = good.left(good.size() / 2);
            break;
        case 3:
            break;
        default: // a valid file of another identity
            bad = read(QDir(other.path()).filePath(names[file]));
            break;
        }
        QVERIFY(bad != good);
        QVERIFY(write(path, bad));
        const auto files = snapshot(dir.path());
        QVERIFY(!loadOrCreateRoomIdentity(dir.path()).has_value());
        QCOMPARE(snapshot(dir.path()), files);
    }
    void aCertificateWithoutItsKeyOrPassphraseIsRejected() {
        for (const auto& missing : {names[1], names[2]}) {
            QTemporaryDir dir;
            QVERIFY(loadOrCreateRoomIdentity(dir.path()).has_value());
            QVERIFY(QFile::remove(QDir(dir.path()).filePath(missing)));
            const auto files = snapshot(dir.path());
            QVERIFY(!loadOrCreateRoomIdentity(dir.path()).has_value());
            QCOMPARE(snapshot(dir.path()), files);
        }
    }
    void anInterruptedCreationWithoutCertificateIsReplaced() {
        QTemporaryDir dir;
        const auto first = loadOrCreateRoomIdentity(dir.path());
        QVERIFY(first.has_value());
        QVERIFY(QFile::remove(cert(dir.path())));
        const auto second = loadOrCreateRoomIdentity(dir.path());
        QVERIFY(second.has_value());
        QVERIFY(second->fingerprint != first->fingerprint);
        QCOMPARE(loadOrCreateRoomIdentity(dir.path())->fingerprint, second->fingerprint);
    }
    void anUnusableDirectoryFails() {
        QTemporaryDir dir;
        const auto blocker = dir.filePath("file");
        QVERIFY(write(blocker, "x"));
        QVERIFY(!loadOrCreateRoomIdentity(blocker).has_value());
        QVERIFY(!loadOrCreateRoomIdentity(blocker + "/below").has_value());
        QCOMPARE(read(blocker), QByteArray("x"));
    }
};

QTEST_GUILESS_MAIN(WindowsRoomIdentityTests)
#include "WindowsRoomIdentityTests.moc"
