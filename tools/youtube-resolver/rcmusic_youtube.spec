# Reviewed Windows-only onedir recipe for the custom parent-broker entrypoint.
# --mode is a build parameter; it is never an executable runtime option.
# No discovery of an extractor/Node executable, runtime config or user scripts.

import argparse
from importlib import metadata
from pathlib import Path
import pyexpat
import sys

from PyInstaller.utils.hooks import copy_metadata


parser = argparse.ArgumentParser(description="Build one fixed RC Music broker entrypoint")
parser.add_argument("--mode", choices=("prod", "offline"), required=True)
options = parser.parse_args()

if (sys.platform != "win32" or sys.implementation.name != "cpython" or
        sys.version_info[:3] != (3, 14, 10) or sys.maxsize <= 2**32):
    raise SystemExit("expected reviewed CPython 3.14.10 Windows x64 build interpreter")
if pyexpat.version_info != (2, 9, 0):
    raise SystemExit("expected reviewed Expat 2.9.0 build dependency")

for package, pinned in (("pyinstaller", "6.22.3"), ("yt-dlp", "2026.3.17"), ("yt-dlp-ejs", "0.8.0")):
    if metadata.version(package) != pinned:
        raise SystemExit("unreviewed build package version")

repository = Path(SPEC).resolve().parents[2]
provider_directory = repository / "tools/youtube-resolver"
entrypoint = (provider_directory / "rcmusic_youtube.py" if options.mode == "prod" else
              repository / "tests/youtube/offline_entry.py")

hiddenimports = [
    "yt_dlp", "yt_dlp.globals", "yt_dlp.version", "yt_dlp.extractor.youtube",
    "yt_dlp.extractor.youtube.jsc.provider", "yt_dlp.extractor.youtube.jsc._registry",
    "yt_dlp_ejs", "yt_dlp_ejs.yt", "yt_dlp_ejs.yt.solver",
]
ejs = metadata.distribution("yt-dlp-ejs")
datas = [(str(ejs.locate_file("yt_dlp_ejs/yt/solver/" + name)), "yt_dlp_ejs/yt/solver")
         for name in ("core.min.js", "lib.min.js")]
if options.mode == "offline":
    hiddenimports += ["rcmusic_youtube", "yt_dlp.extractor.youtube.jsc._director", "yt_dlp.utils.networking"]
    datas += [(str(repository / "tests/youtube/fixtures/synthetic-player.js"), "rcmusic_fixture")]
    # Only the separate probe asks importlib.metadata for this version. The
    # production entrypoint uses its pinned version module, without test data.
    datas += copy_metadata("yt-dlp-ejs")

analysis = Analysis(
    [str(entrypoint)],
    pathex=[str(provider_directory)],
    binaries=[],
    datas=datas,
    hiddenimports=hiddenimports,
    hookspath=[str(provider_directory / "hooks")],
    runtime_hooks=[],
    excludes=["pkg_resources", "ytdlp_plugins", "youtube_dl", "youtube_dlc"],
    noarchive=False,
    optimize=0,
)
archive = PYZ(analysis.pure)
executable = EXE(
    archive,
    analysis.scripts,
    [],
    exclude_binaries=True,
    name="yt-dlp",
    console=True,
    contents_directory="_internal",
    upx=False,
    strip=False,
)
collection = COLLECT(executable, analysis.binaries, analysis.datas, name="extractor", upx=False, strip=False)
