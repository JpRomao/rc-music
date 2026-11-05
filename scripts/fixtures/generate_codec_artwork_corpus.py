"""Author three fixed codec-cover fixtures with the existing pinned FFmpeg."""

from __future__ import annotations

import argparse
import base64
import importlib.util
import json
from pathlib import Path
import platform
import struct
import subprocess
import sys
import zlib


HELPER = Path(__file__).with_name("generate_artwork_corpus.py")
spec = importlib.util.spec_from_file_location("artwork_recipe", HELPER)
helper = importlib.util.module_from_spec(spec)
spec.loader.exec_module(helper)
FFMPEG_SHA256 = "09948d4cdd0650da6ff5a87577469f2a218dc2615ae379f8f734d24c49de0f73"
FFMPEG_SIZE = 227398656
CASES = (
    ("m4a-aac-art", "m4a-aac-lc.m4a", "m4a", 33400,
     "aefe894a1a9277bf4795d421c97d4780fa342eef300819a459ca45329a614558", "ISO MP4/M4A", "AAC", "constant"),
    ("flac-art", "flac-16-stereo.flac", "flac", 60531,
     "103577f9c6b37eea391b86f9a085dc7fb9b0a26571aa907b9d37e47d0b543a1c", "FLAC", "FLAC 16-bit", "lossless"),
    ("ogg-vorbis-art", "ogg-vorbis.ogg", "ogg", 12605,
     "abab4db40eb8380974e1c6ea69eb4c24bfd8eb2803b67b79e2a1a49523a7096f", "Ogg", "Vorbis", "variable"),
)


def identity(path: Path, relative: str) -> dict:
    content = path.read_bytes()
    return {"relative_path": relative, "size_bytes": len(content), "sha256": helper.sha256(content)}


def generate(repository: str, destination: str, ffmpeg_argument: str) -> dict:
    root = helper.plain_absolute(repository)
    output = helper.plain_absolute(destination)
    ffmpeg = helper.plain_absolute(ffmpeg_argument)
    if not output.is_relative_to(root / "out/m0") or output == root / "out/m0" or output.exists():
        raise ValueError("the output must be a new descendant of repository/out/m0")
    if ffmpeg.stat().st_size != FFMPEG_SIZE or helper.sha256(ffmpeg.read_bytes()) != FFMPEG_SHA256:
        raise ValueError("FFmpeg differs from the existing fixture-authoring tool identity")
    sources = []
    for _, name, _, size, digest, _, _, _ in CASES:
        path = helper.plain_absolute(str(root / "tests/audio/corpus/fixtures/valid" / name))
        source = identity(path, f"tests/audio/corpus/fixtures/valid/{name}")
        if source["size_bytes"] != size or source["sha256"] != digest:
            raise ValueError("a source fixture differs from its fixed identity")
        sources.append(source)
    license_path = root / "tests/audio/corpus/LICENSE.assets.txt"
    if helper.sha256(license_path.read_bytes()) != "54b7a0cbbf8050b2ac4e24cf8afd74c68a2c6d6c6b22c0487599dd84d23b8b13":
        raise ValueError("the existing private corpus license differs")
    output.mkdir(parents=True, exist_ok=False)
    (output / "fixtures/artwork").mkdir(parents=True, exist_ok=False)
    image = helper.black_png(1, 1, None)
    image_path = output / "cover.png"
    with image_path.open("xb") as stream:
        stream.write(image)
    # FLAC PICTURE fields: FrontCover, PNG MIME, empty description, grayscale8.
    picture = (struct.pack(">II", 3, 9) + b"image/png" + struct.pack(">IIIIII", 0, 1, 1, 8, 0, len(image)) + image)
    fixtures = []
    for case, source in zip(CASES, sources):
        name, _, extension, _, _, container, codec, bitrate_mode = case
        source_path = root / source["relative_path"]
        relative = f"fixtures/artwork/{name}.{extension}"
        arguments = ["-hide_banner", "-loglevel", "error", "-nostdin", "-n",
                     "-protocol_whitelist", "file,pipe", "-i", str(source_path)]
        if extension == "ogg":
            arguments += ["-map", "0:a:0", "-map_metadata", "0", "-c:a", "copy", "-metadata",
                          "METADATA_BLOCK_PICTURE=" + base64.b64encode(picture).decode("ascii")]
        else:
            arguments += ["-protocol_whitelist", "file,pipe", "-i", str(image_path), "-map", "0:a:0",
                          "-map", "1:v:0", "-map_metadata", "0", "-c", "copy", "-disposition:v:0", "attached_pic"]
        arguments += ["-fflags", "+bitexact", str(output / relative)]
        completed = subprocess.run([str(ffmpeg), *arguments], stdin=subprocess.DEVNULL,
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=30, check=False)
        if completed.returncode:
            raise ValueError(f"FFmpeg rejected {name}: {completed.stderr.decode('utf-8', 'replace')}")
        command = [value.replace(str(output), "<stage>").replace(str(root), "<repository>") for value in arguments]
        fixtures.append({
            "id": name, **identity(output / relative, relative),
            "source_or_generation_recipe": {"recipe_id": "rcmusic-codec-artwork-v1", "source": source,
                                             "arguments": command},
            "license": helper.LICENSE, "container": container, "codec_and_profile": codec,
            "duration_ms": 2000, "duration_tolerance_ms": 25, "sample_rate_hz": 48000, "channels": 2,
            "bit_depth_if_known": 16 if extension == "flac" else None, "bitrate_mode": bitrate_mode,
            "audio_properties_basis": "source-derived; FFmpeg stream copy without re-encoding",
            "expected_tags": {"title": "Canção determinística 日本語 🎵", "artist": "João – Codex", "album": "Álbum de teste Ω"},
            "expected_artwork": {"state": "present", "mime_type": "image/png", "width_px": 1, "height_px": 1,
                                 "encoded_size_bytes": len(image), "encoded_sha256": helper.sha256(image),
                                 "thumbnail_width_px": 1, "thumbnail_height_px": 1, "rgba8888_bytes": 4},
            "expected_capabilities": {"metadata_read": True, "artwork_present": True, "duration_known": True,
                                      "decodable": None, "seekable": None, "play_to_end": None},
        })
    manifest = {
        "schema_id": "rcmusic.codec-artwork.v1", "schema_version": 1, "license": helper.LICENSE,
        "purpose": "canonical-metadata-artwork-fixtures-not-runtime-approval",
        "recipe": {"id": "rcmusic-codec-artwork-v1", "script": identity(Path(__file__), "scripts/fixtures/generate_codec_artwork_corpus.py"),
                   "helper": identity(HELPER, "scripts/fixtures/generate_artwork_corpus.py"),
                   "python_version": platform.python_version(), "zlib_runtime_version": zlib.ZLIB_RUNTIME_VERSION,
                   "ffmpeg": {"version": "8.1.1-full_build-www.gyan.dev", "size_bytes": FFMPEG_SIZE, "sha256": FFMPEG_SHA256,
                              "usage_scope": "fixture-generation-only; not packaged"},
                   "cover": {"kind": "black-grayscale8-png", "width_px": 1, "height_px": 1,
                             "size_bytes": len(image), "sha256": helper.sha256(image)}},
        "fixtures": fixtures,
    }
    content = (json.dumps(manifest, ensure_ascii=False, indent=2, allow_nan=False) + "\n").encode("utf-8")
    with (output / "codec-artwork.v1.json").open("xb") as stream:
        stream.write(content)
    if helper.sha256(ffmpeg.read_bytes()) != FFMPEG_SHA256:
        raise ValueError("FFmpeg changed during generation")
    return {"fixtures": len(fixtures), "fixture_bytes": sum(item["size_bytes"] for item in fixtures),
            "manifest_sha256": helper.sha256(content), "script_sha256": manifest["recipe"]["script"]["sha256"]}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repository-root", required=True)
    parser.add_argument("--output-directory", required=True)
    parser.add_argument("--ffmpeg", required=True)
    args = parser.parse_args()
    try:
        result = generate(args.repository_root, args.output_directory, args.ffmpeg)
    except (OSError, ValueError, subprocess.TimeoutExpired) as error:
        print(f"Codec fixture generation failed: {error}", file=sys.stderr)
        return 1
    print(json.dumps(result))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
