"""Generate a tiny, recording-free AAC-LC signal for temporal codec tests.

The checked-in test header is produced from the JSON stdout with write_file.
FFmpeg is a fixture-generation tool only, never a product or test-runtime dep.
"""
import argparse
import base64
import hashlib
import json
import math
from pathlib import Path
import struct
import subprocess
import tempfile
import wave

parser = argparse.ArgumentParser()
parser.add_argument("--ffmpeg", required=True, type=Path)
parser.add_argument("--scratch", required=True, type=Path)
args = parser.parse_args()
assert args.ffmpeg.is_file() and args.scratch.is_dir()
with tempfile.TemporaryDirectory(prefix="rc-music-temporal-signal-", dir=args.scratch) as temporary:
    root = Path(temporary)
    pcm = bytearray()
    rate = 48000
    for frame in range(2 * rate):
        frequency = 313 if frame < 28800 else 997 if frame < 60000 else 2003 if frame < 76800 else 0
        value = round(10000 * math.sin(2 * math.pi * frequency * frame / rate)) if frequency else 0
        pcm += struct.pack("<h", value)
    source = root / "synthetic.wav"
    with wave.open(str(source), "wb") as output:
        output.setnchannels(1)
        output.setsampwidth(2)
        output.setframerate(rate)
        output.writeframes(pcm)
    target = root / "synthetic.m4a"
    command = [str(args.ffmpeg), "-hide_banner", "-loglevel", "error", "-nostdin",
               "-fflags", "+bitexact", "-i", str(source), "-map", "0:a:0", "-map_metadata", "-1",
               "-c:a", "aac", "-flags:a", "+bitexact", "-b:a", "64000", "-ar", "48000", "-ac", "1",
               "-movflags", "+faststart", str(target)]
    subprocess.run(command, check=True, capture_output=True, timeout=30)
    data = target.read_bytes()
    assert 0 < len(data) < 32768
    print(json.dumps({"rate": rate, "channels": 1, "frames": 96000,
                      "segments": [[0, 28800, 313], [28800, 60000, 997], [60000, 76800, 2003], [76800, 96000, 0]],
                      "pcmSha256": hashlib.sha256(pcm).hexdigest(),
                      "aacSha256": hashlib.sha256(data).hexdigest(),
                      "aacBytes": len(data), "base64": base64.b64encode(data).decode("ascii")}))
