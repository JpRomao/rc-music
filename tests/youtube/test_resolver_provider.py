"""Offline contract tests. Never imports yt-dlp/EJS or starts a process."""

import ast
import contextlib
import dataclasses
import enum
import importlib.util
import io
import json
from pathlib import Path
import struct
import sys
import types
import unittest
from unittest import mock


MODULE_PATH = Path(__file__).resolve().parents[2] / "tools/youtube-resolver/rcmusic_youtube.py"
SPEC = importlib.util.spec_from_file_location("rcmusic_youtube", MODULE_PATH)
resolver = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = resolver
SPEC.loader.exec_module(resolver)
VIDEO = "aB0-_Cd1EF2"


def compact(value):
    return json.dumps(value, ensure_ascii=False, allow_nan=False, separators=(",", ":")).encode("utf-8")


def wire(value):
    payload = compact(value)
    return struct.pack("<I", len(payload)) + payload


def frames(raw):
    result = []
    while raw:
        if len(raw) < 4:
            raise AssertionError("partial prefix")
        count, = struct.unpack("<I", raw[:4])
        if not 0 < count <= len(raw) - 4:
            raise AssertionError("partial payload")
        result.append(json.loads(raw[4:4 + count].decode("utf-8")))
        raw = raw[4 + count:]
    return result


def response(seq=1, responses=None):
    if responses is None:
        responses = [{"type": "result", "data": {"a": "solved"}}]
    return {"v": 1, "kind": "response", "seq": seq, "responses": responses}


def request(kind="n", challenges=None):
    return {"type": kind, "challenges": ["a"] if challenges is None else challenges}


class SplitReader(io.BytesIO):
    def read(self, count=-1):
        return super().read(min(count, 3) if count >= 0 else 3)


class SplitWriter(io.BytesIO):
    def write(self, value):
        return super().write(value[:3])


class Indirect:
    def __init__(self, value):
        self.value = value


def public_api(registry=None):
    class ChallengeType(enum.Enum):
        N = "n"
        SIG = "sig"

    @dataclasses.dataclass(frozen=True)
    class NInput:
        player_url: str
        challenges: list

    @dataclasses.dataclass(frozen=True)
    class SigInput:
        player_url: str
        challenges: list

    @dataclasses.dataclass(frozen=True)
    class Request:
        type: ChallengeType
        input: object
        video_id: str = None

    @dataclasses.dataclass(frozen=True)
    class NOutput:
        results: dict

    @dataclasses.dataclass(frozen=True)
    class SigOutput:
        results: dict

    @dataclasses.dataclass
    class Response:
        type: ChallengeType
        output: object

    @dataclasses.dataclass
    class ProviderResponse:
        request: Request
        response: object = None
        error: Exception = None

    class ProviderError(Exception):
        pass

    class Base:
        def __init_subclass__(cls, **kwargs):
            super().__init_subclass__()
            cls.PROVIDER_KEY = cls.__name__[:-3]

        def __init__(self, ie, logger=None, settings=None):
            self.ie, self.logger, self.settings = ie, logger, settings or {}

        def _get_player(self, video_id, player_url):
            return self.ie._load_player(video_id=video_id, player_url=player_url, fatal=True)

        def bulk_solve(self, requests):
            yield from self._real_bulk_solve(requests)

    registry = registry if registry is not None else Indirect({})

    def register_provider(provider):
        if provider.PROVIDER_KEY in registry.value:
            raise AssertionError("provider already registered")
        registry.value[provider.PROVIDER_KEY] = provider
        return provider

    return types.SimpleNamespace(
        JsChallengeProvider=Base, JsChallengeType=ChallengeType, JsChallengeRequest=Request,
        NChallengeInput=NInput, SigChallengeInput=SigInput, NChallengeOutput=NOutput,
        SigChallengeOutput=SigOutput, JsChallengeResponse=Response,
        JsChallengeProviderResponse=ProviderResponse, JsChallengeProviderError=ProviderError,
        JsChallengeProviderRejectedRequest=ProviderError, register_provider=register_provider,
    )


def public_request(api, kind="n", challenges=None, player="https://www.youtube.com/s/player/A/base.js", video=VIDEO):
    cls = api.NChallengeInput if kind == "n" else api.SigChallengeInput
    tag = api.JsChallengeType.N if kind == "n" else api.JsChallengeType.SIG
    return api.JsChallengeRequest(tag, cls(player, ["a"] if challenges is None else challenges), video)


class FakeIE:
    def __init__(self):
        self.players = []

    def _load_player(self, **kwargs):
        self.players.append(kwargs)
        return "player source: " + kwargs["player_url"]


class Exchange:
    def __init__(self):
        self.closed = False
        self.calls = []
        self.answer = None

    def exchange(self, player, requests):
        self.calls.append((player, requests))
        if self.answer is not None:
            return self.answer
        return [{"type": "result", "data": {key: "solved:" + key[::-1] for key in item["challenges"]}}
                for item in requests]


def info():
    return {"_type": "video", "id": VIDEO, "extractor_key": "Youtube", "title": "歌の名前",
            "url": "https://r1.googlevideo.com/videoplayback?expire=2000000000&sig=A",
            "duration": 120, "ext": "m4a", "acodec": "mp4a.40.2", "vcodec": "none",
            "protocol": "https", "availability": "public", "is_live": False, "was_live": False,
            "live_status": "not_live", "has_drm": False, "http_headers": {"Accept": "*/*"}}


class Bundle:
    def __init__(self):
        self.version = resolver.PINNED_YTDLP_VERSION
        self.globals = types.SimpleNamespace(
            all_plugins_loaded=Indirect(False), plugin_dirs=Indirect(["default"]),
            plugin_ies=Indirect({"foreign": object()}), plugin_pps=Indirect({"foreign": object()}),
            plugin_ies_overrides=Indirect({"foreign": [object()]}),
        )
        self.registry = types.SimpleNamespace(_jsc_providers=Indirect({"foreign": object()}),
                                              _jsc_preferences=Indirect({object()}))
        self.jsc = public_api(self.registry._jsc_providers)
        self.YoutubeIE = FakeIE
        self.constructed, self.closed = 0, 0
        self.options, self.auto_init = None, None
        self.extract_calls, self.ies = [], []
        self.info, self.on_extract, self.error = info(), None, None
        bundle = self

        class YoutubeDL:
            def __init__(self, options, auto_init=True):
                if not bundle.globals.all_plugins_loaded.value or bundle.globals.plugin_dirs.value:
                    raise AssertionError("plugin discovery was not disabled before constructor")
                for name in ("plugin_ies", "plugin_pps", "plugin_ies_overrides"):
                    if getattr(bundle.globals, name).value:
                        raise AssertionError("preloaded plugin data escaped")
                providers = list(bundle.registry._jsc_providers.value.values())
                if len(providers) != 1 or not issubclass(providers[0], bundle.jsc.JsChallengeProvider):
                    raise AssertionError("foreign JSC provider escaped")
                if bundle.registry._jsc_preferences.value:
                    raise AssertionError("foreign provider preferences escaped")
                bundle.constructed += 1
                bundle.options, bundle.auto_init = options, auto_init

            def __enter__(self):
                return self

            def __exit__(self, *args):
                bundle.closed += 1

            def add_info_extractor(self, ie):
                bundle.ies.append(ie)

            def extract_info(self, url, download=True):
                bundle.extract_calls.append((url, download))
                if bundle.on_extract:
                    bundle.on_extract(self)
                if bundle.error:
                    raise bundle.error
                return bundle.info

        self.YoutubeDL = YoutubeDL


class TransportTests(unittest.TestCase):
    def transport(self, answer=None, reader_class=io.BytesIO, writer_class=io.BytesIO):
        reader = reader_class(wire(response() if answer is None else answer))
        writer = writer_class()
        return resolver.ProtocolTransport(reader, writer), writer

    def test_valid_unicode_ordered_exchange_and_correlated_final(self):
        answers = [{"type": "result", "data": {"a": "解決", "á": "Latin"}},
                   {"type": "error", "code": "unavailable"}]
        transport, output = self.transport(response(responses=answers))
        result = transport.exchange("日本語 player\nsource", [request(challenges=["a", "á"]), request("sig", ["s"])])
        self.assertEqual(result, answers)
        self.assertEqual(frames(output.getvalue()), [{"v": 1, "kind": "challenge", "seq": 1,
                         "player": "日本語 player\nsource", "requests": [request(challenges=["a", "á"]), request("sig", ["s"])]}])
        self.assertEqual(transport.seq, 2)
        transport.result(info())
        self.assertEqual(frames(output.getvalue())[-1], {"v": 1, "kind": "result", "seq": 2, "data": info()})
        self.assertTrue(transport.closed)
        with self.assertRaises(resolver.ProtocolError):
            transport.failure("unavailable")

    def test_prefix_payload_and_writes_may_all_be_partial(self):
        transport, output = self.transport(reader_class=SplitReader, writer_class=SplitWriter)
        self.assertEqual(transport.exchange("player", [request()]), response()["responses"])
        self.assertEqual(frames(output.getvalue())[0]["kind"], "challenge")
        expected = len(output.getvalue()) + len(wire(response()))
        self.assertEqual(transport.total_wire_bytes, expected)

    def test_repeated_input_strings_use_exact_set_of_response_keys(self):
        transport, _ = self.transport()
        self.assertEqual(transport.exchange("player", [request(challenges=["a", "a"])]), response()["responses"])

    def test_invalid_group_fields_and_limits_fail_before_any_write(self):
        cases = [[], [request("unknown")], [request(challenges=[])], [request(challenges=[1])],
                 [request(challenges=["x" * (16 * 1024 + 1)])], [request(challenges=["a"] * 257)],
                 [request()] * 65, [request(challenges=["x" * 16384] * 65)],
                 [{"type": "n", "challenges": ["a"], "path": "ignored"}],
                 [request(challenges=["\ud800"])]]
        for requests in cases:
            with self.subTest(requests=str(requests)[:100]):
                transport, output = self.transport()
                with self.assertRaises(resolver.ProtocolError):
                    transport.exchange("player", requests)
                self.assertEqual(output.getvalue(), b"")
                self.assertTrue(transport.closed)

    def test_player_and_encoded_frame_caps_are_independent(self):
        for player in ("é" * (4 * 1024 * 1024 + 1), "\\" * (8 * 1024 * 1024), "\ud800"):
            with self.subTest(length=len(player)):
                transport, output = self.transport()
                with self.assertRaises(resolver.ProtocolError):
                    transport.exchange(player, [request()])
                self.assertEqual(output.getvalue(), b"")

    def test_utf8_input_and_output_string_limits_are_inclusive(self):
        key, value = "é" * 8192, "界" * 5461 + "a"
        answer = response(responses=[{"type": "result", "data": {key: value}}])
        transport, _ = self.transport(answer)
        self.assertEqual(transport.exchange("player", [request(challenges=[key])]), answer["responses"])

    def test_blank_player_inputs_and_solved_values_are_rejected(self):
        for blank in ("", " ", "\t\r\n", "\u00a0\u2003"):
            for role in ("player", "input", "output"):
                with self.subTest(role=role, blank=repr(blank)):
                    key = blank if role == "input" else "a"
                    value = blank if role == "output" else "solved"
                    answer = response(responses=[{"type": "result", "data": {key: value}}])
                    transport, _ = self.transport(answer)
                    with self.assertRaises(resolver.ProtocolError):
                        transport.exchange(blank if role == "player" else "player", [request(challenges=[key])])
                    self.assertTrue(transport.closed)

    def test_non_challenge_metadata_strings_can_remain_blank_in_raw_projection(self):
        # Protocol data only: the separate download codec may reject metadata
        # or headers that this generic JSON envelope can safely transport.
        projection = {"title": "", "artist": " \t", "http_headers": {"Accept": ""}}
        transport, output = self.transport()
        transport.result(projection)
        self.assertEqual(frames(output.getvalue())[-1]["data"], projection)

    def test_invalid_response_envelope_and_partial_or_extraneous_results_close(self):
        cases = []
        for key, value in (("v", True), ("v", 2), ("seq", 0), ("seq", 2), ("seq", True), ("kind", "result")):
            changed = response(); changed[key] = value; cases.append(changed)
        changed = response(); changed["extra"] = 1; cases.append(changed)
        for answers in ([], [{"type": "result", "data": {}}], [{"type": "result", "data": {"a": "ok", "extra": "x"}}],
                        [{"type": "result", "data": {"a": 3}}], [{"type": "result", "data": {"a": "x" * 16385}}],
                        [{"type": "result", "data": {"a": "\ud800"}}], [{"type": "error", "code": "private"}],
                        [{"type": "result", "data": {"a": "ok"}, "extra": False}], response()["responses"] * 2):
            cases.append(response(responses=answers))
        for answer in cases:
            with self.subTest(answer=str(answer)[:100]):
                raw = json.dumps(answer, ensure_ascii=True, separators=(",", ":")).encode()
                output = io.BytesIO()
                transport = resolver.ProtocolTransport(io.BytesIO(struct.pack("<I", len(raw)) + raw), output)
                with self.assertRaises(resolver.ProtocolError):
                    transport.exchange("player", [request()])
                self.assertTrue(transport.closed)
                before = output.getvalue()
                with self.assertRaises(resolver.ProtocolError):
                    transport.exchange("player", [request()])
                self.assertEqual(output.getvalue(), before)

    def test_strict_json_utf8_prefix_limits_and_eof(self):
        malformed = [b'{"v":1,"v":1,"kind":"response","seq":1,"responses":[]}',
                     b'{"v":1,"kind":"response","seq":NaN,"responses":[]}', b"\xff", b"{}trailing"]
        inputs = [struct.pack("<I", len(raw)) + raw for raw in malformed]
        inputs += [b"", b"\x01", struct.pack("<I", 0), struct.pack("<I", 1024 * 1024 + 1),
                   struct.pack("<I", 10) + b"{}"]
        for raw in inputs:
            with self.subTest(raw=raw[:60]):
                transport = resolver.ProtocolTransport(io.BytesIO(raw), io.BytesIO())
                with self.assertRaises(resolver.ProtocolError):
                    transport.exchange("player", [request()])
                self.assertTrue(transport.closed)

    def test_eight_challenges_then_sequence_nine_final_is_the_maximum(self):
        incoming = b"".join(wire(response(seq=index)) for index in range(1, 9))
        output = io.BytesIO(); transport = resolver.ProtocolTransport(io.BytesIO(incoming), output)
        for _ in range(8):
            transport.exchange("player", [request()])
        transport.failure("unavailable")
        self.assertEqual([item["seq"] for item in frames(output.getvalue())], list(range(1, 10)))
        self.assertTrue(transport.closed)

    def test_ninth_challenge_is_refused_without_a_second_terminal(self):
        incoming = b"".join(wire(response(seq=index)) for index in range(1, 9))
        output = io.BytesIO(); transport = resolver.ProtocolTransport(io.BytesIO(incoming), output)
        for _ in range(8):
            transport.exchange("player", [request()])
        with self.assertRaises(resolver.ProtocolError):
            transport.exchange("player", [request()])
        self.assertEqual(len(frames(output.getvalue())), 8)

    def test_aggregate_budget_counts_prefixes_and_is_inclusive(self):
        outgoing = {"v": 1, "kind": "challenge", "seq": 1, "player": "player", "requests": [request()]}
        total = len(wire(outgoing)) + len(wire(response()))
        with mock.patch.object(resolver, "MAX_TOTAL_WIRE_BYTES", total):
            transport, _ = self.transport()
            transport.exchange("player", [request()])
            self.assertEqual(transport.total_wire_bytes, total)
        with mock.patch.object(resolver, "MAX_TOTAL_WIRE_BYTES", total - 1):
            transport, _ = self.transport()
            with self.assertRaises(resolver.ProtocolError):
                transport.exchange("player", [request()])
            self.assertTrue(transport.closed)

    def test_exact_exchange_budget_still_allows_the_independently_bounded_terminal(self):
        outgoing = {"v": 1, "kind": "challenge", "seq": 1, "player": "player", "requests": [request()]}
        total = len(wire(outgoing)) + len(wire(response()))
        for kind in ("result", "failure"):
            with self.subTest(kind=kind), mock.patch.object(resolver, "MAX_TOTAL_WIRE_BYTES", total):
                transport, output = self.transport()
                transport.exchange("player", [request()])
                if kind == "result":
                    transport.result(info())
                else:
                    transport.failure("unavailable")
                self.assertEqual(frames(output.getvalue())[-1]["kind"], kind)
                self.assertEqual(frames(output.getvalue())[-1]["seq"], 2)
                self.assertEqual(transport.total_wire_bytes, total)

    def test_reentrant_io_irreversibly_closes_the_pending_exchange(self):
        output = io.BytesIO()
        transport = None

        class ReentrantReader(io.BytesIO):
            once = False

            def read(self, count=-1):
                if not self.once:
                    self.once = True
                    with self_case.assertRaises(resolver.ProtocolError):
                        transport.result(info())
                return super().read(count)

        self_case = self
        transport = resolver.ProtocolTransport(ReentrantReader(wire(response())), output)
        with self.assertRaises(resolver.ProtocolError):
            transport.exchange("player", [request()])
        self.assertTrue(transport.closed)
        self.assertEqual([item["kind"] for item in frames(output.getvalue())], ["challenge"])

    def test_terminal_projection_envelope_has_its_own_one_mib_cap(self):
        for data in ({"title": "x" * (1024 * 1024)}, {"duration": float("nan")}, {"title": "\ud800"}, {"unknown": 1}):
            transport, output = self.transport()
            with self.assertRaises(resolver.ProtocolError):
                transport.result(data)
            self.assertEqual(output.getvalue(), b"")

    def test_exact_one_mib_projection_can_have_a_larger_terminal_envelope(self):
        # This deliberately oversized title is a raw protocol fixture. It does
        # not assert that a download metadata codec accepts the projected data.
        overhead = len(compact({"title": ""}))
        projection = {"title": "x" * (resolver.MAX_RESULT_BYTES - overhead)}
        self.assertEqual(len(compact(projection)), 1024 * 1024)
        transport, output = self.transport()
        transport.result(projection)
        actual = frames(output.getvalue())
        self.assertEqual(actual, [{"v": 1, "kind": "result", "seq": 1, "data": projection}])
        self.assertGreater(len(output.getvalue()) - 4, resolver.MAX_RESULT_BYTES)
        self.assertLessEqual(len(output.getvalue()) - 4, resolver.MAX_FRAME_BYTES)
        self.assertEqual(transport.total_wire_bytes, 0)
        self.assertTrue(transport.closed)

    def test_projection_one_byte_over_the_compact_data_cap_is_rejected(self):
        overhead = len(compact({"title": ""}))
        projection = {"title": "x" * (resolver.MAX_RESULT_BYTES - overhead + 1)}
        self.assertEqual(len(compact(projection)), 1024 * 1024 + 1)
        transport, output = self.transport()
        with self.assertRaises(resolver.ProtocolError):
            transport.result(projection)
        self.assertEqual(output.getvalue(), b"")
        self.assertTrue(transport.closed)

    def test_failure_has_only_a_fixed_code_and_is_terminal(self):
        for code in ("unavailable", "restricted", "unsupported"):
            transport, output = self.transport()
            transport.failure(code)
            self.assertEqual(frames(output.getvalue()), [{"v": 1, "kind": "failure", "seq": 1, "code": code}])
            self.assertTrue(transport.closed)
            with self.assertRaises(resolver.ProtocolError):
                transport.result(info())
        for code in ("private raw secret", "", 3):
            transport, output = self.transport()
            with self.assertRaises(resolver.ProtocolError):
                transport.failure(code)
            self.assertEqual(output.getvalue(), b"")


class ProviderTests(unittest.TestCase):
    def test_base_is_public_jsc_only_and_availability_is_passive(self):
        api = public_api(); exchange = Exchange(); ie = FakeIE()
        provider = resolver.make_provider(api, exchange)(ie, None, {})
        self.assertIsInstance(provider, api.JsChallengeProvider)
        self.assertTrue(provider.is_available())
        self.assertEqual(ie.players, [])
        self.assertEqual(exchange.calls, [])

    def test_player_url_groups_use_request_video_ids_and_preserve_response_identity(self):
        api = public_api(); exchange = Exchange(); ie = FakeIE()
        provider = resolver.make_provider(api, exchange)(ie, None, {})
        a = public_request(api, challenges=["a"])
        b = public_request(api, "sig", ["s"], "https://www.youtube.com/s/player/B/base.js", "bB0-_Cd1EF2")
        c = public_request(api, "sig", ["z"], a.input.player_url)
        results = list(provider.bulk_solve([a, b, c]))
        self.assertEqual(len(results), 3)
        self.assertEqual([item.request for item in results], [a, b, c])
        self.assertTrue(all(item.error is None for item in results))
        self.assertEqual(len(exchange.calls), 2)
        self.assertEqual(exchange.calls[0][1], [request(challenges=["a"]), request("sig", ["z"])])
        self.assertEqual(ie.players, [{"video_id": VIDEO, "player_url": a.input.player_url, "fatal": True},
                                     {"video_id": b.video_id, "player_url": b.input.player_url, "fatal": True}])
        self.assertIsInstance(results[0].response.output, api.NChallengeOutput)
        self.assertIsInstance(results[1].response.output, api.SigChallengeOutput)
        self.assertEqual(results[2].response.output.results, {"z": "solved:z"})

    def test_parent_unavailable_maps_to_public_error_without_fabricating_a_solution(self):
        api = public_api(); exchange = Exchange(); exchange.answer = [{"type": "error", "code": "unavailable"}]
        selected = public_request(api)
        result = list(resolver.make_provider(api, exchange)(FakeIE(), None, {}).bulk_solve([selected]))
        self.assertEqual(len(result), 1)
        self.assertIs(result[0].request, selected)
        self.assertIsNone(result[0].response)
        self.assertIsInstance(result[0].error, api.JsChallengeProviderError)

    def test_invalid_public_input_is_rejected_before_loading_player_or_dispatching_a_group(self):
        api = public_api()
        cases = [[public_request(api, challenges=[])], [public_request(api, challenges=["x" * 16385])],
                 [public_request(api)] * 65]
        for selected in cases:
            with self.subTest(count=len(selected)):
                exchange = Exchange(); ie = FakeIE()
                results = list(resolver.make_provider(api, exchange)(ie, None, {})._real_bulk_solve(selected))
                self.assertEqual(len(results), len(selected))
                self.assertTrue(all(item.error is not None and item.response is None for item in results))
                self.assertEqual(ie.players, [])
                self.assertEqual(exchange.calls, [])

    def test_player_failure_is_typed_and_keeps_raw_error_out_of_protocol(self):
        api = public_api(); exchange = Exchange()

        class BrokenIE:
            def _load_player(self, **kwargs):
                raise RuntimeError("private raw cookie secret")

        results = list(resolver.make_provider(api, exchange)(BrokenIE(), None, {}).bulk_solve([public_request(api)]))
        self.assertEqual(len(results), 1)
        self.assertIsInstance(results[0].error, api.JsChallengeProviderError)
        self.assertNotIn("secret", str(results[0].error))
        self.assertEqual(exchange.calls, [])


class EntrypointTests(unittest.TestCase):
    def test_import_has_no_eager_third_party_or_process_execution_imports(self):
        tree = ast.parse(MODULE_PATH.read_text(encoding="utf-8"))
        for node in tree.body:
            if isinstance(node, ast.Import):
                for name in node.names:
                    self.assertFalse(name.name.startswith(("yt_dlp", "yt_dlp_ejs", "subprocess")))
            if isinstance(node, ast.ImportFrom):
                self.assertFalse((node.module or "").startswith(("yt_dlp", "yt_dlp_ejs", "subprocess")))
        for node in ast.walk(tree):
            if isinstance(node, ast.Attribute):
                self.assertNotIn(node.attr, ("Popen", "system", "execv", "execve", "popen"))

    def test_runtime_registration_disables_plugins_before_constructing_youtubedl(self):
        bundle = Bundle(); output = io.BytesIO()
        self.assertEqual(resolver.run(VIDEO, io.BytesIO(), output, lambda: bundle), 0)
        self.assertEqual(bundle.constructed, 1)
        self.assertEqual(bundle.closed, 1)
        self.assertIs(bundle.auto_init, False)
        self.assertEqual(len(bundle.ies), 1)
        self.assertIsInstance(bundle.ies[0], FakeIE)
        self.assertEqual(bundle.extract_calls, [("https://www.youtube.com/watch?v=" + VIDEO, False)])
        self.assertEqual(bundle.options["format"],
                         "bestaudio[ext=m4a][vcodec=none][acodec=mp4a.40.2][protocol=https]/"
                         "bestaudio[ext=m4a][vcodec=none][acodec=aac][protocol=https]")
        for option in ("simulate", "skip_download", "noplaylist", "quiet", "no_warnings", "noprogress"):
            self.assertIs(bundle.options[option], True, option)
        for option in ("cachedir", "usenetrc", "allow_multiple_audio_streams", "allow_multiple_video_streams", "check_formats"):
            self.assertIs(bundle.options[option], False, option)
        self.assertEqual(bundle.options["js_runtimes"], {})
        self.assertEqual(bundle.options["remote_components"], set())
        self.assertEqual(bundle.options["postprocessors"], [])
        self.assertEqual(bundle.options["proxy"], "")
        self.assertIsNone(bundle.options["cookiefile"])
        self.assertIsNone(bundle.options["cookiesfrombrowser"])
        self.assertIsNone(bundle.options["netrc_cmd"])
        self.assertEqual(frames(output.getvalue()), [{"v": 1, "kind": "result", "seq": 1, "data": info()}])

    def test_projection_drops_captions_formats_and_does_not_infer_artist(self):
        bundle = Bundle(); output = io.BytesIO()
        bundle.info.update({"description": "private full description", "subtitles": {"ja": []},
                            "formats": [{"url": "private alternate stream"}], "uploader": "Channel is not artist"})
        self.assertEqual(resolver.run(VIDEO, io.BytesIO(), output, lambda: bundle), 0)
        actual = frames(output.getvalue())[0]["data"]
        self.assertEqual(actual, info())
        self.assertNotIn("artist", actual)
        self.assertNotIn(b"private", output.getvalue())

    def test_projection_accepts_the_pinned_http_header_dict_subclass_without_importing_ytdlp(self):
        class Headers(dict):
            pass

        bundle = Bundle(); output = io.BytesIO()
        bundle.info["http_headers"] = Headers({"Accept": "*/*", "User-Agent": "RCMusic synthetic"})
        self.assertEqual(resolver.run(VIDEO, io.BytesIO(), output, lambda: bundle), 0)
        actual = frames(output.getvalue())
        self.assertEqual(len(actual), 1)
        self.assertEqual(actual[0]["kind"], "result")
        self.assertEqual(actual[0]["data"]["http_headers"], dict(bundle.info["http_headers"]))

    def test_single_video_argument_and_exact_internal_version_are_required(self):
        for invalid in ("", "https://www.youtube.com/watch?v=" + VIDEO, VIDEO + "&list=x", "bad", "a" * 12, "-x"):
            with self.subTest(video=invalid):
                output = io.BytesIO(); loader = mock.Mock(side_effect=AssertionError("must stay lazy"))
                self.assertNotEqual(resolver.run(invalid, io.BytesIO(), output, loader), 0)
                loader.assert_not_called()
                self.assertEqual(frames(output.getvalue()), [{"v": 1, "kind": "failure", "seq": 1, "code": "unsupported"}])
        bundle = Bundle(); bundle.version = "2026.03.18"; output = io.BytesIO()
        self.assertNotEqual(resolver.run(VIDEO, io.BytesIO(), output, lambda: bundle), 0)
        self.assertEqual(bundle.constructed, 0)
        actual = frames(output.getvalue())
        self.assertEqual(len(actual), 1)
        self.assertEqual(actual[0]["code"], "unsupported")

    def test_main_rejects_extra_arguments_without_importing_the_runtime(self):
        for args in ([], [VIDEO, "--cookies", "secret"], [VIDEO, "2026.03.17"]):
            with self.subTest(args=args):
                output = io.BytesIO(); loader = mock.Mock(side_effect=AssertionError("must stay lazy"))
                self.assertNotEqual(resolver.main(args, reader=io.BytesIO(), writer=output, runtime_loader=loader), 0)
                loader.assert_not_called()
                actual = frames(output.getvalue())
                self.assertEqual(len(actual), 1)
                self.assertEqual(actual[0]["code"], "unsupported")

    def test_extraction_errors_emit_only_fixed_failure_without_raw_stdout_or_stderr(self):
        bundle = Bundle(); bundle.error = RuntimeError("raw private cookie/path secret")
        output = io.BytesIO(); stdout, stderr = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(stdout), contextlib.redirect_stderr(stderr):
            self.assertNotEqual(resolver.run(VIDEO, io.BytesIO(), output, lambda: bundle), 0)
        self.assertEqual(frames(output.getvalue()), [{"v": 1, "kind": "failure", "seq": 1, "code": "unavailable"}])
        self.assertEqual(stdout.getvalue(), "")
        self.assertEqual(stderr.getvalue(), "")

    def test_provider_challenge_and_final_share_one_sequence_owner(self):
        bundle = Bundle(); output = io.BytesIO()

        def solve(_ydl):
            cls, = bundle.registry._jsc_providers.value.values()
            provider = cls(bundle.ies[0], None, {})
            solved = list(provider.bulk_solve([public_request(bundle.jsc)]))
            self.assertEqual(solved[0].response.output.results, {"a": "solved"})

        bundle.on_extract = solve
        self.assertEqual(resolver.run(VIDEO, io.BytesIO(wire(response())), output, lambda: bundle), 0)
        self.assertEqual([(item["kind"], item["seq"]) for item in frames(output.getvalue())],
                         [("challenge", 1), ("result", 2)])

    def test_corrupt_parent_response_cannot_be_followed_by_a_fabricated_final(self):
        bundle = Bundle(); output = io.BytesIO()

        def solve(_ydl):
            cls, = bundle.registry._jsc_providers.value.values()
            list(cls(bundle.ies[0], None, {}).bulk_solve([public_request(bundle.jsc)]))

        bundle.on_extract = solve
        self.assertNotEqual(resolver.run(VIDEO, io.BytesIO(wire(response(seq=2))), output, lambda: bundle), 0)
        self.assertEqual([item["kind"] for item in frames(output.getvalue())], ["challenge"])

    def test_non_json_or_oversized_projection_is_not_a_success(self):
        for addition in ({"duration": float("nan")}, {"title": "x" * (1024 * 1024)}, {"entries": object()}):
            with self.subTest(fields=list(addition)):
                bundle = Bundle(); bundle.info.update(addition); output = io.BytesIO()
                self.assertNotEqual(resolver.run(VIDEO, io.BytesIO(), output, lambda: bundle), 0)
                self.assertTrue(all(item["kind"] != "result" for item in frames(output.getvalue())))


if __name__ == "__main__":
    unittest.main()
