"""Generate the fixed MP3/APIC corpus in a fresh stage; Python stdlib only."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import stat
import struct
import sys
import zlib


SOURCE_RELATIVE = "tests/audio/corpus/fixtures/valid/mp3-cbr-stereo.mp3"
SOURCE_SIZE = 33318
SOURCE_SHA256 = "b7d0f99cf733c7458dbec3ab62ad397cd98cc6b069db5c12cbb9834885e66b62"
SCRIPT_RELATIVE = "scripts/fixtures/generate_artwork_corpus.py"
LICENSE_RELATIVE = "tests/audio/corpus/LICENSE.assets.txt"
LICENSE = "LicenseRef-MusicPlayer-M0-Private"
MAX_ENCODED = 10 * 1024 * 1024
MAX_PADDING_PAYLOAD = 1024 * 1024
# name, generated width/height, exact PNG byte target, expected artwork diagnostic
CASES = (
    ("mp3-png-small", 1, 1, None, None),
    ("mp3-png-bytes-below", 1, 1, MAX_ENCODED - 1, None),
    ("mp3-png-bytes-at", 1, 1, MAX_ENCODED, None),
    ("mp3-png-bytes-above", 1, 1, MAX_ENCODED + 1, "EncodedBytesExceeded"),
    ("mp3-png-width-below", 4095, 1, None, None),
    ("mp3-png-width-at", 4096, 1, None, None),
    ("mp3-png-width-above", 4097, 1, None, "AxisPixelsExceeded"),
    ("mp3-png-height-below", 1, 4095, None, None),
    ("mp3-png-height-at", 1, 4096, None, None),
    ("mp3-png-height-above", 1, 4097, None, "AxisPixelsExceeded"),
    ("mp3-png-pixels-below", 3999, 4001, None, None),
    ("mp3-png-pixels-at", 4000, 4000, None, None),
    ("mp3-png-pixels-above", 4001, 4000, None, "PixelCountExceeded"),
    ("mp3-invalid-art", None, None, None, "InvalidData"),
)


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def plain_absolute(value: str) -> Path:
    path = Path(value)
    if not path.is_absolute() or value.replace("\\", "/").startswith("//"):
        raise ValueError("an ordinary absolute local path is required")
    if any(part in {".", ".."} for part in value.replace("\\", "/").split("/")):
        raise ValueError("dot path segments are unsupported")
    value.encode("utf-8", "strict")
    for part in path.parts[1:]:
        if (
            part.endswith((".", " "))
            or any(ord(char) < 32 or ord(char) == 127 or char in ':<>"|?*' for char in part)
            or re.fullmatch(r"CON|PRN|AUX|NUL|CONIN\$|CONOUT\$|COM[1-9¹²³]|LPT[1-9¹²³]", part.split(".")[0], re.IGNORECASE)
        ):
            raise ValueError("unsafe path component")
    for ancestor in (path, *path.parents):
        try:
            info = ancestor.lstat()
        except FileNotFoundError:
            continue
        if stat.S_ISLNK(info.st_mode) or getattr(info, "st_file_attributes", 0) & 0x400:
            raise ValueError("reparse points and symlinks are unsupported")
    return path


def synchsafe(value: int) -> bytes:
    if not 0 <= value <= 0x0FFFFFFF:
        raise ValueError("ID3 tag size exceeds its synchsafe field")
    return bytes((value >> 21, (value >> 14) & 127, (value >> 7) & 127, value & 127))


def source_tag_parts(source: bytes) -> tuple[bytes, bytes]:
    if source[:6] != b"ID3\x03\x00\x00" or any(byte & 128 for byte in source[6:10]):
        raise ValueError("the authenticated source must have ID3v2.3 revision 0, flags 0")
    tag_size = sum(byte << shift for byte, shift in zip(source[6:10], (21, 14, 7, 0)))
    if 10 + tag_size >= len(source):
        raise ValueError("the source ID3 body or following audio is incomplete")
    return source[10 : 10 + tag_size], source[10 + tag_size :]


def png_chunk(kind: bytes, payload: bytes) -> bytes:
    return struct.pack(">I", len(payload)) + kind + payload + struct.pack(">I", zlib.crc32(kind + payload) & 0xFFFFFFFF)


def black_png(width: int, height: int, exact_bytes: int | None) -> bytes:
    # PNG grayscale8, no interlace, filter 0 on every black scanline.
    ihdr = struct.pack(">IIBBBBB", width, height, 8, 0, 0, 0, 0)
    raw = bytes(width + 1) * height
    prefix = b"\x89PNG\r\n\x1a\n" + png_chunk(b"IHDR", ihdr) + png_chunk(b"IDAT", zlib.compress(raw, level=9))
    ending = png_chunk(b"IEND", b"")
    if exact_bytes is not None:
        remaining = exact_bytes - len(prefix) - len(ending)
        if remaining < 0 or 0 < remaining < 12:
            raise ValueError("the exact PNG target cannot fit its padding chunk")
        # raNd: ancillary, private, reserved bit valid, safe to copy.
        # Bound each chunk to avoid libpng's separate per-chunk allocation cap.
        count = (remaining + MAX_PADDING_PAYLOAD + 11) // (MAX_PADDING_PAYLOAD + 12)
        payload_left = remaining - 12 * count
        for _ in range(count):
            size = min(MAX_PADDING_PAYLOAD, payload_left)
            prefix += png_chunk(b"raNd", bytes(size))
            payload_left -= size
    image = prefix + ending
    if exact_bytes is not None and len(image) != exact_bytes:
        raise ValueError("the PNG byte target was not reached")
    return image


def with_apic(tag_body: bytes, audio: bytes, image: bytes) -> bytes:
    # ID3v2.3 frame size is big-endian; its empty description uses Latin-1.
    payload = b"\x00image/png\x00\x03\x00" + image
    frame = b"APIC" + struct.pack(">I", len(payload)) + b"\x00\x00" + payload
    body = frame + tag_body
    return b"ID3\x03\x00\x00" + synchsafe(len(body)) + body + audio


def generate(repository: str, destination: str) -> dict:
    root = plain_absolute(repository)
    output = plain_absolute(destination)
    if not root.is_dir():
        raise ValueError("the repository root must exist")
    stage_root = root / "out" / "m0"
    try:
        relative = output.relative_to(stage_root)
    except ValueError as error:
        raise ValueError("the stage must be a new descendant of repository/out/m0") from error
    if not relative.parts or output.exists():
        raise ValueError("the stage must be new; existing outputs are never overwritten")
    source_path = plain_absolute(str(root / SOURCE_RELATIVE))
    with source_path.open("rb") as stream:
        if os.fstat(stream.fileno()).st_size != SOURCE_SIZE:
            raise ValueError("the source MP3 size differs from its fixed identity")
        source = stream.read(SOURCE_SIZE + 1)
    if len(source) != SOURCE_SIZE or sha256(source) != SOURCE_SHA256:
        raise ValueError("the source MP3 hash differs from its fixed identity")
    tag_body, audio = source_tag_parts(source)
    script_path = plain_absolute(str(Path(__file__).absolute()))
    script = script_path.read_bytes()
    license_bytes = plain_absolute(str(root / LICENSE_RELATIVE)).read_bytes()
    if not license_bytes.startswith(LICENSE.encode("ascii")):
        raise ValueError("the existing private corpus license is required")
    source_identity = {"id": "mp3-cbr-stereo", "relative_path": SOURCE_RELATIVE, "size_bytes": SOURCE_SIZE, "sha256": SOURCE_SHA256}
    recipe = {
        "id": "rcmusic-artwork-generator-v1",
        "script": {"relative_path": SCRIPT_RELATIVE, "size_bytes": len(script), "sha256": sha256(script)},
        "python_version": platform.python_version(),
        "python_implementation": platform.python_implementation(),
        "zlib_compile_version": zlib.ZLIB_VERSION,
        "zlib_runtime_version": zlib.ZLIB_RUNTIME_VERSION,
        "arguments": ["--repository-root", "<absolute-repository-root>", "--output-directory", "<new-descendant-of-out/m0>"],
        "source": source_identity,
        "id3": {"version": "2.3.0", "flags": 0, "frame": "APIC", "frame_size_encoding": "big-endian-uint32", "mime": "image/png", "picture_type": 3,
                "description_encoding": "ISO-8859-1", "description": "", "placement": "before-original-tag-body", "audio_and_original_tag_body": "unchanged"},
        "png": {"bit_depth": 8, "color_type": 0, "pixel_value": 0, "scanline_filter": 0, "interlace": 0,
                "zlib_level": 9, "exact_size_padding": {"chunk": "raNd", "value": 0, "position": "before-IEND",
                                                         "max_payload_bytes": MAX_PADDING_PAYLOAD, "crc": "PNG CRC32"}},
        "invalid_artwork": {"encoding": "ASCII", "content": "not-an-image"},
    }
    output.mkdir(parents=True, exist_ok=False)
    artwork_directory = output / "fixtures" / "artwork"
    artwork_directory.mkdir(parents=True, exist_ok=False)
    fixtures = []
    for name, width, height, target, diagnostic in CASES:
        image = b"not-an-image" if width is None else black_png(width, height, target)
        content = with_apic(tag_body, audio, image)
        relative_path = f"fixtures/artwork/{name}.mp3"
        with (output / relative_path).open("xb") as stream:
            stream.write(content)
        fixtures.append({
            "id": name, "relative_path": relative_path, "sha256": sha256(content), "size_bytes": len(content),
            "source_or_generation_recipe": {"recipe_id": recipe["id"], "case_id": name, "source": source_identity},
            "license": LICENSE, "container": "MP3", "codec_and_profile": "MP3 Layer III CBR 128 kb/s",
            "duration_ms": 2000, "duration_tolerance_ms": 50, "sample_rate_hz": 48000, "channels": 2,
            "bit_depth_if_known": None, "bitrate_mode": "constant", "audio_properties_basis": "source-derived; audio bytes unchanged",
            "expected_tags": {"state": "present", "title": "Canção determinística 日本語 🎵", "artist": "João – Codex",
                              "album": "Álbum de teste Ω", "track": None, "year": 2026},
            "encoded_artwork": {"mime_type": "image/png", "size_bytes": len(image), "sha256": sha256(image),
                                "width_px": width, "height_px": height, "pixel_count": None if width is None else width * height},
            "expected_artwork": {"state": "present" if diagnostic is None else "rejected", "encoded_size_bytes": len(image),
                                 "width_px": width, "height_px": height, "pixel_count": None if width is None else width * height,
                                 "expected_action": "decode_and_thumbnail" if diagnostic is None else "discard_artwork_keep_metadata",
                                 "diagnostic_code": diagnostic, "thumbnail_width_px": None, "thumbnail_height_px": None,
                                 "thumbnail_max_axis_px": 1024},
            "expected_capabilities": {"metadata_read": True, "duration_known": True, "tags_present": True,
                                      "artwork_present": diagnostic is None, "decodable": None, "seekable": None, "play_to_end": None},
            "expected_outcome": "TrackMetadata", "expected_error_code": None,
            "requirement_ids": ["FR-020", "FR-021", "NFR-LIMIT-001", "NFR-REL-001"],
        })
    manifest = {
        "schema_id": "rcmusic.audio-artwork.v1", "schema_version": 1,
        "purpose": "canonical-artwork-limit-fixtures-not-runtime-approval", "hash_algorithm": "sha256", "license": LICENSE,
        "license_source": {"relative_path": LICENSE_RELATIVE, "size_bytes": len(license_bytes), "sha256": sha256(license_bytes)},
        "limits": {"max_encoded_bytes": MAX_ENCODED, "max_axis_pixels": 4096, "max_pixel_count": 16000000},
        "recipe": recipe, "fixtures": fixtures,
    }
    manifest_bytes = (json.dumps(manifest, ensure_ascii=False, indent=2, allow_nan=False) + "\n").encode("utf-8")
    with (output / "artwork.v1.json").open("xb") as stream:
        stream.write(manifest_bytes)
    return {"output_directory": str(output), "fixtures": len(fixtures), "fixture_bytes": sum(item["size_bytes"] for item in fixtures),
            "manifest_sha256": sha256(manifest_bytes), "script_sha256": recipe["script"]["sha256"]}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repository-root", required=True)
    parser.add_argument("--output-directory", required=True)
    arguments = parser.parse_args()
    try:
        result = generate(arguments.repository_root, arguments.output_directory)
    except (OSError, ValueError) as error:
        print(f"Artwork fixture generation failed: {error}", file=sys.stderr)
        return 1
    print(json.dumps(result, ensure_ascii=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
