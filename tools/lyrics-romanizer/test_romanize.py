import json
from pathlib import Path
import subprocess
import sys
import unittest


HELPER = Path(__file__).with_name("romanize.py")


def run_helper(payload):
    return subprocess.run(
        [sys.executable, "-I", "-B", HELPER],
        input=payload,
        capture_output=True,
        timeout=15,
        check=False,
    )


class RomanizeTests(unittest.TestCase):
    def test_hepburn_preserves_line_count_spacing_and_mixed_text(self):
        lines = ["日本語の歌", "", "  Hello、世界!\t♪", "かな", "カタカナ", "Déjà vu"]
        payload = json.dumps({"lines": lines}, ensure_ascii=False).encode("utf-8")
        result = run_helper(payload)

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(
            json.loads(result.stdout),
            {"lines": ["nihongo no uta", "", "  Hello、sekai!\t♪", "kana", "katakana", "Déjà vu"]},
        )
        self.assertEqual(result.stderr, b"")

    def test_long_japanese_run_keeps_one_bounded_response_line(self):
        count = 16 * 1024
        payload = json.dumps({"lines": ["歌" * count]}, ensure_ascii=False).encode("utf-8")
        result = run_helper(payload)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(json.loads(result.stdout), {"lines": [" ".join(["uta"] * count)]})

    def test_rejects_invalid_requests_without_partial_output(self):
        invalid = [
            b"",
            b'{"lines":[]}',
            b'{"lines":["valid"],"lines":["duplicate"]}',
            b'{"lines":["valid"],"another":true}',
            b'{"lines":["valid",5]}',
            b'{"lines":["valid","\\ud800"]}',
            b'{"lines":["valid","embedded\\nline"]}',
            b'{"lines":["valid","embedded\\u0000control"]}',
            b'{"lines":["valid"]}{"lines":["trailing"]}',
            b'{"lines":["\xff"]}',
            b'{"lines":' + b'[' * 2000 + b']' * 2000 + b'}',
            json.dumps({"lines": ["x" * (256 * 1024 + 1)]}).encode(),
            json.dumps({"lines": [""] * 8193}).encode(),
            b" " * (2 * 1024 * 1024 + 1),
        ]
        for payload in invalid:
            with self.subTest(length=len(payload), prefix=payload[:40]):
                result = run_helper(payload)
                self.assertEqual(result.returncode, 1)
                self.assertEqual(result.stdout, b"")
                self.assertEqual(result.stderr, b"Romaji conversion failed.\n")


if __name__ == "__main__":
    unittest.main()
