#include "adapters/SignedYoutubeRuntimeManifest.hpp"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

#include <windows.h>

#include <bcrypt.h>

using namespace music_player::m0;

namespace {
// A fresh P-256 key per test run: no private key is ever stored in the repository.
class SigningKey final {
  public:
    SigningKey() {
        BCryptOpenAlgorithmProvider(&algorithm_, BCRYPT_ECDSA_P256_ALGORITHM, nullptr, 0);
        BCryptGenerateKeyPair(algorithm_, &key_, 256, 0);
        BCryptFinalizeKeyPair(key_, 0);
        ULONG size = 0;
        BCryptExportKey(key_, nullptr, BCRYPT_ECCPUBLIC_BLOB, nullptr, 0, &size, 0);
        QByteArray blob(static_cast<qsizetype>(size), '\0');
        BCryptExportKey(key_, nullptr, BCRYPT_ECCPUBLIC_BLOB, reinterpret_cast<PUCHAR>(blob.data()), size, &size, 0);
        public_key_ = blob.mid(sizeof(BCRYPT_ECCKEY_BLOB)); // X||Y
    }
    ~SigningKey() {
        if (key_)
            BCryptDestroyKey(key_);
        if (algorithm_)
            BCryptCloseAlgorithmProvider(algorithm_, 0);
    }
    SigningKey(const SigningKey&) = delete;
    SigningKey& operator=(const SigningKey&) = delete;
    [[nodiscard]] QByteArray publicKey() const { return public_key_; }
    [[nodiscard]] QByteArray sign(const QByteArray& message) const {
        auto hash = QCryptographicHash::hash(message, QCryptographicHash::Sha256);
        QByteArray signature(64, '\0');
        ULONG size = 0;
        BCryptSignHash(key_, nullptr, reinterpret_cast<PUCHAR>(hash.data()), static_cast<ULONG>(hash.size()),
                       reinterpret_cast<PUCHAR>(signature.data()), 64, &size, 0);
        return signature;
    }

  private:
    BCRYPT_ALG_HANDLE algorithm_{};
    BCRYPT_KEY_HANDLE key_{};
    QByteArray public_key_{};
};

QString pin(char c) { return QString(64, QLatin1Char(c)); }
QJsonObject artifact(const QString& role, const QString& path, char hash, qint64 bytes = 10) {
    return {{"role", role}, {"path", path}, {"bytes", bytes}, {"sha256", pin(hash)}};
}
QJsonObject manifest(qint64 sequence = 3) {
    return {{"schemaVersion", 1},
            {"purpose", "rcmusic-youtube-runtime-v1"},
            {"sequence", sequence},
            {"ytDlpVersion", "2026.10.26"},
            {"artifacts", QJsonArray{artifact("Extractor", "extractor/yt-dlp.exe", 'a'),
                                     artifact("Node", "javascript/node.exe", 'b'),
                                     artifact("EjsCore", "extractor/_internal/yt_dlp_ejs/yt/solver/core.min.js", 'c'),
                                     artifact("EjsLibrary", "extractor/_internal/yt_dlp_ejs/yt/solver/lib.min.js", 'd'),
                                     artifact("Support", "extractor/_internal/python314.dll", 'e'),
                                     artifact("Support", "notices/yt-dlp-LICENSE.txt", 'f')}}};
}
QByteArray bytes(const QJsonObject& object) { return QJsonDocument(object).toJson(QJsonDocument::Compact); }
const QString prefix = QStringLiteral("download-tools/youtube/");
} // namespace

class SignedYoutubeRuntimeManifestTests final : public QObject {
    Q_OBJECT
  private slots:
    void signedManifestBecomesTheRuntimeInventory() {
        SigningKey key;
        const auto document = bytes(manifest());
        const auto result =
            verifySignedYoutubeRuntimeManifest(document, key.sign(document), key.publicKey(), 1, prefix);
        QCOMPARE(result.error, SignedYoutubeRuntimeManifestError::None);
        QVERIFY(result.manifest);
        QCOMPARE(result.manifest->sequence, quint64{3});
        QCOMPARE(result.manifest->yt_dlp_version, QStringLiteral("2026.10.26"));
        QCOMPARE(result.manifest->artifacts.size(), std::size_t{6});
        const auto& extractor = result.manifest->artifacts.front();
        QCOMPARE(extractor.role, WindowsYoutubeRuntimeRole::Extractor);
        QCOMPARE(extractor.relative_path, QStringLiteral("extractor/yt-dlp.exe"));
        QCOMPARE(extractor.source_path, QStringLiteral("download-tools/youtube/extractor/yt-dlp.exe"));
        QCOMPARE(extractor.sha256, pin('a').toLatin1());
        QCOMPARE(result.manifest->artifacts.back().role, WindowsYoutubeRuntimeRole::Support);
    }
    void acceptsWhatTheRepositorySigningToolProduces() {
        // Produced once by tools/youtube-resolver/RcMusicRuntimeSigning.psm1 (.NET ECDsa) with a
        // throwaway key that was deleted right after; only its public half is kept here.
        const QByteArray manifest =
            "{\"schemaVersion\":1,\"purpose\":\"rcmusic-youtube-runtime-v1\",\"sequence\":1,\"ytDlpVersion\":\"202"
            "6.11.01\",\"artifacts\":[{\"role\":\"EjsCore\",\"path\":\"extractor/_internal/yt_dlp_ejs/yt/solver/c"
            "ore.min.js\",\"bytes\":52,\"sha256\":\"3409221fd03cdfc738c6bd7e4c488b263a4e13776a93c1750c3e67f83"
            "7e4a791\"},{\"role\":\"EjsLibrary\",\"path\":\"extractor/_internal/yt_dlp_ejs/yt/solver/lib.min.js"
            "\",\"bytes\":51,\"sha256\":\"45b5999e86e154481cb0a26fb934db8b0850cb56b331e5839552a47deb7271bc\"},"
            "{\"role\":\"Extractor\",\"path\":\"extractor/yt-dlp.exe\",\"bytes\":20,\"sha256\":\"e057b19926b3a0ec580"
            "5ec658148a4622bfb84afad8c73742c754aaf2c25d0cb\"},{\"role\":\"Node\",\"path\":\"javascript/node.exe"
            "\",\"bytes\":19,\"sha256\":\"4fbadcd8bb19c2d3b04de347d5be90e29f54adc08c9da7f6477a08094f730813\"}]"
            "}";
        const auto public_key = QByteArray::fromHex("a36edf231bceb3cf79372c9081ce2b78944f3209751b73c4174a4565447717ce"
                                                    "e84e058948729f253e726865f2a9e1e52e26952d493a9b8ad274526873d3e867");
        const auto signature = QByteArray::fromHex("ae1036f99d52935aabfeed6a4889a83e809a951997efa7845c95b992d3f2338f"
                                                   "fc6407f5a7bd36cc41b1e71b58bac5568eb92442806e0f814afccb45c099071b");
        const auto result = verifySignedYoutubeRuntimeManifest(manifest, signature, public_key, 1, prefix);
        QCOMPARE(result.error, SignedYoutubeRuntimeManifestError::None);
        QVERIFY(result.manifest);
        QCOMPARE(result.manifest->artifacts.size(), std::size_t{4});
        QCOMPARE(result.manifest->yt_dlp_version, QStringLiteral("2026.11.01"));
    }
    void anyChangeToTheSignedBytesIsRefusedBeforeReadingThem() {
        SigningKey key;
        SigningKey other;
        const auto document = bytes(manifest());
        const auto signature = key.sign(document);
        auto tampered = document;
        tampered.replace("aaaa", "aaab");
        auto flipped = signature;
        flipped[10] = static_cast<char>(flipped[10] ^ 0x01);
        for (const auto& [text, sig, public_key] :
             {std::tuple{tampered, signature, key.publicKey()}, std::tuple{document, flipped, key.publicKey()},
              std::tuple{document, signature, other.publicKey()},
              std::tuple{document, signature.left(63), key.publicKey()},
              std::tuple{QByteArray("not json"), signature, key.publicKey()}}) {
            const auto result = verifySignedYoutubeRuntimeManifest(text, sig, public_key, 1, prefix);
            QCOMPARE(result.error, SignedYoutubeRuntimeManifestError::BadSignature);
            QVERIFY(!result.manifest);
        }
    }
    void invalidPublicKeysAreRefused() {
        SigningKey key;
        const auto document = bytes(manifest());
        for (const auto& public_key : {QByteArray{}, key.publicKey().left(63), QByteArray(64, '\0')}) {
            const auto result = verifySignedYoutubeRuntimeManifest(document, key.sign(document), public_key, 1, prefix);
            QCOMPARE(result.error, SignedYoutubeRuntimeManifestError::InvalidKey);
        }
    }
    void olderSequencesThanTheCompiledMinimumAreSuperseded() {
        SigningKey key;
        const auto document = bytes(manifest(3));
        QCOMPARE(verifySignedYoutubeRuntimeManifest(document, key.sign(document), key.publicKey(), 3, prefix).error,
                 SignedYoutubeRuntimeManifestError::None);
        const auto old = verifySignedYoutubeRuntimeManifest(document, key.sign(document), key.publicKey(), 4, prefix);
        QCOMPARE(old.error, SignedYoutubeRuntimeManifestError::Superseded);
        QVERIFY(!old.manifest);
    }
    void oversizedDocumentsAreRefusedBeforeHashing() {
        SigningKey key;
        const QByteArray huge(youtube_runtime_manifest_max_bytes + 1, ' ');
        QCOMPARE(verifySignedYoutubeRuntimeManifest(huge, key.sign(huge), key.publicKey(), 1, prefix).error,
                 SignedYoutubeRuntimeManifestError::Malformed);
    }
    void signedButInconsistentInventoriesAreRefused_data() {
        QTest::addColumn<QJsonObject>("document");
        auto with = [](const QString& key, const QJsonValue& value) {
            auto object = manifest();
            object.insert(key, value);
            return object;
        };
        auto artifacts = [&](const QJsonArray& list) { return with("artifacts", list); };
        const auto base = manifest().value("artifacts").toArray();
        auto replaced = [&](int index, const QJsonObject& value) {
            auto list = base;
            list[index] = value;
            return artifacts(list);
        };
        auto appended = [&](const QJsonObject& value) {
            auto list = base;
            list.append(value);
            return artifacts(list);
        };
        QTest::newRow("schema") << with("schemaVersion", 2);
        QTest::newRow("purpose") << with("purpose", "production-candidate-v1");
        QTest::newRow("zero sequence") << with("sequence", 0);
        QTest::newRow("fractional sequence") << with("sequence", 1.5);
        QTest::newRow("extra field") << with("extra", true);
        QTest::newRow("empty version") << with("ytDlpVersion", "");
        QTest::newRow("no artifacts") << artifacts({});
        QTest::newRow("missing node") << artifacts(QJsonArray{base[0], base[2], base[3], base[4]});
        QTest::newRow("duplicate extractor") << appended(artifact("Extractor", "extractor/yt-dlp.exe", 'a'));
        QTest::newRow("moved extractor") << replaced(0, artifact("Extractor", "extractor/other.exe", 'a'));
        QTest::newRow("unknown role") << replaced(4, artifact("Plugin", "extractor/_internal/x.dll", 'e'));
        QTest::newRow("uppercase pin") << replaced(4, QJsonObject{{"role", "Support"},
                                                                  {"path", "extractor/_internal/python314.dll"},
                                                                  {"bytes", 10},
                                                                  {"sha256", QString(64, 'A')}});
        QTest::newRow("short pin") << replaced(4, QJsonObject{{"role", "Support"},
                                                              {"path", "extractor/_internal/python314.dll"},
                                                              {"bytes", 10},
                                                              {"sha256", QString(63, 'e')}});
        QTest::newRow("parent segment") << appended(artifact("Support", "extractor/../x.dll", 'e'));
        QTest::newRow("absolute") << appended(artifact("Support", "/extractor/x.dll", 'e'));
        QTest::newRow("backslash") << appended(artifact("Support", "extractor\\x.dll", 'e'));
        QTest::newRow("drive") << appended(artifact("Support", "C:/x.dll", 'e'));
        QTest::newRow("duplicate path ignoring case")
            << appended(artifact("Support", "Extractor/_internal/PYTHON314.dll", 'e'));
        QTest::newRow("file over 128 MiB")
            << replaced(4, artifact("Support", "extractor/_internal/python314.dll", 'e', 128LL * 1024 * 1024 + 1));
        QTest::newRow("artifact extra field") << replaced(5, QJsonObject{{"role", "Support"},
                                                                         {"path", "notices/yt-dlp-LICENSE.txt"},
                                                                         {"bytes", 10},
                                                                         {"sha256", pin('f')},
                                                                         {"url", "https://example.test"}});
    }
    void signedButInconsistentInventoriesAreRefused() {
        QFETCH(QJsonObject, document);
        SigningKey key;
        const auto text = bytes(document);
        const auto result = verifySignedYoutubeRuntimeManifest(text, key.sign(text), key.publicKey(), 1, prefix);
        QCOMPARE(result.error, SignedYoutubeRuntimeManifestError::Malformed);
        QVERIFY(!result.manifest);
    }
};

QTEST_GUILESS_MAIN(SignedYoutubeRuntimeManifestTests)
#include "SignedYoutubeRuntimeManifestTests.moc"
