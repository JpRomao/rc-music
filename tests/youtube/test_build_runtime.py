"""Local filesystem contracts for the candidate builder, without PyInstaller."""

import hashlib
import importlib.util
from pathlib import Path
import sys
import subprocess
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/youtube-resolver"))
import build_runtime as builder


def digest(data):
    return hashlib.sha256(data).hexdigest()


class RuntimeBuilderTests(unittest.TestCase):
    def test_build_environment_has_private_home_and_no_inherited_python_or_node_controls(self):
        environment = builder.build_environment(self.root / "work", self.root / "venv/python.exe", self.root / "Windows")
        self.assertEqual(environment["USERPROFILE"], str(self.root / "work/home"))
        self.assertEqual(environment["APPDATA"], str(self.root / "work/home/Roaming"))
        self.assertEqual(environment["LOCALAPPDATA"], str(self.root / "work/home/Local"))
        self.assertEqual(environment["TEMP"], str(self.root / "work/temp"))
        self.assertEqual(set(environment), {"SystemRoot", "PATH", "TEMP", "TMP", "USERPROFILE", "APPDATA",
                                            "LOCALAPPDATA", "PYINSTALLER_CONFIG_DIR"})

    def test_builder_cli_loads_its_fixed_planner_in_isolated_python(self):
        result = subprocess.run([sys.executable, "-B", "-I", str(ROOT / "tools/youtube-resolver/build_runtime.py"),
                                 "--help"], capture_output=True, text=True, check=False)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("--config", result.stdout)

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="rcmusic-builder-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)

    def source(self, name="node", data=b"source"):
        path = self.root / (name + ".input")
        path.write_bytes(data)
        return {"name": name, "version": "project", "path": str(path),
                "sha256": digest(data), "provenance": "unit fixture"}

    def test_verification_uses_bytes_and_never_modifies_sources(self):
        source = self.source()
        builder.verify_sources([source])
        self.assertEqual(Path(source["path"]).read_bytes(), b"source")

    def test_verification_rejects_changed_missing_directory_and_symlink(self):
        source = self.source()
        cases = [source | {"sha256": "0" * 64}, source | {"path": str(self.root / "missing")},
                 source | {"path": str(self.root)}]
        for candidate in cases:
            with self.subTest(candidate=candidate), self.assertRaises(ValueError):
                builder.verify_sources([candidate])

    def test_inventory_hashes_all_files_and_assigns_only_fixed_roles(self):
        tree = self.root / "tree"
        (tree / "extractor/_internal").mkdir(parents=True)
        (tree / "javascript").mkdir()
        (tree / "extractor/yt-dlp.exe").write_bytes(b"extractor")
        (tree / "extractor/_internal/library.zip").write_bytes(b"library")
        (tree / "javascript/node.exe").write_bytes(b"node")
        rows, excluded = builder.inventory_tree(tree)
        self.assertEqual(excluded, [])
        self.assertEqual({row["path"]: row["role"] for row in rows}, {
            "extractor/yt-dlp.exe": "Extractor", "extractor/_internal/library.zip": "Support",
            "javascript/node.exe": "Node"})
        self.assertEqual(next(row for row in rows if row["role"] == "Node")["sha256"], digest(b"node"))

    def test_empty_pip_requested_marker_is_the_only_omission_and_is_reported(self):
        tree = self.root / "tree"
        marker = tree / "extractor/_internal/yt_dlp_ejs-0.8.0.dist-info/REQUESTED"
        marker.parent.mkdir(parents=True)
        marker.touch()
        rows, excluded = builder.inventory_tree(tree)
        self.assertEqual(rows, [])
        self.assertFalse(marker.exists())
        self.assertEqual(excluded, [{"path": "extractor/_internal/yt_dlp_ejs-0.8.0.dist-info/REQUESTED",
                                    "bytes": 0, "sha256": digest(b"")}])

    def test_other_empty_or_unclassified_files_fail_and_remain(self):
        for relative, data in (("extractor/_internal/empty.dll", b""),
                               ("extractor/_internal/other.dist-info/REQUESTED", b""),
                               ("unexpected.exe", b"data")):
            tree = self.root / str(len(relative))
            path = tree / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
            with self.subTest(relative=relative), self.assertRaises(ValueError):
                builder.inventory_tree(tree)
            self.assertEqual(path.read_bytes(), data)

    def test_nonempty_requested_marker_is_kept_as_support(self):
        tree = self.root / "tree"
        marker = tree / "extractor/_internal/yt_dlp_ejs-0.8.0.dist-info/REQUESTED"
        marker.parent.mkdir(parents=True)
        marker.write_bytes(b"marker")
        rows, excluded = builder.inventory_tree(tree)
        self.assertEqual(excluded, [])
        self.assertEqual(rows[0]["role"], "Support")
        self.assertTrue(marker.exists())

    def test_failed_inventory_preserves_the_empty_marker_for_inspection(self):
        tree = self.root / "tree"
        marker = tree / "extractor/_internal/yt_dlp_ejs-0.8.0.dist-info/REQUESTED"
        marker.parent.mkdir(parents=True)
        marker.touch()
        (marker.parent / "invalid.dll").touch()
        walk = [(str(marker.parent), [], ["REQUESTED", "invalid.dll"])]
        with mock.patch.object(builder.os, "walk", return_value=walk), self.assertRaises(ValueError):
            builder.inventory_tree(tree)
        self.assertTrue(marker.exists(), "failed inventory must not silently remove its install marker")

    def test_staging_requires_five_licenses_and_only_creates_new_files(self):
        tree = self.root / "tree"
        tree.mkdir()
        sources = [self.source(name) for name in (
            "node", "python-license", "node-license", "yt-dlp-license", "ejs-license", "pyinstaller-license")]
        builder.stage_inputs(tree, sources)
        self.assertEqual((tree / "javascript/node.exe").read_bytes(), b"source")
        self.assertEqual(len(list((tree / "notices").iterdir())), 5)
        with self.assertRaises((ValueError, FileExistsError)):
            builder.stage_inputs(tree, sources)
        self.assertEqual((tree / "javascript/node.exe").read_bytes(), b"source")

    def test_staging_missing_license_fails_before_copy(self):
        tree = self.root / "tree"
        tree.mkdir()
        with self.assertRaises(ValueError):
            builder.stage_inputs(tree, [self.source()])
        self.assertEqual(list(tree.iterdir()), [])


if __name__ == "__main__":
    unittest.main()
