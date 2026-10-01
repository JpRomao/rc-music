#include "FfmpegDownloadCodec.hpp"

#include <QTest>

using namespace music_player::m0;

namespace {
QByteArray probe(const QByteArray& format = "mov,mp4,m4a,3gp,3g2,mj2", const QByteArray& codec = "aac") {
    return "{\"streams\":[{\"codec_type\":\"audio\",\"codec_name\":\"" + codec +
           "\",\"sample_rate\":\"48000\",\"channels\":2}],\"format\":{\"format_name\":\"" + format +
           "\",\"duration\":\"2.123456\"}}";
}
QString option(const QStringList& args, const QString& name) {
    const auto index = args.indexOf(name);
    return index >= 0 && index + 1 < args.size() ? args.at(index + 1) : QString{};
}
} // namespace

class FfmpegDownloadCodecTests final : public QObject {
    Q_OBJECT
  private slots:
    void inspectionArgumentsExposeOnlyTechnicalFieldsAndLocalProtocols() {
        const QString path = QStringLiteral("C:/música 日本/input.bin");
        const auto args = downloadProbeArguments(path);
        QVERIFY(!args.isEmpty());
        QCOMPARE(args.back(), path);
        QCOMPARE(option(args, "-protocol_whitelist"), QStringLiteral("file"));
        QCOMPARE(option(args, "-select_streams"), QStringLiteral("a:0"));
        const auto fields = option(args, "-show_entries");
        QVERIFY(fields.contains("codec_name"));
        QVERIFY(!fields.contains("tags"));
        QVERIFY(!fields.contains("filename"));
        QVERIFY(!args.contains("-show_data"));
        QCOMPARE(option(args, "-enable_drefs"), QStringLiteral("0"));
        QCOMPARE(option(args, "-use_absolute_path"), QStringLiteral("0"));
    }
    void conversionUsesOwnedStdoutAndDisablesExternalMovReferences() {
        const QString path = QStringLiteral("C:/música 日本/input.bin");
        const auto args = downloadConversionArguments(path, DownloadContainer::Mp4);
        QVERIFY(!args.isEmpty());
        QCOMPARE(option(args, "-i"), path);
        QCOMPARE(option(args, "-map"), QStringLiteral("0:a:0"));
        QCOMPARE(option(args, "-c:a"), QStringLiteral("libmp3lame"));
        QCOMPARE(option(args, "-progress"), QStringLiteral("pipe:2"));
        QCOMPARE(option(args, "-protocol_whitelist"), QStringLiteral("file"));
        QCOMPARE(option(args, "-f"), QStringLiteral("mp3"));
        QCOMPARE(args.back(), QStringLiteral("pipe:1"));
        QCOMPARE(option(args, "-map_metadata"), QStringLiteral("-1"));
        QCOMPARE(option(args, "-enable_drefs"), QStringLiteral("0"));
        QCOMPARE(option(args, "-write_xing"), QStringLiteral("0"));
        QVERIFY(!args.contains("-y"));
        const auto plain = downloadConversionArguments(path, DownloadContainer::Mp3);
        QVERIFY(!plain.contains("-enable_drefs"));
    }
    void validationDecodesEntireMp3WithErrorsFatal() {
        const auto args = downloadValidationArguments(QStringLiteral("C:/jobs/converted.part"));
        QVERIFY(!args.isEmpty());
        QCOMPARE(option(args, "-format_whitelist"), QStringLiteral("mp3"));
        QCOMPARE(option(args, "-err_detect"), QStringLiteral("explode"));
        QCOMPARE(option(args, "-f"), QStringLiteral("null"));
        QVERIFY(args.contains("-xerror"));
        QVERIFY(!args.contains("-t"));
        QVERIFY(!args.contains("-frames:a"));
    }
    void commandBuildersNeverAcceptRemoteRelativeOrInjectedPaths() {
        for (const auto& path : QStringList{"https://media.example.test/a.mp4", "input.mp4", "C:input.mp4",
                                            "C:/jobs/a\nfile", QStringLiteral("C:/jobs/a") + QChar(0)}) {
            QVERIFY(downloadProbeArguments(path).isEmpty());
            QVERIFY(downloadConversionArguments(path, DownloadContainer::Mp4).isEmpty());
            QVERIFY(downloadValidationArguments(path).isEmpty());
        }
    }
    void probeRecognizesAudioAndPreservesExactDuration() {
        const auto result = decodeDownloadAudioProbe(probe());
        QVERIFY(result.audio.has_value());
        QCOMPARE(result.error, DownloadFailure::None);
        QCOMPARE(result.audio->container, DownloadContainer::Mp4);
        QCOMPARE(result.audio->codec, QStringLiteral("aac"));
        QCOMPARE(result.audio->duration_us, std::optional<quint64>{2123456});
        const auto unknown = decodeDownloadAudioProbe("{\"streams\":[{\"codec_type\":\"audio\",\"codec_name\":\"opus\","
                                                      "\"sample_rate\":\"48000\",\"channels\":2}],"
                                                      "\"format\":{\"format_name\":\"matroska,webm\"}}");
        QVERIFY(unknown.audio.has_value());
        QVERIFY(!unknown.audio->duration_us.has_value());
    }
    void noAudioAndUnsupportedOutputCannotBecomeValidatedMp3() {
        const auto no_audio =
            decodeDownloadAudioProbe("{\"streams\":[],\"format\":{\"format_name\":\"mov,mp4,m4a,3gp,3g2,mj2\"}}");
        QVERIFY(!no_audio.audio.has_value());
        QCOMPARE(no_audio.error, DownloadFailure::NoAudio);
        QVERIFY(!decodeDownloadAudioProbe(probe(), true).audio.has_value());
        QVERIFY(!decodeDownloadAudioProbe(probe("mp3", "aac"), true).audio.has_value());
        QVERIFY(decodeDownloadAudioProbe(probe("mp3", "mp3"), true).audio.has_value());
    }
    void malformedExcessiveOrContradictoryProbeIsRefused() {
        for (const auto& json : QList<QByteArray>{"{", "{}", "[]", QByteArray(16385, ' '), probe("hls"),
                                                  probe("mov,mp4,m4a,3gp,3g2,mj2", "unknown")})
            QVERIFY(!decodeDownloadAudioProbe(json).audio.has_value());
        auto bad = probe();
        bad.replace("48000", "0");
        QVERIFY(!decodeDownloadAudioProbe(bad).audio.has_value());
        bad = probe();
        bad.replace("\"channels\":2", "\"channels\":0");
        QVERIFY(!decodeDownloadAudioProbe(bad).audio.has_value());
        bad = probe();
        bad.replace("2.123456", "18446744073709551616");
        QVERIFY(!decodeDownloadAudioProbe(bad).audio.has_value());
    }
    void mp3ProbeRejectsImpossibleChannelsAndSamplingRates() {
        auto json = probe("mp3", "mp3");
        json.replace("48000", "384000");
        QVERIFY(!decodeDownloadAudioProbe(json, true).audio.has_value());
        json = probe("mp3", "mp3");
        json.replace("\"channels\":2", "\"channels\":8");
        QVERIFY(!decodeDownloadAudioProbe(json, true).audio.has_value());
        json = probe("mp3", "mp3");
        json.replace("48000", "40000");
        QVERIFY(!decodeDownloadAudioProbe(json, true).audio.has_value());
    }
    void progressIsIncrementalMonotonicAndEndDoesNotValidateMedia() {
        DownloadProgressCodec codec;
        const QByteArray bytes("out_time_us=N/A\nprogress=continue\nout_time_us=123456\nspeed=2.0x\nprogress=continue\n"
                               "out_time_us=223456\nprogress=end\n");
        for (const auto character : bytes)
            QVERIFY(codec.feed(QByteArrayView(&character, 1)));
        QVERIFY(codec.finishEof());
        QVERIFY(!codec.failed());
        QCOMPARE(codec.snapshot().processed_us, std::optional<quint64>{223456});
        QVERIFY(codec.snapshot().ended);
    }
    void malformedRegressiveDuplicateAndOverflowProgressFailClosed() {
        for (const auto& bytes : QList<QByteArray>{"out_time_us=-1\n", "out_time_us=18446744073709551616\n",
                                                   "out_time_us=1\nout_time_us=2\n", "progress=finished\n",
                                                   "out_time_us=10\nprogress=continue\nout_time_us=9\n",
                                                   "out_time_us=10\nprogress=continue\nout_time_us=N/A\n",
                                                   "out_time_us=0\nprogress=end\nanything=else\n"}) {
            DownloadProgressCodec codec;
            QVERIFY(!codec.feed(bytes));
            QVERIFY(codec.failed());
            QVERIFY(!codec.finishEof());
        }
    }
    void progressFramingCannotAccumulateUnboundedOrIncompleteData() {
        DownloadProgressCodec oversized;
        QVERIFY(!oversized.feed(QByteArray(1025, 'a')));
        QVERIFY(oversized.failed());
        DownloadProgressCodec incomplete;
        QVERIFY(incomplete.feed("out_time_us=1\nprogress=en"));
        QVERIFY(!incomplete.finishEof());
        DownloadProgressCodec absent_end;
        QVERIFY(absent_end.feed("out_time_us=1\nprogress=continue\n"));
        QVERIFY(!absent_end.finishEof());
    }
};

QTEST_APPLESS_MAIN(FfmpegDownloadCodecTests)
#include "FfmpegDownloadCodecTests.moc"
