#include "FragmentedAacCodecFixture.hpp"
#include "NativeDecodedAudioTestSupport.hpp"
#include "TemporalAacTestSignal.hpp"
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
#include <cmath>
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
native_audio_test::Pcm recoveredArtifact(const Fixture& h) {
    const auto path = h.directory.filePath(QStringLiteral("recovered.mp3"));
    QFile file{path};
    if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly) || file.write(h.output) != h.output.size())
        return {};
    file.close();
    return native_audio_test::decode(path);
}
// Tolerances concern MPEG framing/codec delay, not the exact PCM edit count.
// The oracle reads the final MP3 and rejects equally long wrong-content files.
bool temporalSignal(const native_audio_test::Pcm& pcm, std::size_t expected_frames, std::size_t silence) {
    if (!pcm.valid || pcm.rate != 48000 || pcm.channels != 1 || pcm.samples.size() + 2 * 1152 < expected_frames ||
        pcm.samples.size() > expected_frames + 2 * 1152)
        return false;
    const auto mid_begin = silence + 7200, mid_end = silence + 14400;
    const auto middle = native_audio_test::tonePower(pcm, 997, mid_begin, mid_end);
    const auto wrong_cut = native_audio_test::tonePower(pcm, 313, mid_begin, mid_end);
    if (middle <= 20 * wrong_cut || native_audio_test::rms(pcm, mid_begin, mid_end) < 2000)
        return false;
    const auto tail_end = pcm.samples.size() - 480, tail_begin = tail_end - 2400;
    const auto tail = native_audio_test::tonePower(pcm, 2003, tail_begin, tail_end);
    if (tail <= 20 * native_audio_test::tonePower(pcm, 997, tail_begin, tail_end))
        return false;
    const auto lead = native_audio_test::rms(pcm, 2400, 4320);
    return silence ? lead >= 0 && lead < 100 : lead > 1000;
}
QByteArray referenceWave(std::size_t frames, std::size_t silence, quint32 rate = 48000, quint16 channels = 1) {
    QByteArray samples(static_cast<qsizetype>(frames * channels * 2), '\0');
    for (auto i = silence; i < frames; ++i) {
        const auto frequency = i - silence < rate * 45 / 100 ? 997.0 : 2003.0;
        const auto value = static_cast<qint16>(
            std::lround(10000 * std::sin(2 * std::numbers::pi * frequency * static_cast<double>(i - silence) / rate)));
        for (quint16 channel = 0; channel < channels; ++channel)
            qToLittleEndian(value, samples.data() + (i * channels + channel) * 2);
    }
    QByteArray wave{"RIFF"};
    const auto little32 = [](quint32 value) {
        QByteArray bytes(4, '\0');
        qToLittleEndian(value, bytes.data());
        return bytes;
    };
    wave += little32(static_cast<quint32>(samples.size() + 36)) + "WAVEfmt " + little32(16);
    QByteArray channel_field(2, '\0'), alignment(2, '\0');
    qToLittleEndian(channels, channel_field.data());
    qToLittleEndian(static_cast<quint16>(channels * 2), alignment.data());
    wave += QByteArray::fromHex("0100") + channel_field + little32(rate) + little32(rate * channels * 2) + alignment +
            QByteArray::fromHex("1000");
    return wave + "data" + little32(static_cast<quint32>(samples.size())) + samples;
}
} // namespace

class WindowsMediaFoundationCodecTests final : public QObject {
    Q_OBJECT
  private slots:
    void encoderTailCannotExtendThePresentationAcrossSupportedPcmProfiles_data() {
        QTest::addColumn<quint32>("rate");
        QTest::addColumn<quint16>("channels");
        QTest::addColumn<quint32>("duration_ms");
        for (quint32 rate : {32000u, 44100u, 48000u})
            for (quint16 channels : {quint16{1}, quint16{2}})
                for (quint32 duration : {600u, 750u}) {
                    const auto name = QStringLiteral("%1Hz-%2ch-%3ms").arg(rate).arg(channels).arg(duration).toLatin1();
                    QTest::newRow(name.constData()) << rate << channels << duration;
                }
    }
    void encoderTailCannotExtendThePresentationAcrossSupportedPcmProfiles() {
        QFETCH(quint32, rate);
        QFETCH(quint16, channels);
        QFETCH(quint32, duration_ms);
        const auto frames = std::uint64_t{rate} * duration_ms / 1000;
        Fixture h;
        QVERIFY(h.save(referenceWave(frames, 0, rate, channels)));
        QCOMPARE(runWindowsMediaFoundationCodec(h.request(), h.callbacks()), MediaFoundationWorkerExit::Success);
        QCOMPARE(h.progress.back(), std::uint64_t{duration_ms} * 1000);
        const auto pcm = recoveredArtifact(h);
        QVERIFY(pcm.valid);
        QCOMPARE(pcm.rate, rate);
        QCOMPARE(pcm.channels, UINT32{channels});
        const auto recovered_frames = pcm.samples.size() / channels;
        qInfo("Profile %u/%u duration=%u inputFrames=%llu recovered=%zu", rate, channels, duration_ms,
              static_cast<unsigned long long>(frames), recovered_frames);
        const auto difference = static_cast<qint64>(recovered_frames) - static_cast<qint64>(frames);
        QVERIFY2(std::abs(difference) <= 1152, "Guard samples must flush coding lookahead, not lengthen the media");
    }
    void recoveredMp3RetainsTrimSilenceAndDistinctSignalMarkers_data() {
        QTest::addColumn<quint32>("movie_scale");
        QTest::addColumn<quint32>("media_scale");
        QTest::addColumn<quint32>("start");
        QTest::addColumn<quint32>("duration");
        QTest::addColumn<quint32>("silence");
        QTest::addColumn<quint32>("expected_frames");
        QTest::addColumn<quint32>("silence_frames");
        QTest::newRow("trim and tail marker") << quint32{48000} << quint32{48000} << quint32{38400} << quint32{28800}
                                              << quint32{0} << quint32{28800} << quint32{0};
        QTest::newRow("explicit leading silence") << quint32{48000} << quint32{48000} << quint32{38400}
                                                  << quint32{28800} << quint32{7200} << quint32{36000} << quint32{7200};
        QTest::newRow("distinct clocks and fractional sample boundary")
            << quint32{90001} << quint32{90000} << quint32{72001} << quint32{54001} << quint32{13501} << quint32{36000}
            << quint32{7200};
    }
    void recoveredMp3RetainsTrimSilenceAndDistinctSignalMarkers() {
        QFETCH(quint32, movie_scale);
        QFETCH(quint32, media_scale);
        QFETCH(quint32, start);
        QFETCH(quint32, duration);
        QFETCH(quint32, silence);
        QFETCH(quint32, expected_frames);
        QFETCH(quint32, silence_frames);
        const auto classic = temporal_aac_fixture::classic();
        QCOMPARE(QCryptographicHash::hash(classic, QCryptographicHash::Sha256).toHex(),
                 QByteArray{temporal_aac_fixture::sha256});
        const auto input =
            fragmented_aac_fixture::make(classic, start, duration, silence, true, movie_scale, media_scale);
        QVERIFY(!input.bytes.isEmpty());
        Fixture h;
        QVERIFY(h.save(input.bytes));
        QCOMPARE(runWindowsMediaFoundationCodec(h.request(), h.callbacks()), MediaFoundationWorkerExit::Success);
        QCOMPARE(h.progress.back(), std::uint64_t{expected_frames} * 1000000 / 48000);
        const auto actual = recoveredArtifact(h);
        qInfo("Recovered MP3 PCM frames=%zu expected=%u rate=%u channels=%u", actual.samples.size(), expected_frames,
              actual.rate, actual.channels);
        QVERIFY(actual.valid);
        Fixture reference;
        QVERIFY(reference.save(referenceWave(expected_frames, silence_frames)));
        QCOMPARE(runWindowsMediaFoundationCodec(reference.request(), reference.callbacks()),
                 MediaFoundationWorkerExit::Success);
        const auto control = recoveredArtifact(reference);
        QVERIFY(control.valid);
        qInfo("Reference MP3 PCM frames=%zu leadRms=%f actualLead=%f middle997=%f middle313=%f", control.samples.size(),
              native_audio_test::rms(control, 2400, 4320), native_audio_test::rms(actual, 2400, 4320),
              native_audio_test::tonePower(actual, 997, silence_frames + 7200, silence_frames + 14400),
              native_audio_test::tonePower(actual, 313, silence_frames + 7200, silence_frames + 14400));
        QVERIFY2(temporalSignal(actual, expected_frames, silence_frames),
                 "Recovered MP3 must preserve the selected 997/2003-Hz section and an actually silent lead");
        QVERIFY(temporalSignal(control, expected_frames, silence_frames));
        const auto difference =
            static_cast<qint64>(actual.samples.size()) - static_cast<qint64>(control.samples.size());
        QVERIFY(std::abs(difference) <= 1152);
    }
    void artifactOracleRejectsWrongCutAndMissingSilenceWithTheSamePcmCounter() {
        const auto classic = temporal_aac_fixture::classic();
        for (const bool wrong_cut : {true, false}) {
            const auto input = fragmented_aac_fixture::make(classic, wrong_cut ? 0 : 38400, wrong_cut ? 28800 : 36000,
                                                            wrong_cut ? 7200 : 0);
            QVERIFY(!input.bytes.isEmpty());
            Fixture h;
            QVERIFY(h.save(input.bytes));
            QCOMPARE(runWindowsMediaFoundationCodec(h.request(), h.callbacks()), MediaFoundationWorkerExit::Success);
            QCOMPARE(h.progress.back(), std::uint64_t{750000});
            const auto actual = recoveredArtifact(h);
            QVERIFY(actual.valid);
            QVERIFY(actual.samples.size() + 2 * 1152 >= 36000);
            QVERIFY(actual.samples.size() <= 36000 + 2 * 1152);
            if (wrong_cut) {
                QVERIFY(native_audio_test::tonePower(actual, 313, 14400, 21600) >
                        20 * native_audio_test::tonePower(actual, 997, 14400, 21600));
            } else {
                QVERIFY(native_audio_test::rms(actual, 2400, 4320) > 1000);
            }
            QVERIFY2(!temporalSignal(actual, 36000, 7200),
                     "An equally long playable MP3 is not sufficient temporal evidence");
        }
    }
    void temporalWindowCapsFailBeforeWritingEvenWithDiscardedMedia() {
        const auto classic = temporal_aac_fixture::classic();
        for (const bool oversized_silence : {false, true}) {
            const auto input = fragmented_aac_fixture::make(classic, oversized_silence ? 0 : 38400, 28800,
                                                            oversized_silence ? 240000 : 0);
            Fixture h;
            QVERIFY(h.save(input.bytes));
            auto request = h.request();
            request.max_duration_us = oversized_silence ? 3000000 : 1000000;
            QCOMPARE(runWindowsMediaFoundationCodec(request, h.callbacks()), MediaFoundationWorkerExit::LimitExceeded);
            QVERIFY(h.output.isEmpty());
        }
    }
    void convertsClassicAacWithAnIgnorableTopLevelExtension() {
        auto input = fixture("m4a-aac-lc.m4a");
        QVERIFY(!input.isEmpty());
        input += fragmented_aac_fixture::box("uuid", QByteArray(16, '\x42'));
        Fixture h;
        QVERIFY(h.save(input));
        QCOMPARE(runWindowsMediaFoundationCodec(h.request(), h.callbacks()), MediaFoundationWorkerExit::Success);
        QVERIFY(!h.output.isEmpty());
        QVERIFY(h.save(h.output));
        QCOMPARE(runWindowsMediaFoundationCodec(h.request(MediaFoundationWorkerMode::Validate), {}),
                 MediaFoundationWorkerExit::Success);
    }
    void convertsFragmentedAacWithoutEdits() {
        const auto input = fragmented_aac_fixture::make(fixture("m4a-aac-lc.m4a"), 0, 24000, 0, false);
        QVERIFY(!input.bytes.isEmpty());
        Fixture h;
        QVERIFY(h.save(input.bytes));
        QCOMPARE(runWindowsMediaFoundationCodec(h.request(), h.callbacks()), MediaFoundationWorkerExit::Success);
        QVERIFY(!h.progress.empty());
        QVERIFY(h.progress.back() > 1'900'000);
        QVERIFY(h.progress.back() < 2'100'000);
        QVERIFY(h.save(h.output));
        QCOMPARE(runWindowsMediaFoundationCodec(h.request(MediaFoundationWorkerMode::Validate), {}),
                 MediaFoundationWorkerExit::Success);
    }
    void materializesFragmentedAacTrimAndLeadingSilence_data() {
        QTest::addColumn<quint32>("silence");
        QTest::newRow("trimmed half second") << quint32{0};
        QTest::newRow("trim and leading silence") << quint32{4800};
    }
    void materializesFragmentedAacTrimAndLeadingSilence() {
        QFETCH(quint32, silence);
        const auto input = fragmented_aac_fixture::make(fixture("m4a-aac-lc.m4a"), 4800, 24000, silence);
        QCOMPARE(input.rate, quint32{48000});
        QVERIFY(!input.bytes.isEmpty());
        Fixture h;
        QVERIFY(h.save(input.bytes));
        QCOMPARE(runWindowsMediaFoundationCodec(h.request(), h.callbacks()), MediaFoundationWorkerExit::Success);
        QVERIFY(!h.progress.empty());
        QCOMPARE(h.progress.back(), std::uint64_t{input.presentation_frames} * 1'000'000 / input.rate);
        QFile unchanged{h.path};
        QVERIFY(unchanged.open(QIODevice::ReadOnly));
        QCOMPARE(unchanged.readAll(), input.bytes);
        unchanged.close();
        QVERIFY(!h.output.isEmpty());
        QVERIFY(h.save(h.output));
        QCOMPARE(runWindowsMediaFoundationCodec(h.request(MediaFoundationWorkerMode::Validate), {}),
                 MediaFoundationWorkerExit::Success);
    }
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
