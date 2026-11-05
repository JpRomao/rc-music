# Build-time hook for the pinned package, never shipped as a runtime module.
# Pure modules stay in PYZ. Do not collect unrelated installed packages or run
# the package's stock hook, which adds optional transports and crypto extras.
from PyInstaller.utils.hooks import collect_submodules


hiddenimports = collect_submodules(
    "yt_dlp",
    filter=lambda name: name != "yt_dlp.__pyinstaller" and not name.startswith("yt_dlp.__pyinstaller."),
)
hiddenimports = sorted(set(hiddenimports) | {
    "yt_dlp.compat._legacy", "yt_dlp.compat._deprecated", "yt_dlp.utils._legacy", "yt_dlp.utils._deprecated",
})
datas = []
excludedimports = ["pkg_resources", "ytdlp_plugins", "youtube_dl", "youtube_dlc"]
