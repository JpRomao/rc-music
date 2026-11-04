#include "adapters/WindowsMediaFoundationCodec.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QTest>
#include <QtEndian>
#include <qt_windows.h>

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <vector>

using namespace music_player::m0;
namespace {
struct Fixture final {
    QTemporaryDir directory{QDir::tempPath() + QStringLiteral("/RC Music native codec 日本-XXXXXX")};
    QString path{directory.filePath(QStringLiteral("entrada com espaços e acentos.audio"))};
    QByteArray output;
    std::vector<std::uint64_t> progress;
    mutable HANDLE input{INVALID_HANDLE_VALUE};
    ~Fixture() { closeInput(); }
    void closeInput() const {
        if (input != INVALID_HANDLE_VALUE)
            CloseHandle(input);
        input = INVALID_HANDLE_VALUE;
    }
    bool save(const QByteArray& bytes) const {
        closeInput();
        QFile file{path};
        if (!directory.isValid() || !file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size())
            return false;
        file.close();
        const auto native = QDir::toNativeSeparators(path).toStdWString();
        input = CreateFileW(native.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                            nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
        return input != INVALID_HANDLE_VALUE;
    }
    MediaFoundationWorkerRequest request(MediaFoundationWorkerMode mode = MediaFoundationWorkerMode::Convert) const {
        MediaFoundationWorkerRequest result;
        result.mode = mode;
        result.input_handle = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(input));
        result.max_input_bytes = 1024 * 1024;
        result.max_output_bytes = 1024 * 1024;
        result.max_duration_us = 60 * 1000000;
        return result;
    }
    MediaFoundationCodecCallbacks callbacks() {
        return {[this](std::span<const std::uint8_t> bytes) {
                    output.append(reinterpret_cast<const char*>(bytes.data()), static_cast<qsizetype>(bytes.size()));
                    return true;
                },
                [this](std::uint64_t value) {
                    progress.push_back(value);
                    return true;
                }};
    }
};
QByteArray fixture(const QString& leaf) {
    QFile file{QString::fromUtf8(RC_MUSIC_TEST_AUDIO_FIXTURES) + '/' + leaf};
    if (!file.open(QIODevice::ReadOnly))
        return {};
    const auto bytes = file.readAll();
    const auto expected =
        leaf == "m4a-aac-lc.m4a"         ? "aefe894a1a9277bf4795d421c97d4780fa342eef300819a459ca45329a614558"
        : leaf == "wav-pcm-mono.wav"     ? "03338fdac9010c786b2e1849b27565545208254d51bd6ae611f1936ff0fab04f"
        : leaf == "mp3-vbr-stereo.mp3"   ? "74a5c55e50a8a73198525878f1a0463de918b0490910a5f7681bb933c31b29c9"
        : leaf == "mp3-unicode-tags.mp3" ? "65e66cf43d999e98cf37cb64003d29b374a1238e454eca138c0dc5be2b18c000"
                                         : "b7d0f99cf733c7458dbec3ab62ad397cd98cc6b069db5c12cbb9834885e66b62";
    return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex() == expected ? bytes : QByteArray{};
}
} // namespace

class WindowsMediaFoundationCodecTests final : public QObject {
    Q_OBJECT
  private slots:
    void convertsTrustedLocalFormatsIntoNativelyValidatedMp3_data() {
        QTest::addColumn<QString>("leaf");
        for (const auto* leaf :
             {"mp3-cbr-stereo.mp3", "mp3-vbr-stereo.mp3", "mp3-unicode-tags.mp3", "m4a-aac-lc.m4a", "wav-pcm-mono.wav"})
            QTest::newRow(leaf) << QString::fromLatin1(leaf);
    }
    void convertsTrustedLocalFormatsIntoNativelyValidatedMp3() {
        QFETCH(QString, leaf);
        Fixture h;
        const auto input = fixture(leaf);
        QVERIFY(!input.isEmpty());
        QVERIFY(h.save(input));
        QCOMPARE(runWindowsMediaFoundationCodec(h.request(), h.callbacks()), MediaFoundationWorkerExit::Success);
        QVERIFY(!h.output.isEmpty());
        QVERIFY(h.output.size() < 1024 * 1024);
        QVERIFY(!h.progress.empty());
        QVERIFY(h.progress.back() > 0);
        QVERIFY(std::is_sorted(h.progress.begin(), h.progress.end()));
        QVERIFY(h.save(h.output));
        int writes = 0;
        const MediaFoundationCodecCallbacks validation{[&](std::span<const std::uint8_t>) {
                                                           ++writes;
                                                           return false;
                                                       },
                                                       {}};
        QCOMPARE(runWindowsMediaFoundationCodec(h.request(MediaFoundationWorkerMode::Validate), validation),
                 MediaFoundationWorkerExit::Success);
        QCOMPARE(writes, 0);
    }
    void validatesRealMp3ByContentIncludingVbrAndUnicodeTags_data() {
        QTest::addColumn<QString>("leaf");
        for (const auto* leaf : {"mp3-cbr-stereo.mp3", "mp3-vbr-stereo.mp3", "mp3-unicode-tags.mp3"})
            QTest::newRow(leaf) << QString::fromLatin1(leaf);
    }
    void resamplesA24KhzPcmWavIntoValidated44100HzMp3() {
        auto bytes = fixture("wav-pcm-mono.wav");
        QVERIFY(bytes.size() >= 44);
        QCOMPARE(bytes.mid(12, 4), QByteArray{"fmt "});
        QCOMPARE(qFromLittleEndian<quint16>(bytes.constData() + 22), quint16{1});
        QCOMPARE(qFromLittleEndian<quint16>(bytes.constData() + 32), quint16{2});
        qToLittleEndian<quint32>(24'000, bytes.data() + 24);
        qToLittleEndian<quint32>(48'000, bytes.data() + 28);
        Fixture h;
        QVERIFY(h.save(bytes));
        QCOMPARE(runWindowsMediaFoundationCodec(h.request(), h.callbacks()), MediaFoundationWorkerExit::Success);
        QVERIFY(h.output.size() >= 4);
        QCOMPARE(static_cast<unsigned char>(h.output[0]), 0xffu);
        QCOMPARE(static_cast<unsigned char>(h.output[1]) & 0xfeu, 0xfau);
        QCOMPARE((static_cast<unsigned char>(h.output[2]) >> 2) & 3u, 0u);
        QVERIFY(h.save(h.output));
        QCOMPARE(runWindowsMediaFoundationCodec(h.request(MediaFoundationWorkerMode::Validate), {}),
                 MediaFoundationWorkerExit::Success);
    }
    void validatesRealMp3ByContentIncludingVbrAndUnicodeTags() {
        QFETCH(QString, leaf);
        Fixture h;
        QVERIFY(h.save(fixture(leaf)));
        QCOMPARE(runWindowsMediaFoundationCodec(h.request(MediaFoundationWorkerMode::Validate), {}),
                 MediaFoundationWorkerExit::Success);
    }
    void rejectsDisguisedAndTruncatedMp3WithoutWriting_data() {
        QTest::addColumn<QByteArray>("bytes");
        QTest::addColumn<int>("expected");
        QTest::newRow("WAV disguised as MP3")
            << fixture("wav-pcm-mono.wav") << static_cast<int>(MediaFoundationWorkerExit::Unsupported);
        QTest::newRow("M4A disguised as MP3")
            << fixture("m4a-aac-lc.m4a") << static_cast<int>(MediaFoundationWorkerExit::Unsupported);
        QTest::newRow("garbage") << QByteArray{"not an MP3 file"}
                                 << static_cast<int>(MediaFoundationWorkerExit::Validation);
        auto truncated = fixture("mp3-cbr-stereo.mp3");
        QVERIFY(truncated.size() > 1);
        truncated.chop(1);
        QTest::newRow("incomplete last MP3 frame")
            << truncated << static_cast<int>(MediaFoundationWorkerExit::Validation);
    }
    void rejectsDisguisedAndTruncatedMp3WithoutWriting() {
        QFETCH(QByteArray, bytes);
        QFETCH(int, expected);
        Fixture h;
        QVERIFY(h.save(bytes));
        QCOMPARE(static_cast<int>(
                     runWindowsMediaFoundationCodec(h.request(MediaFoundationWorkerMode::Validate), h.callbacks())),
                 expected);
        QVERIFY(h.output.isEmpty());
    }
    void rejectsAnMp4WithoutAnAudioHandler() {
        auto bytes = fixture("m4a-aac-lc.m4a");
        const auto handler = bytes.indexOf("soun");
        QVERIFY(handler > 0);
        bytes.replace(handler, 4, "vide");
        Fixture h;
        QVERIFY(h.save(bytes));
        QCOMPARE(runWindowsMediaFoundationCodec(h.request(), h.callbacks()), MediaFoundationWorkerExit::NoAudio);
        QVERIFY(h.output.isEmpty());
    }
    void inputOutputAndDurationCapsAreEnforced_data() {
        QTest::addColumn<int>("limit");
        QTest::newRow("input bytes") << 0;
        QTest::newRow("output bytes") << 1;
        QTest::newRow("decoded duration") << 2;
    }
    void inputOutputAndDurationCapsAreEnforced() {
        QFETCH(int, limit);
        Fixture h;
        const auto bytes = fixture("wav-pcm-mono.wav");
        QVERIFY(h.save(bytes));
        auto request = h.request();
        if (limit == 0)
            request.max_input_bytes = static_cast<std::uint64_t>(bytes.size() - 1);
        else if (limit == 1)
            request.max_output_bytes = 8;
        else
            request.max_duration_us = 1000;
        QCOMPARE(runWindowsMediaFoundationCodec(request, h.callbacks()), MediaFoundationWorkerExit::LimitExceeded);
        QVERIFY(static_cast<std::uint64_t>(h.output.size()) <= request.max_output_bytes);
    }
    void callbackFailureOrExceptionCannotReportSuccess_data() {
        QTest::addColumn<bool>("throws");
        QTest::newRow("writer refuses") << false;
        QTest::newRow("writer throws") << true;
    }
    void callbackFailureOrExceptionCannotReportSuccess() {
        QFETCH(bool, throws);
        Fixture h;
        QVERIFY(h.save(fixture("wav-pcm-mono.wav")));
        int writes = 0;
        const MediaFoundationCodecCallbacks callbacks{[&](std::span<const std::uint8_t>) {
                                                          ++writes;
                                                          if (throws)
                                                              throw std::runtime_error{"test callback failure"};
                                                          return false;
                                                      },
                                                      {}};
        QCOMPARE(runWindowsMediaFoundationCodec(h.request(), callbacks), MediaFoundationWorkerExit::Output);
        QCOMPARE(writes, 1);
    }
    void progressRefusalStopsBeforeSuccessfulCompletion() {
        Fixture h;
        QVERIFY(h.save(fixture("wav-pcm-mono.wav")));
        auto callbacks = h.callbacks();
        callbacks.progress = [](std::uint64_t) { return false; };
        QCOMPARE(runWindowsMediaFoundationCodec(h.request(), callbacks), MediaFoundationWorkerExit::Output);
    }
    void inputIsBorrowedWithoutClosingItOrChangingItsBytes() {
        Fixture h;
        const auto original = fixture("wav-pcm-mono.wav");
        QVERIFY(h.save(original));
        QCOMPARE(runWindowsMediaFoundationCodec(h.request(), h.callbacks()), MediaFoundationWorkerExit::Success);
        BY_HANDLE_FILE_INFORMATION information{};
        QVERIFY(GetFileInformationByHandle(h.input, &information));
        QFile source{h.path};
        QVERIFY(source.open(QIODevice::ReadOnly));
        QCOMPARE(source.readAll(), original);
        QVERIFY(!h.output.isEmpty());
    }
    void inputObjectRemainsUsableAfterItsOriginalNameIsRemoved() {
        Fixture h;
        QVERIFY(h.save(fixture("wav-pcm-mono.wav")));
        const auto moved = h.directory.filePath(QStringLiteral("renamed outside request.audio"));
        QVERIFY(QFile::rename(h.path, moved));
        QVERIFY(!QFile::exists(h.path));
        QCOMPARE(runWindowsMediaFoundationCodec(h.request(), h.callbacks()), MediaFoundationWorkerExit::Success);
        QVERIFY(!h.output.isEmpty());
        BY_HANDLE_FILE_INFORMATION information{};
        QVERIFY(GetFileInformationByHandle(h.input, &information));
    }
    void directoriesAndPipesAreNeverTreatedAsInputMedia() {
        Fixture h;
        const auto native = QDir::toNativeSeparators(h.directory.path()).toStdWString();
        const HANDLE directory = CreateFileW(native.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                             nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
        QVERIFY(directory != INVALID_HANDLE_VALUE);
        const auto close_directory = qScopeGuard([directory] { CloseHandle(directory); });
        auto request = h.request();
        request.input_handle = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(directory));
        QCOMPARE(runWindowsMediaFoundationCodec(request, h.callbacks()), MediaFoundationWorkerExit::Input);
        HANDLE read = nullptr, write = nullptr;
        QVERIFY(CreatePipe(&read, &write, nullptr, 0));
        const auto close_pipe = qScopeGuard([read, write] {
            CloseHandle(read);
            CloseHandle(write);
        });
        request.input_handle = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(read));
        QCOMPARE(runWindowsMediaFoundationCodec(request, h.callbacks()), MediaFoundationWorkerExit::Input);
        QVERIFY(h.output.isEmpty());
    }
    void invalidRequestAndClosedInputFailBeforeOutput() {
        Fixture h;
        QVERIFY(h.save(fixture("wav-pcm-mono.wav")));
        auto request = h.request();
        request.max_duration_us = 0;
        QCOMPARE(runWindowsMediaFoundationCodec(request, h.callbacks()), MediaFoundationWorkerExit::InvalidRequest);
        request = h.request();
        h.closeInput();
        QCOMPARE(runWindowsMediaFoundationCodec(request, h.callbacks()), MediaFoundationWorkerExit::Input);
        QVERIFY(h.output.isEmpty());
    }
};
QTEST_GUILESS_MAIN(WindowsMediaFoundationCodecTests)
#include "WindowsMediaFoundationCodecTests.moc"
