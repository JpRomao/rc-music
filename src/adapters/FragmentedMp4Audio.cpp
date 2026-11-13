#include "FragmentedMp4Audio.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <initializer_list>
#include <limits>
#include <new>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace music_player::m0 {
namespace {
using Bytes = std::vector<std::uint8_t>;
using View = std::span<const std::uint8_t>;
using Status = Mp4AudioRemuxStatus;
struct Failure final {
    Status status;
};
void check(bool condition, Status status = Status::Invalid) {
    if (!condition)
        throw Failure{status};
}
constexpr std::uint32_t tag(std::string_view name) {
    return (static_cast<std::uint32_t>(name[0]) << 24) | (static_cast<std::uint32_t>(name[1]) << 16) |
           (static_cast<std::uint32_t>(name[2]) << 8) | static_cast<std::uint32_t>(name[3]);
}
std::uint32_t be32(View b, std::size_t p) {
    check(p <= b.size() && b.size() - p >= 4);
    return (std::uint32_t{b[p]} << 24) | (std::uint32_t{b[p + 1]} << 16) | (std::uint32_t{b[p + 2]} << 8) | b[p + 3];
}
std::uint64_t be64(View b, std::size_t p) {
    const auto high = be32(b, p);
    return (std::uint64_t{high} << 32) | be32(b, p + 4);
}
std::uint64_t add(std::uint64_t a, std::uint64_t b) {
    check(b <= std::numeric_limits<std::uint64_t>::max() - a);
    return a + b;
}
std::uint64_t signedOffset(std::uint64_t base, std::int32_t offset) {
    if (offset >= 0)
        return add(base, static_cast<std::uint32_t>(offset));
    const auto magnitude = static_cast<std::uint64_t>(-std::int64_t{offset});
    check(base >= magnitude);
    return base - magnitude;
}
struct Budget final {
    Mp4AudioRemuxLimits limits;
    std::uint64_t bytes{0};
    std::uint64_t boxes{0};
    void metadata(std::uint64_t amount) {
        check(bytes <= limits.max_metadata_bytes && amount <= limits.max_metadata_bytes - bytes, Status::LimitExceeded);
        bytes += amount;
    }
    void box() { check(++boxes <= limits.max_boxes, Status::LimitExceeded); }
};
struct Box final {
    std::uint32_t type{0};
    View body;
    std::vector<Box> children;
};
bool container(std::uint32_t type) {
    switch (type) {
    case tag("moov"):
    case tag("trak"):
    case tag("mdia"):
    case tag("minf"):
    case tag("stbl"):
    case tag("dinf"):
    case tag("edts"):
    case tag("mvex"):
    case tag("moof"):
    case tag("traf"):
        return true;
    default:
        return false;
    }
}
struct Width final {
    std::uint64_t size;
    std::size_t header;
    std::uint32_t type;
};
Width width(View bytes, std::uint64_t available) {
    check(bytes.size() >= 8 && available >= 8);
    auto length = std::uint64_t{be32(bytes, 0)};
    const auto type = be32(bytes, 4);
    std::size_t header = 8;
    if (length == 1) {
        length = be64(bytes, 8);
        header = 16;
    } else if (length == 0) {
        length = available;
    }
    if (type == tag("uuid"))
        header += 16;
    check(length >= header && length <= available);
    return {length, header, type};
}
std::vector<Box> children(View bytes, Budget& budget, unsigned depth) {
    check(depth <= 24, Status::LimitExceeded);
    std::vector<Box> result;
    std::size_t p = 0;
    while (p < bytes.size()) {
        budget.box();
        const auto w = width(bytes.subspan(p), bytes.size() - p);
        const auto n = static_cast<std::size_t>(w.size);
        Box b{w.type, bytes.subspan(p + w.header, n - w.header), {}};
        if (container(b.type))
            b.children = children(b.body, budget, depth + 1);
        result.push_back(std::move(b));
        p += n;
    }
    return result;
}
const Box* optional(const Box& parent, std::uint32_t type) {
    const Box* found = nullptr;
    for (const auto& child : parent.children) {
        if (child.type == type) {
            check(!found);
            found = &child;
        }
    }
    return found;
}
const Box& one(const Box& parent, std::uint32_t type) {
    const auto* found = optional(parent, type);
    check(found != nullptr);
    return *found;
}
void allowed(const Box& box, std::initializer_list<std::uint32_t> names) {
    for (const auto& child : box.children)
        check(std::find(names.begin(), names.end(), child.type) != names.end(), Status::Unsupported);
}
std::uint8_t version(const Box& b) {
    check(b.body.size() >= 4);
    return b.body[0];
}
std::uint32_t flags(const Box& b) { return be32(b.body, 0) & 0x00ffffffU; }
void fullZero(const Box& b) {
    check(version(b) == 0, Status::Unsupported);
    check(flags(b) == 0, Status::Unsupported);
}
struct SourceBox final {
    std::uint32_t type;
    std::uint64_t offset;
    std::uint64_t size;
    std::size_t header;
};
class Source final {
  public:
    Source(std::uint64_t size, const Mp4AudioReadAt& read, Budget& budget)
        : size_(size), read_(read), budget_(budget) {}
    void read(std::uint64_t at, std::span<std::uint8_t> output) {
        check(at <= size_ && output.size() <= size_ - at && static_cast<bool>(read_));
        budget_.metadata(output.size());
        check(read_(at, output));
    }
    SourceBox box(std::uint64_t at) { return box(at, size_); }
    SourceBox box(std::uint64_t at, std::uint64_t end) {
        std::array<std::uint8_t, 32> bytes{};
        check(end <= size_ && at <= end && end - at >= 8);
        read(at, std::span(bytes).first(8));
        if (be32(bytes, 0) == 1) {
            check(end - at >= 16);
            read(add(at, 8), std::span(bytes).subspan(8, 8));
        }
        const auto w = width(bytes, end - at);
        budget_.box();
        return {w.type, at, w.size, w.header};
    }
    Bytes load(const SourceBox& b) {
        check(b.size <= std::numeric_limits<std::size_t>::max(), Status::LimitExceeded);
        check(b.size <= budget_.limits.max_metadata_bytes - budget_.bytes, Status::LimitExceeded);
        Bytes bytes(static_cast<std::size_t>(b.size));
        read(b.offset, bytes);
        const auto current = width(bytes, b.size);
        check(current.size == b.size && current.header == b.header && current.type == b.type);
        return bytes;
    }

  private:
    std::uint64_t size_;
    const Mp4AudioReadAt& read_;
    Budget& budget_;
};
Box root(const Bytes& bytes, Budget& budget) {
    const auto w = width(bytes, bytes.size());
    Box b{w.type, View(bytes).subspan(w.header), {}};
    b.children = children(b.body, budget, 1);
    return b;
}
std::uint32_t timescale(const Box& b, std::size_t v0size, std::size_t v1size) {
    const auto v = version(b);
    check(v <= 1, Status::Unsupported);
    check(flags(b) == 0, Status::Unsupported);
    check(b.body.size() == (v == 0 ? v0size : v1size));
    const auto scale = be32(b.body, v == 0 ? 12 : 20);
    check(scale != 0);
    return scale;
}
void emptyTable(const Box& b, std::uint32_t rowWidth, bool sampleSize = false) {
    fullZero(b);
    const auto p = sampleSize ? std::size_t{8} : std::size_t{4};
    const auto count = be32(b.body, p);
    const auto start = p + 4;
    const auto uniform = sampleSize ? be32(b.body, 4) : 0;
    const auto stride = uniform ? 0U : rowWidth;
    check(stride == 0 || count <= (b.body.size() - start) / stride);
    check(b.body.size() - start == std::uint64_t{count} * stride);
    check(count == 0, Status::Unsupported);
}
struct Movie final {
    const Box* mvhd;
    const Box* tkhd;
    const Box* mdhd;
    const Box* hdlr;
    const Box* smhd;
    const Box* dinf;
    const Box* stsd;
    std::uint32_t track;
    std::uint32_t movie_scale;
    std::uint32_t media_scale;
    std::uint32_t duration;
    std::uint32_t sample_size;
    std::vector<Mp4AudioEdit> edits;
};
std::vector<Mp4AudioEdit> edits(const Box& track) {
    const auto* edts = optional(track, tag("edts"));
    if (!edts)
        return {};
    allowed(*edts, {tag("elst")});
    const auto& elst = one(*edts, tag("elst"));
    const auto v = version(elst);
    check(v <= 1, Status::Unsupported);
    check(flags(elst) == 0, Status::Unsupported);
    const auto count = be32(elst.body, 4);
    const auto stride = v == 0 ? 12U : 20U;
    check(count <= (elst.body.size() - 8) / stride && elst.body.size() - 8 == std::uint64_t{count} * stride);
    check(count == 1 || count == 2, Status::Unsupported);
    std::vector<Mp4AudioEdit> result;
    std::uint64_t presentation = 0;
    for (std::size_t i = 0; i < count; ++i) {
        const auto p = 8 + i * stride;
        const auto duration = v == 0 ? be32(elst.body, p) : be64(elst.body, p);
        const auto time = v == 0 ? std::int64_t{std::bit_cast<std::int32_t>(be32(elst.body, p + 4))}
                                 : std::bit_cast<std::int64_t>(be64(elst.body, p + 8));
        check(be32(elst.body, p + stride - 4) == 0x10000, Status::Unsupported);
        check(duration != 0, Status::Unsupported);
        check(i + 1 == count ? time >= 0 : time == -1, Status::Unsupported);
        presentation = add(presentation, duration);
        result.push_back({time, duration});
    }
    return result;
}
struct Descriptor final {
    std::uint8_t type;
    View body;
};
Descriptor descriptor(View bytes, std::size_t& p) {
    check(p < bytes.size());
    const auto type = bytes[p++];
    std::uint32_t length = 0;
    bool ended = false;
    for (unsigned i = 0; i < 4; ++i) {
        check(p < bytes.size());
        const auto byte = bytes[p++];
        length = (length << 7) | (byte & 0x7fU);
        if (!(byte & 0x80)) {
            ended = true;
            break;
        }
    }
    check(ended && length <= bytes.size() - p);
    const auto body = bytes.subspan(p, length);
    p += length;
    return {type, body};
}
void aacConfig(View bytes) {
    std::size_t position = 0;
    auto bits = [&](unsigned count) {
        check(position <= bytes.size() * 8 && count <= bytes.size() * 8 - position);
        std::uint32_t value = 0;
        for (unsigned i = 0; i < count; ++i, ++position)
            value = (value << 1) | ((bytes[position / 8] >> (7 - position % 8)) & 1U);
        return value;
    };
    check(bits(5) == 2, Status::Unsupported); // AAC-LC; other valid AAC profiles stay explicitly unsupported.
    const auto frequency = bits(4);
    if (frequency == 15)
        check(bits(24) != 0);
    else
        check(frequency <= 12, Status::Unsupported);
    const auto channels = bits(4);
    check(channels >= 1 && channels <= 7, Status::Unsupported); // No program-config-element parser.
    static_cast<void>(bits(1));               // frameLengthFlag: both 1024 and 960-sample AAC-LC are valid.
    check(bits(1) == 0, Status::Unsupported); // dependsOnCoreCoder
    check(bits(1) == 0, Status::Unsupported); // extensionFlag
}
void elementaryStream(const Box& esds) {
    fullZero(esds);
    std::size_t p = 4;
    const auto es = descriptor(esds.body, p);
    check(es.type == 3 && p == esds.body.size() && es.body.size() >= 3);
    const auto f = es.body[2];
    check((f & 0xe0) == 0, Status::Unsupported); // No URL, dependent ES, or external clock stream.
    std::size_t ep = 3;
    check(ep <= es.body.size());
    bool haveDecoder = false;
    bool haveSl = false;
    while (ep < es.body.size()) {
        const auto child = descriptor(es.body, ep);
        if (child.type == 6) {
            check(!haveSl && !child.body.empty());
            haveSl = true;
            continue;
        }
        check(child.type == 4, Status::Unsupported);
        check(!haveDecoder && child.body.size() >= 13);
        haveDecoder = true;
        check(child.body[0] == 0x40 && (child.body[1] >> 2) == 5, Status::Unsupported);
        check((child.body[1] & 1) == 1);
        std::size_t dp = 13;
        bool haveConfig = false;
        while (dp < child.body.size()) {
            const auto config = descriptor(child.body, dp);
            check(config.type == 5, Status::Unsupported);
            check(!haveConfig);
            haveConfig = true;
            aacConfig(config.body);
        }
        check(haveConfig);
    }
    check(haveDecoder);
}
void sampleDescription(const Box& entry, Budget& budget) {
    check(entry.type == tag("mp4a"), Status::Unsupported);
    check(entry.body.size() >= 28);
    check(std::all_of(entry.body.begin(), entry.body.begin() + 6, [](auto n) { return n == 0; }));
    check(entry.body[6] == 0 && entry.body[7] == 1);
    check(be32(entry.body, 8) == 0 && be32(entry.body, 12) == 0, Status::Unsupported);
    check((be32(entry.body, 16) >> 16) != 0 && be32(entry.body, 24) != 0);
    Box extensions{0, {}, children(entry.body.subspan(28), budget, 8)};
    allowed(extensions, {tag("esds"), tag("btrt"), tag("free"), tag("skip")});
    elementaryStream(one(extensions, tag("esds")));
    if (const auto* btrt = optional(extensions, tag("btrt")))
        check(btrt->body.size() == 12);
}
Movie movie(const Box& moov, Budget& budget) {
    allowed(moov, {tag("mvhd"), tag("trak"), tag("mvex"), tag("udta"), tag("meta"), tag("iods"), tag("free")});
    const auto tracks =
        std::count_if(moov.children.begin(), moov.children.end(), [](const Box& b) { return b.type == tag("trak"); });
    check(tracks == 1, tracks == 0 ? Status::Invalid : Status::Unsupported);
    const auto& trak = one(moov, tag("trak"));
    allowed(trak, {tag("tkhd"), tag("edts"), tag("mdia")});
    const auto& mvhd = one(moov, tag("mvhd"));
    const auto movieScale = timescale(mvhd, 100, 112);
    const auto& tkhd = one(trak, tag("tkhd"));
    const auto tv = version(tkhd);
    check(tv <= 1, Status::Unsupported);
    check(tkhd.body.size() == (tv == 0 ? 84U : 96U));
    const auto track = be32(tkhd.body, tv == 0 ? 12 : 20);
    check(track != 0);
    const auto& mdia = one(trak, tag("mdia"));
    allowed(mdia, {tag("mdhd"), tag("hdlr"), tag("minf")});
    const auto& mdhd = one(mdia, tag("mdhd"));
    const auto mediaScale = timescale(mdhd, 24, 36);
    const auto& hdlr = one(mdia, tag("hdlr"));
    fullZero(hdlr);
    check(hdlr.body.size() >= 24);
    check(be32(hdlr.body, 8) == tag("soun"), Status::Unsupported);
    const auto& minf = one(mdia, tag("minf"));
    allowed(minf, {tag("smhd"), tag("dinf"), tag("stbl")});
    const auto& smhd = one(minf, tag("smhd"));
    fullZero(smhd);
    check(smhd.body.size() == 8);
    const auto& dinf = one(minf, tag("dinf"));
    allowed(dinf, {tag("dref")});
    const auto& dref = one(dinf, tag("dref"));
    fullZero(dref);
    const auto refs = be32(dref.body, 4);
    const auto referenceBoxes = children(dref.body.subspan(8), budget, 7);
    check(refs == referenceBoxes.size());
    check(refs == 1, Status::Unsupported);
    const auto& ref = referenceBoxes.front();
    check(ref.type == tag("url "), Status::Unsupported);
    check(version(ref) == 0 && flags(ref) == 1, Status::Unsupported);
    check(ref.body.size() == 4);
    const auto& stbl = one(minf, tag("stbl"));
    allowed(stbl, {tag("stsd"), tag("stts"), tag("stsc"), tag("stsz"), tag("stco"), tag("co64"), tag("ctts"),
                   tag("stss"), tag("free")});
    const auto& stsd = one(stbl, tag("stsd"));
    fullZero(stsd);
    const auto descriptions = be32(stsd.body, 4);
    const auto entries = children(stsd.body.subspan(8), budget, 7);
    check(entries.size() == descriptions);
    check(descriptions == 1, Status::Unsupported);
    sampleDescription(entries.front(), budget);
    emptyTable(one(stbl, tag("stts")), 8);
    emptyTable(one(stbl, tag("stsc")), 12);
    emptyTable(one(stbl, tag("stsz")), 4, true);
    const auto* stco = optional(stbl, tag("stco"));
    const auto* co64 = optional(stbl, tag("co64"));
    check((stco != nullptr) != (co64 != nullptr));
    emptyTable(stco ? *stco : *co64, stco ? 4U : 8U);
    if (const auto* ctts = optional(stbl, tag("ctts")))
        emptyTable(*ctts, 8);
    if (const auto* stss = optional(stbl, tag("stss")))
        emptyTable(*stss, 4);
    const auto& mvex = one(moov, tag("mvex"));
    allowed(mvex, {tag("trex"), tag("mehd")});
    if (const auto* mehd = optional(mvex, tag("mehd"))) {
        check(version(*mehd) <= 1 && flags(*mehd) == 0, Status::Unsupported);
        check(mehd->body.size() == (version(*mehd) == 0 ? 8U : 12U));
    }
    const auto& trex = one(mvex, tag("trex"));
    fullZero(trex);
    check(trex.body.size() == 24);
    check(be32(trex.body, 4) == track && be32(trex.body, 8) == 1);
    return {&mvhd,
            &tkhd,
            &mdhd,
            &hdlr,
            &smhd,
            &dinf,
            &stsd,
            track,
            movieScale,
            mediaScale,
            be32(trex.body, 12),
            be32(trex.body, 16),
            edits(trak)};
}
struct Packets final {
    std::vector<Mp4AudioReadRange> ranges;
    std::vector<std::uint32_t> durations;
    std::uint64_t duration{0};
    std::uint64_t bytes{0};
    std::optional<std::uint32_t> sequence;
};
void fragment(const Box& moof, const SourceBox& source, const Movie& m, Packets& packets, Budget& budget) {
    allowed(moof, {tag("mfhd"), tag("traf"), tag("free")});
    const auto& mfhd = one(moof, tag("mfhd"));
    fullZero(mfhd);
    check(mfhd.body.size() == 8);
    const auto sequence = be32(mfhd.body, 4);
    check(!packets.sequence || sequence > *packets.sequence);
    packets.sequence = sequence;
    const auto& traf = one(moof, tag("traf"));
    allowed(traf, {tag("tfhd"), tag("tfdt"), tag("trun"), tag("free")});
    const auto& tfhd = one(traf, tag("tfhd"));
    check(version(tfhd) == 0, Status::Unsupported);
    const auto hf = flags(tfhd);
    check((hf & ~0x03003bU) == 0, Status::Unsupported);
    check((hf & 0x010000U) == 0, Status::Unsupported);
    check(!((hf & 1) && (hf & 0x020000U)));
    check(be32(tfhd.body, 4) == m.track);
    std::size_t hp = 8;
    std::uint64_t base = source.offset; // The only traf is also the first traf.
    if (hf & 1) {
        base = be64(tfhd.body, hp);
        hp += 8;
    }
    auto field = [&](std::uint32_t mask, std::uint32_t fallback) {
        if (!(hf & mask))
            return fallback;
        const auto value = be32(tfhd.body, hp);
        hp += 4;
        return value;
    };
    check(field(2, 1) == 1);
    const auto defaultDuration = field(8, m.duration);
    const auto defaultSize = field(0x10, m.sample_size);
    static_cast<void>(field(0x20, 0));
    check(hp == tfhd.body.size());
    const auto& tfdt = one(traf, tag("tfdt"));
    check(version(tfdt) <= 1, Status::Unsupported);
    check(flags(tfdt) == 0, Status::Unsupported);
    check(tfdt.body.size() == (version(tfdt) == 0 ? 8U : 12U));
    const auto decodeTime = version(tfdt) == 0 ? be32(tfdt.body, 4) : be64(tfdt.body, 4);
    check(decodeTime == packets.duration, Status::Unsupported);
    bool haveRun = false;
    std::uint64_t cursor = base;
    for (const auto& run : traf.children) {
        if (run.type != tag("trun"))
            continue;
        haveRun = true;
        check(version(run) <= 1, Status::Unsupported);
        const auto rf = flags(run);
        check((rf & ~0xf05U) == 0, Status::Unsupported);
        check(!((rf & 4) && (rf & 0x400)));
        const auto count = be32(run.body, 4);
        std::size_t rp = 8;
        if (rf & 1) {
            cursor = signedOffset(base, std::bit_cast<std::int32_t>(be32(run.body, rp)));
            rp += 4;
        }
        if (rf & 4) {
            static_cast<void>(be32(run.body, rp));
            rp += 4;
        }
        std::uint32_t stride = 0;
        for (const auto mask : {0x100U, 0x200U, 0x400U, 0x800U})
            if (rf & mask)
                stride += 4;
        check(rp <= run.body.size());
        check(stride == 0 || count <= (run.body.size() - rp) / stride);
        check(run.body.size() - rp == std::uint64_t{count} * stride);
        check(packets.ranges.size() <= budget.limits.max_samples &&
                  count <= budget.limits.max_samples - packets.ranges.size(),
              Status::LimitExceeded);
        check(count != 0, Status::Unsupported);
        auto sampleField = [&](std::uint32_t mask, std::uint32_t fallback) {
            if (!(rf & mask))
                return fallback;
            const auto value = be32(run.body, rp);
            rp += 4;
            return value;
        };
        for (std::size_t i = 0; i < count; ++i) {
            const auto duration = sampleField(0x100, defaultDuration);
            const auto size = sampleField(0x200, defaultSize);
            static_cast<void>(sampleField(0x400, 0));
            check(sampleField(0x800, 0) == 0, Status::Unsupported);
            check(size != 0 && duration != 0);
            packets.ranges.push_back({cursor, 0, size});
            packets.durations.push_back(duration);
            cursor = add(cursor, size);
            packets.bytes = add(packets.bytes, size);
            packets.duration = add(packets.duration, duration);
        }
    }
    check(haveRun);
}
class Writer final {
  public:
    explicit Writer(std::uint64_t limit) : limit_(limit) {}
    Bytes data;
    void append(View b) {
        check(data.size() <= limit_ && b.size() <= limit_ - data.size(), Status::LimitExceeded);
        data.insert(data.end(), b.begin(), b.end());
    }
    void u32(std::uint32_t n) {
        const std::array<std::uint8_t, 4> b{static_cast<std::uint8_t>(n >> 24), static_cast<std::uint8_t>(n >> 16),
                                            static_cast<std::uint8_t>(n >> 8), static_cast<std::uint8_t>(n)};
        append(b);
    }
    void u64(std::uint64_t n) {
        u32(static_cast<std::uint32_t>(n >> 32));
        u32(static_cast<std::uint32_t>(n));
    }
    void patch(std::size_t p, std::uint32_t n) {
        check(p <= data.size() && data.size() - p >= 4);
        for (int shift = 24; shift >= 0; shift -= 8)
            data[p++] = static_cast<std::uint8_t>(n >> shift);
    }
    std::size_t begin(std::uint32_t name) {
        const auto p = data.size();
        u32(0);
        u32(name);
        return p;
    }
    void copyBox(const Box& box) {
        const auto p = begin(box.type);
        append(box.body);
        end(p); // A source size==0 must not swallow newly appended canonical siblings.
    }
    void end(std::size_t p) {
        check(data.size() - p <= std::numeric_limits<std::uint32_t>::max(), Status::LimitExceeded);
        patch(p, static_cast<std::uint32_t>(data.size() - p));
    }

  private:
    std::uint64_t limit_;
};
// Update only the reader-view duration; original presentation intent lives in plan.edits.
void temporalHeader(Writer& w, const Box& box, std::uint64_t duration, std::size_t durationV0) {
    const auto p = w.begin(box.type);
    if (version(box) == 1) {
        w.append(box.body.first(durationV0 + 8));
        w.u64(duration);
        w.append(box.body.subspan(durationV0 + 16));
    } else if (duration <= std::numeric_limits<std::uint32_t>::max()) {
        w.append(box.body.first(durationV0));
        w.u32(static_cast<std::uint32_t>(duration));
        w.append(box.body.subspan(durationV0 + 4));
    } else {
        w.u32((be32(box.body, 0) & 0xffffffU) | 0x01000000U);
        w.u64(be32(box.body, 4));
        w.u64(be32(box.body, 8));
        w.append(box.body.subspan(12, durationV0 - 12));
        w.u64(duration);
        w.append(box.body.subspan(durationV0 + 4));
    }
    w.end(p);
}
std::uint64_t movieDuration(std::uint64_t mediaDuration, std::uint32_t mediaScale, std::uint32_t movieScale) {
    const auto whole = mediaDuration / mediaScale;
    check(whole <= std::numeric_limits<std::uint64_t>::max() / movieScale);
    // rem and both scales are 32-bit, so this intermediate cannot overflow uint64_t.
    const auto numerator = (mediaDuration % mediaScale) * movieScale;
    const auto fraction = numerator / mediaScale + (numerator % mediaScale != 0 ? 1U : 0U);
    return add(whole * movieScale, fraction); // Ceil prevents a reader from truncating raw media.
}
Mp4AudioRemuxPlan canonical(const Bytes& ftyp, const Movie& m, Packets packets, Mp4AudioRemuxLimits limits) {
    check(!packets.ranges.empty());
    Writer w(limits.max_metadata_bytes);
    w.append(ftyp);
    const auto presentation = movieDuration(packets.duration, m.media_scale, m.movie_scale);
    const auto moov = w.begin(tag("moov"));
    temporalHeader(w, *m.mvhd, presentation, 16);
    const auto trak = w.begin(tag("trak"));
    temporalHeader(w, *m.tkhd, presentation, 20);
    const auto mdia = w.begin(tag("mdia"));
    temporalHeader(w, *m.mdhd, packets.duration, 16);
    w.copyBox(*m.hdlr);
    const auto minf = w.begin(tag("minf"));
    w.copyBox(*m.smhd);
    w.copyBox(*m.dinf);
    const auto stbl = w.begin(tag("stbl"));
    w.copyBox(*m.stsd);
    const auto stts = w.begin(tag("stts"));
    w.u32(0);
    const auto sttsCount = w.data.size();
    w.u32(0);
    std::uint32_t runs = 0;
    for (std::size_t i = 0; i < packets.durations.size();) {
        auto end = i + 1;
        while (end < packets.durations.size() && packets.durations[end] == packets.durations[i])
            ++end;
        w.u32(static_cast<std::uint32_t>(end - i));
        w.u32(packets.durations[i]);
        ++runs;
        i = end;
    }
    w.patch(sttsCount, runs);
    w.end(stts);
    const auto stsc = w.begin(tag("stsc"));
    for (const auto n : {0U, 1U, 1U, static_cast<std::uint32_t>(packets.ranges.size()), 1U})
        w.u32(n);
    w.end(stsc);
    const auto stsz = w.begin(tag("stsz"));
    w.u32(0);
    w.u32(0);
    w.u32(static_cast<std::uint32_t>(packets.ranges.size()));
    for (const auto& packet : packets.ranges)
        w.u32(packet.length);
    w.end(stsz);
    const auto stco = w.begin(tag("stco"));
    w.u32(0);
    w.u32(1);
    const auto offsetField = w.data.size();
    w.u32(0);
    for (const auto p : {stco, stbl, minf, mdia, trak, moov})
        w.end(p);
    if (packets.bytes <= std::numeric_limits<std::uint32_t>::max() - 8U) {
        w.u32(static_cast<std::uint32_t>(packets.bytes + 8));
        w.u32(tag("mdat"));
    } else {
        w.u32(1);
        w.u32(tag("mdat"));
        w.u64(add(packets.bytes, 16));
    }
    check(w.data.size() <= std::numeric_limits<std::uint32_t>::max(), Status::LimitExceeded);
    w.patch(offsetField, static_cast<std::uint32_t>(w.data.size()));
    std::uint64_t cursor = w.data.size();
    for (auto& range : packets.ranges) {
        range.output_offset = cursor;
        cursor = add(cursor, range.length);
    }
    return {Status::Ready, std::move(w.data), std::move(packets.ranges), cursor, m.movie_scale, m.media_scale, m.edits};
}
Mp4AudioRemuxPlan inspect(std::uint64_t size, const Mp4AudioReadAt& read, Mp4AudioRemuxLimits limits) {
    if (size < 8)
        return {};
    Budget budget{limits};
    Source source(size, read, budget);
    std::array<std::uint8_t, 8> sniff{};
    source.read(0, sniff);
    const auto first = be32(sniff, 4);
    if (first != tag("ftyp") && first != tag("moov") && first != tag("moof") && first != tag("mdat") &&
        first != tag("free") && first != tag("skip") && first != tag("styp") && first != tag("wide"))
        return {};
    std::vector<SourceBox> fragments;
    std::vector<SourceBox> media;
    std::optional<SourceBox> ftyp;
    std::optional<SourceBox> moov;
    bool restricted_top = false;
    for (std::uint64_t p = 0; p < size;) {
        const auto b = source.box(p);
        switch (b.type) {
        case tag("ftyp"):
            check(!ftyp);
            ftyp = b;
            break;
        case tag("moov"):
            check(!moov);
            moov = b;
            break;
        case tag("moof"):
            check(fragments.size() < limits.max_fragments, Status::LimitExceeded);
            fragments.push_back(b);
            break;
        case tag("mdat"):
            media.push_back(b);
            break;
        case tag("free"):
        case tag("skip"):
        case tag("wide"):
        case tag("sidx"):
        case tag("styp"):
        case tag("mfra"):
            break;
        default:
            restricted_top = true;
        }
        p += b.size; // width() proved this addition stays inside size.
    }
    check(ftyp && moov);
    const auto ftypBodySize = ftyp->size - ftyp->header;
    check(ftypBodySize >= 8 && (ftypBodySize % 4) == 0);
    if (fragments.empty()) {
        // Detect fragmented initialization from immediate moov headers only.
        // Classic extensions, covers and nested metadata belong to the native
        // reader, not this remux profile or its metadata allocation budget.
        bool fragmented_initialization = false;
        const auto end = moov->offset + moov->size;
        for (auto p = moov->offset + moov->header; p < end;) {
            const auto child = source.box(p, end);
            fragmented_initialization = fragmented_initialization || child.type == tag("mvex");
            p += child.size;
        }
        if (!fragmented_initialization)
            return {};
    }
    // Never fall back from a recognized fragmented input that violates this
    // supported profile: malformed metadata and budgets still fail closed.
    check(!restricted_top, Status::Unsupported);
    auto movieBytes = source.load(*moov);
    const auto movieBox = root(movieBytes, budget);
    auto fileType = source.load(*ftyp);
    const auto m = movie(movieBox, budget);
    Packets packets;
    for (const auto& b : fragments) {
        const auto bytes = source.load(b);
        const auto moof = root(bytes, budget);
        fragment(moof, b, m, packets, budget);
    }
    if (!m.edits.empty()) {
        const auto& edit = m.edits.back();
        const auto start = static_cast<std::uint64_t>(edit.media_time);
        check(start <= packets.duration);
        check(edit.movie_duration <= movieDuration(packets.duration - start, m.media_scale, m.movie_scale));
    }
    // Validate against payloads, not box/file bounds. No media bytes are read.
    for (const auto& packet : packets.ranges) {
        const auto next = std::upper_bound(media.begin(), media.end(), packet.source_offset,
                                           [](std::uint64_t offset, const SourceBox& b) { return offset < b.offset; });
        check(next != media.begin());
        const auto& mdat = *std::prev(next);
        check(packet.source_offset >= add(mdat.offset, mdat.header));
        check(add(packet.source_offset, packet.length) <= add(mdat.offset, mdat.size));
    }
    auto sorted = packets.ranges;
    std::sort(sorted.begin(), sorted.end(),
              [](const auto& a, const auto& b) { return a.source_offset < b.source_offset; });
    for (std::size_t i = 1; i < sorted.size(); ++i)
        check(add(sorted[i - 1].source_offset, sorted[i - 1].length) <= sorted[i].source_offset);
    return canonical(fileType, m, std::move(packets), limits);
}
} // namespace
Mp4AudioRemuxPlan inspectFragmentedMp4Audio(std::uint64_t size, const Mp4AudioReadAt& read,
                                            Mp4AudioRemuxLimits limits) {
    try {
        return inspect(size, read, limits);
    } catch (const Failure& f) {
        return {.status = f.status};
    } catch (const std::bad_alloc&) {
        return {.status = Status::LimitExceeded};
    } catch (const std::length_error&) {
        return {.status = Status::LimitExceeded};
    } catch (...) {
        return {.status = Status::Invalid};
    }
}
} // namespace music_player::m0
