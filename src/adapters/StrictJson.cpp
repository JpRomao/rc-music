#include "StrictJson.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <QStringDecoder>

#include <vector>

namespace music_player::m0::detail {
namespace {
bool isHex(char value) {
    return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f') || (value >= 'A' && value <= 'F');
}

bool jsonWhitespace(char value) { return value == ' ' || value == '\t' || value == '\r' || value == '\n'; }

bool readEscapedUnit(const QByteArray& payload, qsizetype& position, unsigned& unit) {
    if (payload.size() - position < 4)
        return false;
    unit = 0;
    for (int digit = 0; digit < 4; ++digit) {
        const char value = payload[position++];
        if (!isHex(value))
            return false;
        const auto number = value <= '9' ? value - '0' : ((value | 0x20) - 'a' + 10);
        unit = unit * 16 + static_cast<unsigned>(number);
    }
    return true;
}

// Qt's JSON parser accepts some unknown escapes and unpaired escaped surrogates.
// Validate string lexemes before letting it construct values or collapse object keys.
bool skipStrictString(const QByteArray& payload, qsizetype& position) {
    ++position;
    // Qt's per-string UTF-8 conversion can strip a leading BOM, including in member names.
    // No protocol string may begin with one; an interior BOM in a title remains literal data.
    if (payload.size() - position >= 3 && static_cast<unsigned char>(payload[position]) == 0xef &&
        static_cast<unsigned char>(payload[position + 1]) == 0xbb &&
        static_cast<unsigned char>(payload[position + 2]) == 0xbf)
        return false;
    while (position < payload.size()) {
        const unsigned char value = static_cast<unsigned char>(payload[position++]);
        if (value == '"')
            return true;
        if (value < 0x20)
            return false;
        if (value != '\\')
            continue;
        if (position == payload.size())
            return false;
        const char escape = payload[position++];
        if (escape == '"' || escape == '\\' || escape == '/' || escape == 'b' || escape == 'f' || escape == 'n' ||
            escape == 'r' || escape == 't')
            continue;
        if (escape != 'u')
            return false;
        unsigned unit = 0;
        if (!readEscapedUnit(payload, position, unit) || (unit >= 0xdc00 && unit <= 0xdfff))
            return false;
        if (unit >= 0xd800 && unit <= 0xdbff) {
            if (payload.size() - position < 6 || payload[position] != '\\' || payload[position + 1] != 'u')
                return false;
            position += 2;
            if (!readEscapedUnit(payload, position, unit) || unit < 0xdc00 || unit > 0xdfff)
                return false;
        }
    }
    return false;
}

bool jsonDigit(char value) { return value >= '0' && value <= '9'; }

bool skipStrictNumber(const QByteArray& payload, qsizetype& position) {
    if (payload[position] == '-')
        ++position;
    if (position == payload.size() || !jsonDigit(payload[position]))
        return false;
    if (payload[position] == '0') {
        ++position;
        if (position < payload.size() && jsonDigit(payload[position]))
            return false;
    } else {
        while (position < payload.size() && jsonDigit(payload[position]))
            ++position;
    }
    if (position < payload.size() && payload[position] == '.') {
        ++position;
        if (position == payload.size() || !jsonDigit(payload[position]))
            return false;
        while (position < payload.size() && jsonDigit(payload[position]))
            ++position;
    }
    if (position < payload.size() && (payload[position] == 'e' || payload[position] == 'E')) {
        ++position;
        if (position < payload.size() && (payload[position] == '+' || payload[position] == '-'))
            ++position;
        if (position == payload.size() || !jsonDigit(payload[position]))
            return false;
        while (position < payload.size() && jsonDigit(payload[position]))
            ++position;
    }
    return position == payload.size() || jsonWhitespace(payload[position]) || payload[position] == ',' ||
           payload[position] == ']' || payload[position] == '}';
}
} // namespace

bool strictJsonPreflight(const QByteArray& payload) {
    QStringDecoder decoder{QStringDecoder::Utf8};
    const QString decoded = decoder(payload);
    if (decoder.hasError() || decoded.toUtf8() != payload)
        return false;

    struct Scope final {
        char opening;
        QSet<QString> keys;
    };
    std::vector<Scope> scopes;
    scopes.reserve(4);
    for (qsizetype position = 0; position < payload.size();) {
        const char value = payload[position];
        if (value == '{' || value == '[') {
            if (scopes.size() == 4)
                return false;
            scopes.push_back({value, {}});
            ++position;
        } else if (value == '}' || value == ']') {
            if (scopes.empty() || (value == '}' ? scopes.back().opening != '{' : scopes.back().opening != '['))
                return false;
            scopes.pop_back();
            ++position;
        } else if (value == '"') {
            const qsizetype start = position;
            if (!skipStrictString(payload, position))
                return false;
            qsizetype next = position;
            while (next < payload.size() && jsonWhitespace(payload[next]))
                ++next;
            if (next < payload.size() && payload[next] == ':') {
                if (scopes.empty() || scopes.back().opening != '{')
                    return false;
                // Only decode a lexically verified key; QJson still owns JSON grammar validation.
                const auto key_doc = QJsonDocument::fromJson('[' + payload.mid(start, position - start) + ']');
                if (!key_doc.isArray() || key_doc.array().size() != 1 || !key_doc.array().first().isString())
                    return false;
                const auto key = key_doc.array().first().toString();
                if (scopes.back().keys.contains(key))
                    return false;
                scopes.back().keys.insert(key);
            }
        } else if (jsonDigit(value) || value == '-') {
            if (!skipStrictNumber(payload, position))
                return false;
        } else if (value == '+' || value == '.') {
            return false;
        } else {
            ++position;
        }
    }
    return scopes.empty();
}
} // namespace music_player::m0::detail
