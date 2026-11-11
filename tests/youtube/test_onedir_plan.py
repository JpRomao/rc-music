"""Offline data contracts; no package install, build, runtime or filesystem scan."""

import builtins
import contextlib
import copy
import importlib.metadata
import importlib.util
import io
import json
from pathlib import Path
import pyexpat
import sys
import types
import unittest
from unittest import mock


ROOT = Path(__file__).resolve().parents[2]
MODULE_PATH = ROOT / "tools/youtube-resolver/onedir_plan.py"
SPEC = importlib.util.spec_from_file_location("rcmusic_onedir_plan", MODULE_PATH)
planner = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(planner)
RECIPE = ROOT / "tools/youtube-resolver/rcmusic_youtube.spec"
HOOK = ROOT / "tools/youtube-resolver/hooks/hook-yt_dlp.py"
SHA = "0123456789abcdef" * 4
REPOSITORY = "C:/rcmusic project"
PROJECT_PATHS = {
    "entrypoint": "tools/youtube-resolver/rcmusic_youtube.py",
    "spec": "tools/youtube-resolver/rcmusic_youtube.spec",
    "hook": "tools/youtube-resolver/hooks/hook-yt_dlp.py",
    "build-lock": "tools/youtube-resolver/requirements-build.lock",
    "fixture": "tests/youtube/fixtures/synthetic-player.js",
    "offline-entry": "tests/youtube/offline_entry.py",
}


def inputs(mode="prod"):
    sources = [
        {"name": name, "version": version,
         "path": ("C:/Python314/python.exe" if name == "python" else
                  "C:/Program Files/nodejs/node.exe" if name == "node" else
                  f"{REPOSITORY}/out/product/wheels/{name}-{version}.whl"),
         "sha256": SHA, "provenance": f"reviewed official source: {name}"}
        for name, version in planner.SOURCE_PINS.items()
    ]
    names = ("entrypoint", "spec", "hook", "build-lock")
    if mode == "offline":
        names += ("fixture", "offline-entry")
    sources.extend({"name": name, "version": "project", "path": f"{REPOSITORY}/{PROJECT_PATHS[name]}",
                    "sha256": SHA, "provenance": "reviewed project input"} for name in names)
    return {"repository_root": REPOSITORY,
            "venv_python": f"{REPOSITORY}/out/product/onedir/venv/Scripts/python.exe",
            "artifact_root": f"{REPOSITORY}/out/product/onedir/artifacts",
            "work_root": f"{REPOSITORY}/out/product/onedir/work",
            "receipt_path": f"{REPOSITORY}/out/product/onedir/runtime-source-auth.json",
            "mode": mode, "sources": sources}


def entry(role, path, size=1):
    return {"role": role, "path": path, "bytes": size, "sha256": SHA}


def inventory():
    return ([entry(role, path) for role, path in planner.ROLE_PATHS.items()] +
            [entry("Support", path) for path in planner.REQUIRED_NOTICE_PATHS])


class OnedirPlanTests(unittest.TestCase):
    def test_production_build_has_only_fixed_argv_and_production_entrypoint(self):
        values = inputs()
        plan = planner.make_build_plan(**values)
        self.assertEqual(plan.get("schemaVersion"), 1)
        self.assertEqual(plan.get("purpose"), "production-candidate-v1")
        self.assertEqual(plan.get("entrypoint"), REPOSITORY + "/" + PROJECT_PATHS["entrypoint"])
        self.assertEqual(plan.get("argv"), [values["venv_python"], "-I", "-m", "PyInstaller", "--distpath",
                                            values["artifact_root"], "--workpath", values["work_root"],
                                            REPOSITORY + "/" + PROJECT_PATHS["spec"], "--", "--mode", "prod"])
        self.assertEqual(plan.get("notice_paths"), list(planner.REQUIRED_NOTICE_PATHS))
        self.assertNotIn("--noconfirm", plan.get("argv", []))
        self.assertNotIn("environment", plan)

    def test_offline_build_has_separate_entrypoint_and_explicit_purpose(self):
        plan = planner.make_build_plan(**inputs("offline"))
        self.assertEqual(plan.get("purpose"), "offline-synthetic-v1")
        self.assertEqual(plan.get("entrypoint"), REPOSITORY + "/" + PROJECT_PATHS["offline-entry"])
        self.assertEqual(plan.get("argv", [])[-3:], ["--", "--mode", "offline"])

    def test_planner_returns_owned_canonical_data(self):
        values = inputs()
        before = copy.deepcopy(values)
        plan = planner.make_build_plan(**values)
        self.assertEqual(values, before)
        self.assertEqual([source["name"] for source in plan.get("sources", [])],
                         sorted(source["name"] for source in values["sources"]))
        values["sources"][0]["sha256"] = "f" * 64
        self.assertEqual(plan.get("sources", [{}])[0].get("sha256"), SHA)
        alternate = before | {"sources": list(reversed(before["sources"]))}
        self.assertEqual(planner.make_build_plan(**alternate), plan)
        json.dumps(plan, allow_nan=False)

    def test_only_two_build_modes_are_admitted(self):
        for mode in ("", "test", "PROD", "prod --onefile", None, True):
            with self.subTest(mode=mode), self.assertRaises(ValueError):
                planner.make_build_plan(**(inputs() | {"mode": mode}))

    def test_source_versions_are_fixed_including_dist_and_runtime_version_distinction(self):
        for index in range(len(planner.SOURCE_PINS)):
            values = inputs()
            values["sources"][index]["version"] += ".unreviewed"
            with self.subTest(source=values["sources"][index]["name"]), self.assertRaises(ValueError):
                planner.make_build_plan(**values)

    def test_new_build_requires_the_current_python_pin(self):
        values = inputs()
        python = next(source for source in values["sources"] if source["name"] == "python")
        python["version"] = "3.14.10"
        planner.make_build_plan(**values)
        python["version"] = "3.13.1"
        with self.assertRaisesRegex(ValueError, "unreviewed source version"):
            planner.make_build_plan(**values)

    def test_every_required_source_is_present_once(self):
        for index in range(len(inputs()["sources"])):
            values = inputs()
            removed = values["sources"].pop(index)
            with self.subTest(missing=removed["name"]), self.assertRaises(ValueError):
                planner.make_build_plan(**values)
        values = inputs()
        values["sources"].append(copy.deepcopy(values["sources"][0]))
        with self.assertRaises(ValueError):
            planner.make_build_plan(**values)

    def test_offline_requires_fixture_and_offline_entry_hashes(self):
        for name in ("fixture", "offline-entry"):
            values = inputs("offline")
            values["sources"] = [source for source in values["sources"] if source["name"] != name]
            with self.subTest(name=name), self.assertRaises(ValueError):
                planner.make_build_plan(**values)

    def test_fixed_project_records_must_cover_the_actual_recipe_and_entrypoint(self):
        for name in PROJECT_PATHS:
            values = inputs("offline")
            next(source for source in values["sources"] if source["name"] == name)["path"] = "C:/other/entry.py"
            with self.subTest(name=name), self.assertRaises(ValueError):
                planner.make_build_plan(**values)

    def test_source_records_reject_malformed_or_unreviewed_data(self):
        changes = ({"sha256": SHA.upper()}, {"sha256": "0" * 63}, {"sha256": b"0" * 64},
                   {"path": "https://example.invalid/archive.whl"}, {"path": "../python.exe"},
                   {"provenance": ""}, {"provenance": "private\nnew field"}, {"provenance": "x" * 2049},
                   {"extra": "not approved"}, {"name": "Python"}, {"version": 3131})
        for changed in changes:
            values = inputs()
            values["sources"][0].update(changed)
            with self.subTest(changed=changed), self.assertRaises(ValueError):
                planner.make_build_plan(**values)
        for bad_sources in ({}, None, ["source"], [inputs()["sources"][0]] * 65):
            with self.subTest(bad_sources=type(bad_sources).__name__), self.assertRaises(ValueError):
                planner.make_build_plan(**(inputs() | {"sources": bad_sources}))
        values = inputs()
        values["sources"].append({"name": "unreviewed-tool", "version": "project", "path": "C:/other/tool.exe",
                                  "sha256": SHA, "provenance": "not in this recipe"})
        with self.assertRaises(ValueError):
            planner.make_build_plan(**values)

    def test_optional_notice_source_is_copied_without_runtime_discovery(self):
        values = inputs()
        record = {"name": "node-license", "version": "project", "path": "C:/Program Files/nodejs/LICENSE",
                  "sha256": SHA, "provenance": "reviewed same Node installation"}
        values["sources"].append(record)
        plan = planner.make_build_plan(**values)
        self.assertIn(record, plan.get("sources", []))
        record["provenance"] = "changed"
        self.assertNotIn(record, plan.get("sources", []))

    def test_paths_are_local_absolute_bounded_and_traversal_free(self):
        for name in ("repository_root", "venv_python", "artifact_root", "work_root", "receipt_path"):
            for path in ("relative/file", "//server/share/file", "\\\\?\\C:\\device", "C:relative",
                         "C:/repo/../escape", "C:/repo/./same", "C:/bad\x00path", "C:/bad\npath"):
                with self.subTest(name=name, path=path), self.assertRaises(ValueError):
                    planner.make_build_plan(**(inputs() | {name: path}))

    def test_output_paths_stay_below_out_and_do_not_overlap(self):
        changes = ({"artifact_root": REPOSITORY + "/outside"},
                   {"work_root": REPOSITORY + "/outside"},
                   {"receipt_path": REPOSITORY + "/outside.json"},
                   {"artifact_root": REPOSITORY + "/out"},
                   {"work_root": inputs()["artifact_root"]},
                   {"work_root": inputs()["artifact_root"] + "/work"},
                   {"artifact_root": inputs()["work_root"] + "/artifacts"},
                   {"receipt_path": inputs()["artifact_root"] + "/self.json"},
                   {"venv_python": inputs()["artifact_root"] + "/python.exe"})
        for changed in changes:
            with self.subTest(changed=changed), self.assertRaises(ValueError):
                planner.make_build_plan(**(inputs() | changed))

    def test_source_inputs_cannot_be_overwritten_by_the_build(self):
        for root in ("artifact_root", "work_root"):
            values = inputs()
            values["sources"][0]["path"] = values[root] + "/python.exe"
            with self.subTest(root=root), self.assertRaises(ValueError):
                planner.make_build_plan(**values)

    def test_receipt_cannot_collide_with_or_live_under_build_work(self):
        for relative in ("", "/build-journal.json", "/pyinstaller.log", "/nested/receipt.json"):
            with self.subTest(relative=relative), self.assertRaises(ValueError):
                planner.make_build_plan(**(inputs() | {"receipt_path": inputs()["work_root"] + relative}))

    def test_inventory_preserves_every_file_and_returns_sorted_owned_records(self):
        values = inventory() + [entry("Support", "extractor/_internal/python313.dll", 123)]
        before = copy.deepcopy(values)
        actual = planner.validate_inventory(list(reversed(values)))
        self.assertEqual(actual, sorted(values, key=lambda value: value["path"]))
        self.assertEqual(values, before)
        values[0]["sha256"] = "a" * 64
        self.assertEqual(next(row for row in actual if row["role"] == "Extractor")["sha256"], SHA)

    def test_all_unique_roles_and_notices_are_required(self):
        for index in range(len(inventory())):
            values = inventory()
            removed = values.pop(index)
            with self.subTest(missing=removed["path"]), self.assertRaises(ValueError):
                planner.validate_inventory(values)

    def test_roles_have_fixed_case_sensitive_paths(self):
        for role in planner.ROLE_PATHS:
            values = inventory()
            next(row for row in values if row["role"] == role)["path"] = "extractor/_internal/wrong.bin"
            with self.subTest(role=role), self.assertRaises(ValueError):
                planner.validate_inventory(values)
        for role in ("Unknown", "extractor", "Support ", None):
            with self.subTest(role=role), self.assertRaises(ValueError):
                planner.validate_inventory(inventory() + [entry(role, "notices/unknown.txt")])

    def test_duplicate_role_path_and_file_directory_collisions_are_refused(self):
        extras = [entry("Extractor", "extractor/yt-dlp.exe"),
                  entry("Support", "notices/PYTHON-license.TXT"),
                  entry("Support", "extractor/_internal/yt_dlp_ejs"),
                  entry("Support", "notices/python-LICENSE.txt/child")]
        for extra in extras:
            with self.subTest(extra=extra), self.assertRaises(ValueError):
                planner.validate_inventory(inventory() + [extra])
        with self.assertRaises(ValueError):
            planner.validate_inventory(inventory() + [entry("Support", "notices/Case/a.txt"),
                                                       entry("Support", "notices/case/b.txt")])

    def test_relative_paths_use_the_existing_native_grammar(self):
        bad = ("javascript/helper.dll", "extractor/other.exe", "unknown/file", "/notices/a", "C:/notices/a",
               "notices/../escape", "notices/./same", "notices//empty", "notices\\other.txt",
               "notices/ads:stream", "notices/CON", "notices/com1.txt", "notices/LPT9.cfg", "notices/NUL.x",
               "notices/a.", "notices/a ", "notices/café.txt", "notices/a\x00b", "notices/a\nb")
        for path in bad:
            with self.subTest(path=path), self.assertRaises(ValueError):
                planner.validate_inventory(inventory() + [entry("Support", path)])

    def test_inventory_schema_hash_and_byte_types_are_strict(self):
        changes = ({"extra": 0}, {"sha256": SHA.upper()}, {"sha256": "a" * 65},
                   {"sha256": b"a" * 64}, {"bytes": True}, {"bytes": 1.5}, {"bytes": -1}, {"bytes": "1"})
        for changed in changes:
            values = inventory()
            values[0].update(changed)
            with self.subTest(changed=changed), self.assertRaises(ValueError):
                planner.validate_inventory(values)
        for bad in (None, {}, [None], (), "artifacts"):
            with self.subTest(bad=bad), self.assertRaises(ValueError):
                planner.validate_inventory(bad)

    def test_inventory_file_count_boundary_includes_notices(self):
        values = inventory()
        values.extend(entry("Support", f"notices/additional-{index}.txt")
                      for index in range(planner.MAX_FILES - len(values)))
        self.assertEqual(len(planner.validate_inventory(values)), 256)
        with self.assertRaises(ValueError):
            planner.validate_inventory(values + [entry("Support", "notices/one-too-many.txt")])

    def test_inventory_individual_and_aggregate_byte_boundaries(self):
        values = inventory() + [entry("Support", "extractor/_internal/at-limit.dll", planner.MAX_FILE_BYTES)]
        self.assertEqual(len(planner.validate_inventory(values)), len(values))
        values[-1]["bytes"] += 1
        with self.assertRaises(ValueError):
            planner.validate_inventory(values)
        values = inventory()
        values.extend(entry("Support", f"notices/part-{index}.txt", planner.MAX_FILE_BYTES)
                      for index in range(3))
        remaining = planner.MAX_TOTAL_BYTES - sum(row["bytes"] for row in values)
        values.append(entry("Support", "notices/final-part.txt", remaining))
        self.assertEqual(sum(row["bytes"] for row in planner.validate_inventory(values)), planner.MAX_TOTAL_BYTES)
        values[0]["bytes"] += 1
        with self.assertRaises(ValueError):
            planner.validate_inventory(values)

    def test_inventory_segment_length_path_length_and_depth_boundaries(self):
        paths = ("notices/" + "a" * 255,
                 "extractor/_internal/" + "/".join(["a"] * 14),
                 "extractor/_internal/" + "/".join(["a" * 255, "b" * 255, "c" * 255, "d" * 236]))
        for path in paths:
            with self.subTest(valid=len(path)):
                self.assertEqual(len(planner.validate_inventory(inventory() + [entry("Support", path)])), 10)
        for path in (paths[0] + "a", paths[1] + "/a", paths[2] + "d"):
            with self.subTest(invalid=len(path)), self.assertRaises(ValueError):
                planner.validate_inventory(inventory() + [entry("Support", path)])

    def test_every_inventory_artifact_including_support_must_be_nonempty(self):
        with self.assertRaises(ValueError):
            planner.validate_inventory(inventory() + [entry("Support", "notices/empty.txt", 0)])
        for index in range(len(inventory())):
            values = inventory()
            values[index]["bytes"] = 0
            with self.subTest(path=values[index]["path"]), self.assertRaises(ValueError):
                planner.validate_inventory(values)

    def test_receipt_is_complete_data_outside_the_artifact_tree(self):
        for mode, purpose in (("prod", "production-candidate-v1"), ("offline", "offline-synthetic-v1")):
            with self.subTest(mode=mode):
                plan = planner.make_build_plan(**inputs(mode))
                artifacts = inventory()
                receipt = planner.source_auth_receipt(plan, artifacts)
                self.assertEqual(set(receipt), {"schemaVersion", "purpose", "artifact_root", "sources", "artifacts"})
                self.assertEqual(receipt.get("schemaVersion"), 1)
                self.assertEqual(receipt.get("purpose"), purpose)
                self.assertEqual(receipt.get("artifact_root"), inputs()["artifact_root"])
                self.assertEqual(receipt.get("sources"), plan.get("sources"))
                self.assertEqual(receipt.get("artifacts"), planner.validate_inventory(artifacts))
                json.dumps(receipt, allow_nan=False)
                plan.get("sources", [{}])[0]["sha256"] = "f" * 64
                artifacts[0]["bytes"] = 99
                self.assertEqual(receipt.get("sources", [{}])[0].get("sha256"), SHA)
                self.assertEqual(next(row for row in receipt.get("artifacts", []) if row["role"] == "Extractor")["bytes"], 1)

    def test_receipt_revalidates_plan_instead_of_blessing_mutated_argv_or_purpose(self):
        plan = planner.make_build_plan(**inputs())
        changes = ({"purpose": "offline-synthetic-v1"}, {"entrypoint": "C:/arbitrary.py"},
                   {"argv": ["cmd.exe", "/c", "arbitrary"]}, {"canLaunch": True},
                   {"receipt_path": inputs()["artifact_root"] + "/self.json"})
        for changed in changes:
            with self.subTest(changed=changed), self.assertRaises(ValueError):
                planner.source_auth_receipt(plan | changed, inventory())
        with self.assertRaises(ValueError):
            planner.source_auth_receipt(plan, inventory()[:-1])
        for invalid in (None, [], "plan", {}):
            with self.subTest(invalid=invalid), self.assertRaises(ValueError):
                planner.source_auth_receipt(invalid, inventory())

    def test_pure_functions_do_not_open_scan_hash_download_or_spawn(self):
        values, artifacts = inputs(), inventory()
        deny = mock.Mock(side_effect=AssertionError("pure planner attempted I/O"))
        with mock.patch.object(builtins, "open", deny), mock.patch.object(Path, "open", deny), \
                mock.patch.object(Path, "stat", deny), mock.patch.object(Path, "iterdir", deny):
            plan = planner.make_build_plan(**values)
            planner.validate_inventory(artifacts)
            planner.source_auth_receipt(plan, artifacts)
        deny.assert_not_called()


class RecipeContracts(unittest.TestCase):
    def execute_recipe(self, mode, *, python_version=(3, 14, 10), expat_version=(2, 9, 0)):
        captured = {}

        def analysis(scripts, **kwargs):
            captured["analysis"] = {"scripts": scripts, **kwargs}
            return types.SimpleNamespace(pure=[], scripts=scripts, binaries=[], datas=kwargs.get("datas", []))

        def exe(*args, **kwargs):
            captured["exe"] = kwargs
            return object()

        def collect(*args, **kwargs):
            captured["collect"] = kwargs

        class Distribution:
            version = "0.8.0"

            @staticmethod
            def locate_file(path):
                return ROOT / "out/product/fake-venv/Lib/site-packages" / path

        namespace = {"SPEC": str(RECIPE), "Analysis": analysis, "PYZ": lambda *args: object(),
                     "EXE": exe, "COLLECT": collect}
        hooks = types.ModuleType("PyInstaller.utils.hooks")

        def copy_metadata(name):
            captured.setdefault("metadata", []).append(name)
            return [(str(ROOT / "out/product/fake-venv/Lib/site-packages/yt_dlp_ejs-0.8.0.dist-info"),
                     "yt_dlp_ejs-0.8.0.dist-info")]

        hooks.copy_metadata = copy_metadata
        source = RECIPE.read_text(encoding="utf-8")
        with contextlib.redirect_stderr(io.StringIO()), \
                mock.patch.dict(sys.modules, {"PyInstaller": types.ModuleType("PyInstaller"),
                                               "PyInstaller.utils": types.ModuleType("PyInstaller.utils"),
                                               "PyInstaller.utils.hooks": hooks}), \
                mock.patch.object(sys, "argv", [str(RECIPE), "--mode", mode]), \
                mock.patch.object(sys, "version_info", python_version), \
                mock.patch.object(pyexpat, "version_info", expat_version), \
                mock.patch.object(importlib.metadata, "version", side_effect=lambda name: planner.SOURCE_PINS[name]), \
                mock.patch.object(importlib.metadata, "distribution", return_value=Distribution()):
            try:
                exec(compile(source, str(RECIPE), "exec"), namespace)
            except SystemExit as error:
                captured["exit"] = error.code
        return captured

    def test_recipe_rejects_legacy_python_and_outdated_expat(self):
        old_python = self.execute_recipe("prod", python_version=(3, 13, 1))
        self.assertEqual(old_python.get("exit"), "expected reviewed CPython 3.14.10 Windows x64 build interpreter")
        old_expat = self.execute_recipe("prod", expat_version=(2, 8, 5))
        self.assertEqual(old_expat.get("exit"), "expected reviewed Expat 2.9.0 build dependency")
        self.assertNotIn("analysis", old_python)
        self.assertNotIn("analysis", old_expat)

    def test_recipe_selects_only_known_entrypoints_and_keeps_onedir_console_io(self):
        for mode, relative in (("prod", PROJECT_PATHS["entrypoint"]), ("offline", PROJECT_PATHS["offline-entry"])):
            with self.subTest(mode=mode):
                result = self.execute_recipe(mode)
                self.assertNotIn("exit", result)
                self.assertEqual(result.get("analysis", {}).get("scripts"), [str(ROOT / relative)])
                self.assertEqual(result.get("exe", {}).get("name"), "yt-dlp")
                self.assertIs(result.get("exe", {}).get("exclude_binaries"), True)
                self.assertIs(result.get("exe", {}).get("console"), True)
                self.assertIs(result.get("exe", {}).get("upx"), False)
                self.assertEqual(result.get("exe", {}).get("contents_directory"), "_internal")
                self.assertEqual(result.get("collect", {}).get("name"), "extractor")
                self.assertIs(result.get("collect", {}).get("upx"), False)

    def test_recipe_rejects_arbitrary_mode_or_entrypoint(self):
        for mode in ("", "test", "C:/arbitrary.py", "prod --onefile"):
            with self.subTest(mode=mode):
                result = self.execute_recipe(mode)
                self.assertEqual(result.get("exit"), 2)
                self.assertNotIn("analysis", result)

    def test_recipe_places_both_ejs_files_at_fixed_package_paths(self):
        result = self.execute_recipe("prod")
        analysis = result.get("analysis", {})
        expected = [(str(ROOT / "out/product/fake-venv/Lib/site-packages/yt_dlp_ejs/yt/solver" / name),
                     "yt_dlp_ejs/yt/solver") for name in ("core.min.js", "lib.min.js")]
        self.assertEqual(analysis.get("datas"), expected)
        self.assertIn("yt_dlp_ejs", analysis.get("hiddenimports", []))
        self.assertIn("yt_dlp_ejs.yt.solver", analysis.get("hiddenimports", []))
        self.assertNotIn("yt_dlp_ejs", analysis.get("excludes", []))

    def test_recipe_includes_dynamic_loader_modules_and_custom_hook(self):
        result = self.execute_recipe("prod")
        analysis = result.get("analysis", {})
        expected = {"yt_dlp", "yt_dlp.globals", "yt_dlp.version", "yt_dlp.extractor.youtube",
                    "yt_dlp.extractor.youtube.jsc.provider", "yt_dlp.extractor.youtube.jsc._registry"}
        self.assertTrue(expected.issubset(set(analysis.get("hiddenimports", []))))
        self.assertEqual(analysis.get("hookspath"), [str(HOOK.parent)])
        self.assertIs(analysis.get("noarchive"), False)
        self.assertEqual(analysis.get("pathex"), [str(ROOT / "tools/youtube-resolver")])

    def test_offline_bundles_its_fixture_and_production_module_without_adding_them_to_prod(self):
        production = self.execute_recipe("prod").get("analysis", {})
        offline = self.execute_recipe("offline").get("analysis", {})
        fixture = (str(ROOT / PROJECT_PATHS["fixture"]), "rcmusic_fixture")
        self.assertIn(fixture, offline.get("datas", []))
        self.assertNotIn(fixture, production.get("datas", []))
        self.assertIn("rcmusic_youtube", offline.get("hiddenimports", []))
        self.assertNotIn("rcmusic_youtube", production.get("hiddenimports", []))
        self.assertIn("yt_dlp.extractor.youtube.jsc._director", offline.get("hiddenimports", []))
        self.assertIn("yt_dlp.utils.networking", offline.get("hiddenimports", []))

    def test_offline_has_ejs_version_metadata_without_adding_test_metadata_to_prod(self):
        production = self.execute_recipe("prod")
        offline = self.execute_recipe("offline")
        self.assertEqual(production.get("metadata", []), [])
        self.assertEqual(offline.get("metadata"), ["yt-dlp-ejs"])
        self.assertIn((str(ROOT / "out/product/fake-venv/Lib/site-packages/yt_dlp_ejs-0.8.0.dist-info"),
                       "yt_dlp_ejs-0.8.0.dist-info"), offline.get("analysis", {}).get("datas", []))

    def test_custom_hook_collects_pinned_package_without_pyinstaller_or_optional_dependencies(self):
        candidates = ["yt_dlp", "yt_dlp.compat._legacy", "yt_dlp.utils._deprecated",
                      "yt_dlp.extractor.youtube.jsc.provider", "yt_dlp.__pyinstaller", "yt_dlp.__pyinstaller.hook"]
        calls = []

        def collect(package, **kwargs):
            calls.append(package)
            predicate = kwargs.get("filter", lambda _name: True)
            return [name for name in candidates if predicate(name)]

        hooks = types.ModuleType("PyInstaller.utils.hooks")
        hooks.collect_submodules = collect
        namespace = {}
        with mock.patch.dict(sys.modules, {"PyInstaller": types.ModuleType("PyInstaller"),
                                           "PyInstaller.utils": types.ModuleType("PyInstaller.utils"),
                                           "PyInstaller.utils.hooks": hooks}):
            exec(compile(HOOK.read_text(encoding="utf-8"), str(HOOK), "exec"), namespace)
        self.assertEqual(calls, ["yt_dlp"])
        self.assertIn("yt_dlp.extractor.youtube.jsc.provider", namespace.get("hiddenimports", []))
        self.assertNotIn("yt_dlp.__pyinstaller", namespace.get("hiddenimports", []))
        self.assertNotIn("yt_dlp.__pyinstaller.hook", namespace.get("hiddenimports", []))
        for name in ("yt_dlp.compat._legacy", "yt_dlp.compat._deprecated", "yt_dlp.utils._legacy", "yt_dlp.utils._deprecated"):
            self.assertIn(name, namespace.get("hiddenimports", []))
        self.assertEqual(namespace.get("datas"), [])
        for name in ("pkg_resources", "ytdlp_plugins", "youtube_dl", "youtube_dlc"):
            self.assertIn(name, namespace.get("excludedimports", []))


if __name__ == "__main__":
    unittest.main()
