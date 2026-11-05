from __future__ import annotations

import copy
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
from types import SimpleNamespace
import unittest
from unittest import mock
import uuid


REPOSITORY = Path(__file__).resolve().parents[2]
SCRIPT = REPOSITORY / "tools/download-runtime/build_profile.py"
ONEDIR = REPOSITORY / "tools/youtube-resolver/onedir_plan.py"
NETWORK = (
    ("Bootstrap", "RCMusicDownloadWorker.exe"), ("Payload", "rcmusic-download-network.dll"),
    ("Core", "Qt6Core.dll"), ("Network", "Qt6Network.dll"), ("Msvcp140", "msvcp140.dll"),
    ("Msvcp140_1", "msvcp140_1.dll"), ("Vcruntime140", "vcruntime140.dll"),
    ("Vcruntime140_1", "vcruntime140_1.dll"), ("Plugin", "qrcmusicschanneloffline.dll"),
)
PINS = {
    "python": "3.13.1", "node": "24.20.0", "altgraph": "0.17.5", "packaging": "26.3",
    "pefile": "2024.8.26", "pyinstaller": "6.22.3", "pyinstaller-hooks-contrib": "2026.9",
    "pywin32-ctypes": "0.2.3", "setuptools": "84.2.1", "yt-dlp": "2026.3.17", "yt-dlp-ejs": "0.8.0",
}
NOTICES = ("python-LICENSE.txt", "node-LICENSE.txt", "yt-dlp-LICENSE.txt", "ejs-LICENSE.txt", "pyinstaller-COPYING.txt")
YT_ROLES = {
    "Extractor": "extractor/yt-dlp.exe", "Node": "javascript/node.exe",
    "EjsCore": "extractor/_internal/yt_dlp_ejs/yt/solver/core.min.js",
    "EjsLibrary": "extractor/_internal/yt_dlp_ejs/yt/solver/lib.min.js",
}


def load_module(path):
    spec = importlib.util.spec_from_file_location("rcmusic_profile_fixture_" + uuid.uuid4().hex, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def write(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)
    return path


class BuildProfileTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.run_id = uuid.uuid4().hex
        cls.run_root = REPOSITORY / "out/product/test-runs/download-profile" / cls.run_id
        cls.keep_failures = False
        cls.module = load_module(SCRIPT)
        if cls.run_root.exists():
            raise RuntimeError("owned fixture root must be fresh")
        for path in cls.run_root.parents:
            if path.exists() and (path.is_symlink() or getattr(path.lstat(), "st_file_attributes", 0) & 0x400):
                raise RuntimeError("unsafe fixture ancestor")
        cls.run_root.mkdir(parents=True)

    def setUp(self):
        self.root = self.run_root / (self._testMethodName + " espaço 音")
        self.root.mkdir()
        self.network = []
        for role, leaf in NETWORK:
            path = write(self.root / "sources/network" / leaf, ("NON-PE owned network fixture " + role).encode())
            self.network.append({"role": role, "path": str(path), "bytes": path.stat().st_size, "sha256": digest(path)})
        media = write(self.root / "sources/media/RCMusicMediaWorker.exe", b"NON-PE owned MF fixture")
        self.config = {"schemaVersion": 1, "output_directory": str(self.root / "candidate"), "network": self.network,
                       "media": {"path": str(media), "bytes": media.stat().st_size, "sha256": digest(media)}, "youtube": None}
        self.output = Path(self.config["output_directory"])

    def tearDown(self):
        result = self._outcome.result
        if result.failures or result.errors:
            type(self).keep_failures = True

    @classmethod
    def tearDownClass(cls):
        expected = REPOSITORY / "out/product/test-runs/download-profile" / cls.run_id
        if cls.run_root != expected or len(cls.run_id) != 32 or cls.run_root.resolve() != expected.absolute():
            raise RuntimeError("unsafe fixture cleanup root")
        if cls.keep_failures:
            print("Preserved owned failing fixtures:", cls.run_root)
            return
        for directory, directories, files in os.walk(cls.run_root, followlinks=False):
            for name in [*directories, *files]:
                path = Path(directory) / name
                if path.is_symlink() or getattr(path.lstat(), "st_file_attributes", 0) & 0x400:
                    print("Preserved fixture containing a reparse point:", cls.run_root)
                    return
        shutil.rmtree(cls.run_root)

    def add_youtube(self):
        tree = self.root / "sources/youtube"
        artifacts = []
        paths = {**YT_ROLES, **{"notice-" + name: "notices/" + name for name in NOTICES},
                 "support": "extractor/_internal/python313.dll"}
        for role, relative in paths.items():
            path = write(tree / relative, ("NON-PE synthetic artifact " + relative).encode())
            artifacts.append({"role": role if role in YT_ROLES else "Support", "path": relative,
                              "bytes": path.stat().st_size, "sha256": digest(path)})
        names = {**PINS, **{name: "project" for name in ("entrypoint", "spec", "hook", "build-lock",
                                                       "python-license", "node-license", "yt-dlp-license", "ejs-license", "pyinstaller-license")}}
        sources = [{"name": name, "version": version,
                    "path": str(self.root / "historical-inputs" / (name + (".exe" if name in ("python", "node") else ".whl"))),
                    "sha256": "0" * 64, "provenance": "Owned historical observation; original intentionally absent"}
                   for name, version in names.items()]
        receipt = {"schemaVersion": 1, "purpose": "production-candidate-v1", "artifact_root": str(tree),
                   "sources": sources, "artifacts": artifacts}
        path = self.root / "sources/youtube-receipt.json"
        self.yt = SimpleNamespace(tree=tree, receipt=receipt, path=path)
        self.save_youtube()
        return self.yt

    def save_youtube(self, raw=None):
        data = json.dumps(self.yt.receipt, ensure_ascii=False, allow_nan=False).encode() if raw is None else raw
        write(self.yt.path, data)
        self.config["youtube"] = {"receipt_path": str(self.yt.path), "receipt_sha256": digest(self.yt.path)}

    def rejected(self, config=None):
        with self.assertRaises((ValueError, FileNotFoundError, FileExistsError)):
            self.module.plan(self.config if config is None else config)
        self.assertFalse(self.output.exists())

    def test_direct_plan_is_read_only_and_emits_one_relative_inventory_and_header(self):
        candidate = self.module.plan(self.config)
        self.assertIsInstance(candidate, dict)
        self.assertEqual(len(candidate["files"]), 10)
        self.assertFalse(self.output.exists())
        expected = {"payload/download-tools/network/" + leaf for _, leaf in NETWORK} | {"payload/download-tools/media/RCMusicMediaWorker.exe"}
        self.assertEqual({row["relative_path"] for row in candidate["files"]}, expected)
        header = candidate["header_text"]
        for _, leaf in NETWORK:
            self.assertIn("download-tools/network/" + leaf, header)
        self.assertIn("download-tools/media/RCMusicMediaWorker.exe", header)
        self.assertIn("prepare_storage = true", header)
        self.assertIn("WindowsLocalMediaBackend::MediaFoundation", header)
        self.assertIn("compiledPlayerNativeIngressProfiles()", header)
        self.assertIn("makePackagedPlayerDownloadProfile(std::move(profile), config_root)", header)
        self.assertNotIn(str(REPOSITORY), header)
        self.assertNotIn(str(REPOSITORY).replace("\\", "/"), header)
        self.assertNotIn("payload/", header)
        self.assertNotIn("C:/Windows", header)

    def test_direct_materialization_copies_exact_bytes_and_keeps_sources(self):
        before = {row["path"]: digest(Path(row["path"])) for row in [*self.network, self.config["media"]]}
        candidate = self.module.plan(self.config)
        result = self.module.materialize(candidate)
        self.assertIsInstance(result, dict)
        expected = {row["relative_path"] for row in candidate["files"]} | {"PlayerDownloadPackageProfile.hpp", "download-profile-receipt.json"}
        self.assertEqual({p.relative_to(self.output).as_posix() for p in self.output.rglob("*") if p.is_file()}, expected)
        for row in candidate["files"]:
            self.assertEqual(digest(self.output / row["relative_path"]), row["sha256"])
            self.assertEqual((self.output / row["relative_path"]).stat().st_size, row["bytes"])
        self.assertEqual((self.output / "PlayerDownloadPackageProfile.hpp").read_text(encoding="utf-8"), candidate["header_text"])
        receipt = json.loads((self.output / "download-profile-receipt.json").read_text(encoding="utf-8"))
        self.assertEqual(receipt["state"], "candidate")
        self.assertEqual(len(receipt["files"]), 10)
        self.assertGreaterEqual(len(receipt["recipe_files"]), 2)
        self.assertEqual({path: digest(Path(path)) for path in before}, before)

    def test_youtube_uses_the_complete_pinned_production_tree_without_reopening_historical_sources(self):
        yt = self.add_youtube()
        candidate = self.module.plan(self.config)
        self.assertIsInstance(candidate, dict)
        self.assertEqual(len(candidate["files"]), 10 + len(yt.receipt["artifacts"]))
        self.assertIn("WindowsYoutubeRuntimeRole::Extractor", candidate["header_text"])
        for row in yt.receipt["artifacts"]:
            self.assertIn("download-tools/youtube/" + row["path"], candidate["header_text"])
        self.module.materialize(candidate)
        for row in yt.receipt["artifacts"]:
            self.assertEqual(digest(self.output / "payload/download-tools/youtube" / row["path"]), row["sha256"])
        self.assertFalse((self.root / "historical-inputs").exists())

    def test_network_roles_are_exact_unique_and_order_is_canonical(self):
        reversed_config = copy.deepcopy(self.config)
        reversed_config["network"].reverse()
        candidate = self.module.plan(reversed_config)
        self.assertIsInstance(candidate, dict)
        canonical = self.module.plan(self.config)
        self.assertEqual(candidate["header_text"], canonical["header_text"])
        for mode in ("missing", "duplicate", "unknown"):
            with self.subTest(mode=mode):
                config = copy.deepcopy(self.config)
                if mode == "missing": config["network"].pop()
                elif mode == "duplicate": config["network"][-1]["role"] = "Bootstrap"
                else: config["network"][-1]["role"] = "Other"
                self.rejected(config)

    def test_config_and_source_fields_types_pins_and_byte_caps_are_closed(self):
        mutations = (
            lambda c: c.update(extra=True), lambda c: c.update(schemaVersion=True), lambda c: c.update(schemaVersion=2),
            lambda c: c["network"][0].update(extra=True), lambda c: c["media"].update(extra=True),
            lambda c: c["network"][0].update(bytes=True), lambda c: c["media"].update(bytes=0),
            lambda c: c["media"].update(bytes=128 * 1024 * 1024 + 1),
            lambda c: c["network"][0].update(sha256="A" * 64), lambda c: c["media"].update(sha256="0" * 64),
            lambda c: c["media"].update(bytes=c["media"]["bytes"] + 1),
        )
        for index, mutate in enumerate(mutations):
            with self.subTest(index=index):
                config = copy.deepcopy(self.config)
                mutate(config)
                self.rejected(config)

    def test_local_path_grammar_rejects_devices_unc_ads_reserved_and_relative_before_open(self):
        for path in ("relative.exe", "//server.invalid/share/input.exe", "//./NUL", "C:/NUL.exe",
                     "C:/fixture/COM¹.exe", "C:/fixture/CONIN$/input.exe", "C:/fixture/input.exe:stream"):
            with self.subTest(path=path):
                config = copy.deepcopy(self.config)
                config["media"]["path"] = path
                self.rejected(config)
        for path in ("relative-output", str(REPOSITORY / "out" / ("forbidden-" + self.run_id)), str(REPOSITORY)):
            with self.subTest(output=path):
                config = copy.deepcopy(self.config)
                config["output_directory"] = path
                with self.assertRaises((ValueError, FileExistsError)):
                    self.module.plan(config)

    def test_youtube_receipt_pin_closed_schema_versions_and_source_names_are_enforced(self):
        self.add_youtube()
        original = copy.deepcopy(self.yt.receipt)
        for mode in ("pin", "purpose", "extra", "sources-missing", "sources-duplicate", "source-extra", "version", "offline-source", "inventory-role"):
            with self.subTest(mode=mode):
                self.yt.receipt = copy.deepcopy(original)
                if mode == "purpose": self.yt.receipt["purpose"] = "offline-synthetic-v1"
                elif mode == "extra": self.yt.receipt["extra"] = True
                elif mode == "sources-missing": self.yt.receipt["sources"].pop()
                elif mode == "sources-duplicate": self.yt.receipt["sources"].append(copy.deepcopy(self.yt.receipt["sources"][0]))
                elif mode == "source-extra": self.yt.receipt["sources"][0]["extra"] = True
                elif mode == "version": self.yt.receipt["sources"][0]["version"] = "unreviewed"
                elif mode == "offline-source": self.yt.receipt["sources"][0]["name"] = "offline-entry"
                elif mode == "inventory-role": self.yt.receipt["artifacts"][0]["role"] = "Other"
                self.save_youtube()
                if mode == "pin": self.config["youtube"]["receipt_sha256"] = "0" * 64
                self.rejected()

    def test_youtube_receipt_json_rejects_duplicate_keys_invalid_utf8_depth_and_size(self):
        self.add_youtube()
        valid = json.dumps(self.yt.receipt).encode()
        for raw in (valid.replace(b'"schemaVersion": 1', b'"schemaVersion": 1,"schemaVersion": 1'),
                    valid + b"{}", b'{"bad":"\xff"}', b"[" * 10 + b"0" + b"]" * 10, valid + b" " * (8 * 1024 * 1024)):
            with self.subTest(size=len(raw)):
                self.save_youtube(raw)
                self.rejected()

    def test_youtube_tree_is_closed_and_never_removes_even_the_empty_build_marker(self):
        yt = self.add_youtube()
        extra = write(yt.tree / "extractor/_internal/yt_dlp_ejs-0.8.0.dist-info/REQUESTED", b"")
        self.rejected()
        self.assertTrue(extra.is_file())
        extra.unlink()
        unknown_dir = yt.tree / "unknown-empty-directory"
        unknown_dir.mkdir()
        self.rejected()
        self.assertTrue(unknown_dir.is_dir())

    def test_reparse_metadata_is_rejected_without_reading_the_source(self):
        source = Path(self.config["media"]["path"])
        original = Path.lstat
        def observed(path, *args, **kwargs):
            value = original(path, *args, **kwargs)
            if path == source:
                return SimpleNamespace(st_mode=value.st_mode, st_file_attributes=0x400)
            return value
        with mock.patch.object(Path, "lstat", observed):
            self.rejected()
        self.assertTrue(source.is_file())

    def test_drift_of_sources_receipt_or_complete_youtube_tree_fails_before_output(self):
        yt = self.add_youtube()
        for mode in ("network", "media", "receipt", "youtube-extra"):
            with self.subTest(mode=mode):
                candidate = self.module.plan(self.config)
                self.assertIsInstance(candidate, dict)
                path = Path(self.network[0]["path"]) if mode == "network" else Path(self.config["media"]["path"]) if mode == "media" else yt.path
                previous = path.read_bytes()
                extra = yt.tree / "extra.bin"
                if mode == "youtube-extra": write(extra, b"must survive")
                else: path.write_bytes(previous + b"changed")
                with self.assertRaises((ValueError, FileExistsError)):
                    self.module.materialize(candidate)
                self.assertFalse(self.output.exists())
                if mode == "youtube-extra":
                    self.assertEqual(extra.read_bytes(), b"must survive")
                    extra.unlink()
                else:
                    self.assertEqual(path.read_bytes(), previous + b"changed")
                    path.write_bytes(previous)

    def test_existing_output_and_tampered_plan_are_preserved(self):
        candidate = self.module.plan(self.config)
        self.assertIsInstance(candidate, dict)
        sentinel = write(self.output / "preserve.keep", b"existing output")
        with self.assertRaises(FileExistsError): self.module.plan(self.config)
        with self.assertRaises((ValueError, FileExistsError)): self.module.materialize(candidate)
        self.assertEqual(sentinel.read_bytes(), b"existing output")
        sentinel.unlink()
        self.output.rmdir()
        candidate = self.module.plan(self.config)
        candidate["header_text"] += "\n// forged header\n"
        with self.assertRaises(ValueError): self.module.materialize(candidate)
        self.assertFalse(self.output.exists())

    def test_output_cannot_be_nested_in_the_closed_youtube_source_tree(self):
        yt = self.add_youtube()
        self.config["output_directory"] = str(yt.tree / "candidate")
        with self.assertRaises(ValueError): self.module.plan(self.config)
        self.assertFalse((yt.tree / "candidate").exists())

    def test_recipe_drift_in_an_owned_clone_is_rejected(self):
        clone = self.root / "owned-recipe-repository"
        script = write(clone / "tools/download-runtime/build_profile.py", SCRIPT.read_bytes())
        write(clone / "tools/youtube-resolver/onedir_plan.py", ONEDIR.read_bytes())
        module = load_module(script)
        config = copy.deepcopy(self.config)
        config["output_directory"] = str(clone / "out/product/candidate")
        candidate = module.plan(config)
        self.assertIsInstance(candidate, dict)
        script.write_bytes(script.read_bytes() + b"\n# own changed recipe\n")
        with self.assertRaises(ValueError): module.materialize(candidate)
        self.assertFalse(Path(config["output_directory"]).exists())

    def test_each_plan_uses_the_same_current_planner_bytes_recorded_in_its_recipe_snapshot(self):
        self.add_youtube()
        clone = self.root / "owned-planner-repository"
        script = write(clone / "tools/download-runtime/build_profile.py", SCRIPT.read_bytes())
        planner = write(clone / "tools/youtube-resolver/onedir_plan.py", ONEDIR.read_bytes())
        module = load_module(script)
        config = copy.deepcopy(self.config)
        config["output_directory"] = str(clone / "out/product/candidate")
        first = module.plan(config)
        self.assertIsInstance(first, dict)
        planner.write_bytes(planner.read_bytes() + b'\n\ndef validate_inventory(entries):\n    raise ValueError("owned planner revision Y")\n')
        with self.assertRaisesRegex(ValueError, "owned planner revision Y"):
            module.plan(config)
        self.assertFalse(Path(config["output_directory"]).exists())

    def test_cli_works_from_foreign_cwd_and_plan_only_never_materializes(self):
        config_path = write(self.root / "configuration.json", json.dumps(self.config, ensure_ascii=False).encode())
        command = [sys.executable, "-B", "-I", str(SCRIPT), "--config", str(config_path)]
        result = subprocess.run([*command, "--plan-only"], cwd=self.run_root, capture_output=True, text=True, timeout=20)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIsInstance(json.loads(result.stdout), dict)
        self.assertFalse(self.output.exists())
        result = subprocess.run(command, cwd=self.run_root, capture_output=True, text=True, timeout=20)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue((self.output / "PlayerDownloadPackageProfile.hpp").is_file())
        self.assertTrue((self.output / "download-profile-receipt.json").is_file())

    def test_cli_rejects_duplicate_configuration_keys_before_any_output(self):
        raw = json.dumps(self.config).replace('"schemaVersion": 1', '"schemaVersion": 1,"schemaVersion": 1')
        config_path = write(self.root / "duplicate-configuration.json", raw.encode())
        result = subprocess.run([sys.executable, "-B", "-I", str(SCRIPT), "--config", str(config_path), "--plan-only"],
                                cwd=self.run_root, capture_output=True, text=True, timeout=20)
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(self.output.exists())

    def test_cli_ascii_stdout_emits_lossless_json_for_unicode_configuration_paths(self):
        config_path = write(self.root / "configuration-unicode.json", json.dumps(self.config, ensure_ascii=False).encode())
        arguments = [str(SCRIPT), "--config", str(config_path), "--plan-only"]
        bootstrap = ("import runpy, sys; sys.stdout.reconfigure(encoding='ascii'); sys.argv=" + repr(arguments) +
                     "; runpy.run_path(sys.argv[0], run_name='__main__')")
        result = subprocess.run([sys.executable, "-B", "-I", "-c", bootstrap], cwd=self.run_root,
                                capture_output=True, text=True, encoding="ascii", timeout=20)
        self.assertEqual(result.returncode, 0, result.stderr)
        candidate = json.loads(result.stdout)
        self.assertEqual(candidate["config"]["output_directory"], self.config["output_directory"])
        self.assertEqual(candidate["config"]["media"]["path"], self.config["media"]["path"])
        self.assertFalse(self.output.exists())


if __name__ == "__main__":
    unittest.main()
