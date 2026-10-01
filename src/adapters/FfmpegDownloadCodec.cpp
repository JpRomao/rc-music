#include "FfmpegDownloadCodec.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
#include <limits>

namespace music_player::m0 {
namespace {
bool localPath(const QString& path) {
    if (path.size() < 4 || path.size() > 32700 ||
        !((path[0] >= 'a' && path[0] <= 'z') || (path[0] >= 'A' && path[0] <= 'Z')) || path[1] != ':' ||
        (path[2] != '/' && path[2] != '\\'))
        return false;
    return std::none_of(path.begin(), path.end(), [](QChar character) { return character.unicode() < 32; });
}

QStringList inputLimits() {
    return {"-protocol_whitelist", "file",    "-format_whitelist", "mov,mp3,wav,flac,ogg,matroska,webm",
            "-probesize",          "1048576", "-analyzeduration",  "5000000",
            "-max_streams",        "16"};
}
QStringList movRestrictions() { return {"-enable_drefs", "0", "-use_absolute_path", "0", "-ignore_chapters", "1"}; }

std::optional<quint64> decimal(QByteArrayView value) {
    if (value.empty())
        return {};
    quint64 result = 0;
    for (const auto character : value) {
        if (character < '0' || character > '9')
            return {};
        const auto digit = static_cast<quint64>(character - '0');
        if (result > (std::numeric_limits<quint64>::max() - digit) / 10)
            return {};
        result = result * 10 + digit;
    }
    return result;
}

std::optional<quint64> duration(const QString& value) {
    const auto bytes = value.toLatin1();
    const auto point = bytes.indexOf('.');
    const auto seconds = decimal(point < 0 ? QByteArrayView(bytes) : QByteArrayView(bytes).first(point));
    if (!seconds || *seconds > std::numeric_limits<quint64>::max() / 1000000)
        return {};
    quint64 micros = 0;
    if (point >= 0) {
        auto fraction = bytes.mid(point + 1);
        if (fraction.isEmpty() || fraction.size() > 6)
            return {};
        while (fraction.size() < 6)
            fraction += '0';
        const auto parsed = decimal(fraction);
        if (!parsed)
            return {};
        micros = *parsed;
    }
    const auto whole = *seconds * 1000000;
    if (micros > std::numeric_limits<quint64>::max() - whole)
        return {};
    return whole + micros;
}

std::optional<DownloadContainer> container(const QString& name) {
    if (name == "mp3")
        return DownloadContainer::Mp3;
    if (name == "mov,mp4,m4a,3gp,3g2,mj2")
        return DownloadContainer::Mp4;
    if (name == "wav")
        return DownloadContainer::Wav;
    if (name == "flac")
        return DownloadContainer::Flac;
    if (name == "ogg")
        return DownloadContainer::Ogg;
    if (name == "matroska,webm")
        return DownloadContainer::Matroska;
    return {};
}

bool knownCodec(const QString& name) {
    static const QStringList names{"mp3",       "aac",       "alac",      "flac",      "vorbis", "opus",
                                   "pcm_s16le", "pcm_s24le", "pcm_s32le", "pcm_f32le", "pcm_u8"};
    return names.contains(name);
}

bool onlyKeys(const QJsonObject& object, const QStringList& keys) {
    for (auto entry = object.begin(); entry != object.end(); ++entry)
        if (!keys.contains(entry.key()))
            return false;
    return true;
}
} // namespace

QStringList downloadProbeArguments(const QString& local_input) {
    if (!localPath(local_input))
        return {};
    QStringList args{"-hide_banner", "-v", "quiet"};
    args += inputLimits();
    args += movRestrictions();
    args += QStringList{"-select_streams",
                        "a:0",
                        "-show_entries",
                        "format=format_name,duration:stream=codec_name,codec_type,sample_rate,channels,duration",
                        "-of",
                        "json",
                        local_input};
    return args;
}

QStringList downloadConversionArguments(const QString& local_input, DownloadContainer kind) {
    if (!localPath(local_input))
        return {};
    QStringList args{"-hide_banner", "-nostdin",  "-loglevel", "quiet",
                     "-nostats",     "-progress", "pipe:2",    "-stats_period",
                     "0.5",          "-xerror",   "-abort_on", "empty_output+empty_output_stream"};
    args += inputLimits();
    if (kind == DownloadContainer::Mp4)
        args += movRestrictions();
    args += QStringList{"-i",         local_input,      "-map", "0:a:0",         "-vn",  "-sn",
                        "-dn",        "-map_metadata",  "-1",   "-map_chapters", "-1",   "-c:a",
                        "libmp3lame", "-threads:a",     "1",    "-b:a",          "192k", "-f",
                        "mp3",        "-id3v2_version", "0",    "-write_id3v1",  "0",    "-write_xing",
                        "0",          "pipe:1"};
    return args;
}

QStringList downloadValidationArguments(const QString& local_mp3) {
    if (!localPath(local_mp3))
        return {};
    return {"-hide_banner",
            "-nostdin",
            "-loglevel",
            "quiet",
            "-nostats",
            "-xerror",
            "-abort_on",
            "empty_output+empty_output_stream",
            "-protocol_whitelist",
            "file",
            "-format_whitelist",
            "mp3",
            "-probesize",
            "1048576",
            "-analyzeduration",
            "5000000",
            "-max_streams",
            "16",
            "-err_detect",
            "explode",
            "-i",
            local_mp3,
            "-map",
            "0:a:0",
            "-vn",
            "-sn",
            "-dn",
            "-threads:a",
            "1",
            "-f",
            "null",
            "-"};
}

DownloadAudioProbe decodeDownloadAudioProbe(QByteArrayView json, bool require_mp3) {
    if (json.empty() || json.size() > 16384)
        return {};
    const QByteArray bytes(json.data(), json.size());
    if (QString::fromUtf8(bytes).toUtf8() != bytes)
        return {};
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
        return {};
    const auto object = document.object();
    if (!onlyKeys(object, {"streams", "format", "programs", "stream_groups"}) || !object["streams"].isArray() ||
        !object["format"].isObject())
        return {};
    for (const auto* field : {"programs", "stream_groups"}) {
        if (object.contains(field) && (!object[field].isArray() || !object[field].toArray().isEmpty()))
            return {};
    }
    const auto format = object["format"].toObject();
    if (!onlyKeys(format, {"format_name", "duration"}) || !format["format_name"].isString())
        return {};
    const auto kind = container(format["format_name"].toString());
    if (!kind || (require_mp3 && *kind != DownloadContainer::Mp3))
        return {};
    const auto streams = object["streams"].toArray();
    if (streams.isEmpty())
        return {{}, DownloadFailure::NoAudio};
    if (streams.size() != 1 || !streams[0].isObject())
        return {};
    const auto stream = streams[0].toObject();
    if (!onlyKeys(stream, {"codec_name", "codec_type", "sample_rate", "channels", "duration"}) ||
        stream["codec_type"] != "audio" || !stream["codec_name"].isString() || !stream["sample_rate"].isString() ||
        !stream["channels"].isDouble())
        return {};
    const auto codec = stream["codec_name"].toString();
    if (!knownCodec(codec) || (require_mp3 && codec != "mp3"))
        return {};
    const auto sample_rate = decimal(stream["sample_rate"].toString().toLatin1());
    const auto channels = stream["channels"].toDouble();
    if (!sample_rate || *sample_rate < 8000 || *sample_rate > 384000 || channels < 1 || channels > 8 ||
        channels != static_cast<int>(channels))
        return {};
    if (codec == "mp3") {
        constexpr quint64 mp3_rates[]{8000, 11025, 12000, 16000, 22050, 24000, 32000, 44100, 48000};
        if (channels > 2 || std::none_of(std::begin(mp3_rates), std::end(mp3_rates),
                                         [&](quint64 rate) { return rate == *sample_rate; }))
            return {};
    }
    DownloadAudioInspection audio{*kind, codec, {}};
    for (const auto& metadata : {format, stream}) {
        if (!metadata.contains("duration"))
            continue;
        if (!metadata["duration"].isString())
            return {};
        const auto raw = metadata["duration"].toString();
        if (raw == "N/A")
            continue;
        const auto parsed = duration(raw);
        if (!parsed)
            return {};
        audio.duration_us = parsed;
    }
    return {audio, DownloadFailure::None};
}

bool DownloadProgressCodec::feed(QByteArrayView bytes) {
    if (failed_)
        return false;
    for (const auto character : bytes) {
        if (snapshot_.ended || ++received_bytes_ > 4 * 1024 * 1024 || ++block_bytes_ > 16384) {
            failed_ = true;
            return false;
        }
        if (character == '\n') {
            if (!line_.isEmpty() && line_.back() == '\r')
                line_.chop(1);
            if (!consumeLine()) {
                failed_ = true;
                return false;
            }
            line_.clear();
        } else {
            if (static_cast<unsigned char>(character) < 32 && character != '\r') {
                failed_ = true;
                return false;
            }
            line_ += character;
            if (line_.size() > 1024) {
                failed_ = true;
                return false;
            }
        }
    }
    return true;
}

bool DownloadProgressCodec::consumeLine() {
    const auto separator = line_.indexOf('=');
    if (separator <= 0)
        return false;
    const auto key = line_.left(separator);
    const auto value = line_.mid(separator + 1);
    static const QList<QByteArray> fields{"bitrate",    "total_size",  "out_time_us", "out_time_ms", "out_time",
                                          "dup_frames", "drop_frames", "speed",       "progress"};
    if (!fields.contains(key) || block_keys_.contains(key))
        return false;
    block_keys_.insert(key);
    if (key == "out_time_us") {
        if (value == "N/A")
            return !snapshot_.processed_us;
        const auto time = decimal(value);
        if (!time || (snapshot_.processed_us && *time < *snapshot_.processed_us))
            return false;
        block_time_ = time;
    } else if (key == "progress") {
        if (value != "continue" && value != "end")
            return false;
        if (block_time_)
            snapshot_.processed_us = block_time_;
        snapshot_.ended = value == "end";
        block_keys_.clear();
        block_time_.reset();
        block_bytes_ = 0;
    }
    return true;
}

bool DownloadProgressCodec::finishEof() {
    if (!line_.isEmpty() || !snapshot_.ended)
        failed_ = true;
    return !failed_;
}
bool DownloadProgressCodec::failed() const { return failed_; }
DownloadConversionProgress DownloadProgressCodec::snapshot() const { return snapshot_; }
} // namespace music_player::m0
