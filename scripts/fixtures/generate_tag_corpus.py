"""Generate canonical ID3v2.3 text-limit fixtures in a new out/m0 stage."""

from __future__ import annotations

import argparse
import importlib.util
import json
import os
from pathlib import Path
import platform
import re
import struct
import sys


SCRIPT_RELATIVE = "scripts/fixtures/generate_tag_corpus.py"
HELPER_RELATIVE = "scripts/fixtures/generate_artwork_corpus.py"
SOURCE_RELATIVE = "tests/audio/corpus/fixtures/valid/mp3-cbr-stereo.mp3"
SOURCE_SIZE = 33318
SOURCE_SHA256 = "b7d0f99cf733c7458dbec3ab62ad397cd98cc6b069db5c12cbb9834885e66b62"
LICENSE = "LicenseRef-MusicPlayer-M0-Private"
FIELDS = {"title": b"TIT2", "artist": b"TPE1", "album": b"TALB"}
SOURCE_TAGS = {"title": "Canção determinística 日本語 🎵", "artist": "João – Codex", "album": "Álbum de teste Ω"}
CASES = tuple(
    (f"mp3-{field}-{boundary}", {field: size}, False)
    for field in FIELDS
    for boundary, size in (("below", 16383), ("at", 16384), ("above", 16385))
) + (("mp3-all-fields-at", dict.fromkeys(FIELDS, 16384), True),)


def load_helpers():
    path = Path(__file__).absolute().with_name("generate_artwork_corpus.py")
    spec = importlib.util.spec_from_file_location("rcmusic_artwork_fixture_helpers", path)
    if spec is None or spec.loader is None:
        raise ValueError("the explicit sibling artwork helper cannot be loaded")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module, path


def parse_frames(body: bytes) -> tuple[list[tuple[bytes, bytes]], bytes]:
    frames = []
    offset = 0
    while offset < len(body):
        if body[offset] == 0:
            padding = body[offset:]
            if any(padding):
                raise ValueError("the authenticated ID3 padding must contain only zeros")
            return frames, padding
        if len(body) - offset < 10:
            raise ValueError("the source ID3 frame header is incomplete")
        frame_id = body[offset : offset + 4]
        size = struct.unpack(">I", body[offset + 4 : offset + 8])[0]
        end = offset + 10 + size
        if not re.fullmatch(rb"[A-Z0-9]{4}", frame_id) or body[offset + 8 : offset + 10] != b"\x00\x00" or end > len(body):
            raise ValueError("the source must contain bounded ID3v2.3 frames with flags zero")
        frames.append((frame_id, body[offset:end]))
        offset = end
    return frames, b""


def generated_text(size: int) -> tuple[str, dict]:
    count, odd = divmod(size, 2)
    suffix = "a" if odd else ""
    text = "é" * count + suffix
    pattern = {"character": "é", "repeat_count": count, "suffix": suffix,
               "utf8_size_bytes": len(text.encode("utf-8")), "utf16le_size_bytes": len(text.encode("utf-16le"))}
    if pattern["utf8_size_bytes"] != size:
        raise ValueError("the text pattern did not reach its exact UTF-8 size")
    return text, pattern


def text_frame(frame_id: bytes, text: str) -> bytes:
    payload = b"\x01\xff\xfe" + text.encode("utf-16le")
    return frame_id + struct.pack(">I", len(payload)) + b"\x00\x00" + payload


def generate(repository: str, destination: str) -> dict:
    helpers, helper_path = load_helpers()
    root = helpers.plain_absolute(repository)
    output = helpers.plain_absolute(destination)
    if not root.is_dir():
        raise ValueError("the repository root must exist")
    try:
        relative = output.relative_to(root / "out" / "m0")
    except ValueError as error:
        raise ValueError("the stage must be a new descendant of repository/out/m0") from error
    if not relative.parts or output.exists():
        raise ValueError("the stage must be new; existing outputs are never overwritten")
    source_path = helpers.plain_absolute(str(root / SOURCE_RELATIVE))
    with source_path.open("rb") as stream:
        if os.fstat(stream.fileno()).st_size != SOURCE_SIZE:
            raise ValueError("the source MP3 size differs from its fixed identity")
        source = stream.read(SOURCE_SIZE + 1)
    if len(source) != SOURCE_SIZE or helpers.sha256(source) != SOURCE_SHA256:
        raise ValueError("the source MP3 hash differs from its fixed identity")
    body, audio = helpers.source_tag_parts(source)
    frames, padding = parse_frames(body)
    for frame_id in FIELDS.values():
        if sum(item[0] == frame_id for item in frames) != 1:
            raise ValueError("the source must have exactly one title, artist and album frame")
    if any(frame_id == b"TRCK" for frame_id, _ in frames):
        raise ValueError("the authenticated baseline must not already contain a track frame")
    scripts = []
    for name, path in ((SCRIPT_RELATIVE, Path(__file__).absolute()), (HELPER_RELATIVE, helper_path)):
        content = helpers.plain_absolute(str(path)).read_bytes()
        scripts.append({"relative_path": name, "size_bytes": len(content), "sha256": helpers.sha256(content)})
    license_path = "tests/audio/corpus/LICENSE.assets.txt"
    license_bytes = helpers.plain_absolute(str(root / license_path)).read_bytes()
    if not license_bytes.startswith(LICENSE.encode("ascii")):
        raise ValueError("the existing private corpus license is required")
    source_identity = {"id": "mp3-cbr-stereo", "relative_path": SOURCE_RELATIVE, "size_bytes": SOURCE_SIZE, "sha256": SOURCE_SHA256}
    recipe = {
        "id": "rcmusic-text-limit-generator-v1", "scripts": scripts, "source": source_identity,
        "python_version": platform.python_version(), "python_implementation": platform.python_implementation(),
        "arguments": ["--repository-root", "<absolute-repository-root>", "--output-directory", "<new-descendant-of-out/m0>"],
        "id3": {"version": "2.3.0", "tag_flags": 0, "frame_flags": 0, "frame_size": "big-endian-uint32",
                "text_encoding_byte": 1, "text_encoding": "UTF-16LE", "bom_hex": "fffe", "terminator": "none",
                "unchanged_frames_padding_audio": "preserved byte-for-byte", "new_track_frame": "TRCK only in mp3-all-fields-at"},
        "text_pattern": "repeat é floor(utf8_size/2) times; append a iff utf8_size is odd",
        "aggregate_retained_bytes": 3 * 16384 + 2 * 4,
    }
    output.mkdir(parents=True, exist_ok=False)
    (output / "fixtures" / "tags").mkdir(parents=True, exist_ok=False)
    fixtures = []
    for name, sizes, aggregate in CASES:
        generated = {field: generated_text(size) for field, size in sizes.items()}
        replacements = {FIELDS[field]: text_frame(FIELDS[field], value[0]) for field, value in generated.items()}
        modified = b"".join(replacements.get(frame_id, raw) for frame_id, raw in frames)
        if aggregate:
            modified += text_frame(b"TRCK", "1")
        modified += padding
        content = b"ID3\x03\x00\x00" + helpers.synchsafe(len(modified)) + modified + audio
        relative_path = f"fixtures/tags/{name}.mp3"
        with (output / relative_path).open("xb") as stream:
            stream.write(content)
        expected_tags = {"state": "present", **SOURCE_TAGS, "track": 1 if aggregate else None, "year": 2026}
        diagnostics = []
        retained = 4 + (4 if aggregate else 0)
        for field in FIELDS:
            if field not in generated:
                retained += len(SOURCE_TAGS[field].encode("utf-8"))
            elif sizes[field] > 16384:
                expected_tags[field] = None
                diagnostics.append({"code": "FieldTooLarge", "field": field.capitalize(),
                                    "observed_bytes": sizes[field], "limit_bytes": 16384})
            else:
                expected_tags[field] = {"pattern": generated[field][1]}
                retained += sizes[field]
        display_title = name if expected_tags["title"] is None else expected_tags["title"]
        fixtures.append({
            "id": name, "relative_path": relative_path, "size_bytes": len(content), "sha256": helpers.sha256(content),
            "source_or_generation_recipe": {"recipe_id": recipe["id"], "case_id": name, "source": source_identity,
                                             "fields": {field: {"frame_id": FIELDS[field].decode("ascii"), **value[1],
                                                                "id3_payload_size_bytes": 3 + value[1]["utf16le_size_bytes"]}
                                                        for field, value in generated.items()},
                                             "added_track_number": 1 if aggregate else None},
            "license": LICENSE, "container": "MP3", "codec_and_profile": "MP3 Layer III CBR 128 kb/s",
            "duration_ms": 2000, "duration_tolerance_ms": 50, "sample_rate_hz": 48000, "channels": 2,
            "bit_depth_if_known": None, "bitrate_mode": "constant", "audio_properties_basis": "source-derived; audio bytes unchanged",
            "expected_tags": expected_tags, "expected_display_title": display_title,
            "expected_metadata_diagnostics": diagnostics, "expected_retained_metadata_bytes": retained,
            "expected_artwork": {"state": "absent", "encoded_size_bytes": 0, "width_px": None, "height_px": None,
                                 "pixel_count": None, "expected_action": "none"},
            "expected_capabilities": {"metadata_read": True, "duration_known": True, "tags_present": True,
                                      "artwork_present": False, "decodable": None, "seekable": None, "play_to_end": None},
            "expected_outcome": "TrackMetadata", "expected_error_code": None,
            "requirement_ids": ["FR-020", "FR-022", "NFR-LIMIT-002", "NFR-REL-001"],
        })
    manifest = {
        "schema_id": "rcmusic.audio-text-limits.v1", "schema_version": 1,
        "purpose": "canonical-metadata-text-limits-not-runtime-approval", "hash_algorithm": "sha256", "license": LICENSE,
        "license_source": {"relative_path": license_path, "size_bytes": len(license_bytes), "sha256": helpers.sha256(license_bytes)},
        "limits": {"field_utf8_bytes": 16384, "total_retained_bytes": 262144, "numeric_retained_bytes_each": 4},
        "recipe": recipe, "fixtures": fixtures,
    }
    manifest_bytes = (json.dumps(manifest, ensure_ascii=False, indent=2, allow_nan=False) + "\n").encode("utf-8")
    with (output / "text-limits.v1.json").open("xb") as stream:
        stream.write(manifest_bytes)
    return {"output_directory": str(output), "fixtures": len(fixtures), "fixture_bytes": sum(item["size_bytes"] for item in fixtures),
            "manifest_sha256": helpers.sha256(manifest_bytes), "scripts": scripts}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repository-root", required=True)
    parser.add_argument("--output-directory", required=True)
    arguments = parser.parse_args()
    try:
        result = generate(arguments.repository_root, arguments.output_directory)
    except (OSError, ValueError) as error:
        print(f"Text fixture generation failed: {error}", file=sys.stderr)
        return 1
    print(json.dumps(result, ensure_ascii=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
