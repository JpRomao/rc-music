"""Local seam tests only: no real yt-dlp import, Node, EJS or network I/O.

The later frozen integration must use the default loaders and actual packages.
These tests reuse the existing provider's public-API doubles, while exercising
the unchanged production run/configure/provider/transport implementations.
"""

import contextlib
import importlib.util
import io
import json
from pathlib import Path
import sys
import tempfile
import types
import unittest
from unittest import mock


HERE = Path(__file__).resolve().parent


def load_source(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


entry = load_source("rcmusic_offline_entry_unit", HERE / "offline_entry.py")
provider_tests = load_source("rcmusic_offline_provider_doubles", HERE / "test_resolver_provider.py")
resolver = provider_tests.resolver
VIDEO = "testVideo01"
PLAYER_URL = "https://www.youtube.com/s/player/rcmusic-offline/base.js"
EXPECTED_N = {"abcdef": "fedcba", "曲A😀": "😀A曲"}
EXPECTED_SIG = {"abcd": "bcda", "a b%?": " b%?a", "曲A😀": "A😀曲"}
TITLE_PREFIX = "RC Music offline v1 "
EXPECTED_RECEIPT = {"yt_dlp": "2026.10.26", "ejs": "0.8.0", "n": 2, "sig": 3,
                    "http": 0, "socket": 0, "init": 0, "players": 1, "providers": 1}


class Harness:
    def __init__(self):
        self.trace = []
        self.instances = []
        self.ies = []
        self.requests = []
        self.players = []
        self.director_ies = []
        self.director_transform = lambda value: value
        self.on_director = None
        self.load_error = None
        self.dependency_error = None
        self.ejs_version = resolver.PINNED_EJS_VERSION
        self.registry = types.SimpleNamespace(
            _jsc_providers=provider_tests.Indirect({"foreign": object()}),
            _jsc_preferences=provider_tests.Indirect({object()}),
        )
        self.api = provider_tests.public_api(self.registry._jsc_providers)
        harness = self

        class YoutubeIE:
            @classmethod
            def ie_key(cls):
                return "Youtube"

            def _load_player(self, *args, **kwargs):
                raise AssertionError("original player loader must not run")

            def _real_initialize(self):
                raise AssertionError("original initialization must not run")

        class YoutubeDL:
            def __init__(self, options, auto_init=True):
                harness.instances.append(self)
                self.params = options
                self.auto_init = auto_init
                self._ies_instances = {}

            def __enter__(self):
                return self

            def __exit__(self, *_args):
                harness.trace.append("downloader-closed")

            def add_info_extractor(self, ie):
                harness.ies.append(ie)
                ie._downloader = self
                self._ies_instances[ie.ie_key()] = ie

            def extract_info(self, *args, **kwargs):
                raise AssertionError("original extraction must not run")

            def urlopen(self, *args, **kwargs):
                raise AssertionError("original HTTP must not run")

        class HTTPHeaderDict(dict):
            def items(self):
                raise AssertionError("production must normalize concrete dict storage")

        self.headers_class = HTTPHeaderDict
        self.runtime = types.SimpleNamespace(
            version=resolver.PINNED_YTDLP_VERSION,
            globals=types.SimpleNamespace(
                plugin_dirs=provider_tests.Indirect(["default"]),
                all_plugins_loaded=provider_tests.Indirect(False),
                plugin_ies=provider_tests.Indirect({"foreign": object()}),
                plugin_pps=provider_tests.Indirect({"foreign": object()}),
                plugin_ies_overrides=provider_tests.Indirect({"foreign": [object()]}),
            ),
            jsc=self.api, registry=self.registry, YoutubeDL=YoutubeDL, YoutubeIE=YoutubeIE,
        )
        self.originals = (YoutubeDL.extract_info, YoutubeDL.urlopen,
                          YoutubeIE._load_player, YoutubeIE._real_initialize)

    def production(self):
        self.trace.append("production")
        return resolver

    def runtime_loader(self):
        self.trace.append("runtime")
        if self.load_error:
            raise self.load_error
        return self.runtime

    def dependencies(self):
        self.trace.append("dependencies")
        if self.dependency_error:
            raise self.dependency_error
        return types.SimpleNamespace(initialize_jsc_director=self.initialize_director,
                                     HTTPHeaderDict=self.headers_class, ejs_version=self.ejs_version)

    def initialize_director(self, ie):
        self.director_ies.append(ie)
        harness = self

        class Director:
            def bulk_solve(self, requests):
                harness.requests.extend(requests)
                if harness.on_director:
                    harness.on_director(ie)
                cls, = harness.registry._jsc_providers.value.values()
                provider = cls(ie, None, {})
                # Read the player through the patched real-API seam, then use
                # the production provider and framed transport unchanged.
                original = ie._load_player

                def capture(*args, **kwargs):
                    result = original(*args, **kwargs)
                    harness.players.append((args, kwargs, result))
                    return result

                with mock.patch.object(ie, "_load_player", side_effect=capture):
                    solved = list(provider.bulk_solve(requests))
                pairs = [(item.request, item.response) for item in solved if item.response is not None]
                return harness.director_transform(pairs)

        return Director()

    def execute(self, *, responses=None, probe=None, argv=None):
        if responses is None:
            responses = [{"type": "result", "data": dict(EXPECTED_N)},
                         {"type": "result", "data": dict(EXPECTED_SIG)}]
        incoming = io.BytesIO(provider_tests.wire(provider_tests.response(responses=responses)))
        outgoing = io.BytesIO()
        stdout, stderr = io.StringIO(), io.StringIO()
        with mock.patch.object(resolver, "_load_runtime", side_effect=self.runtime_loader):
            with contextlib.redirect_stdout(stdout), contextlib.redirect_stderr(stderr):
                code = entry.main([VIDEO] if argv is None else argv, reader=incoming, writer=outgoing,
                                  production_loader=self.production, dependencies_loader=self.dependencies,
                                  guard_probe=probe)
        return code, provider_tests.frames(outgoing.getvalue()), stdout.getvalue(), stderr.getvalue()

    def assert_restored(self, testcase):
        testcase.assertEqual((self.runtime.YoutubeDL.extract_info, self.runtime.YoutubeDL.urlopen,
                              self.runtime.YoutubeIE._load_player, self.runtime.YoutubeIE._real_initialize),
                             self.originals)


class OfflineEntryTests(unittest.TestCase):
    def assert_failure(self, result, code="unavailable", *, challenge=False):
        status, packets, stdout, stderr = result
        self.assertNotEqual(status, 0)
        self.assertEqual(stdout, "")
        self.assertEqual(stderr, "")
        self.assertTrue(packets)
        self.assertTrue(all(item["kind"] != "result" for item in packets))
        self.assertEqual(packets[-1], {"v": 1, "kind": "failure", "seq": 2 if challenge else 1, "code": code})

    def test_import_is_lazy_and_does_not_load_real_dependencies(self):
        names = []
        original = __import__

        def observe(name, *args, **kwargs):
            names.append(name)
            if name.startswith(("yt_dlp", "yt_dlp_ejs")):
                raise AssertionError("real runtime imported during source import")
            return original(name, *args, **kwargs)

        with mock.patch("builtins.__import__", side_effect=observe):
            load_source("rcmusic_offline_entry_import_probe", HERE / "offline_entry.py")
        self.assertFalse(any(name.startswith(("yt_dlp", "yt_dlp_ejs")) for name in names))

    def test_real_production_flow_uses_both_public_request_types_and_exact_vectors(self):
        harness = Harness()
        code, packets, stdout, stderr = harness.execute()
        self.assertEqual(code, 0)
        self.assertEqual((stdout, stderr), ("", ""))
        self.assertEqual([(item["kind"], item["seq"]) for item in packets], [("challenge", 1), ("result", 2)])
        self.assertEqual(packets[0]["requests"], [{"type": "n", "challenges": list(EXPECTED_N)},
                                                   {"type": "sig", "challenges": list(EXPECTED_SIG)}])
        self.assertEqual(packets[0]["player"], (HERE / "fixtures/synthetic-player.js").read_text(encoding="utf-8"))
        self.assertEqual(len(harness.requests), 2)
        self.assertEqual([item.video_id for item in harness.requests], [VIDEO, VIDEO])
        self.assertEqual([item.input.player_url for item in harness.requests], [PLAYER_URL, PLAYER_URL])
        self.assertIs(harness.director_ies[0], harness.ies[0])
        self.assertIs(harness.ies[0]._jsc_director.__class__, harness.director_ies[0]._jsc_director.__class__)
        self.assertEqual(harness.trace[:3], ["production", "runtime", "dependencies"])
        self.assertEqual(harness.trace[-1], "downloader-closed")
        self.assertEqual(set(harness.registry._jsc_providers.value), {"Rcmusic"})
        self.assertFalse(harness.instances[0].auto_init)
        harness.assert_restored(self)

    def test_projection_is_complete_synthetic_audio_and_headers_use_concrete_dict(self):
        harness = Harness()
        code, packets, _, _ = harness.execute()
        self.assertEqual(code, 0)
        title = packets[-1]["data"]["title"]
        self.assertTrue(title.startswith(TITLE_PREFIX))
        self.assertEqual(json.loads(title[len(TITLE_PREFIX):]), EXPECTED_RECEIPT)
        self.assertEqual(packets[-1]["data"], {
            "_type": "video", "id": VIDEO, "extractor_key": "Youtube", "title": title,
            "artist": "RC Music synthetic fixture", "url": "https://r1.googlevideo.com/videoplayback?expire=2000000000&sig=RCMUSIC_OFFLINE",
            "duration": 2, "filesize": 100, "ext": "m4a", "acodec": "mp4a.40.2", "vcodec": "none",
            "protocol": "https", "availability": "public", "is_live": False, "was_live": False,
            "live_status": "not_live", "has_drm": False, "http_headers": {"Accept": "*/*"},
        })

    def test_cli_rejects_extra_flags_urls_and_non_ascii_ids_before_lazy_loading(self):
        for argv in ([], [VIDEO, "--offline"], ["https://youtu.be/" + VIDEO], ["短" * 11], [VIDEO + "1"], [None]):
            with self.subTest(argv=argv):
                harness = Harness()
                self.assert_failure(harness.execute(argv=argv), "unsupported")
                self.assertEqual(harness.trace, [])

    def test_default_source_loader_uses_production_module_from_the_fixed_path(self):
        harness = Harness()
        outgoing = io.BytesIO()
        loader = types.SimpleNamespace(exec_module=mock.Mock())
        spec = types.SimpleNamespace(loader=loader)
        with mock.patch.object(resolver, "_load_runtime", side_effect=harness.runtime_loader):
            with mock.patch("importlib.util.spec_from_file_location", return_value=spec) as make_spec:
                with mock.patch("importlib.util.module_from_spec", return_value=resolver):
                    result = entry.main([VIDEO], reader=io.BytesIO(provider_tests.wire(provider_tests.response(responses=[
                        {"type": "result", "data": EXPECTED_N}, {"type": "result", "data": EXPECTED_SIG}]))),
                                        writer=outgoing, dependencies_loader=harness.dependencies)
        self.assertEqual(result, 0)
        make_spec.assert_called_once()
        self.assertEqual(make_spec.call_args.args[0], "rcmusic_youtube")
        self.assertEqual(Path(make_spec.call_args.args[1]).resolve(), provider_tests.MODULE_PATH.resolve())
        loader.exec_module.assert_called_once_with(resolver)
        self.assertEqual(provider_tests.frames(outgoing.getvalue())[-1]["kind"], "result")

    def test_frozen_loader_uses_private_pyz_and_fixed_fixture_location(self):
        harness = Harness()
        with tempfile.TemporaryDirectory(prefix="rcmusic-offline-单元-") as temporary:
            internal = Path(temporary)
            fixture = internal / "rcmusic_fixture/synthetic-player.js"
            fixture.parent.mkdir()
            fixture.write_text("// unique frozen synthetic player\n", encoding="utf-8")
            with mock.patch.object(sys, "frozen", True, create=True), mock.patch.object(sys, "_MEIPASS", str(internal), create=True):
                with mock.patch.dict(sys.modules, {"rcmusic_youtube": resolver}):
                    with mock.patch.object(resolver, "_load_runtime", side_effect=harness.runtime_loader):
                        output = io.BytesIO()
                        code = entry.main([VIDEO], reader=io.BytesIO(provider_tests.wire(provider_tests.response(responses=[
                            {"type": "result", "data": EXPECTED_N}, {"type": "result", "data": EXPECTED_SIG}]))),
                                          writer=output, dependencies_loader=harness.dependencies)
            self.assertEqual(code, 0)
            self.assertEqual(provider_tests.frames(output.getvalue())[0]["player"], "// unique frozen synthetic player\n")

    def test_missing_frozen_fixture_fails_without_source_fallback_or_path_leak(self):
        harness = Harness()
        with tempfile.TemporaryDirectory(prefix="rcmusic-offline-secret-") as temporary:
            with mock.patch.object(sys, "frozen", True, create=True), mock.patch.object(sys, "_MEIPASS", temporary, create=True):
                self.assert_failure(harness.execute())
        harness.assert_restored(self)

    def test_runtime_and_ejs_versions_are_checked_before_exchange(self):
        for dependency, value in (("runtime", "2026.03.16"), ("ejs", "0.7.0"), ("ejs", None)):
            with self.subTest(dependency=dependency, value=value):
                harness = Harness()
                if dependency == "runtime":
                    harness.runtime.version = value
                else:
                    harness.ejs_version = value
                self.assert_failure(harness.execute(), "unsupported")
                self.assertEqual(harness.requests, [])
                harness.assert_restored(self)

    def test_dependency_and_runtime_errors_are_fixed_and_restore_methods(self):
        for field in ("load_error", "dependency_error"):
            with self.subTest(field=field):
                harness = Harness()
                setattr(harness, field, RuntimeError("C:/private/token-secret"))
                self.assert_failure(harness.execute())
                harness.assert_restored(self)

    def test_wrong_solved_value_cannot_produce_projection(self):
        for kind, mapping in (("n", EXPECTED_N), ("sig", EXPECTED_SIG)):
            with self.subTest(kind=kind):
                responses = [{"type": "result", "data": dict(EXPECTED_N)}, {"type": "result", "data": dict(EXPECTED_SIG)}]
                responses[0 if kind == "n" else 1]["data"][next(iter(mapping))] = "wrong-but-well-formed"
                harness = Harness()
                self.assert_failure(harness.execute(responses=responses), challenge=True)
                harness.assert_restored(self)

    def test_missing_duplicated_or_wrong_request_pair_cannot_pass_count_check(self):
        transforms = [lambda pairs: pairs[:1], lambda pairs: [pairs[0], pairs[0]],
                      lambda pairs: pairs + [pairs[0]], lambda pairs: [(object(), pairs[0][1]), pairs[1]]]
        for transform in transforms:
            with self.subTest(transform=transform):
                harness = Harness()
                harness.director_transform = transform
                self.assert_failure(harness.execute(), challenge=True)

    def test_real_director_pair_order_is_not_assumed(self):
        harness = Harness()
        harness.director_transform = lambda pairs: list(reversed(pairs))
        self.assertEqual(harness.execute()[0], 0)

    def test_failed_provider_does_not_fabricate_success(self):
        harness = Harness()
        self.assert_failure(harness.execute(responses=[{"type": "error", "code": "unavailable"},
                                                       {"type": "result", "data": dict(EXPECTED_SIG)}]), challenge=True)

    def test_guard_is_active_before_production_and_runtime_lazy_loaders(self):
        for target in ("production", "runtime_loader", "dependencies"):
            with self.subTest(target=target):
                harness = Harness()
                original = getattr(harness, target)

                def guarded_load(original=original):
                    try:
                        sys.audit("socket.connect", object(), ("127.0.0.1", 1))
                    except Exception:
                        pass
                    return original()

                setattr(harness, target, guarded_load)
                self.assert_failure(harness.execute(), "restricted")
                self.assertEqual(harness.requests, [])

    def test_guard_rejects_caught_socket_and_http_audit_attempts(self):
        events = ("socket.__new__", "socket.connect", "socket.getaddrinfo", "socket.sendto",
                  "http.client.connect", "http.client.send")
        for event in events:
            with self.subTest(event=event):
                harness = Harness()

                def caught_probe(guard):
                    try:
                        guard.audit(event, (object(),))
                    except Exception:
                        pass

                self.assert_failure(harness.execute(probe=caught_probe), "restricted")
                self.assertEqual(harness.requests, [])

    def test_guard_rejects_caught_patched_urlopen_and_real_initialize(self):
        for method in ("http", "initialize"):
            with self.subTest(method=method):
                harness = Harness()

                def caught_probe(_guard):
                    try:
                        if method == "http":
                            harness.runtime.YoutubeDL.urlopen(None, "https://example.invalid/private")
                        else:
                            harness.runtime.YoutubeIE()._real_initialize()
                    except Exception:
                        pass

                self.assert_failure(harness.execute(probe=caught_probe), "restricted")
                self.assertEqual(harness.requests, [])
                harness.assert_restored(self)

    def test_caught_network_attempt_during_director_still_prevents_final_projection(self):
        harness = Harness()

        def swallowed_attempt(ie):
            try:
                ie._downloader.urlopen("https://example.invalid/private")
            except Exception:
                pass

        harness.on_director = swallowed_attempt
        self.assert_failure(harness.execute(), "restricted", challenge=True)
        harness.assert_restored(self)

    def test_unrelated_audit_events_are_not_network_attempts(self):
        harness = Harness()
        self.assertEqual(harness.execute(probe=lambda guard: guard.audit("open", ("fixture", "r", 0)))[0], 0)

    def test_audit_guard_is_inactive_after_entry_returns(self):
        harness = Harness()
        self.assertEqual(harness.execute()[0], 0)
        # Audit-only event: never constructs a socket or attempts a connection.
        sys.audit("socket.connect", object(), ("127.0.0.1", 1))


if __name__ == "__main__":
    unittest.main()
