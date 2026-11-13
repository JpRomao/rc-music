"""Pinned API resolver with an injected, stop-and-wait parent JS broker.

This module starts no processes. It is not a qualified product runtime.
"""

from __future__ import annotations

import json
import math
import re
import struct
import sys

PINNED_YTDLP_VERSION = "2026.10.26"
PINNED_EJS_VERSION = "0.8.0"
FORMAT_SELECTOR = (
    "bestaudio[ext=m4a][vcodec=none][acodec=mp4a.40.2][protocol=https]/"
    "bestaudio[ext=m4a][vcodec=none][acodec=aac][protocol=https]"
)
MAX_FRAME_BYTES = 16 * 1024 * 1024
MAX_PLAYER_BYTES = 8 * 1024 * 1024
MAX_RESPONSE_BYTES = 1024 * 1024
MAX_RESULT_BYTES = 1024 * 1024
MAX_TOTAL_WIRE_BYTES = 64 * 1024 * 1024
MAX_REQUESTS = 64
MAX_CHALLENGES = 256
MAX_STRING_BYTES = 16 * 1024
MAX_CHALLENGE_BYTES = 1024 * 1024
MAX_CHALLENGE_GROUPS = 8
PROJECTION_FIELDS = (
    "_type", "id", "extractor_key", "url", "title", "artist", "duration",
    "filesize", "filesize_approx", "ext", "acodec", "vcodec", "protocol",
    "availability", "is_live", "was_live", "live_status", "has_drm",
    "http_headers", "entries", "requested_formats", "fragments",
)
# Unicode White_Space used by QString::trimmed(), without Python's additional
# U+001C..U+001F separators. Only player/challenge/solved values require text.
_QT_WHITESPACE = "\t\n\v\f\r \u0085\u00a0\u1680" + "".join(chr(value) for value in range(0x2000, 0x200B)) + "\u2028\u2029\u202f\u205f\u3000"


class ProtocolError(Exception):
    """Closed/invalid broker protocol; never include provider data in its message."""

    def __init__(self, code="unavailable"):
        self.code = code if code in ("unavailable", "restricted", "unsupported") else "unavailable"
        super().__init__(self.code)


def _string(value, maximum, *, nonblank=False):
    if type(value) is not str or len(value) > maximum:
        raise ProtocolError()
    if nonblank and not value.strip(_QT_WHITESPACE):
        raise ProtocolError()
    try:
        encoded = value.encode("utf-8", errors="strict")
    except UnicodeError:
        raise ProtocolError() from None
    if len(encoded) > maximum:
        raise ProtocolError()
    return len(encoded)


def _json_payload(value, maximum):
    remaining = maximum

    def charge(count):
        nonlocal remaining
        if count > remaining:
            raise ProtocolError()
        remaining -= count

    def normalize(item):
        if type(item) is str:
            charge(_string(item, remaining) + 2)
            return item
        elif isinstance(item, dict):
            charge(2)
            captured = {}
            # HTTPHeaderDict is a dict subclass in the pinned API. Use concrete
            # container operations, never a subclass serializer/items callback.
            for index, (key, child) in enumerate(dict.items(item)):
                charge((1 if index else 0) + _string(key, remaining) + 3)
                captured[key] = normalize(child)
            return captured
        elif isinstance(item, (list, tuple)):
            charge(2)
            captured = []
            iterator = list.__iter__(item) if isinstance(item, list) else tuple.__iter__(item)
            for index, child in enumerate(iterator):
                if index:
                    charge(1)
                captured.append(normalize(child))
            return captured
        elif item is None:
            charge(4)
            return None
        elif type(item) is bool:
            charge(4 if item else 5)
            return item
        elif type(item) is int:
            charge(len(str(item)))
            return item
        elif type(item) is float and math.isfinite(item):
            charge(len(repr(item)))
            return item
        else:
            raise ProtocolError()

    try:
        captured = normalize(value)
        payload = json.dumps(captured, ensure_ascii=False, allow_nan=False, separators=(",", ":")).encode("utf-8")
    except (ValueError, TypeError, UnicodeError, RecursionError):
        raise ProtocolError() from None
    if not payload or len(payload) > maximum:
        raise ProtocolError()
    return payload


def _requests(requests):
    if type(requests) is not list or not 0 < len(requests) <= MAX_REQUESTS:
        raise ProtocolError()
    captured, total = [], 0
    for item in requests:
        if type(item) is not dict or set(item) != {"type", "challenges"} or item["type"] not in ("n", "sig"):
            raise ProtocolError()
        values = item["challenges"]
        if type(values) is not list or not 0 < len(values) <= MAX_CHALLENGES:
            raise ProtocolError()
        for challenge in values:
            total += _string(challenge, MAX_STRING_BYTES, nonblank=True)
            if total > MAX_CHALLENGE_BYTES:
                raise ProtocolError()
        captured.append({"type": item["type"], "challenges": list(values)})
    return captured


def _responses(responses, requests):
    if type(responses) is not list or len(responses) != len(requests):
        raise ProtocolError()
    captured = []
    for response, request in zip(responses, requests):
        if type(response) is not dict:
            raise ProtocolError()
        if response.get("type") == "error":
            if set(response) != {"type", "code"} or response["code"] != "unavailable":
                raise ProtocolError()
            captured.append({"type": "error", "code": "unavailable"})
            continue
        if set(response) != {"type", "data"} or response["type"] != "result":
            raise ProtocolError()
        data = response["data"]
        if type(data) is not dict or set(data) != set(request["challenges"]):
            raise ProtocolError()
        for key, value in data.items():
            _string(key, MAX_STRING_BYTES)
            _string(value, MAX_STRING_BYTES, nonblank=True)
        captured.append({"type": "result", "data": dict(data)})
    return captured


def _strict_json(payload):
    def pairs(items):
        result = {}
        for key, value in items:
            if key in result:
                raise ProtocolError()
            result[key] = value
        return result

    def constant(_value):
        raise ProtocolError()

    try:
        return json.loads(payload.decode("utf-8", errors="strict"), object_pairs_hook=pairs, parse_constant=constant)
    except (ValueError, UnicodeError, RecursionError):
        raise ProtocolError() from None


class ProtocolTransport:
    def __init__(self, reader, writer):
        self.reader = reader
        self.writer = writer
        self.seq = 1
        self.closed = False
        self.total_wire_bytes = 0
        self._pending = False

    def _fail(self):
        self.closed = True
        raise ProtocolError()

    def _ready(self):
        if self.closed or self._pending or not 1 <= self.seq <= MAX_CHALLENGE_GROUPS + 1:
            self._fail()

    def _alive(self):
        if self.closed:
            self._fail()

    def _write(self, payload, *, charge_exchange=True):
        frame = struct.pack("<I", len(payload)) + payload
        if charge_exchange and len(frame) > MAX_TOTAL_WIRE_BYTES - self.total_wire_bytes:
            self._fail()
        view = memoryview(frame)
        offset = 0
        while offset < len(frame):
            count = self.writer.write(view[offset:])
            if type(count) is not int or not 0 < count <= len(frame) - offset:
                self._fail()
            offset += count
            if charge_exchange:
                self.total_wire_bytes += count
            self._alive()
        self.writer.flush()
        self._alive()

    def _read_exact(self, count):
        if count > MAX_TOTAL_WIRE_BYTES - self.total_wire_bytes:
            self._fail()
        chunks = bytearray()
        while len(chunks) < count:
            remaining = count - len(chunks)
            part = self.reader.read(remaining)
            if type(part) is not bytes or not part or len(part) > remaining:
                self._fail()
            self.total_wire_bytes += len(part)
            self._alive()
            chunks.extend(part)
        return bytes(chunks)

    def exchange(self, player, requests):
        self._ready()
        if self.seq > MAX_CHALLENGE_GROUPS:
            self._fail()
        self._pending = True
        try:
            _string(player, MAX_PLAYER_BYTES, nonblank=True)
            captured = _requests(requests)
            self._write(_json_payload({"v": 1, "kind": "challenge", "seq": self.seq,
                                       "player": player, "requests": captured}, MAX_FRAME_BYTES))
            size, = struct.unpack("<I", self._read_exact(4))
            if not 0 < size <= MAX_RESPONSE_BYTES:
                self._fail()
            response = _strict_json(self._read_exact(size))
            if type(response) is not dict or set(response) != {"v", "kind", "seq", "responses"}:
                self._fail()
            if (type(response["v"]) is not int or response["v"] != 1 or response["kind"] != "response"
                    or type(response["seq"]) is not int or response["seq"] != self.seq):
                self._fail()
            answer = _responses(response["responses"], captured)
            self._alive()
            self.seq += 1
            return answer
        except Exception:
            self.closed = True
            raise ProtocolError() from None
        finally:
            self._pending = False

    def _terminal(self, packet, maximum=MAX_RESPONSE_BYTES):
        self._ready()
        self._pending = True
        try:
            self._write(_json_payload(packet, maximum), charge_exchange=False)
            self.closed = True
        except Exception:
            self.closed = True
            raise ProtocolError() from None
        finally:
            self._pending = False

    def result(self, projection):
        self._ready()
        try:
            if type(projection) is not dict or set(projection) - set(PROJECTION_FIELDS):
                self._fail()
            _json_payload(projection, MAX_RESULT_BYTES)
        except Exception:
            self.closed = True
            raise ProtocolError() from None
        self._terminal({"v": 1, "kind": "result", "seq": self.seq, "data": projection}, MAX_FRAME_BYTES)

    def failure(self, code):
        if code not in ("unavailable", "restricted", "unsupported"):
            self._fail()
        self._terminal({"v": 1, "kind": "failure", "seq": self.seq, "code": code})


def make_provider(api, transport):
    class RcmusicJCP(api.JsChallengeProvider):
        _SUPPORTED_TYPES = (api.JsChallengeType.N, api.JsChallengeType.SIG)
        PROVIDER_VERSION = "1"

        def is_available(self):
            return not transport.closed

        def _real_bulk_solve(self, requests):
            if not requests:
                return
            groups = {}
            try:
                if type(requests) is not list or len(requests) > MAX_REQUESTS * MAX_CHALLENGE_GROUPS:
                    raise ProtocolError()
                for index, request in enumerate(requests):
                    if not isinstance(request, api.JsChallengeRequest):
                        raise ProtocolError()
                    if request.type == api.JsChallengeType.N and isinstance(request.input, api.NChallengeInput):
                        kind = "n"
                    elif request.type == api.JsChallengeType.SIG and isinstance(request.input, api.SigChallengeInput):
                        kind = "sig"
                    else:
                        raise ProtocolError()
                    _string(request.input.player_url, 8192)
                    group = groups.setdefault(request.input.player_url, [])
                    group.append((index, request, {"type": kind, "challenges": request.input.challenges}))
                if len(groups) > MAX_CHALLENGE_GROUPS:
                    raise ProtocolError()
                # Validate all groups before a player load or partial dispatch.
                captured = [(player, group, _requests([item[2] for item in group]))
                            for player, group in groups.items()]
            except Exception:
                for request in requests:
                    yield api.JsChallengeProviderResponse(request=request, error=api.JsChallengeProviderError("unavailable"))
                return
            solved = [None] * len(requests)
            for player_url, group, wire_requests in captured:
                try:
                    if transport.closed:
                        raise ProtocolError()
                    player = self._get_player(group[0][1].video_id, player_url)
                    _string(player, MAX_PLAYER_BYTES, nonblank=True)
                    answers = _responses(transport.exchange(player, wire_requests), wire_requests)
                    for (index, request, _wire), answer in zip(group, answers):
                        if answer["type"] == "error":
                            solved[index] = api.JsChallengeProviderResponse(
                                request=request, error=api.JsChallengeProviderError("unavailable"))
                        else:
                            output = api.NChallengeOutput if request.type == api.JsChallengeType.N else api.SigChallengeOutput
                            solved[index] = api.JsChallengeProviderResponse(
                                request=request, response=api.JsChallengeResponse(
                                    type=request.type, output=output(results=answer["data"])))
                except Exception:
                    for index, request, _wire in group:
                        solved[index] = api.JsChallengeProviderResponse(
                            request=request, error=api.JsChallengeProviderError("unavailable"))
            yield from solved

    return RcmusicJCP


def fixed_options():
    return {
        "format": FORMAT_SELECTOR, "simulate": True, "skip_download": True, "noplaylist": True,
        "quiet": True, "no_warnings": True, "noprogress": True, "verbose": False,
        "cachedir": False, "cookiefile": None, "cookiesfrombrowser": None,
        "usenetrc": False, "netrc_location": None, "netrc_cmd": None,
        "username": None, "password": None, "videopassword": None,
        "proxy": "", "socket_timeout": 20, "retries": 0, "extractor_retries": 0,
        "fragment_retries": 0, "remote_components": set(), "js_runtimes": {},
        "allow_multiple_audio_streams": False, "allow_multiple_video_streams": False,
        "allow_unplayable_formats": False, "check_formats": False, "ignoreerrors": False,
        "postprocessors": [], "writesubtitles": False, "writeautomaticsub": False,
        "writeinfojson": False, "writedescription": False, "writethumbnail": False,
        "getcomments": False, "fixup": "never", "bidi_workaround": False,
        "compat_opts": set(), "logger": _SilentLogger(),
    }


class _SilentLogger:
    def debug(self, *_args, **_kwargs):
        pass

    def info(self, *_args, **_kwargs):
        pass

    def warning(self, *_args, **_kwargs):
        pass

    def error(self, *_args, **_kwargs):
        pass


def configure_runtime(runtime, transport):
    if runtime.version != PINNED_YTDLP_VERSION:
        raise ProtocolError("unsupported")
    runtime.globals.plugin_dirs.value = []
    runtime.globals.all_plugins_loaded.value = True
    for name in ("plugin_ies", "plugin_pps", "plugin_ies_overrides"):
        getattr(runtime.globals, name).value.clear()
    runtime.registry._jsc_providers.value.clear()
    runtime.registry._jsc_preferences.value.clear()
    provider = make_provider(runtime.jsc, transport)
    runtime.jsc.register_provider(provider)
    return provider


def _load_runtime():
    # Imports are deliberately confined to the one-shot, configured worker.
    # No CLI option parser, runtime detection, EJS package or plugin loader.
    import importlib
    from types import SimpleNamespace

    package = importlib.import_module("yt_dlp")
    globals_module = importlib.import_module("yt_dlp.globals")
    version = importlib.import_module("yt_dlp.version").__version__
    jsc = importlib.import_module("yt_dlp.extractor.youtube.jsc.provider")
    registry = importlib.import_module("yt_dlp.extractor.youtube.jsc._registry")
    youtube_ie = importlib.import_module("yt_dlp.extractor.youtube").YoutubeIE
    return SimpleNamespace(version=version, globals=globals_module, jsc=jsc, registry=registry,
                           YoutubeDL=package.YoutubeDL, YoutubeIE=youtube_ie)


def _video_id(value):
    return type(value) is str and re.fullmatch(r"[A-Za-z0-9_-]{11}", value) is not None


def _emit_failure(transport, code):
    if not transport.closed:
        try:
            transport.failure(code)
        except ProtocolError:
            pass


def run(video_id, reader, writer, runtime_loader=None):
    transport = ProtocolTransport(reader, writer)
    if not _video_id(video_id):
        _emit_failure(transport, "unsupported")
        return 1
    try:
        runtime = (runtime_loader or _load_runtime)()
        configure_runtime(runtime, transport)
        with runtime.YoutubeDL(fixed_options(), auto_init=False) as downloader:
            downloader.add_info_extractor(runtime.YoutubeIE())
            extracted = downloader.extract_info("https://www.youtube.com/watch?v=" + video_id, download=False)
        if transport.closed:
            return 1
        if type(extracted) is not dict:
            raise ProtocolError("unsupported")
        # Match yt-dlp's pinned projected-print traversal: absent/None fields
        # are omitted, while False, zero and explicitly empty values survive.
        projection = {key: extracted[key] for key in PROJECTION_FIELDS
                      if key in extracted and extracted[key] is not None}
        try:
            _json_payload(projection, MAX_RESULT_BYTES)
            _json_payload({"v": 1, "kind": "result", "seq": transport.seq, "data": projection}, MAX_FRAME_BYTES)
        except ProtocolError:
            raise ProtocolError("unsupported") from None
        transport.result(projection)
        return 0
    except ProtocolError as error:
        _emit_failure(transport, error.code)
    except Exception:
        _emit_failure(transport, "unavailable")
    return 1


def main(argv=None, *, reader=None, writer=None, runtime_loader=None):
    argv = sys.argv[1:] if argv is None else argv
    reader = sys.stdin.buffer if reader is None else reader
    writer = sys.stdout.buffer if writer is None else writer
    if type(argv) not in (list, tuple) or len(argv) != 1:
        _emit_failure(ProtocolTransport(reader, writer), "unsupported")
        return 1
    return run(argv[0], reader, writer, runtime_loader)


if __name__ == "__main__":
    raise SystemExit(main())
