#include "FragmentedAacCodecFixture.hpp"
#include "adapters/FragmentedMp4Audio.hpp"

#include <QCryptographicHash>
#include <QFile>
#include <QTest>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <span>
#include <stdexcept>
#include <string_view>
#include <vector>

using namespace music_player::m0;

namespace {
using Bytes = std::vector<std::uint8_t>;
void u32(Bytes& b, std::uint32_t v) {
    for (int s = 24; s >= 0; s -= 8)
        b.push_back(static_cast<std::uint8_t>(v >> s));
}
void u64(Bytes& b, std::uint64_t v) {
    u32(b, static_cast<std::uint32_t>(v >> 32));
    u32(b, static_cast<std::uint32_t>(v));
}
void set32(Bytes& b, std::size_t p, std::uint32_t v) {
    for (int s = 24; s >= 0; s -= 8)
        b.at(p++) = static_cast<std::uint8_t>(v >> s);
}
std::uint32_t get32(const Bytes& b, std::size_t p) {
    std::uint32_t v = 0;
    for (int i = 0; i < 4; ++i)
        v = (v << 8) | b.at(p++);
    return v;
}
void append(Bytes& b, const Bytes& more) { b.insert(b.end(), more.begin(), more.end()); }
Bytes words(std::initializer_list<std::uint32_t> values) {
    Bytes result;
    for (const auto v : values)
        u32(result, v);
    return result;
}
Bytes box(std::string_view name, const Bytes& data) {
    Bytes result;
    u32(result, static_cast<std::uint32_t>(data.size() + 8));
    result.insert(result.end(), name.begin(), name.end());
    append(result, data);
    return result;
}
Bytes boxes(std::initializer_list<Bytes> values) {
    Bytes result;
    for (const auto& value : values)
        append(result, value);
    return result;
}
Bytes descriptor(std::uint8_t tag, const Bytes& body) {
    Bytes result{tag, static_cast<std::uint8_t>(body.size())};
    append(result, body);
    return result;
}
Bytes aacDescription() {
    Bytes decoder{0x40, 0x15, 0, 0, 0};
    append(decoder, words({128000, 128000}));
    append(decoder, descriptor(5, {0x11, 0x90}));
    Bytes es{0, 1, 0};
    append(es, descriptor(4, decoder));
    append(es, descriptor(6, {2}));
    auto esds = words({0});
    append(esds, descriptor(3, es));
    Bytes entry(28);
    entry[7] = 1;
    entry[17] = 2;
    entry[19] = 16;
    set32(entry, 24, 48000U << 16);
    append(entry, box("esds", esds));
    auto stsd = words({0, 1});
    append(stsd, box("mp4a", entry));
    return box("stsd", stsd);
}
Bytes movie(bool edit = true) {
    Bytes mvhd(100);
    set32(mvhd, 12, 1000);
    set32(mvhd, 16, 60);
    Bytes tkhd(84);
    set32(tkhd, 0, 3);
    set32(tkhd, 12, 1);
    set32(tkhd, 20, 60);
    Bytes mdhd(24);
    set32(mdhd, 12, 48000);
    Bytes handler(24);
    std::memcpy(handler.data() + 8, "soun", 4);
    const auto dref = box("dref", boxes({words({0, 1}), box("url ", words({1}))}));
    const auto stbl = box("stbl", boxes({aacDescription(), box("stts", words({0, 0})), box("stsc", words({0, 0})),
                                         box("stsz", words({0, 0, 0})), box("stco", words({0, 0}))}));
    const auto minf = box("minf", boxes({box("smhd", words({0, 0})), box("dinf", dref), stbl}));
    const auto mdia = box("mdia", boxes({box("mdhd", mdhd), box("hdlr", handler), minf}));
    auto trak = box("tkhd", tkhd);
    if (edit)
        append(trak, box("edts", box("elst", words({0, 1, 60, 128, 0x10000}))));
    append(trak, mdia);
    return box("moov",
               boxes({box("mvhd", mvhd), box("trak", trak), box("mvex", box("trex", words({0, 1, 1, 1024, 0, 0})))}));
}
struct Sample {
    Bytes data;
    std::uint32_t duration;
};
struct Fixture {
    Bytes data;
    std::vector<std::uint64_t> offsets;
    std::vector<Sample> samples{{{0xa1, 0xa2, 0xa3}, 1024}, {{0xb1, 0xb2}, 1024}, {{0xc1, 0xc2, 0xc3, 0xc4}, 960}};
};
struct FragmentOptions {
    std::uint32_t tfhd_flags{0x020000};
    std::uint32_t trun_flags{0x301};
    std::uint32_t description{1};
    std::uint32_t default_duration{1024};
    std::uint32_t default_size{3};
    std::int32_t composition{0};
    bool split_run{false};
    bool trun_v1{false};
};
void fragment(Fixture& f, std::uint32_t sequence, std::uint64_t time, std::size_t first, std::size_t count,
              FragmentOptions options = {}) {
    Bytes tfdt = words({0x01000000});
    u64(tfdt, time);
    Bytes tfhd = words({options.tfhd_flags, 1});
    if (options.tfhd_flags & 1)
        u64(tfhd, f.data.size());
    if (options.tfhd_flags & 2)
        u32(tfhd, options.description);
    if (options.tfhd_flags & 8)
        u32(tfhd, options.default_duration);
    if (options.tfhd_flags & 0x10)
        u32(tfhd, options.default_size);
    if (options.tfhd_flags & 0x20)
        u32(tfhd, 0);
    auto run = [&](std::size_t begin, std::size_t end, std::uint32_t flags, std::uint32_t offset) {
        Bytes result = words({flags | (options.trun_v1 ? 0x01000000U : 0U), static_cast<std::uint32_t>(end - begin)});
        if (flags & 1)
            u32(result, offset);
        if (flags & 4)
            u32(result, 0);
        for (auto i = begin; i < end; ++i) {
            if (flags & 0x100)
                u32(result, f.samples[i].duration);
            if (flags & 0x200)
                u32(result, static_cast<std::uint32_t>(f.samples[i].data.size()));
            if (flags & 0x400)
                u32(result, 0);
            if (flags & 0x800)
                u32(result, static_cast<std::uint32_t>(options.composition));
        }
        return box("trun", result);
    };
    const auto make = [&](std::uint32_t offset) {
        auto runs = options.split_run ? boxes({run(first, first + 1, options.trun_flags, offset),
                                               run(first + 1, first + count, options.trun_flags & ~1U, 0)})
                                      : run(first, first + count, options.trun_flags, offset);
        return box("moof", boxes({box("mfhd", words({0, sequence})),
                                  box("traf", boxes({box("tfhd", tfhd), box("tfdt", tfdt), runs}))}));
    };
    const auto moof = make(0);
    if ((options.tfhd_flags & 1) && !(options.trun_flags & 1)) {
        const auto base = f.data.size() + moof.size() + 8;
        set32(tfhd, 8, static_cast<std::uint32_t>(base >> 32));
        set32(tfhd, 12, static_cast<std::uint32_t>(base));
    }
    append(f.data, make(static_cast<std::uint32_t>(moof.size() + 8)));
    Bytes media;
    for (auto i = first; i < first + count; ++i) {
        f.offsets.push_back(f.data.size() + 8 + media.size());
        append(media, f.samples[i].data);
    }
    append(f.data, box("mdat", media));
}
Fixture fixture(bool edit = true) {
    Fixture f;
    f.data = box("ftyp", boxes({Bytes{'M', '4', 'A', ' '}, words({0}), Bytes{'i', 's', 'o', 'm'}}));
    append(f.data, movie(edit));
    fragment(f, 1, 0, 0, 2);
    fragment(f, 2, 2048, 2, 1);
    return f;
}
std::size_t locate(const Bytes& bytes, std::string_view type, std::size_t occurrence = 0) {
    for (std::size_t i = 4; i + 4 <= bytes.size(); ++i) {
        if (std::memcmp(bytes.data() + i, type.data(), 4) == 0 && occurrence-- == 0)
            return i - 4;
    }
    return bytes.size();
}
void replaceBox(Bytes& bytes, std::string_view type, const Bytes& replacement) {
    const auto at = locate(bytes, type);
    const auto oldSize = get32(bytes, at);
    const auto delta = static_cast<std::int64_t>(replacement.size()) - oldSize;
    for (const auto parent :
         {"moov", "trak", "mdia", "minf", "stbl", "dinf", "edts", "mvex", "moof", "traf", "stsd", "mp4a"}) {
        const auto p = locate(bytes, parent);
        if (p < at && p + get32(bytes, p) >= at + oldSize)
            set32(bytes, p, static_cast<std::uint32_t>(get32(bytes, p) + delta));
    }
    bytes.erase(bytes.begin() + static_cast<std::ptrdiff_t>(at),
                bytes.begin() + static_cast<std::ptrdiff_t>(at + oldSize));
    bytes.insert(bytes.begin() + static_cast<std::ptrdiff_t>(at), replacement.begin(), replacement.end());
}
std::size_t pathBox(const Bytes& bytes, std::initializer_list<std::string_view> path) {
    std::size_t start = 0, end = bytes.size(), found = bytes.size();
    for (const auto type : path) {
        found = bytes.size();
        for (auto at = start; at + 8 <= end;) {
            const auto small = get32(bytes, at);
            const auto size = small == 0   ? end - at
                              : small == 1 ? (std::uint64_t{get32(bytes, at + 8)} << 32) | get32(bytes, at + 12)
                                           : small;
            const auto header = small == 1 ? 16U : 8U;
            if (size < header || size > end - at)
                return bytes.size();
            if (std::memcmp(bytes.data() + at + 4, type.data(), 4) == 0) {
                found = at;
                start = at + header;
                end = at + static_cast<std::size_t>(size);
                break;
            }
            at += static_cast<std::size_t>(size);
        }
        if (found == bytes.size())
            return found;
    }
    return found;
}
Mp4AudioRemuxPlan inspect(const Bytes& bytes, Mp4AudioRemuxLimits limits = {}) {
    return inspectFragmentedMp4Audio(
        bytes.size(),
        [&](std::uint64_t offset, std::span<std::uint8_t> output) {
            if (offset > bytes.size() || output.size() > bytes.size() - offset)
                return false;
            std::copy_n(bytes.begin() + static_cast<std::ptrdiff_t>(offset), output.size(), output.begin());
            return true;
        },
        limits);
}
} // namespace

class FragmentedMp4AudioTests final : public QObject {
    Q_OBJECT
  private slots:
    void editPresentationDurationCannotOverflow() {
        auto f = fixture();
        auto list = words({0x01000000, 2});
        u64(list, std::numeric_limits<std::uint64_t>::max());
        u64(list, std::numeric_limits<std::uint64_t>::max());
        u32(list, 0x10000);
        u64(list, 60);
        u64(list, 128);
        u32(list, 0x10000);
        replaceBox(f.data, "elst", box("elst", list));
        QCOMPARE(inspect(f.data).status, Mp4AudioRemuxStatus::Invalid);
    }
    void boundedMutationSweepKeepsEveryReturnedRangeSafe() {
        const auto original = fixture();
        std::uint32_t state = 0x6d703461;
        auto random = [&] {
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;
            return state;
        };
        unsigned ready = 0;
        for (unsigned iteration = 0; iteration < 2048; ++iteration) {
            auto bytes = original.data;
            if (iteration % 4 == 0)
                bytes.resize(8 + random() % (bytes.size() - 7));
            else
                for (unsigned j = 0; j < 1 + iteration % 3; ++j)
                    bytes.at(random() % bytes.size()) ^= static_cast<std::uint8_t>(1U << (random() % 8));
            bool outOfBounds = false;
            const auto p = inspectFragmentedMp4Audio(
                bytes.size(),
                [&](std::uint64_t at, std::span<std::uint8_t> out) {
                    if (at > bytes.size() || out.size() > bytes.size() - at) {
                        outOfBounds = true;
                        return false;
                    }
                    std::copy_n(bytes.begin() + static_cast<std::ptrdiff_t>(at), out.size(), out.begin());
                    return true;
                },
                {.max_metadata_bytes = 16384, .max_samples = 64, .max_boxes = 128, .max_fragments = 8});
            QVERIFY(!outOfBounds);
            if (p.status != Mp4AudioRemuxStatus::Ready) {
                QVERIFY(p.header.empty());
                QVERIFY(p.packets.empty());
                continue;
            }
            ++ready;
            QVERIFY(!p.header.empty());
            QVERIFY(!p.packets.empty());
            QVERIFY(pathBox(p.header, {"moov", "trak", "mdia", "minf", "stbl", "stts"}) < p.header.size());
            std::uint64_t cursor = p.header.size();
            auto classic = p.header;
            for (const auto& range : p.packets) {
                QVERIFY(range.length != 0);
                QVERIFY(range.source_offset <= bytes.size());
                QVERIFY(range.length <= bytes.size() - range.source_offset);
                QCOMPARE(range.output_offset, cursor);
                classic.insert(classic.end(), bytes.begin() + static_cast<std::ptrdiff_t>(range.source_offset),
                               bytes.begin() + static_cast<std::ptrdiff_t>(range.source_offset + range.length));
                cursor += range.length;
            }
            QCOMPARE(p.normalized_size, cursor);
            QCOMPARE(inspect(classic).status, Mp4AudioRemuxStatus::Passthrough);
        }
        QVERIFY(ready > 0);
    }
    void zeroToEndMetadataIsReboxedWhenCanonicalOrderChanges_data() {
        QTest::addColumn<QString>("parent");
        QTest::addColumn<QString>("child");
        QTest::newRow("handler") << QStringLiteral("mdia") << QStringLiteral("hdlr");
        QTest::newRow("sound-header") << QStringLiteral("minf") << QStringLiteral("smhd");
        QTest::newRow("data-reference") << QStringLiteral("minf") << QStringLiteral("dinf");
        QTest::newRow("sample-description") << QStringLiteral("stbl") << QStringLiteral("stsd");
    }
    void zeroToEndMetadataIsReboxedWhenCanonicalOrderChanges() {
        QFETCH(QString, parent);
        QFETCH(QString, child);
        auto f = fixture();
        const auto pa = locate(f.data, parent.toStdString());
        const auto ch = locate(f.data, child.toStdString());
        const auto parentSize = get32(f.data, pa), childSize = get32(f.data, ch);
        Bytes body(f.data.begin() + static_cast<std::ptrdiff_t>(pa + 8),
                   f.data.begin() + static_cast<std::ptrdiff_t>(ch));
        body.insert(body.end(), f.data.begin() + static_cast<std::ptrdiff_t>(ch + childSize),
                    f.data.begin() + static_cast<std::ptrdiff_t>(pa + parentSize));
        Bytes moved(f.data.begin() + static_cast<std::ptrdiff_t>(ch),
                    f.data.begin() + static_cast<std::ptrdiff_t>(ch + childSize));
        set32(moved, 0, 0);
        append(body, moved);
        replaceBox(f.data, parent.toStdString(), box(parent.toStdString(), body));
        const auto p = inspect(f.data);
        QCOMPARE(p.status, Mp4AudioRemuxStatus::Ready);
        QVERIFY(pathBox(p.header, {"moov", "trak", "mdia", "hdlr"}) < p.header.size());
        QVERIFY(pathBox(p.header, {"moov", "trak", "mdia", "minf", "smhd"}) < p.header.size());
        QVERIFY(pathBox(p.header, {"moov", "trak", "mdia", "minf", "dinf", "dref"}) < p.header.size());
        QVERIFY(pathBox(p.header, {"moov", "trak", "mdia", "minf", "stbl", "stsd"}) < p.header.size());
        QVERIFY(pathBox(p.header, {"moov", "trak", "mdia", "minf", "stbl", "stts"}) < p.header.size());
    }
    void metadataReloadMustMatchTheScannedBoxHeader_data() {
        QTest::addColumn<QString>("name");
        QTest::newRow("ftyp-length-changed") << QStringLiteral("ftyp");
        QTest::newRow("moov-type-changed") << QStringLiteral("moov");
        QTest::newRow("moof-length-changed") << QStringLiteral("moof");
    }
    void metadataReloadMustMatchTheScannedBoxHeader() {
        QFETCH(QString, name);
        const auto f = fixture();
        const auto at = locate(f.data, name.toStdString());
        bool changed = false;
        const auto p = inspectFragmentedMp4Audio(f.data.size(), [&](std::uint64_t offset, std::span<std::uint8_t> out) {
            if (offset > f.data.size() || out.size() > f.data.size() - offset)
                return false;
            std::copy_n(f.data.begin() + static_cast<std::ptrdiff_t>(offset), out.size(), out.begin());
            if (offset == at && out.size() > 16) {
                changed = true;
                if (name == "moov")
                    std::memcpy(out.data() + 4, "free", 4);
                else {
                    out[0] = 0;
                    out[1] = 0;
                    out[2] = 0;
                    out[3] = 8;
                }
            }
            return true;
        });
        QVERIFY(changed);
        QCOMPARE(p.status, Mp4AudioRemuxStatus::Invalid);
        QVERIFY(p.header.empty());
        QVERIFY(p.packets.empty());
    }
    void unsupportedFragmentProfiles_data() {
        QTest::addColumn<int>("mutation");
        QTest::addColumn<int>("expected");
        const auto invalid = int(Mp4AudioRemuxStatus::Invalid);
        const auto unsupported = int(Mp4AudioRemuxStatus::Unsupported);
        QTest::newRow("sample-description-override") << 0 << invalid;
        QTest::newRow("trex-description") << 1 << invalid;
        QTest::newRow("positive-composition-offset") << 2 << unsupported;
        QTest::newRow("negative-composition-offset") << 3 << unsupported;
        QTest::newRow("unknown-tfhd-flag") << 4 << unsupported;
        QTest::newRow("unknown-trun-flag") << 5 << unsupported;
        QTest::newRow("conflicting-first-sample-flags") << 6 << invalid;
        QTest::newRow("conflicting-base-flags") << 7 << invalid;
        QTest::newRow("empty-duration-fragment") << 8 << unsupported;
        QTest::newRow("encrypted-auxiliary-data") << 9 << unsupported;
        QTest::newRow("missing-decode-timestamp") << 10 << invalid;
        QTest::newRow("duplicate-tfhd") << 11 << invalid;
        QTest::newRow("multiple-descriptions") << 12 << unsupported;
        QTest::newRow("video-handler") << 13 << unsupported;
        QTest::newRow("unknown-movie-version") << 14 << unsupported;
        QTest::newRow("dependent-elementary-stream") << 15 << unsupported;
        QTest::newRow("external-clock-stream") << 16 << unsupported;
    }
    void unsupportedFragmentProfiles() {
        QFETCH(int, mutation);
        QFETCH(int, expected);
        auto f = fixture(false);
        f.data.resize(locate(f.data, "moof"));
        f.offsets.clear();
        FragmentOptions options;
        switch (mutation) {
        case 0:
            options.tfhd_flags |= 2;
            options.description = 2;
            break;
        case 2:
            options.trun_flags |= 0x800;
            options.composition = 1;
            break;
        case 3:
            options.trun_flags |= 0x800;
            options.composition = -1;
            options.trun_v1 = true;
            break;
        case 4:
            options.tfhd_flags |= 4;
            break;
        case 5:
            options.trun_flags |= 2;
            break;
        case 6:
            options.trun_flags |= 0x404;
            break;
        case 7:
            options.tfhd_flags |= 1;
            break;
        case 8:
            options.tfhd_flags |= 0x10000;
            break;
        }
        fragment(f, 1, 0, 0, 3, options);
        if (mutation == 1)
            set32(f.data, locate(f.data, "trex") + 16, 2);
        if (mutation == 9 || mutation == 11) {
            const auto pos = locate(f.data, "tfhd");
            Bytes original(f.data.begin() + static_cast<std::ptrdiff_t>(pos),
                           f.data.begin() + static_cast<std::ptrdiff_t>(pos + get32(f.data, pos)));
            replaceBox(f.data, "tfhd", boxes({original, mutation == 9 ? box("senc", words({0, 0})) : original}));
        }
        if (mutation == 10)
            std::memcpy(f.data.data() + locate(f.data, "tfdt") + 4, "free", 4);
        if (mutation == 12) {
            const auto at = locate(f.data, "mp4a");
            const Bytes entry(f.data.begin() + static_cast<std::ptrdiff_t>(at),
                              f.data.begin() + static_cast<std::ptrdiff_t>(at + get32(f.data, at)));
            replaceBox(f.data, "stsd", box("stsd", boxes({words({0, 2}), entry, entry})));
        }
        if (mutation == 13)
            std::memcpy(f.data.data() + locate(f.data, "hdlr") + 16, "vide", 4);
        if (mutation == 14)
            f.data.at(locate(f.data, "mvhd") + 8) = 2;
        if (mutation == 15 || mutation == 16) {
            const auto at = locate(f.data, "esds");
            Bytes es{0, 1, static_cast<std::uint8_t>(mutation == 15 ? 0x80 : 0x20), 0, 2};
            es.insert(es.end(), f.data.begin() + static_cast<std::ptrdiff_t>(at + 17),
                      f.data.begin() + static_cast<std::ptrdiff_t>(at + get32(f.data, at)));
            replaceBox(f.data, "esds", box("esds", boxes({words({0}), descriptor(3, es)})));
        }
        const auto p = inspect(f.data);
        QCOMPARE(int(p.status), expected);
        QVERIFY(p.packets.empty());
        QVERIFY(p.header.empty());
    }
    void sparseLargeMdatUses64BitSizeWithoutReadingOrAllocatingPayload() {
        auto f = fixture(false);
        f.data.resize(locate(f.data, "moof"));
        f.offsets.clear();
        fragment(f, 1, 0, 0, 2);
        const auto mdat = locate(f.data, "mdat");
        f.data.resize(mdat);
        append(f.data, words({1, 0x6d646174}));
        u64(f.data, 0x100000010ULL);
        const auto run = locate(f.data, "trun");
        set32(f.data, run + 16, get32(f.data, run + 16) + 8);
        set32(f.data, run + 24, 0x80000000);
        set32(f.data, run + 32, 0x80000000);
        const auto sourceSize = static_cast<std::uint64_t>(f.data.size()) + 0x100000000ULL;
        bool payloadRead = false;
        const auto p = inspectFragmentedMp4Audio(sourceSize, [&](std::uint64_t at, std::span<std::uint8_t> out) {
            if (at > sourceSize || out.size() > sourceSize - at)
                return false;
            if (at >= f.data.size()) {
                payloadRead = true;
                std::fill(out.begin(), out.end(), 0);
                return true;
            }
            if (out.size() > f.data.size() - at)
                return false;
            std::copy_n(f.data.begin() + static_cast<std::ptrdiff_t>(at), out.size(), out.begin());
            return true;
        });
        QCOMPARE(p.status, Mp4AudioRemuxStatus::Ready);
        QVERIFY(!payloadRead);
        QCOMPARE(p.packets.size(), std::size_t{2});
        QCOMPARE(p.packets[0].length, 0x80000000U);
        QCOMPARE(p.packets[1].source_offset, std::uint64_t{f.data.size()} + 0x80000000ULL);
        const auto outputMdat = locate(p.header, "mdat");
        QCOMPARE(get32(p.header, outputMdat), 1U);
        QCOMPARE(get32(p.header, outputMdat + 8), 1U);
        QCOMPARE(get32(p.header, outputMdat + 12), 16U);
        QCOMPARE(p.normalized_size, p.header.size() + 0x100000000ULL);
        QCOMPARE(get32(p.header, locate(p.header, "stco") + 16), p.header.size());
    }
    void trustedGoogleShapedFixturePreservesEveryAacPacket_data() {
        QTest::addColumn<unsigned>("silence");
        QTest::newRow("trim") << 0U;
        QTest::newRow("trim-with-leading-silence") << 4800U;
    }
    void trustedGoogleShapedFixturePreservesEveryAacPacket() {
        QFETCH(unsigned, silence);
        QFile file{QFINDTESTDATA("../audio/corpus/fixtures/valid/m4a-aac-lc.m4a")};
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto classic = file.readAll();
        QCOMPARE(QCryptographicHash::hash(classic, QCryptographicHash::Sha256).toHex(),
                 QByteArray("aefe894a1a9277bf4795d421c97d4780fa342eef300819a459ca45329a614558"));
        const auto input = fragmented_aac_fixture::make(classic, 4800, 24000, silence);
        QVERIFY(!input.bytes.isEmpty());
        const Bytes bytes(input.bytes.begin(), input.bytes.end());
        const auto p = inspect(bytes);
        QCOMPARE(p.status, Mp4AudioRemuxStatus::Ready);
        QCOMPARE(p.movie_timescale, 48000U);
        QCOMPARE(p.media_timescale, 48000U);
        QCOMPARE(p.edits.size(), silence ? std::size_t{2} : std::size_t{1});
        if (silence) {
            QCOMPARE(p.edits[0].media_time, -1);
            QCOMPARE(p.edits[0].movie_duration, 4800U);
        }
        QCOMPARE(p.edits.back().media_time, 4800);
        QCOMPARE(p.edits.back().movie_duration, 24000U);
        const auto sizes = fragmented_aac_fixture::trustedBox(classic, "stsz");
        const auto chunks = fragmented_aac_fixture::trustedBox(classic, "stco");
        const auto count = qFromBigEndian<quint32>(sizes.constData() + 16);
        std::uint64_t cursor = qFromBigEndian<quint32>(chunks.constData() + 16);
        QCOMPARE(p.packets.size(), count);
        for (std::size_t i = 0; i < p.packets.size(); ++i) {
            const auto& packet = p.packets[i];
            const auto length = qFromBigEndian<quint32>(sizes.constData() + 20 + i * 4);
            QCOMPARE(packet.length, length);
            QVERIFY(cursor + length <= static_cast<std::uint64_t>(classic.size()));
            QVERIFY(packet.source_offset + length <= bytes.size());
            QCOMPARE(std::memcmp(bytes.data() + packet.source_offset, classic.constData() + cursor, length), 0);
            cursor += length;
        }
        QVERIFY(get32(p.header, locate(p.header, "mdhd") + 24) > 24000U + silence);
        QCOMPARE(locate(p.header, "mvex"), p.header.size());
        QCOMPARE(locate(p.header, "edts"), p.header.size());
    }
    void extendedWidthsAndZeroToEndAreSupported() {
        auto f = fixture();
        const auto ftypSize = get32(f.data, 0);
        Bytes extended = words({1, 0x66747970});
        u64(extended, ftypSize + 8);
        extended.insert(extended.end(), f.data.begin() + 8, f.data.begin() + ftypSize);
        replaceBox(f.data, "ftyp", extended);
        const auto stco = locate(f.data, "stco");
        set32(f.data, stco, 0);
        const auto lastMdat = locate(f.data, "mdat", 1);
        set32(f.data, lastMdat, 0);
        const auto p = inspect(f.data);
        QCOMPARE(p.status, Mp4AudioRemuxStatus::Ready);
        QCOMPARE(p.packets.size(), std::size_t{3});
        QCOMPARE(p.packets.back().source_offset, lastMdat + 8);
        QCOMPARE(get32(p.header, 0), 1U);
        f = fixture();
        const auto mdat = locate(f.data, "mdat", 1);
        Bytes payload(f.data.begin() + static_cast<std::ptrdiff_t>(mdat + 8), f.data.end());
        auto wideMdat = words({1, 0x6d646174});
        u64(wideMdat, payload.size() + 16);
        append(wideMdat, payload);
        f.data.resize(mdat);
        append(f.data, wideMdat);
        const auto run = locate(f.data, "trun", 1);
        set32(f.data, run + 16, get32(f.data, run + 16) + 8);
        const auto widePlan = inspect(f.data);
        QCOMPARE(widePlan.status, Mp4AudioRemuxStatus::Ready);
        QCOMPARE(widePlan.packets.back().source_offset, mdat + 16);
    }
    void temporalV1AndDurationPromotionRetainMetadata() {
        auto f = fixture(false);
        for (const auto name : {"mvhd", "tkhd", "mdhd"}) {
            const bool track = std::string_view(name) == "tkhd";
            const auto old = locate(f.data, name);
            const auto oldSize = get32(f.data, old);
            const std::size_t duration = track ? 20 : 16;
            auto body = words({0x01000000U | (track ? 3U : 0U)});
            u64(body, 0x123456789ULL);
            u64(body, 0x23456789aULL);
            body.insert(body.end(), f.data.begin() + static_cast<std::ptrdiff_t>(old + 20),
                        f.data.begin() + static_cast<std::ptrdiff_t>(old + 8 + duration));
            u64(body, 0x123456789ULL);
            body.insert(body.end(), f.data.begin() + static_cast<std::ptrdiff_t>(old + 8 + duration + 4),
                        f.data.begin() + static_cast<std::ptrdiff_t>(old + oldSize));
            replaceBox(f.data, name, box(name, body));
        }
        const auto p = inspect(f.data);
        QCOMPARE(p.status, Mp4AudioRemuxStatus::Ready);
        for (const auto name : {"mvhd", "tkhd", "mdhd"}) {
            const auto at = locate(p.header, name);
            QCOMPARE(p.header.at(at + 8), 1);
            QCOMPARE(get32(p.header, at + 12), 1U);
            QCOMPARE(get32(p.header, at + 16), 0x23456789U);
        }
        QCOMPARE(get32(p.header, locate(p.header, "mvhd") + 36), 63U);
        QCOMPARE(get32(p.header, locate(p.header, "tkhd") + 40), 63U);
        QCOMPARE(get32(p.header, locate(p.header, "mdhd") + 36), 3008U);
        f = fixture(false);
        f.data.resize(locate(f.data, "moof"));
        f.offsets.clear();
        set32(f.data, locate(f.data, "mdhd") + 20, 1);
        set32(f.data, locate(f.data, "mvhd") + 20, 1);
        f.samples[0].duration = 0xffffffff;
        f.samples[1].duration = 1;
        f.samples[2].duration = 1;
        fragment(f, 1, 0, 0, 3);
        const auto promoted = inspect(f.data);
        QCOMPARE(promoted.status, Mp4AudioRemuxStatus::Ready);
        for (const auto name : {"mvhd", "tkhd", "mdhd"}) {
            const auto at = locate(promoted.header, name);
            const auto duration = std::string_view(name) == "tkhd" ? 36U : 32U;
            QCOMPARE(promoted.header.at(at + 8), 1);
            QCOMPARE(get32(promoted.header, at + duration), 1U);
            QCOMPARE(get32(promoted.header, at + duration + 4), 1U);
        }
    }
    void offsetAndDurationArithmeticCannotWrap_data() {
        QTest::addColumn<int>("kind");
        QTest::newRow("base-plus-signed-offset") << 0;
        QTest::newRow("packet-end-overflow") << 1;
        QTest::newRow("movie-timescale-overflow") << 2;
    }
    void offsetAndDurationArithmeticCannotWrap() {
        QFETCH(int, kind);
        auto f = fixture(false);
        f.data.resize(locate(f.data, "moof"));
        f.offsets.clear();
        fragment(f, 1, 0, 0, 3, {.tfhd_flags = 1, .trun_flags = kind == 1 ? 0x300U : 0x301U});
        const auto base = locate(f.data, "tfhd");
        if (kind < 2) {
            set32(f.data, base + 16, 0xffffffff);
            set32(f.data, base + 20, kind == 1 ? 0xfffffffe : 0xffffffff);
        } else {
            set32(f.data, locate(f.data, "mdhd") + 20, 1);
            set32(f.data, locate(f.data, "mvhd") + 20, 0xffffffff);
            const auto trun = locate(f.data, "trun");
            for (std::size_t i = 0; i < 3; ++i)
                set32(f.data, trun + 20 + i * 8, 0xffffffff);
        }
        const auto p = inspect(f.data);
        QCOMPARE(p.status, Mp4AudioRemuxStatus::Invalid);
        QVERIFY(p.packets.empty());
        QVERIFY(p.header.empty());
    }
    void metadataBudgetAlsoBoundsExpandedCanonicalTables() {
        auto f = fixture(false);
        f.data.resize(locate(f.data, "moof"));
        f.offsets.clear();
        f.samples.assign(1000, {{1, 2, 3}, 1024});
        fragment(f, 1, 0, 0, 1000, {.tfhd_flags = 0x20018, .trun_flags = 1});
        QCOMPARE(inspect(f.data).status, Mp4AudioRemuxStatus::Ready);
        QCOMPARE(inspect(f.data, {.max_metadata_bytes = 2000}).status, Mp4AudioRemuxStatus::LimitExceeded);
    }
    void planningNeverReadsMediaPayload() {
        const auto f = fixture();
        bool mediaRead = false;
        std::uint64_t total = 0;
        const auto p = inspectFragmentedMp4Audio(f.data.size(), [&](std::uint64_t at, std::span<std::uint8_t> out) {
            total += out.size();
            for (std::size_t i = 0; i < f.samples.size(); ++i)
                if (at < f.offsets[i] + f.samples[i].data.size() && at + out.size() > f.offsets[i])
                    mediaRead = true;
            if (at > f.data.size() || out.size() > f.data.size() - at)
                return false;
            std::copy_n(f.data.begin() + static_cast<std::ptrdiff_t>(at), out.size(), out.begin());
            return true;
        });
        QCOMPARE(p.status, Mp4AudioRemuxStatus::Ready);
        QVERIFY(!mediaRead);
        QVERIFY(total < 8 * 1024 * 1024);
    }
    void hostileStructures_data() {
        QTest::addColumn<int>("mutation");
        QTest::addColumn<int>("expected");
        const auto invalid = int(Mp4AudioRemuxStatus::Invalid);
        const auto unsupported = int(Mp4AudioRemuxStatus::Unsupported);
        QTest::newRow("short-box-size") << 0 << invalid;
        QTest::newRow("extended-size-overflow") << 1 << invalid;
        QTest::newRow("short-nested-box") << 2 << invalid;
        QTest::newRow("trun-count-overrun") << 3 << invalid;
        QTest::newRow("stsd-count-overrun") << 4 << invalid;
        QTest::newRow("dref-count-overrun") << 5 << invalid;
        QTest::newRow("stsz-count-overrun") << 6 << invalid;
        QTest::newRow("elst-count-overrun") << 7 << invalid;
        QTest::newRow("mismatched-fragment-track") << 8 << invalid;
        QTest::newRow("mismatched-trex-track") << 9 << invalid;
        QTest::newRow("noncontiguous-tfdt") << 10 << unsupported;
        QTest::newRow("nonzero-first-tfdt") << 11 << unsupported;
        QTest::newRow("missing-trun") << 12 << invalid;
        QTest::newRow("zero-sample-size") << 13 << invalid;
        QTest::newRow("zero-sample-duration") << 14 << invalid;
        QTest::newRow("zero-media-timescale") << 15 << invalid;
        QTest::newRow("duplicate-sequence") << 16 << invalid;
        QTest::newRow("truncated-mehd") << 17 << invalid;
        QTest::newRow("edit-beyond-complete-media") << 18 << invalid;
        QTest::newRow("multiple-tracks") << 19 << unsupported;
        QTest::newRow("nonempty-uniform-classic-table") << 20 << unsupported;
    }
    void hostileStructures() {
        QFETCH(int, mutation);
        QFETCH(int, expected);
        auto f = fixture();
        switch (mutation) {
        case 0:
            set32(f.data, 0, 7);
            break;
        case 1:
            set32(f.data, 0, 1);
            set32(f.data, 8, 0xffffffff);
            set32(f.data, 12, 0xffffffff);
            break;
        case 2:
            set32(f.data, locate(f.data, "mdhd"), 4);
            break;
        case 3:
            set32(f.data, locate(f.data, "trun") + 12, 0xffffffff);
            break;
        case 4:
            set32(f.data, locate(f.data, "stsd") + 12, 0xffffffff);
            break;
        case 5:
            set32(f.data, locate(f.data, "dref") + 12, 0xffffffff);
            break;
        case 6:
            set32(f.data, locate(f.data, "stsz") + 16, 0xffffffff);
            break;
        case 7:
            set32(f.data, locate(f.data, "elst") + 12, 0xffffffff);
            break;
        case 8:
            set32(f.data, locate(f.data, "tfhd", 1) + 12, 2);
            break;
        case 9:
            set32(f.data, locate(f.data, "trex") + 12, 2);
            break;
        case 10:
            set32(f.data, locate(f.data, "tfdt", 1) + 16, 2049);
            break;
        case 11:
            set32(f.data, locate(f.data, "tfdt") + 16, 1);
            break;
        case 12:
            std::memcpy(f.data.data() + locate(f.data, "trun") + 4, "free", 4);
            break;
        case 13:
            set32(f.data, locate(f.data, "trun") + 24, 0);
            break;
        case 14:
            set32(f.data, locate(f.data, "trun") + 20, 0);
            break;
        case 15:
            set32(f.data, locate(f.data, "mdhd") + 20, 0);
            break;
        case 16:
            set32(f.data, locate(f.data, "mfhd", 1) + 12, 1);
            break;
        case 17: {
            const auto p = locate(f.data, "trex");
            const Bytes trex(f.data.begin() + static_cast<std::ptrdiff_t>(p),
                             f.data.begin() + static_cast<std::ptrdiff_t>(p + get32(f.data, p)));
            replaceBox(f.data, "trex", boxes({trex, box("mehd", words({0}))}));
            break;
        }
        case 18:
            set32(f.data, locate(f.data, "elst") + 20, 3009);
            break;
        case 19: {
            const auto p = locate(f.data, "trak");
            const Bytes track(f.data.begin() + static_cast<std::ptrdiff_t>(p),
                              f.data.begin() + static_cast<std::ptrdiff_t>(p + get32(f.data, p)));
            replaceBox(f.data, "trak", boxes({track, track}));
            break;
        }
        case 20:
            set32(f.data, locate(f.data, "stsz") + 12, 3);
            set32(f.data, locate(f.data, "stsz") + 16, 1);
            break;
        }
        const auto p = inspect(f.data);
        QCOMPARE(int(p.status), expected);
        QVERIFY(p.header.empty());
        QVERIFY(p.packets.empty());
        QVERIFY(p.edits.empty());
    }
    void classicExtensionsAndMetadataDoNotEnterFragmentedRemuxRestrictions() {
        const auto f = fixture(false);
        const auto plan = inspect(f.data);
        QCOMPARE(plan.status, Mp4AudioRemuxStatus::Ready);
        auto classic = plan.header;
        for (const auto& packet : plan.packets)
            classic.insert(classic.end(), f.data.begin() + static_cast<std::ptrdiff_t>(packet.source_offset),
                           f.data.begin() + static_cast<std::ptrdiff_t>(packet.source_offset + packet.length));
        QCOMPARE(inspect(classic).status, Mp4AudioRemuxStatus::Passthrough);
        auto extended = classic;
        append(extended, box("uuid", Bytes(16, 0x42)));
        QCOMPARE(inspect(extended).status, Mp4AudioRemuxStatus::Passthrough);
        auto limits = Mp4AudioRemuxLimits{};
        limits.max_metadata_bytes = 256;
        QVERIFY(get32(classic, locate(classic, "moov")) > limits.max_metadata_bytes);
        QCOMPARE(inspect(classic, limits).status, Mp4AudioRemuxStatus::Passthrough);
        auto fragmented = f.data;
        append(fragmented, box("uuid", Bytes(16, 0x42)));
        QCOMPARE(inspect(fragmented).status, Mp4AudioRemuxStatus::Unsupported);
        QCOMPARE(inspect(f.data, limits).status, Mp4AudioRemuxStatus::LimitExceeded);
    }
    void canonicalClassicAndOtherFormatsPassThrough() {
        const auto f = fixture(false);
        const auto plan = inspect(f.data);
        QCOMPARE(plan.status, Mp4AudioRemuxStatus::Ready);
        QVERIFY(plan.edits.empty());
        auto classic = plan.header;
        for (const auto& packet : plan.packets)
            classic.insert(classic.end(), f.data.begin() + static_cast<std::ptrdiff_t>(packet.source_offset),
                           f.data.begin() + static_cast<std::ptrdiff_t>(packet.source_offset + packet.length));
        const auto p = inspect(classic);
        QCOMPARE(p.status, Mp4AudioRemuxStatus::Passthrough);
        QVERIFY(p.header.empty());
        QVERIFY(p.packets.empty());
        QCOMPARE(inspect(Bytes{'R', 'I', 'F', 'F', 0, 0, 0, 0}).status, Mp4AudioRemuxStatus::Passthrough);
        QCOMPARE(inspect({}).status, Mp4AudioRemuxStatus::Passthrough);
        replaceBox(classic, "ftyp", box("ftyp", {}));
        QCOMPARE(inspect(classic).status, Mp4AudioRemuxStatus::Invalid);
    }
    void truncationAndReadFailuresNeverReturnPartialPlan() {
        const auto f = fixture();
        const std::vector<std::size_t> cuts{8,
                                            12,
                                            19,
                                            locate(f.data, "moov") + 7,
                                            locate(f.data, "tfhd") + 10,
                                            locate(f.data, "mdat") + 9,
                                            f.data.size() - 1};
        for (const auto n : cuts) {
            const Bytes shortInput(f.data.begin(), f.data.begin() + static_cast<std::ptrdiff_t>(n));
            QCOMPARE(inspect(shortInput).status, Mp4AudioRemuxStatus::Invalid);
        }
        QCOMPARE(inspectFragmentedMp4Audio(f.data.size(), {}).status, Mp4AudioRemuxStatus::Invalid);
        QCOMPARE(inspectFragmentedMp4Audio(f.data.size(), [](auto, auto) { return false; }).status,
                 Mp4AudioRemuxStatus::Invalid);
        QCOMPARE(inspectFragmentedMp4Audio(f.data.size(), [](auto, auto) -> bool { throw std::runtime_error("read"); })
                     .status,
                 Mp4AudioRemuxStatus::Invalid);
        auto read = [&](std::uint64_t at, std::span<std::uint8_t> out) {
            if (at > f.data.size() || out.size() > f.data.size() - at)
                return false;
            std::copy_n(f.data.begin() + static_cast<std::ptrdiff_t>(at), out.size(), out.begin());
            return true;
        };
        QCOMPARE(inspectFragmentedMp4Audio(f.data.size() + 1, read).status, Mp4AudioRemuxStatus::Invalid);
        QCOMPARE(inspectFragmentedMp4Audio(f.data.size() - 1, read).status, Mp4AudioRemuxStatus::Invalid);
    }
    void configuredBudgetsAreEnforced_data() {
        QTest::addColumn<int>("kind");
        QTest::newRow("metadata") << 0;
        QTest::newRow("samples") << 1;
        QTest::newRow("boxes") << 2;
        QTest::newRow("fragments") << 3;
        QTest::newRow("zero-metadata") << 4;
        QTest::newRow("zero-samples") << 5;
        QTest::newRow("zero-boxes") << 6;
        QTest::newRow("zero-fragments") << 7;
    }
    void configuredBudgetsAreEnforced() {
        QFETCH(int, kind);
        auto limits = Mp4AudioRemuxLimits{};
        switch (kind) {
        case 0:
            limits.max_metadata_bytes = 64;
            break;
        case 1:
            limits.max_samples = 2;
            break;
        case 2:
            limits.max_boxes = 5;
            break;
        case 3:
            limits.max_fragments = 1;
            break;
        case 4:
            limits.max_metadata_bytes = 0;
            break;
        case 5:
            limits.max_samples = 0;
            break;
        case 6:
            limits.max_boxes = 0;
            break;
        case 7:
            limits.max_fragments = 0;
            break;
        }
        const auto p = inspect(fixture().data, limits);
        QCOMPARE(p.status, Mp4AudioRemuxStatus::LimitExceeded);
        QVERIFY(p.header.empty());
        QVERIFY(p.packets.empty());
        QVERIFY(p.edits.empty());
    }
    void depthAndDefaultCountCannotAllocateUnboundedly() {
        auto f = fixture();
        auto nested = box("free", {});
        for (int i = 0; i < 26; ++i)
            nested = box("moov", nested);
        replaceBox(f.data, "moov", nested);
        QCOMPARE(inspect(f.data).status, Mp4AudioRemuxStatus::LimitExceeded);
        f = fixture();
        f.data.resize(locate(f.data, "moof"));
        f.offsets.clear();
        fragment(f, 1, 0, 0, 3, {.tfhd_flags = 0x20018, .trun_flags = 1});
        set32(f.data, locate(f.data, "trun") + 12, 0xffffffff);
        QCOMPARE(inspect(f.data).status, Mp4AudioRemuxStatus::LimitExceeded);
    }
    void unsafeSampleDescriptionsAreRejected_data() {
        QTest::addColumn<int>("mutation");
        QTest::addColumn<int>("expected");
        QTest::newRow("external-url") << 0 << int(Mp4AudioRemuxStatus::Unsupported);
        QTest::newRow("external-urn") << 1 << int(Mp4AudioRemuxStatus::Unsupported);
        QTest::newRow("dangling-data-reference") << 2 << int(Mp4AudioRemuxStatus::Invalid);
        QTest::newRow("encrypted-sample-entry") << 3 << int(Mp4AudioRemuxStatus::Unsupported);
        QTest::newRow("protection-inside-mp4a") << 4 << int(Mp4AudioRemuxStatus::Unsupported);
        QTest::newRow("quicktime-sample-entry") << 5 << int(Mp4AudioRemuxStatus::Unsupported);
        QTest::newRow("mp3-object-type-in-mp4a") << 6 << int(Mp4AudioRemuxStatus::Unsupported);
        QTest::newRow("unsupported-audio-object-type") << 7 << int(Mp4AudioRemuxStatus::Unsupported);
        QTest::newRow("decoder-config-length-overrun") << 8 << int(Mp4AudioRemuxStatus::Invalid);
        QTest::newRow("es-descriptor-length-overrun") << 9 << int(Mp4AudioRemuxStatus::Invalid);
        QTest::newRow("external-es-url") << 10 << int(Mp4AudioRemuxStatus::Unsupported);
        QTest::newRow("unterminated-descriptor-length") << 11 << int(Mp4AudioRemuxStatus::Invalid);
        QTest::newRow("missing-esds") << 12 << int(Mp4AudioRemuxStatus::Invalid);
    }
    void unsafeSampleDescriptionsAreRejected() {
        QFETCH(int, mutation);
        QFETCH(int, expected);
        auto f = fixture();
        const auto entry = locate(f.data, "mp4a");
        const auto esds = locate(f.data, "esds");
        switch (mutation) {
        case 0:
            set32(f.data, locate(f.data, "url ") + 8, 0);
            break;
        case 1:
            std::memcpy(f.data.data() + locate(f.data, "url ") + 4, "urn ", 4);
            break;
        case 2:
            f.data.at(entry + 15) = 2;
            break;
        case 3:
            std::memcpy(f.data.data() + entry + 4, "enca", 4);
            break;
        case 4: {
            Bytes original(f.data.begin() + static_cast<std::ptrdiff_t>(esds),
                           f.data.begin() + static_cast<std::ptrdiff_t>(esds + get32(f.data, esds)));
            replaceBox(f.data, "esds", boxes({original, box("sinf", box("frma", Bytes{'m', 'p', '4', 'a'}))}));
            break;
        }
        case 5:
            set32(f.data, entry + 16, 0x00010000);
            break;
        case 6:
            QCOMPARE(f.data.at(esds + 19), 0x40);
            f.data.at(esds + 19) = 0x6b;
            break;
        case 7:
            f.data.at(esds + 34) = 0x31;
            break;
        case 8:
            f.data.at(esds + 33) = 127;
            break;
        case 9:
            f.data.at(esds + 13) = 127;
            break;
        case 10:
            f.data.at(esds + 16) = 0x40;
            break;
        case 11:
            replaceBox(f.data, "esds", box("esds", boxes({words({0}), {3, 0x80, 0x80, 0x80, 0x80}})));
            break;
        case 12:
            replaceBox(f.data, "esds", box("btrt", words({0, 0, 0})));
            break;
        }
        const auto p = inspect(f.data);
        QCOMPARE(int(p.status), expected);
        QVERIFY(p.header.empty());
        QVERIFY(p.packets.empty());
    }
    void leadingSilenceAndV1EditsAreReturnedExactly() {
        auto f = fixture();
        auto list = words({0x01000000, 2});
        u64(list, 10);
        u64(list, std::numeric_limits<std::uint64_t>::max());
        u32(list, 0x10000);
        u64(list, 60);
        u64(list, 128);
        u32(list, 0x10000);
        replaceBox(f.data, "edts", box("edts", box("elst", list)));
        const auto p = inspect(f.data);
        QCOMPARE(p.status, Mp4AudioRemuxStatus::Ready);
        QCOMPARE(p.edits.size(), std::size_t{2});
        QCOMPARE(p.edits[0].media_time, -1);
        QCOMPARE(p.edits[0].movie_duration, 10U);
        QCOMPARE(p.edits[1].media_time, 128);
        QCOMPARE(p.edits[1].movie_duration, 60U);
        QCOMPARE(locate(p.header, "edts"), p.header.size());
        QCOMPARE(get32(p.header, locate(p.header, "mdhd") + 24), 3008U);
    }
    void unsupportedEditsNeverLoseIntent_data() {
        QTest::addColumn<int>("mutation");
        QTest::newRow("dwell-rate") << 0;
        QTest::newRow("double-rate") << 1;
        QTest::newRow("fractional-rate") << 2;
        QTest::newRow("negative-other-than-empty") << 3;
        QTest::newRow("only-empty") << 4;
        QTest::newRow("two-media-edits") << 5;
        QTest::newRow("trailing-empty") << 6;
        QTest::newRow("three-edits") << 7;
        QTest::newRow("zero-duration") << 8;
        QTest::newRow("empty-edit-list") << 9;
    }
    void unsupportedEditsNeverLoseIntent() {
        QFETCH(int, mutation);
        auto f = fixture();
        auto list = words({0, 1, 60, 128, 0x10000});
        switch (mutation) {
        case 0:
            set32(list, 16, 0);
            break;
        case 1:
            set32(list, 16, 0x20000);
            break;
        case 2:
            set32(list, 16, 0x10001);
            break;
        case 3:
            set32(list, 12, 0xfffffffe);
            break;
        case 4:
            set32(list, 12, 0xffffffff);
            break;
        case 5:
            list = words({0, 2, 20, 0, 0x10000, 40, 1024, 0x10000});
            break;
        case 6:
            list = words({0, 2, 50, 0, 0x10000, 10, 0xffffffff, 0x10000});
            break;
        case 7:
            list = words({0, 3, 10, 0xffffffff, 0x10000, 20, 0, 0x10000, 30, 1024, 0x10000});
            break;
        case 8:
            set32(list, 8, 0);
            break;
        case 9:
            list = words({0, 0});
            break;
        }
        replaceBox(f.data, "elst", box("elst", list));
        const auto p = inspect(f.data);
        QCOMPARE(p.status, Mp4AudioRemuxStatus::Unsupported);
        QVERIFY(p.header.empty());
        QVERIFY(p.edits.empty());
        QVERIFY(p.packets.empty());
    }
    void packetBoundsAreEntirelyLocal_data() {
        QTest::addColumn<int>("mutation");
        QTest::newRow("moof-instead-of-media") << 0;
        QTest::newRow("mdat-header-instead-of-payload") << 1;
        QTest::newRow("packet-crosses-mdat-end") << 2;
        QTest::newRow("offset-beyond-source") << 3;
        QTest::newRow("overlapping-across-fragments") << 4;
        QTest::newRow("offset-before-source") << 5;
    }
    void packetBoundsAreEntirelyLocal() {
        QFETCH(int, mutation);
        auto f = fixture();
        const auto run = locate(f.data, "trun");
        switch (mutation) {
        case 0:
            set32(f.data, run + 16, 0);
            break;
        case 1:
            set32(f.data, run + 16, get32(f.data, run + 16) - 8);
            break;
        case 2:
            set32(f.data, run + 24, 6);
            break;
        case 3:
            set32(f.data, run + 16, 0x7fffffff);
            break;
        case 4: {
            const auto later = locate(f.data, "trun", 1);
            const auto base = locate(f.data, "moof", 1);
            set32(f.data, later + 16, static_cast<std::uint32_t>(f.offsets[0] - base));
            break;
        }
        case 5:
            set32(f.data, run + 16, 0x80000000);
            break;
        }
        const auto p = inspect(f.data);
        QCOMPARE(p.status, Mp4AudioRemuxStatus::Invalid);
        QVERIFY(p.header.empty());
        QVERIFY(p.packets.empty());
        QVERIFY(p.edits.empty());
        QCOMPARE(p.normalized_size, 0U);
    }
    void fragmentDefaultsAndOverrides_data() {
        QTest::addColumn<unsigned>("tfhd");
        QTest::addColumn<unsigned>("trun");
        QTest::addColumn<bool>("split");
        QTest::addColumn<bool>("v1");
        QTest::newRow("google-duration-and-description") << 0x2002aU << 0x201U << false << false;
        QTest::newRow("trex-defaults") << 0x20000U << 0x1U << false << false;
        QTest::newRow("tfhd-duration-size-flags") << 0x2003aU << 0x1U << false << false;
        QTest::newRow("implicit-moof-base") << 0U << 0x301U << false << false;
        QTest::newRow("explicit-base") << 0x1U << 0x301U << false << false;
        QTest::newRow("explicit-payload-base-no-offset") << 0x1U << 0x300U << false << false;
        QTest::newRow("implicit-second-run") << 0x20000U << 0x301U << true << false;
        QTest::newRow("first-sample-flags") << 0x20000U << 0x305U << false << false;
        QTest::newRow("sample-flags") << 0x20000U << 0x701U << false << false;
        QTest::newRow("zero-composition-v0") << 0x20000U << 0xb01U << false << false;
        QTest::newRow("zero-composition-v1") << 0x20000U << 0xb01U << false << true;
    }
    void fragmentDefaultsAndOverrides() {
        QFETCH(unsigned, tfhd);
        QFETCH(unsigned, trun);
        QFETCH(bool, split);
        QFETCH(bool, v1);
        auto f = fixture();
        f.data.resize(locate(f.data, "moof"));
        f.offsets.clear();
        for (auto& sample : f.samples) {
            sample.data.resize(3);
            sample.duration = 1024;
        }
        set32(f.data, locate(f.data, "trex") + 24, 3);
        fragment(f, 1, 0, 0, 3, {.tfhd_flags = tfhd, .trun_flags = trun, .split_run = split, .trun_v1 = v1});
        const auto p = inspect(f.data);
        QCOMPARE(p.status, Mp4AudioRemuxStatus::Ready);
        QCOMPARE(p.packets.size(), std::size_t{3});
        for (std::size_t i = 0; i < p.packets.size(); ++i) {
            QCOMPARE(p.packets[i].source_offset, f.offsets[i]);
            QCOMPARE(p.packets[i].length, 3U);
        }
        QCOMPARE(get32(p.header, locate(p.header, "mdhd") + 24), 3072U);
    }
    void canonicalViewPreservesPacketsAndExactTiming() {
        const auto f = fixture();
        const auto p = inspect(f.data);
        QCOMPARE(p.status, Mp4AudioRemuxStatus::Ready);
        QCOMPARE(p.packets.size(), f.samples.size());
        QCOMPARE(p.movie_timescale, 1000U);
        QCOMPARE(p.media_timescale, 48000U);
        QCOMPARE(p.edits.size(), std::size_t{1});
        QCOMPARE(p.edits[0].media_time, 128);
        QCOMPARE(p.edits[0].movie_duration, 60U);
        QCOMPARE(locate(p.header, "mvex"), p.header.size());
        QCOMPARE(locate(p.header, "edts"), p.header.size());
        const auto mdhd = locate(p.header, "mdhd");
        QCOMPARE(get32(p.header, mdhd + 24), 3008U);
        // ceil(3008 * 1000 / 48000): the reader must not clip raw PCM before edits.
        QCOMPARE(get32(p.header, locate(p.header, "mvhd") + 24), 63U);
        QCOMPARE(get32(p.header, locate(p.header, "tkhd") + 28), 63U);
        const auto stts = locate(p.header, "stts");
        QCOMPARE(get32(p.header, stts + 12), 2U);
        QCOMPARE(get32(p.header, stts + 16), 2U);
        QCOMPARE(get32(p.header, stts + 20), 1024U);
        QCOMPARE(get32(p.header, stts + 24), 1U);
        QCOMPARE(get32(p.header, stts + 28), 960U);
        const auto stsc = locate(p.header, "stsc");
        QCOMPARE(get32(p.header, stsc + 12), 1U);
        QCOMPARE(get32(p.header, stsc + 16), 1U);
        QCOMPARE(get32(p.header, stsc + 20), 3U);
        QCOMPARE(get32(p.header, stsc + 24), 1U);
        const auto stsz = locate(p.header, "stsz");
        QCOMPARE(get32(p.header, stsz + 12), 0U);
        QCOMPARE(get32(p.header, stsz + 16), 3U);
        QCOMPARE(get32(p.header, stsz + 20), 3U);
        QCOMPARE(get32(p.header, stsz + 24), 2U);
        QCOMPARE(get32(p.header, stsz + 28), 4U);
        QCOMPARE(get32(p.header, locate(p.header, "stco") + 16), p.header.size());
        std::uint64_t cursor = p.header.size();
        for (std::size_t i = 0; i < p.packets.size(); ++i) {
            const auto& range = p.packets[i];
            QCOMPARE(range.source_offset, f.offsets[i]);
            QCOMPARE(range.output_offset, cursor);
            QCOMPARE(range.length, f.samples[i].data.size());
            const Bytes original(f.data.begin() + static_cast<std::ptrdiff_t>(range.source_offset),
                                 f.data.begin() + static_cast<std::ptrdiff_t>(range.source_offset + range.length));
            QVERIFY(original == f.samples[i].data);
            cursor += range.length;
        }
        QCOMPARE(p.normalized_size, cursor);
        const auto mdat = locate(p.header, "mdat");
        QCOMPARE(mdat + 8, p.header.size());
        QCOMPARE(get32(p.header, mdat), 17U);
    }
};

QTEST_APPLESS_MAIN(FragmentedMp4AudioTests)
#include "FragmentedMp4AudioTests.moc"
