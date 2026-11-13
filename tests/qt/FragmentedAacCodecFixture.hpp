#pragma once

#include <QByteArray>
#include <QtEndian>

#include <algorithm>
#include <cstdint>
#include <vector>

namespace fragmented_aac_fixture {
inline QByteArray u32(quint32 value) {
    QByteArray bytes(4, '\0');
    qToBigEndian(value, bytes.data());
    return bytes;
}
inline QByteArray box(const char* type, const QByteArray& payload) {
    return u32(static_cast<quint32>(payload.size() + 8)) + QByteArray(type, 4) + payload;
}
// Only called with the checksum-authenticated, generated two-second corpus
// fixture. This is not the production/untrusted MP4 parser.
inline QByteArray trustedBox(const QByteArray& bytes, const char* type) {
    const auto at = bytes.indexOf(QByteArray(type, 4));
    if (at < 4)
        return {};
    const auto size = qFromBigEndian<quint32>(bytes.constData() + at - 4);
    if (size < 8 || size > static_cast<quint64>(bytes.size() - at + 4))
        return {};
    return bytes.mid(at - 4, size);
}
struct Media final {
    QByteArray bytes;
    quint32 rate{0};
    quint32 presentation_frames{0};
};
inline Media make(const QByteArray& classic, quint32 media_start, quint32 duration, quint32 silence = 0,
                  bool with_edits = true, quint32 movie_timescale = 0, quint32 media_timescale = 0) {
    auto mvhd = trustedBox(classic, "mvhd");
    auto tkhd = trustedBox(classic, "tkhd");
    auto mdhd = trustedBox(classic, "mdhd");
    const auto sizes = trustedBox(classic, "stsz");
    const auto chunks = trustedBox(classic, "stco");
    const auto durations = trustedBox(classic, "stts");
    if (mvhd.size() < 28 || tkhd.size() < 32 || mdhd.size() < 28 || sizes.size() < 20 || chunks.size() != 20 ||
        durations.size() < 16)
        return {};
    const auto rate = qFromBigEndian<quint32>(mdhd.constData() + 20);
    const auto movie_scale = movie_timescale ? movie_timescale : rate;
    const auto media_scale = media_timescale ? media_timescale : rate;
    const auto count = qFromBigEndian<quint32>(sizes.constData() + 16);
    const auto first = qFromBigEndian<quint32>(chunks.constData() + 16);
    if (!rate || count < 2 || count > 1000 || sizes.size() != 20 + count * 4 ||
        qFromBigEndian<quint32>(sizes.constData() + 12) != 0 || qFromBigEndian<quint32>(chunks.constData() + 12) != 1)
        return {};
    struct Packet {
        QByteArray bytes;
        quint32 duration;
    };
    std::vector<quint32> times;
    const auto entries = qFromBigEndian<quint32>(durations.constData() + 12);
    if (durations.size() != 16 + entries * 8)
        return {};
    for (quint32 i = 0; i < entries; ++i) {
        const auto n = qFromBigEndian<quint32>(durations.constData() + 16 + i * 8);
        const auto time = qFromBigEndian<quint32>(durations.constData() + 20 + i * 8);
        if (n > count || !time || times.size() + n > count)
            return {};
        const auto scaled = quint64{time} * media_scale;
        if (scaled % rate || scaled / rate > 0xffffffffULL)
            return {};
        times.insert(times.end(), n, static_cast<quint32>(scaled / rate));
    }
    if (times.size() != count)
        return {};
    std::vector<Packet> packets;
    quint64 offset = first;
    for (quint32 i = 0; i < count; ++i) {
        const auto size = qFromBigEndian<quint32>(sizes.constData() + 20 + i * 4);
        if (!size || offset + size > static_cast<quint64>(classic.size()))
            return {};
        packets.push_back({classic.mid(static_cast<qsizetype>(offset), size), times[i]});
        offset += size;
    }
    qToBigEndian(movie_scale, mvhd.data() + 20);
    qToBigEndian(duration + silence, mvhd.data() + 24);
    qToBigEndian(duration + silence, tkhd.data() + 28);
    qToBigEndian(media_scale, mdhd.data() + 20);
    const auto default_duration = static_cast<quint32>(quint64{1024} * media_scale / rate);
    auto edits = u32(0) + u32(silence ? 2 : 1);
    if (silence)
        edits += u32(silence) + u32(0xffffffff) + u32(0x00010000);
    edits += u32(duration) + u32(media_start) + u32(0x00010000);
    const auto stbl =
        box("stbl", trustedBox(classic, "stsd") + box("stts", u32(0) + u32(0)) + box("stsc", u32(0) + u32(0)) +
                        box("stco", u32(0) + u32(0)) + box("stsz", u32(0) + u32(0) + u32(0)));
    const auto minf = box("minf", trustedBox(classic, "dinf") + stbl + trustedBox(classic, "smhd"));
    const auto mdia = box("mdia", mdhd + trustedBox(classic, "hdlr") + minf);
    const auto trak = box("trak", tkhd + (with_edits ? box("edts", box("elst", edits)) : QByteArray{}) + mdia);
    const auto trex = box("trex", u32(0) + u32(1) + u32(1) + u32(0) + u32(0) + u32(0));
    const auto moov = box("moov", mvhd + box("mvex", trex) + trak);
    std::vector<QByteArray> fragments;
    std::vector<quint32> fragment_times;
    quint32 decode_time = 0;
    for (unsigned part = 0; part < 2; ++part) {
        const auto begin = part == 0 ? 0u : count / 2;
        const auto end = part == 0 ? count / 2 : count;
        const bool varying = std::any_of(times.begin() + begin, times.begin() + end,
                                         [default_duration](quint32 t) { return t != default_duration; });
        auto run = u32(varying ? 0x301 : 0x201) + u32(end - begin) + u32(0);
        QByteArray data;
        quint32 elapsed = 0;
        for (auto i = begin; i < end; ++i) {
            if (varying)
                run += u32(packets[i].duration);
            run += u32(static_cast<quint32>(packets[i].bytes.size()));
            data += packets[i].bytes;
            elapsed += packets[i].duration;
        }
        const auto tfhd = box("tfhd", u32(0x2002a) + u32(1) + u32(1) + u32(default_duration) + u32(0));
        const auto tfdt = box("tfdt", u32(0) + u32(decode_time));
        const auto mfhd = box("mfhd", u32(0) + u32(part + 1));
        auto moof = box("moof", mfhd + box("traf", tfhd + tfdt + box("trun", run)));
        qToBigEndian(static_cast<quint32>(moof.size() + 8), run.data() + 8);
        moof = box("moof", mfhd + box("traf", tfhd + tfdt + box("trun", run)));
        fragments.push_back(moof + box("mdat", data));
        fragment_times.push_back(elapsed);
        decode_time += elapsed;
    }
    auto index = u32(0) + u32(1) + u32(media_scale) + u32(0) + u32(0) + QByteArray::fromHex("00000002");
    for (std::size_t i = 0; i < fragments.size(); ++i)
        index += u32(static_cast<quint32>(fragments[i].size())) + u32(fragment_times[i]) + u32(0x90000000);
    auto bytes = box("ftyp", QByteArray("dash", 4) + u32(0) + QByteArray("iso6mp41", 8)) + moov + box("sidx", index);
    for (const auto& fragment : fragments)
        bytes += fragment;
    return {std::move(bytes), rate,
            static_cast<quint32>(quint64{duration} * rate / movie_scale + quint64{silence} * rate / movie_scale)};
}
} // namespace fragmented_aac_fixture
