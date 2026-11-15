// Manual parent for the contextual helper. CTest runs pure self-test only.
#include "AppContainerStorageProbeOptions.hpp"

#include <cstdio>

namespace {
using StorageOptions = music_player::sandbox_probe::AppContainerStorageProbeOptions;
enum class Mode { Refused, Help, SelfTest, Context };
struct Options final {
    Mode mode{Mode::Refused};
    QString evidence{};
};
QString workspace() { return QString::fromUtf8(RC_MUSIC_SANDBOX_WORKSPACE_ROOT); }
Options parseOptions(const QStringList&, const QString&) {
    return {}; // Red: exact opt-in must precede every host operation.
}
int selfTest() {
    const auto root = workspace();
    const auto path = root + QStringLiteral("/out/product/context-parser-not-created");
    unsigned passed{}, failed{};
    const auto check = [&](const QStringList& args, const Mode mode) {
        if (parseOptions(args, root).mode == mode)
            ++passed;
        else
            ++failed;
    };
    check({"--self-test"}, Mode::SelfTest);
    check({"--help"}, Mode::Help);
    check({"--allow-context-registry-query", "--allow-profile-changes", "--evidence-dir", path}, Mode::Context);
    for (const auto& args : {QStringList{}, QStringList{"--allow-profile-changes", "--evidence-dir", path},
                             QStringList{"--allow-context-registry-query"}, QStringList{"--readonly"},
                             QStringList{"--allow-wfp-subscription"}, QStringList{"--self-test", path},
                             QStringList{"--allow-context-registry-query", "--allow-profile-changes", "--evidence-dir",
                                         root + "/out/PRODUCT/x"},
                             QStringList{"--allow-context-registry-query", "--allow-profile-changes", "--evidence-dir",
                                         path, "--helper-path", "untrusted.exe"}})
        check(args, Mode::Refused);
    const int count = std::printf("{\"passed\":%u,\"failed\":%u,\"profileOrWorkerCalls\":0}\n", passed, failed);
    return failed || count < 0 || std::fflush(stdout) != 0 ? 1 : 0;
}
} // namespace
int wmain(const int argc, const wchar_t* const* argv) {
    if (argc == 2 && argv && argv[1] && QString::fromWCharArray(argv[1]) == QStringLiteral("--self-test"))
        return selfTest();
    return 2;
}
