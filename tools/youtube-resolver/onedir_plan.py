"""Pure, data-only planner for the reviewed Windows onedir build.

The caller supplies observed paths, hashes, versions and byte counts. These
functions never inspect a filesystem, import a runtime, download, copy or start
a process. Their receipt is evidence for review, not launch authority.
"""

from __future__ import annotations

import ntpath
import re


SOURCE_PINS = {
    "python": "3.14.10",
    "node": "24.20.0",
    "altgraph": "0.17.5",
    "packaging": "26.3",
    "pefile": "2024.8.26",
    "pyinstaller": "6.22.3",
    "pyinstaller-hooks-contrib": "2026.9",
    "pywin32-ctypes": "0.2.3",
    "setuptools": "84.2.1",
    "yt-dlp": "2026.3.17",
    "yt-dlp-ejs": "0.8.0",
}
PROJECT_SOURCES = (
    "entrypoint", "spec", "hook", "build-lock", "fixture", "offline-entry",
    "python-license", "node-license", "yt-dlp-license", "ejs-license", "pyinstaller-license",
)
ROLE_PATHS = {
    "Extractor": "extractor/yt-dlp.exe",
    "Node": "javascript/node.exe",
    "EjsCore": "extractor/_internal/yt_dlp_ejs/yt/solver/core.min.js",
    "EjsLibrary": "extractor/_internal/yt_dlp_ejs/yt/solver/lib.min.js",
}
REQUIRED_NOTICE_PATHS = (
    "notices/python-LICENSE.txt",
    "notices/node-LICENSE.txt",
    "notices/yt-dlp-LICENSE.txt",
    "notices/ejs-LICENSE.txt",
    "notices/pyinstaller-COPYING.txt",
)
MAX_FILES = 256
MAX_FILE_BYTES = 128 * 1024 * 1024
MAX_TOTAL_BYTES = 512 * 1024 * 1024
_PROJECT_PATHS = {
    "entrypoint": "tools/youtube-resolver/rcmusic_youtube.py",
    "spec": "tools/youtube-resolver/rcmusic_youtube.spec",
    "hook": "tools/youtube-resolver/hooks/hook-yt_dlp.py",
    "build-lock": "tools/youtube-resolver/requirements-build.lock",
    "fixture": "tests/youtube/fixtures/synthetic-player.js",
    "offline-entry": "tests/youtube/offline_entry.py",
}
_PLAN_INPUTS = ("repository_root", "venv_python", "artifact_root", "work_root", "receipt_path", "mode", "sources")
_PLAN_KEYS = frozenset((*_PLAN_INPUTS, "schemaVersion", "purpose", "entrypoint", "spec_path", "notice_paths", "argv"))


def _text(value, maximum):
    if type(value) is not str or not value.strip() or len(value) > maximum:
        raise ValueError("invalid text")
    if any(ord(unit) < 32 or ord(unit) == 127 or 0x202A <= ord(unit) <= 0x202E or
           0x2066 <= ord(unit) <= 0x2069 or ord(unit) in (0x2028, 0x2029) for unit in value):
        raise ValueError("invalid text")
    try:
        if len(value.encode("utf-8", errors="strict")) > maximum:
            raise ValueError("text limit")
    except UnicodeError:
        raise ValueError("invalid Unicode") from None
    return value


def _absolute(value):
    value = _text(value, 8192).replace("\\", "/")
    if not re.match(r"^[A-Za-z]:/", value):
        raise ValueError("expected local absolute Windows path")
    value = value.rstrip("/")
    components = value[3:].split("/")
    if not components or any(not part or part in (".", "..") or part.endswith((".", " ")) or
                             any(unit in '<>:"|?*' for unit in part) for part in components):
        raise ValueError("invalid absolute path")
    return value[0].upper() + value[1:]


def _within(path, parent):
    path, parent = path.casefold(), parent.casefold()
    return path == parent or path.startswith(parent + "/")


def _descendant(path, parent):
    return path.casefold() != parent.casefold() and _within(path, parent)


def _digest(value):
    if type(value) is not str or re.fullmatch(r"[0-9a-f]{64}", value) is None:
        raise ValueError("expected lowercase SHA-256")
    return value


def _sources(values, repository_root, artifact_root, work_root, mode):
    if type(values) is not list or not values or len(values) > 64:
        raise ValueError("invalid source list")
    required = set(SOURCE_PINS) | {"entrypoint", "spec", "hook", "build-lock"}
    if mode == "offline":
        required.update(("fixture", "offline-entry"))
    seen_names, seen_paths, result = set(), set(), []
    for value in values:
        if type(value) is not dict or set(value) != {"name", "version", "path", "sha256", "provenance"}:
            raise ValueError("invalid source record")
        name = _text(value["name"], 64)
        if name not in SOURCE_PINS and name not in PROJECT_SOURCES:
            raise ValueError("unknown source")
        if name in seen_names:
            raise ValueError("duplicate source")
        seen_names.add(name)
        version = _text(value["version"], 64)
        if version != SOURCE_PINS.get(name, "project"):
            raise ValueError("unreviewed source version")
        path = _absolute(value["path"])
        if _within(path, artifact_root) or _within(path, work_root):
            raise ValueError("source overlaps build output")
        if path.casefold() in seen_paths:
            raise ValueError("source path collision")
        seen_paths.add(path.casefold())
        if name in _PROJECT_PATHS:
            expected = repository_root + "/" + _PROJECT_PATHS[name]
            if path.casefold() != expected.casefold():
                raise ValueError("project source does not cover fixed build input")
            path = expected
        elif name in ("python", "node"):
            if ntpath.basename(path).casefold() != name + ".exe":
                raise ValueError("unexpected interpreter source")
        elif name in SOURCE_PINS and not path.lower().endswith(".whl"):
            raise ValueError("expected pinned wheel")
        result.append({"name": name, "version": version, "path": path, "sha256": _digest(value["sha256"]),
                       "provenance": _text(value["provenance"], 2048)})
    if not required.issubset(seen_names):
        raise ValueError("missing reviewed source")
    return sorted(result, key=lambda value: value["name"])


def _relative(value):
    value = _text(value, 1024)
    components = value.split("/")
    if len(components) > 16:
        raise ValueError("relative path depth")
    for part in components:
        if (not part or len(part) > 255 or part in (".", "..") or part.endswith(".") or
                re.fullmatch(r"[A-Za-z0-9_.-]+", part) is None):
            raise ValueError("invalid relative path")
        base = part.split(".", 1)[0].upper()
        if base in ("CON", "PRN", "AUX", "NUL") or re.fullmatch(r"(?:COM|LPT)[1-9]", base):
            raise ValueError("reserved Windows filename")
    return value, components


def make_build_plan(*, repository_root, venv_python, artifact_root, work_root, receipt_path, mode, sources):
    """Return a canonical owned dict, or raise ValueError for an invalid plan.

    All paths are explicit local absolute Windows paths. Output directories
    must be disjoint descendants of repository_root/out; the caller checks
    freshness before execution. mode is exactly 'prod' or 'offline'. sources
    is a list of exact {name, version, path, sha256, provenance} dictionaries,
    one per SOURCE_PINS entry. PROJECT_SOURCES use version 'project'; entrypoint,
    spec, hook and build-lock are required, plus fixture/offline-entry in offline
    mode. Remaining license sources are optional explicit observations.
    The returned argv only describes a build; it never executes that build.
    """
    if type(mode) is not str or mode not in ("prod", "offline"):
        raise ValueError("invalid build mode")
    repository_root = _absolute(repository_root)
    venv_python = _absolute(venv_python)
    artifact_root = _absolute(artifact_root)
    work_root = _absolute(work_root)
    receipt_path = _absolute(receipt_path)
    out = repository_root + "/out"
    if any(not _descendant(path, out) for path in (artifact_root, work_root, receipt_path, venv_python)):
        raise ValueError("build paths must stay below repository out")
    if _within(artifact_root, work_root) or _within(work_root, artifact_root):
        raise ValueError("overlapping build directories")
    if (_within(receipt_path, artifact_root) or _within(artifact_root, receipt_path) or
            _within(work_root, receipt_path) or _within(receipt_path, work_root)):
        raise ValueError("receipt must be outside the artifact tree")
    if _within(venv_python, artifact_root) or _within(venv_python, work_root):
        raise ValueError("interpreter overlaps build output")
    if ntpath.basename(venv_python).casefold() != "python.exe":
        raise ValueError("expected private venv interpreter")
    sources = _sources(sources, repository_root, artifact_root, work_root, mode)
    spec_path = repository_root + "/" + _PROJECT_PATHS["spec"]
    entrypoint = repository_root + "/" + _PROJECT_PATHS["entrypoint" if mode == "prod" else "offline-entry"]
    argv = [venv_python, "-I", "-m", "PyInstaller", "--distpath", artifact_root,
            "--workpath", work_root, spec_path, "--", "--mode", mode]
    return {"schemaVersion": 1,
            "purpose": "production-candidate-v1" if mode == "prod" else "offline-synthetic-v1",
            "mode": mode, "repository_root": repository_root, "venv_python": venv_python,
            "artifact_root": artifact_root, "work_root": work_root, "receipt_path": receipt_path,
            "entrypoint": entrypoint, "spec_path": spec_path,
            "notice_paths": list(REQUIRED_NOTICE_PATHS), "sources": sources, "argv": argv}


def validate_inventory(entries):
    """Return sorted owned artifact dicts or raise ValueError.

    Each entry has exactly {role, path, bytes, sha256}. Paths use the existing
    WindowsYoutubeRuntime grammar and fixed role paths. All four unique roles
    and REQUIRED_NOTICE_PATHS are required, with every other file a Support
    under extractor/_internal/ or notices/. Every file has 1..128 MiB; hashes
    are lowercase SHA-256 hex. Source names are canonical lowercase; 'project'
    denotes an input record, not the component's pinned semantic version.
    Validation of these observations does not prove their filesystem identity.
    """
    if type(entries) is not list or not entries or len(entries) > MAX_FILES:
        raise ValueError("invalid inventory count")
    files, directories, roles, result = {}, {}, set(), []
    total = 0
    for value in entries:
        if type(value) is not dict or set(value) != {"role", "path", "bytes", "sha256"}:
            raise ValueError("invalid artifact record")
        role = value["role"]
        if type(role) is not str or (role not in ROLE_PATHS and role != "Support"):
            raise ValueError("unknown artifact role")
        path, components = _relative(value["path"])
        if role in ROLE_PATHS:
            if path != ROLE_PATHS[role] or role in roles:
                raise ValueError("duplicate or misplaced artifact role")
            roles.add(role)
        elif not path.startswith(("extractor/_internal/", "notices/")):
            raise ValueError("unreviewed support location")
        size = value["bytes"]
        if type(size) is not int or not 0 < size <= MAX_FILE_BYTES or size > MAX_TOTAL_BYTES - total:
            raise ValueError("artifact byte limit")
        total += size
        key = path.casefold()
        if key in files or key in directories:
            raise ValueError("artifact path collision")
        for index in range(1, len(components)):
            directory = "/".join(components[:index])
            directory_key = directory.casefold()
            if directory_key in files or (directory_key in directories and directories[directory_key] != directory):
                raise ValueError("file/directory or casing collision")
            directories[directory_key] = directory
        files[key] = path
        result.append({"role": role, "path": path, "bytes": size, "sha256": _digest(value["sha256"])})
    if roles != set(ROLE_PATHS) or not all(path in files.values() for path in REQUIRED_NOTICE_PATHS):
        raise ValueError("missing artifact role or notice")
    return sorted(result, key=lambda value: value["path"])


def source_auth_receipt(plan, inventory):
    """Return schemaVersion=1 data for the caller to save outside artifacts.

    Revalidates the plan and inventory; includes purpose, artifact_root,
    sources, and flat artifacts. Contains no process/launch capability.
    """
    if (type(plan) is not dict or set(plan) != _PLAN_KEYS or
            type(plan["schemaVersion"]) is not int or plan["schemaVersion"] != 1):
        raise ValueError("invalid build plan")
    canonical = make_build_plan(**{key: plan[key] for key in _PLAN_INPUTS})
    if plan != canonical:
        raise ValueError("build plan was changed")
    return {"schemaVersion": 1, "purpose": canonical["purpose"], "artifact_root": canonical["artifact_root"],
            "sources": canonical["sources"], "artifacts": validate_inventory(inventory)}
