"""Build-time candidate recipe; never runtime configuration or launch authority."""

from __future__ import annotations

import argparse
from contextlib import ExitStack
import hashlib
import json
import ntpath
import os
from pathlib import Path
import re
import stat
from types import ModuleType


ROOT = Path(os.path.abspath(__file__)).parents[2]
ONEDIR = ROOT / "tools/youtube-resolver/onedir_plan.py"
NETWORK = (
    ("Bootstrap", "RCMusicDownloadWorker.exe"), ("Payload", "rcmusic-download-network.dll"),
    ("Core", "Qt6Core.dll"), ("Network", "Qt6Network.dll"), ("Msvcp140", "msvcp140.dll"),
    ("Msvcp140_1", "msvcp140_1.dll"), ("Vcruntime140", "vcruntime140.dll"),
    ("Vcruntime140_1", "vcruntime140_1.dll"), ("Plugin", "qrcmusicschanneloffline.dll"),
)
MAX_FILE = 128 * 1024 * 1024
MAX_JSON = 8 * 1024 * 1024


def _closed(value, fields):
    if type(value) is not dict or set(value) != set(fields):
        raise ValueError("unknown, missing or invalid fields")


def _text(value, maximum=30000):
    if type(value) is not str or not value.strip():
        raise ValueError("a nonblank string is required")
    try:
        if len(value.encode("utf-16-le", errors="strict")) // 2 > maximum:
            raise ValueError("string limit")
        if len(value.encode("utf-8", errors="strict")) > maximum:
            raise ValueError("UTF8 string limit")
    except UnicodeError:
        raise ValueError("invalid Unicode") from None
    if any(ord(unit) < 32 or ord(unit) == 127 or 0x202A <= ord(unit) <= 0x202E or
           0x2066 <= ord(unit) <= 0x2069 or ord(unit) in (0x2028, 0x2029) for unit in value):
        raise ValueError("control characters are refused")
    return value


def _digest(value):
    if type(value) is not str or re.fullmatch(r"[0-9a-f]{64}", value) is None:
        raise ValueError("an explicit lowercase SHA256 is required")
    return value


def _local(value):
    value = _text(value).replace("\\", "/")
    if re.match(r"^[A-Za-z]:/", value) is None:
        raise ValueError("a local absolute Windows path is required")
    for part in value[3:].split("/"):
        if not part or part in (".", "..") or part.endswith((".", " ")) or any(unit in '<>:"|?*' for unit in part):
            raise ValueError("invalid Windows path component")
        stem = part.split(".", 1)[0].rstrip(" ").upper()
        if stem in ("CON", "PRN", "AUX", "NUL", "CONIN$", "CONOUT$") or re.fullmatch(r"(?:COM|LPT)[1-9¹²³]", stem):
            raise ValueError("reserved Windows path component")
    return Path(value[0].upper() + value[1:])


def _key(path):
    return str(path).replace("\\", "/").casefold()


def _within(path, parent):
    path, parent = _key(path), _key(parent)
    return path == parent or path.startswith(parent + "/")


def _plain(path):
    # All callers validate local path syntax before reaching filesystem APIs.
    # Inspect every existing component without resolving through a reparse point.
    for component in reversed([path, *path.parents]):
        try:
            observed = component.lstat()
        except FileNotFoundError:
            continue
        if stat.S_ISLNK(observed.st_mode) or getattr(observed, "st_file_attributes", 0) & 0x400:
            raise ValueError("reparse or symbolic paths are refused")
    return path


def _size(value):
    if type(value) is not int or not 0 < value <= MAX_FILE:
        raise ValueError("file size must be 1..128 MiB")
    return value


def _metadata(path, expected_size=None):
    _plain(path)
    observed = path.stat()
    if not stat.S_ISREG(observed.st_mode) or not 0 < observed.st_size <= MAX_FILE:
        raise ValueError("a bounded regular input file is required")
    if expected_size is not None and observed.st_size != expected_size:
        raise ValueError("input size changed")
    return observed


def _hash_stream(stream, size, target=None):
    observed = os.fstat(stream.fileno())
    if not stat.S_ISREG(observed.st_mode) or observed.st_size != size:
        raise ValueError("opened input size or type changed")
    stream.seek(0)
    digest = hashlib.sha256()
    remaining = size
    while remaining:
        data = stream.read(min(65536, remaining))
        if not data:
            raise ValueError("input ended before its declared size")
        remaining -= len(data)
        digest.update(data)
        if target is not None:
            target.write(data)
    if stream.read(1) or os.fstat(stream.fileno()).st_size != size:
        raise ValueError("input grew while being read")
    stream.seek(0)
    return digest.hexdigest()


def _hash_file(path, size):
    _plain(path)
    with path.open("rb") as stream:
        return _hash_stream(stream, size)


def _json_pairs(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError("duplicate JSON field")
        result[key] = value
    return result


def _strict_json(data):
    if not 0 < len(data) <= MAX_JSON:
        raise ValueError("JSON size limit")
    try:
        text = data.decode("utf-8", errors="strict")
    except UnicodeError:
        raise ValueError("JSON must be strict UTF8") from None
    depth, quoted, escaped = 0, False, False
    for unit in text:
        if quoted:
            if escaped:
                escaped = False
            elif unit == "\\":
                escaped = True
            elif unit == '"':
                quoted = False
        elif unit == '"':
            quoted = True
        elif unit in "[{":
            depth += 1
            if depth > 8:
                raise ValueError("JSON depth limit")
        elif unit in "]}":
            depth -= 1
            if depth < 0:
                raise ValueError("invalid JSON nesting")
    def invalid_constant(_):
        raise ValueError("nonfinite JSON number")
    try:
        return json.loads(text, object_pairs_hook=_json_pairs, parse_constant=invalid_constant)
    except (UnicodeError, RecursionError, json.JSONDecodeError):
        raise ValueError("invalid JSON") from None


def _read_json(path, expected_hash=None):
    _plain(path)
    observed = path.stat()
    if not stat.S_ISREG(observed.st_mode) or not 0 < observed.st_size <= MAX_JSON:
        raise ValueError("JSON file size or type is invalid")
    with path.open("rb") as stream:
        data = stream.read(MAX_JSON + 1)
    if len(data) != observed.st_size or len(data) > MAX_JSON:
        raise ValueError("JSON file changed")
    actual_hash = hashlib.sha256(data).hexdigest()
    if expected_hash is not None and actual_hash != expected_hash:
        raise ValueError("receipt bytes do not match the explicit pin")
    return _strict_json(data), len(data), actual_hash


def _load_planner():
    _plain(_local(str(ONEDIR)))
    # Reload the known repository source for every plan. A cached validator
    # must never be paired with a snapshot of newer recipe bytes on disk.
    # Loading writes no pyc and discovers no runtime or third-party module.
    source = ONEDIR.read_bytes()
    module = ModuleType("rcmusic_download_profile_onedir")
    module.__file__ = str(ONEDIR)
    exec(compile(source, str(ONEDIR), "exec"), module.__dict__)
    return module


def _source(value, role, leaf, component):
    fields = ("role", "path", "bytes", "sha256") if component == "network" else ("path", "bytes", "sha256")
    _closed(value, fields)
    path = _local(value["path"])
    if path.name.casefold() != leaf.casefold():
        raise ValueError("source basename does not match its fixed role")
    return {"component": component, "role": role, "source_path": str(path),
            "relative_path": "payload/download-tools/" + component + "/" + leaf,
            "bytes": _size(value["bytes"]), "sha256": _digest(value["sha256"])}


def _tree(root, inventory):
    _plain(root)
    if not root.is_dir():
        raise ValueError("YouTube artifact root is missing")
    files = {row["path"] for row in inventory}
    directories = {""}
    for path in files:
        components = path.split("/")
        directories.update("/".join(components[:index]) for index in range(1, len(components)))
    pending = [""]
    seen = set()
    while pending:
        relative = pending.pop()
        directory = root / relative
        _plain(directory)
        with os.scandir(directory) as items:
            for item in items:
                child = relative + "/" + item.name if relative else item.name
                if child not in files and child not in directories:
                    raise ValueError("undeclared file or directory in YouTube tree")
                path = _plain(directory / item.name)
                kind = path.lstat().st_mode
                if child in files:
                    if not stat.S_ISREG(kind):
                        raise ValueError("artifact is not a regular file")
                    seen.add(child)
                else:
                    if not stat.S_ISDIR(kind):
                        raise ValueError("artifact directory is not a directory")
                    pending.append(child)
    if seen != files:
        raise ValueError("YouTube tree does not match the complete inventory")


def _youtube(value):
    _closed(value, ("receipt_path", "receipt_sha256"))
    receipt_path = _local(value["receipt_path"])
    expected_hash = _digest(value["receipt_sha256"])
    receipt, size, _ = _read_json(receipt_path, expected_hash)
    _closed(receipt, ("schemaVersion", "purpose", "artifact_root", "sources", "artifacts"))
    if type(receipt["schemaVersion"]) is not int or receipt["schemaVersion"] != 1 or receipt["purpose"] != "production-candidate-v1":
        raise ValueError("a production-candidate-v1 YouTube receipt is required")
    planner = _load_planner()
    required = {**planner.SOURCE_PINS, **{name: "project" for name in (
        "entrypoint", "spec", "hook", "build-lock", "python-license", "node-license",
        "yt-dlp-license", "ejs-license", "pyinstaller-license")}}
    values = receipt["sources"]
    if type(values) is not list or len(values) != len(required):
        raise ValueError("incorrect production source list")
    seen, paths = set(), set()
    for source in values:
        _closed(source, ("name", "version", "path", "sha256", "provenance"))
        name = _text(source["name"], 64)
        if name not in required or name in seen or type(source["version"]) is not str or source["version"] != required[name]:
            raise ValueError("unknown, duplicate or unreviewed production source")
        seen.add(name)
        path = _local(source["path"])
        if _key(path) in paths:
            raise ValueError("duplicate historical source path")
        paths.add(_key(path))
        _digest(source["sha256"])
        _text(source["provenance"], 2048)
        # Historical observations are metadata from the trusted producer, not
        # fresh filesystem authority. Do not reopen the build's original inputs.
    inventory = planner.validate_inventory(receipt["artifacts"])
    root = _local(receipt["artifact_root"])
    _tree(root, inventory)
    files = [{"component": "youtube", "role": row["role"], "source_path": str(root / row["path"]),
              "relative_path": "payload/download-tools/youtube/" + row["path"],
              "bytes": row["bytes"], "sha256": row["sha256"]} for row in inventory]
    return files, root, {"path": str(receipt_path), "sha256": expected_hash, "bytes": size}


def _header(files, include_youtube, features=None):
    network = [row for row in files if row["component"] == "network"]
    media = next(row for row in files if row["component"] == "media")
    def relative(row):
        return row["relative_path"].removeprefix("payload/")
    lines = ["#pragma once", "", '#include "app/PlayerDownloadsBuildProfile.hpp"', "#include <utility>", "",
             "namespace music_player::m0 {", "inline std::optional<WindowsPlayerDownloadProfile>",
             "configuredPlayerDownloadsBuildProfile(const QString& config_root) {", "    WindowsPlayerDownloadProfile profile;",
             "    profile.prepare_storage = true;", "    profile.execution.network.artifacts = {{"]
    for row in network:
        lines.append('        {QStringLiteral("' + relative(row) + '"), QByteArrayLiteral("' + row["sha256"] + '")},')
    lines.extend(["    }};", '    profile.execution.media.source_executable = QStringLiteral("' + relative(media) + '");',
                  '    profile.execution.media.source_sha256 = QByteArrayLiteral("' + media["sha256"] + '");',
                  "    profile.execution.job.converter.backend = WindowsLocalMediaBackend::MediaFoundation;"])
    if features is not None:
        lines.extend(["    profile.execution.provision_media_profile = " + str(features["provision_media_profile"]).lower() + ";",
                      "    profile.options.embedded_browser_ingress = " + str(features["embedded_browser_ingress"]).lower() + ";"])
    if include_youtube:
        lines.extend(["    profile.execution.youtube.emplace();", "    profile.execution.youtube->runtime.artifacts = {"])
        for row in files:
            if row["component"] == "youtube":
                leaf = row["relative_path"].removeprefix("payload/download-tools/youtube/")
                lines.append("        {WindowsYoutubeRuntimeRole::" + row["role"] + ', QStringLiteral("' + leaf +
                             '"), QStringLiteral("' + relative(row) + '"), QByteArrayLiteral("' + row["sha256"] + '")},')
        lines.append("    };")
    lines.append("    profile.options.native_ingress = compiledPlayerNativeIngressProfiles();")
    if not include_youtube:
        lines.extend(["    if (profile.options.native_ingress) {", "        profile.options.native_ingress->youtube = {};",
                      "        profile.options.native_ingress->youtube_peer_check_factory = {};", "    }"])
    lines.extend(["    return makePackagedPlayerDownloadProfile(std::move(profile), config_root);", "}", "} // namespace music_player::m0", ""])
    return "\n".join(lines)


def _plan(config, *, allow_existing_output=False):
    fields = ("schemaVersion", "output_directory", "network", "media", "youtube")
    if type(config) is dict and "features" in config:
        fields += ("features",)
    _closed(config, fields)
    if type(config["schemaVersion"]) is not int or config["schemaVersion"] != 1:
        raise ValueError("unsupported configuration schema")
    features = None
    if "features" in config:
        feature_names = ("provision_media_profile", "embedded_browser_ingress")
        _closed(config["features"], feature_names)
        if any(type(config["features"][name]) is not bool for name in feature_names):
            raise ValueError("feature values must be booleans")
        features = {name: config["features"][name] for name in feature_names}
    output = _plain(_local(config["output_directory"]))
    parent = _local(str(ROOT / "out/product"))
    if not _within(output, parent) or _key(output) == _key(parent):
        raise ValueError("output must be below the recipe repository out/product")
    if not allow_existing_output and output.exists():
        raise FileExistsError("output already exists")
    values = config["network"]
    if type(values) is not list or len(values) != len(NETWORK):
        raise ValueError("exactly nine network source roles are required")
    by_role = {}
    for value in values:
        _closed(value, ("role", "path", "bytes", "sha256"))
        role = _text(value["role"], 64)
        if role not in dict(NETWORK) or role in by_role:
            raise ValueError("unknown or duplicate network role")
        by_role[role] = value
    files = [_source(by_role[role], role, leaf, "network") for role, leaf in NETWORK]
    files.append(_source(config["media"], "Media", "RCMusicMediaWorker.exe", "media"))
    youtube_receipt, tree = None, None
    if config["youtube"] is not None:
        youtube_files, tree, youtube_receipt = _youtube(config["youtube"])
        files.extend(youtube_files)
        if _within(output, tree) or _within(tree, output):
            raise ValueError("output overlaps the closed YouTube source tree")
    seen = set()
    for row in files:
        path = _local(row["source_path"])
        if _key(path) in seen:
            raise ValueError("source path collision")
        seen.add(_key(path))
        if _within(path, output) or _within(output, path.parent):
            raise ValueError("output overlaps an input source location")
        _metadata(path, row["bytes"])
    # Only after every size/type is known may a potentially large hash run.
    for row in files:
        if _hash_file(Path(row["source_path"]), row["bytes"]) != row["sha256"]:
            raise ValueError("input file SHA256 changed")
    recipes = []
    for path in (Path(__file__).absolute(), ONEDIR):
        path = _local(str(path))
        size = _metadata(path).st_size
        recipes.append({"path": str(path), "bytes": size, "sha256": _hash_file(path, size)})
    canonical = {"schemaVersion": 1, "output_directory": str(output),
                 "network": [{"role": row["role"], "path": row["source_path"], "bytes": row["bytes"], "sha256": row["sha256"]}
                             for row in files if row["component"] == "network"],
                 "media": {key: files[len(NETWORK)][{"path": "source_path"}.get(key, key)] for key in ("path", "bytes", "sha256")},
                 "youtube": None if youtube_receipt is None else {"receipt_path": youtube_receipt["path"], "receipt_sha256": youtube_receipt["sha256"]}}
    if features is not None:
        canonical["features"] = features
    return {"schemaVersion": 1, "purpose": "player-download-package-profile-v1", "config": canonical,
            "output_directory": str(output), "files": files, "header_text": _header(files, youtube_receipt is not None, features),
            "recipe_files": recipes, "youtube_receipt": youtube_receipt}


def plan(config):
    """Inspect an explicit configuration without creating output."""
    return _plan(config)


def materialize(candidate):
    """Write a fresh candidate only after revalidating its input snapshot."""
    if type(candidate) is not dict or "config" not in candidate:
        raise ValueError("invalid candidate plan")
    current = plan(candidate["config"])
    if candidate != current:
        raise ValueError("candidate plan was changed or its snapshot is stale")
    output = Path(current["output_directory"])
    observations = [*current["files"], *current["recipe_files"]]
    if current["youtube_receipt"] is not None:
        observations.append(current["youtube_receipt"])
    with ExitStack() as stack:
        streams = {}
        for row in observations:
            path = Path(row.get("source_path", row.get("path")))
            _metadata(path, row["bytes"])
            stream = stack.enter_context(path.open("rb"))
            if _hash_stream(stream, row["bytes"]) != row["sha256"]:
                raise ValueError("input changed before materialization")
            streams[str(path)] = stream
        if plan(current["config"]) != current:
            raise ValueError("input or recipe snapshot changed before output creation")
        output.mkdir(parents=True, exist_ok=False)
        for row in current["files"]:
            target = output / row["relative_path"]
            _plain(target)
            target.parent.mkdir(parents=True, exist_ok=True)
            with target.open("xb") as writer:
                copied_hash = _hash_stream(streams[row["source_path"]], row["bytes"], writer)
                writer.flush()
                os.fsync(writer.fileno())
            if copied_hash != row["sha256"] or _metadata(target, row["bytes"]).st_size != row["bytes"] or _hash_file(target, row["bytes"]) != row["sha256"]:
                raise ValueError("copied bytes changed; partial output was preserved")
        header = output / "PlayerDownloadPackageProfile.hpp"
        header_bytes = current["header_text"].encode("utf-8", errors="strict")
        with header.open("xb") as stream:
            stream.write(header_bytes)
            stream.flush()
            os.fsync(stream.fileno())
        header_hash = hashlib.sha256(header_bytes).hexdigest()
        if _hash_file(header, len(header_bytes)) != header_hash:
            raise ValueError("generated header bytes changed; partial output was preserved")
        if _plan(current["config"], allow_existing_output=True) != current:
            raise ValueError("input snapshot changed; partial output was preserved")
        receipt = {"schemaVersion": 1, "purpose": current["purpose"], "state": "candidate",
                   "files": [{key: row[key] for key in ("component", "role", "relative_path", "bytes", "sha256")} for row in current["files"]],
                   "header": {"relative_path": header.name, "bytes": len(header_bytes), "sha256": header_hash},
                   "recipe_files": current["recipe_files"], "youtube_receipt": current["youtube_receipt"]}
        if "features" in current["config"]:
            receipt["features"] = current["config"]["features"]
        receipt_path = output / "download-profile-receipt.json"
        with receipt_path.open("x", encoding="utf-8", newline="\n") as stream:
            json.dump(receipt, stream, ensure_ascii=False, allow_nan=False, indent=2)
            stream.write("\n")
            stream.flush()
            os.fsync(stream.fileno())
    return {"output_directory": str(output), "header_path": str(header), "receipt_path": str(receipt_path)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", type=Path, required=True)
    parser.add_argument("--plan-only", action="store_true")
    args = parser.parse_args()
    try:
        config, _, _ = _read_json(_local(str(args.config)))
        candidate = plan(config)
        result = candidate if args.plan_only else materialize(candidate)
    except (ValueError, OSError) as error:
        parser.error(str(error))
    # stdout may use a Windows legacy code page; JSON escapes preserve Unicode.
    print(json.dumps(result, ensure_ascii=True))


if __name__ == "__main__":
    main()
