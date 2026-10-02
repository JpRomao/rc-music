#pragma once

namespace music_player::m0 {

// Common one-shot worker body. The legacy EXE and private DLL entry use this
// same implementation. This C++ helper is internal, not the cross-CRT ABI.
[[nodiscard]] int runWindowsDownloadNetworkWorker(int argc, char* argv[]);

} // namespace music_player::m0
