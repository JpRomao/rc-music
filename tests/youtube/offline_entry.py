"""Separate, test-only real-API entry point; never selected by a product flag.

The frozen test executable imports ``rcmusic_youtube`` from its private PYZ.
Source execution loads that same production module from its fixed repository
path. No runtime, fixture path, command, URL or environment switch is accepted
from the CLI: its only argument is one eleven-character video identifier.

Callable loader/probe arguments below belong solely to local Python tests.
They are not command-line options or production resolver configuration.
"""

from __future__ import annotations

import contextlib
import importlib
import importlib.util
import io
import json
from pathlib import Path
import re
import struct
import sys


_PLAYER_URL = "https://www.youtube.com/s/player/rcmusic-offline/base.js"
_N_VECTORS = {"abcdef": "fedcba", "曲A😀": "😀A曲"}
_SIG_VECTORS = {"abcd": "bcda", "a b%?": " b%?a", "曲A😀": "A😀曲"}


class _OfflineFailure(Exception):
    def __init__(self, code="unavailable"):
        self.code = code
        super().__init__(code)


class _NetworkGuard:
    """Attempt counters for this Python execution, not an OS sandbox claim."""

    def __init__(self):
        self.active = False
        self.http = 0
        self.socket = 0
        self.initializations = 0
        self.players = 0
        self.error_type = _OfflineFailure

    def audit(self, event, _args):
        if not self.active:
            return
        if event.startswith("socket."):
            self.socket += 1
            raise self.error_type("restricted")
        if event.startswith("http.client."):
            self.http += 1
            raise self.error_type("restricted")

    def deny_http(self, *_args, **_kwargs):
        self.http += 1
        raise self.error_type("restricted")

    def deny_initialize(self, *_args, **_kwargs):
        self.initializations += 1
        raise self.error_type("restricted")

    def check(self):
        if self.http or self.socket or self.initializations:
            raise self.error_type("restricted")


class _QuietText(io.TextIOBase):
    @property
    def encoding(self):
        return "utf-8"

    def write(self, value):
        return len(value)


def _write_failure(writer, code):
    # This is used only before production run owns the wire sequence.
    if code not in ("unavailable", "restricted", "unsupported"):
        code = "unavailable"
    payload = json.dumps({"v": 1, "kind": "failure", "seq": 1, "code": code},
                         separators=(",", ":")).encode("utf-8")
    pending = memoryview(struct.pack("<I", len(payload)) + payload)
    try:
        while pending:
            count = writer.write(pending)
            if type(count) is not int or not 0 < count <= len(pending):
                return
            pending = pending[count:]
        writer.flush()
    except Exception:
        pass


def _load_production():
    if getattr(sys, "frozen", False):
        return importlib.import_module("rcmusic_youtube")
    path = Path(__file__).resolve().parents[2] / "tools/youtube-resolver/rcmusic_youtube.py"
    spec = importlib.util.spec_from_file_location("rcmusic_youtube", path)
    if spec is None or spec.loader is None:
        raise _OfflineFailure()
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def _load_dependencies():
    # All actual runtime imports remain lazy and occur under the audit guard.
    from importlib import metadata
    from types import SimpleNamespace

    director = importlib.import_module("yt_dlp.extractor.youtube.jsc._director")
    networking = importlib.import_module("yt_dlp.utils.networking")
    return SimpleNamespace(initialize_jsc_director=director.initialize_jsc_director,
                           HTTPHeaderDict=networking.HTTPHeaderDict,
                           ejs_version=metadata.version("yt-dlp-ejs"))


def _read_fixture(maximum):
    if getattr(sys, "frozen", False):
        path = Path(sys._MEIPASS) / "rcmusic_fixture/synthetic-player.js"
    else:
        path = Path(__file__).resolve().parent / "fixtures/synthetic-player.js"
    with path.open("rb") as source:
        raw = source.read(maximum + 1)
    if not raw or len(raw) > maximum:
        raise _OfflineFailure()
    # Match text-mode universal newlines on both source and frozen Windows
    # copies, after enforcing the byte quota and lossless UTF-8 decoding.
    return raw.decode("utf-8", errors="strict").replace("\r\n", "\n").replace("\r", "\n")


@contextlib.contextmanager
def _replace_methods(replacements):
    previous = []
    try:
        for owner, name, replacement in replacements:
            previous.append((owner, name, name in owner.__dict__, getattr(owner, name)))
            setattr(owner, name, replacement)
        yield
    finally:
        for owner, name, was_own, original in reversed(previous):
            if was_own:
                setattr(owner, name, original)
            else:
                delattr(owner, name)


def _offline_methods(production, runtime, dependencies, video_id, player, guard):
    def load_player(_ie, *, video_id=None, player_url=None, fatal=True):
        if video_id != expected_video or player_url != _PLAYER_URL or fatal is not True:
            raise production.ProtocolError()
        # This returns only the already-read local fixture. A denied attempt
        # caught by director code remains sticky; check it after bulk_solve so
        # the provider's framed exchange retains its actual sequence owner.
        guard.players += 1
        if guard.players != 1:
            raise production.ProtocolError()
        return player

    def extract_info(downloader, url, download=False, *args, **kwargs):
        if url != "https://www.youtube.com/watch?v=" + expected_video or download is not False or args or kwargs:
            raise production.ProtocolError()
        guard.check()
        ie = downloader._ies_instances[runtime.YoutubeIE.ie_key()]
        if type(ie) is not runtime.YoutubeIE or ie._downloader is not downloader:
            raise production.ProtocolError()
        providers = runtime.registry._jsc_providers.value
        if len(providers) != 1 or set(providers) != {"Rcmusic"}:
            raise production.ProtocolError()
        director = dependencies.initialize_jsc_director(ie)
        ie._jsc_director = director
        # The real director has this public collection. Unit doubles may omit
        # it; the real registry is independently checked above in either case.
        if hasattr(director, "providers") and set(director.providers) != {"Rcmusic"}:
            raise production.ProtocolError()
        api = runtime.jsc
        requests = [
            api.JsChallengeRequest(type=api.JsChallengeType.N,
                                   input=api.NChallengeInput(player_url=_PLAYER_URL, challenges=list(_N_VECTORS)),
                                   video_id=expected_video),
            api.JsChallengeRequest(type=api.JsChallengeType.SIG,
                                   input=api.SigChallengeInput(player_url=_PLAYER_URL, challenges=list(_SIG_VECTORS)),
                                   video_id=expected_video),
        ]
        answers = director.bulk_solve(requests)
        guard.check()
        if type(answers) is not list or len(answers) != len(requests):
            raise production.ProtocolError()
        remaining = list(requests)
        verified = {"n": 0, "sig": 0}
        for pair in answers:
            if type(pair) is not tuple or len(pair) != 2:
                raise production.ProtocolError()
            request, response = pair
            matches = [item for item in remaining if request == item]
            if len(matches) != 1 or not isinstance(response, api.JsChallengeResponse):
                raise production.ProtocolError()
            original = matches[0]
            if response.type != original.type:
                raise production.ProtocolError()
            if original.type == api.JsChallengeType.N:
                expected, output_type, kind = _N_VECTORS, api.NChallengeOutput, "n"
            else:
                expected, output_type, kind = _SIG_VECTORS, api.SigChallengeOutput, "sig"
            if not isinstance(response.output, output_type) or response.output.results != expected:
                raise production.ProtocolError()
            verified[kind] += len(response.output.results)
            remaining.remove(original)
        if remaining or guard.players != 1:
            raise production.ProtocolError()
        guard.check()
        receipt = {"yt_dlp": runtime.version, "ejs": dependencies.ejs_version,
                   "n": verified["n"], "sig": verified["sig"], "http": guard.http,
                   "socket": guard.socket, "init": guard.initializations,
                   "players": guard.players, "providers": len(providers)}
        title = "RC Music offline v1 " + json.dumps(receipt, ensure_ascii=False, separators=(",", ":"))
        return {
            "_type": "video", "id": expected_video, "extractor_key": "Youtube", "title": title,
            "artist": "RC Music synthetic fixture",
            "url": "https://r1.googlevideo.com/videoplayback?expire=2000000000&sig=RCMUSIC_OFFLINE",
            "duration": 2, "filesize": 100, "ext": "m4a", "acodec": "mp4a.40.2", "vcodec": "none",
            "protocol": "https", "availability": "public", "is_live": False, "was_live": False,
            "live_status": "not_live", "has_drm": False,
            "http_headers": dependencies.HTTPHeaderDict({"Accept": "*/*"}),
        }

    expected_video = video_id
    return [(runtime.YoutubeDL, "extract_info", extract_info),
            (runtime.YoutubeDL, "urlopen", guard.deny_http),
            (runtime.YoutubeIE, "_load_player", load_player),
            (runtime.YoutubeIE, "_real_initialize", guard.deny_initialize)]


def main(argv=None, *, reader=None, writer=None, production_loader=None,
         dependencies_loader=None, guard_probe=None):
    """Run the real resolver/director/provider against the original fixture.

    ``production_loader()`` returns the production module; its ``_load_runtime``
    is still called. ``dependencies_loader()`` returns an object containing
    ``initialize_jsc_director``, ``HTTPHeaderDict`` and ``ejs_version``. The
    optional test-only ``guard_probe(guard)`` runs while all guards are active.
    A guard exposes ``audit(event, args)``, ``deny_http`` and
    ``deny_initialize``; attempts remain fatal even if the caller catches them.
    """
    argv = sys.argv[1:] if argv is None else argv
    reader = sys.stdin.buffer if reader is None else reader
    writer = sys.stdout.buffer if writer is None else writer
    if (type(argv) not in (list, tuple) or len(argv) != 1 or type(argv[0]) is not str
            or re.fullmatch(r"[A-Za-z0-9_-]{11}", argv[0]) is None):
        _write_failure(writer, "unsupported")
        return 1

    guard = _NetworkGuard()
    run_started = False
    guard.active = True
    try:
        # The permanent audit callback becomes inert in finally. It is active
        # before even production's lazy import or the actual runtime import.
        sys.addaudithook(guard.audit)
        with contextlib.redirect_stdout(_QuietText()), contextlib.redirect_stderr(_QuietText()):
            production = (production_loader or _load_production)()
            guard.error_type = production.ProtocolError
            guard.check()
            runtime = production._load_runtime()
            guard.check()
            dependencies = (dependencies_loader or _load_dependencies)()
            guard.check()
            if (runtime.version != production.PINNED_YTDLP_VERSION
                    or dependencies.ejs_version != production.PINNED_EJS_VERSION):
                raise production.ProtocolError("unsupported")
            player = _read_fixture(production.MAX_PLAYER_BYTES)
            replacements = _offline_methods(production, runtime, dependencies, argv[0], player, guard)
            with _replace_methods(replacements):
                if guard_probe is not None:
                    guard_probe(guard)
                guard.check()
                run_started = True
                code = production.run(argv[0], reader, writer, runtime_loader=lambda: runtime)
                guard.check()
                return code
    except Exception as error:
        if not run_started:
            code = "restricted" if guard.http or guard.socket or guard.initializations else getattr(error, "code", "unavailable")
            _write_failure(writer, code)
        return 1
    finally:
        guard.active = False


if __name__ == "__main__":
    raise SystemExit(main())
