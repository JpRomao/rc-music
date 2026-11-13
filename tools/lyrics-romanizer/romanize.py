"""Convert Japanese lyric lines to Hepburn romaji through one local JSON request."""

import json
import re
import sys
import unicodedata


MAX_WIRE_BYTES = 2 * 1024 * 1024
MAX_TEXT_BYTES = 256 * 1024
MAX_LINES = 8192
MAX_CONVERTER_CHARS = 512
JAPANESE_RUN = re.compile(
    r"[々〻\u3041-\u3096\u3099-\u309f\u30a1-\u30fa\u30fc-\u30ff"
    r"\u31f0-\u31ff\u3400-\u4dbf\u4e00-\u9fff\uf900-\ufaff\uff66-\uff9f]+"
)
CONTROLS = re.compile(r"[\x00-\x08\x0a-\x1f\x7f-\x9f]")


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError("duplicate key")
        result[key] = value
    return result


def validate_lines(lines):
    if not isinstance(lines, list) or not 1 <= len(lines) <= MAX_LINES:
        raise ValueError("invalid line count")
    total = 0
    for line in lines:
        if not isinstance(line, str) or CONTROLS.search(line):
            raise ValueError("invalid line")
        total += len(line.encode("utf-8"))
        if total > MAX_TEXT_BYTES:
            raise ValueError("text exceeds limit")


def romanize_chunk(source, converter):
    tokens = converter.convert(source)
    if "".join(token["orig"] for token in tokens) != source:
        return source
    return " ".join(token["hepburn"] or token["orig"] for token in tokens)


def romanize_run(source, converter):
    # Normalize matched kana without changing punctuation or Latin text.
    # Chunk very long runs because pykakasi repeatedly scans their suffixes.
    normalized = unicodedata.normalize("NFKC", source)
    return " ".join(
        romanize_chunk(normalized[start : start + MAX_CONVERTER_CHARS], converter)
        for start in range(0, len(normalized), MAX_CONVERTER_CHARS)
    )


def convert(payload):
    if len(payload) > MAX_WIRE_BYTES:
        raise ValueError("request exceeds limit")
    request = json.loads(payload.decode("utf-8"), object_pairs_hook=unique_object)
    if not isinstance(request, dict) or request.keys() != {"lines"}:
        raise ValueError("invalid request")
    lines = request["lines"]
    validate_lines(lines)

    from pykakasi import kakasi

    converter = kakasi()
    converted = [JAPANESE_RUN.sub(lambda match: romanize_run(match.group(), converter), line) for line in lines]
    validate_lines(converted)
    response = json.dumps({"lines": converted}, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
    if len(response) > MAX_WIRE_BYTES:
        raise ValueError("response exceeds limit")
    return response


def main():
    try:
        response = convert(sys.stdin.buffer.read(MAX_WIRE_BYTES + 1))
        sys.stdout.buffer.write(response)
        sys.stdout.buffer.flush()
        return 0
    except Exception:
        # The parent needs a typed failure, never untrusted text or a local path.
        sys.stderr.buffer.write(b"Romaji conversion failed.\n")
        return 1


if __name__ == "__main__":
    sys.exit(main())
