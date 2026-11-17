"""Gera os sinais de teste do spike de sincronia da sala de escuta.

Saida (48 kHz, estereo, 16 bits), so com a biblioteca padrao:
- clicks-left.wav: 3 min, clique de 1 ms por segundo so no canal esquerdo;
- clicks-right.wav: o mesmo, so no canal direito;
- tone-1k.wav: 20 s de seno de 1 kHz nos dois canais (sonda de tom).

Os cliques ficam em 0,5 s + k segundos (k = 0..179). A conversao para MP3
usa o ffmpeg do PATH: python make_clicks.py <pasta> --mp3
"""

import argparse
import math
import shutil
import struct
import subprocess
import sys
import wave
from pathlib import Path

RATE = 48_000
SECONDS = 180
CLICK_OFFSET_S = 0.5
CLICK_LEN = RATE // 1000  # 1 ms
CLICK_AMPLITUDE = 0.5
TONE_HZ = 1000.0
TONE_SECONDS = 20
TONE_AMPLITUDE = 0.2


def write_stereo(path: Path, frames: int, sample_at) -> None:
    with wave.open(str(path), "wb") as out:
        out.setnchannels(2)
        out.setsampwidth(2)
        out.setframerate(RATE)
        chunk = []
        for i in range(frames):
            left, right = sample_at(i)
            chunk.append(struct.pack("<hh", int(left * 32767), int(right * 32767)))
            if len(chunk) == RATE:
                out.writeframes(b"".join(chunk))
                chunk = []
        out.writeframes(b"".join(chunk))


def click_value(i: int) -> float:
    t = i - int(CLICK_OFFSET_S * RATE)
    if t < 0:
        return 0.0
    return CLICK_AMPLITUDE if (t % RATE) < CLICK_LEN else 0.0


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("out_dir", type=Path)
    parser.add_argument("--mp3", action="store_true", help="converte com o ffmpeg do PATH (320 kbit/s)")
    args = parser.parse_args()
    args.out_dir.mkdir(parents=True, exist_ok=True)

    frames = SECONDS * RATE
    left = args.out_dir / "clicks-left.wav"
    right = args.out_dir / "clicks-right.wav"
    tone = args.out_dir / "tone-1k.wav"
    write_stereo(left, frames, lambda i: (click_value(i), 0.0))
    write_stereo(right, frames, lambda i: (0.0, click_value(i)))
    write_stereo(
        tone,
        TONE_SECONDS * RATE,
        lambda i: (TONE_AMPLITUDE * math.sin(2 * math.pi * TONE_HZ * i / RATE),) * 2,
    )

    if args.mp3:
        ffmpeg = shutil.which("ffmpeg")
        if ffmpeg is None:
            print("ffmpeg nao encontrado no PATH", file=sys.stderr)
            return 1
        for wav in (left, right, tone):
            subprocess.run(
                [ffmpeg, "-hide_banner", "-loglevel", "error", "-y", "-i", str(wav),
                 "-c:a", "libmp3lame", "-b:a", "320k", str(wav.with_suffix(".mp3"))],
                check=True,
            )
    print(f"ok: {args.out_dir}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
