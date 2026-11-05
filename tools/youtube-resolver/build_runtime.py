"""Materialize a reviewed onedir plan; never installs or downloads packages.

Outputs must be fresh and remain available for inspection after a failure.
This is a developer build recipe, not runtime launch authority or a sandbox.
"""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import stat
import subprocess
import sys

_spec = importlib.util.spec_from_file_location("rcmusic_onedir_plan", Path(__file__).with_name("onedir_plan.py"))
_planner = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_planner)
ROLE_PATHS = _planner.ROLE_PATHS
make_build_plan = _planner.make_build_plan
source_auth_receipt = _planner.source_auth_receipt

NOTICE_SOURCES = {
    "python-license": "notices/python-LICENSE.txt",
    "node-license": "notices/node-LICENSE.txt",
    "yt-dlp-license": "notices/yt-dlp-LICENSE.txt",
    "ejs-license": "notices/ejs-LICENSE.txt",
    "pyinstaller-license": "notices/pyinstaller-COPYING.txt",
}
EMPTY_INSTALL_MARKER = "extractor/_internal/yt_dlp_ejs-0.8.0.dist-info/REQUESTED"


def _plain_path(path):
    path = Path(path).absolute()
    for component in [path, *path.parents]:
        if component.exists() or component.is_symlink():
            attributes = component.lstat()
            if stat.S_ISLNK(attributes.st_mode) or getattr(attributes, "st_file_attributes", 0) & 0x400:
                raise ValueError("reparse or symbolic path refused")
    return path


def _hash(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def verify_sources(sources):
    for source in sources:
        path = _plain_path(source["path"])
        if not path.is_file() or _hash(path) != source["sha256"]:
            raise ValueError("source missing or hash changed: " + source["name"])


def inventory_tree(root):
    root = _plain_path(root)
    if not root.is_dir():
        raise ValueError("artifact directory missing")
    roles = {path: role for role, path in ROLE_PATHS.items()}
    rows, excluded = [], []
    for directory, directories, files in os.walk(root, followlinks=False):
        for name in [*directories, *files]:
            _plain_path(Path(directory) / name)
        for name in files:
            path = Path(directory) / name
            if not path.is_file():
                raise ValueError("nonregular artifact")
            relative = path.relative_to(root).as_posix()
            size = path.stat().st_size
            if relative == EMPTY_INSTALL_MARKER and size == 0:
                excluded.append({"path": relative, "bytes": 0, "sha256": _hash(path)})
                continue
            role = roles.get(relative)
            if role is None and relative.startswith(("extractor/_internal/", "notices/")):
                role = "Support"
            if role is None or not 1 <= size <= 128 * 1024 * 1024:
                raise ValueError("unknown or invalid-size artifact")
            rows.append({"role": role, "path": relative, "bytes": size, "sha256": _hash(path)})
    # Do not mutate a partial/invalid inventory. Only this exact empty pip
    # install marker may be omitted, after every observed file was checked.
    for marker in excluded:
        path = _plain_path(root / marker["path"])
        if path.stat().st_size != 0 or _hash(path) != marker["sha256"]:
            raise ValueError("installation marker changed")
        path.unlink()
    return sorted(rows, key=lambda row: row["path"]), excluded


def stage_inputs(root, sources):
    root = _plain_path(root)
    by_name = {source["name"]: source for source in sources}
    required = {"node": "javascript/node.exe", **NOTICE_SOURCES}
    if not required.keys() <= by_name.keys():
        raise ValueError("node and all five license inputs are required")
    verify_sources([by_name[name] for name in required])
    for name, relative in required.items():
        destination = _plain_path(root / relative)
        if destination.exists():
            raise FileExistsError("staging destination exists")
    for name, relative in required.items():
        source = Path(by_name[name]["path"])
        destination = root / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        with source.open("rb") as reader, destination.open("xb") as writer:
            while chunk := reader.read(1024 * 1024):
                writer.write(chunk)
        if _hash(destination) != by_name[name]["sha256"]:
            raise ValueError("staged bytes changed")


def _write_new(path, value):
    with path.open("x", encoding="utf-8", newline="\n") as stream:
        json.dump(value, stream, ensure_ascii=False, allow_nan=False, indent=2)
        stream.write("\n")


def build_environment(work, venv_python, system_root):
    return {
        "SystemRoot": str(system_root),
        "PATH": os.pathsep.join((str(Path(venv_python).parent), sys.base_prefix,
                                 str(Path(system_root) / "System32"))),
        "TEMP": str(work / "temp"), "TMP": str(work / "temp"),
        "USERPROFILE": str(work / "home"), "APPDATA": str(work / "home/Roaming"),
        "LOCALAPPDATA": str(work / "home/Local"),
        "PYINSTALLER_CONFIG_DIR": str(work / "pyinstaller-config"),
    }


def build(plan, *, runner=None):
    arguments = {key: plan[key] for key in (
        "repository_root", "venv_python", "artifact_root", "work_root", "receipt_path", "mode", "sources")}
    plan = make_build_plan(**arguments)
    artifacts, work, receipt = (_plain_path(plan[key]) for key in ("artifact_root", "work_root", "receipt_path"))
    for output in (artifacts, work, receipt):
        if output.exists():
            raise FileExistsError("all build outputs must be fresh")
    verify_sources(plan["sources"])
    # No network/package installation or global environment modifications here.
    work.mkdir(parents=True)
    receipt.parent.mkdir(parents=True, exist_ok=True)
    temporary = work / "temp"
    temporary.mkdir()
    for directory in (work / "home/Roaming", work / "home/Local"):
        directory.mkdir(parents=True)
    system_root = os.environ.get("SystemRoot", "")
    if not system_root or not Path(system_root).is_dir():
        raise ValueError("Windows SystemRoot unavailable")
    environment = build_environment(work, plan["venv_python"], system_root)
    journal = {"schemaVersion": 1, "purpose": "youtube-runtime-build-v1", "success": False,
               "excluded_install_markers": [], "venv_python_sha256": _hash(Path(plan["venv_python"]))}
    runner = runner or subprocess.run
    try:
        with (work / "pyinstaller.log").open("xb") as log:
            runner(plan["argv"], cwd=plan["repository_root"], env=environment, check=True,
                   stdout=log, stderr=subprocess.STDOUT,
                   creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
        verify_sources(plan["sources"])
        stage_inputs(artifacts, plan["sources"])
        inventory, exclusions = inventory_tree(artifacts)
        journal["excluded_install_markers"] = exclusions
        result = source_auth_receipt(plan, inventory)
        verify_sources(plan["sources"])
        _write_new(receipt, result)
        journal.update(success=True, artifacts=len(inventory), bytes=sum(row["bytes"] for row in inventory),
                       receipt_sha256=_hash(receipt))
        return result
    finally:
        _write_new(work / "build-journal.json", journal)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", type=Path, required=True)
    arguments = parser.parse_args()
    with arguments.config.open(encoding="utf-8") as stream:
        plan = make_build_plan(**json.load(stream))
    result = build(plan)
    print(json.dumps({"purpose": result["purpose"], "artifacts": len(result["artifacts"])}))


if __name__ == "__main__":
    main()
