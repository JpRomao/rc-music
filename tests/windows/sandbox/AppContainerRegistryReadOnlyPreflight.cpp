// Manual, read-only Registry diagnostic. CTest may run --self-test only.
// This program does not accept a key name or path and never creates a fixture.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <winternl.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <sstream>
#include <string>
#include <string_view>

// SDK 22621 supplies the types but not this user-mode declaration. The
// documented ZwOpenKeyEx signature is shared by its Nt user-mode entry point.
extern "C" NTSYSAPI NTSTATUS NTAPI NtOpenKeyEx(PHANDLE key, ACCESS_MASK access, POBJECT_ATTRIBUTES attributes,
                                               ULONG open_options);

namespace {
constexpr std::uint32_t not_called = std::numeric_limits<std::uint32_t>::max();
constexpr std::size_t maximum_output = 16 * 1024;
constexpr REGSAM read_access = KEY_QUERY_VALUE | READ_CONTROL | KEY_WOW64_64KEY;
constexpr ULONG no_reparse = OBJ_DONT_REPARSE | OBJ_CASE_INSENSITIVE;
constexpr NTSTATUS type_mismatch = static_cast<NTSTATUS>(0xc0000024UL);
constexpr NTSTATUS reparse_encountered = static_cast<NTSTATUS>(0xc000050bUL);
static_assert(sizeof(NTSTATUS) == sizeof(std::uint32_t));

enum class Mode { Invalid, ReadOnly, SelfTest };
Mode parse(const int argc, const char* const* argv) {
    if (argc != 2 || !argv || !argv[0] || !argv[1])
        return Mode::Invalid;
    const std::string_view argument(argv[1]);
    if (argument == "--readonly")
        return Mode::ReadOnly;
    if (argument == "--self-test")
        return Mode::SelfTest;
    return Mode::Invalid;
}

const char* boolean(const bool value) { return value ? "true" : "false"; }
int emit(const std::string& bytes, const int result) {
    if (bytes.size() >= maximum_output) {
        constexpr auto failed = "{\"schema\":1,\"readOnly\":true,\"error\":\"output-limit\"}\n";
        static_cast<void>(std::fwrite(failed, 1, std::char_traits<char>::length(failed), stdout));
        return 1;
    }
    const auto written = std::fwrite(bytes.data(), 1, bytes.size(), stdout);
    if (written != bytes.size() || std::fflush(stdout) != 0)
        return 1;
    return result;
}

int parserSelfTest() {
    struct Case final {
        std::array<const char*, 4> arguments;
        int count;
        Mode expected;
    };
    constexpr std::array cases{
        Case{{"probe", "--readonly", nullptr, nullptr}, 2, Mode::ReadOnly},
        Case{{"probe", "--self-test", nullptr, nullptr}, 2, Mode::SelfTest},
        Case{{"probe", nullptr, nullptr, nullptr}, 1, Mode::Invalid},
        Case{{"probe", "--readOnly", nullptr, nullptr}, 2, Mode::Invalid},
        Case{{"probe", "--readonly ", nullptr, nullptr}, 2, Mode::Invalid},
        Case{{"probe", "--readonly", "HKCU\\Software", nullptr}, 3, Mode::Invalid},
        Case{{"probe", "--self-test", "--readonly", nullptr}, 3, Mode::Invalid},
        Case{{"probe", "--allow-profile-changes", nullptr, nullptr}, 2, Mode::Invalid},
        Case{{"probe", "--allow-wfp-subscription", nullptr, nullptr}, 2, Mode::Invalid},
        Case{{"probe", "--readonly=HKCU", nullptr, nullptr}, 2, Mode::Invalid},
        Case{{"probe", "", nullptr, nullptr}, 2, Mode::Invalid},
        Case{{"probe", nullptr, nullptr, nullptr}, 2, Mode::Invalid},
    };
    unsigned passed = 0, failed = 0;
    for (const auto& test : cases) {
        if (parse(test.count, test.arguments.data()) == test.expected)
            ++passed;
        else
            ++failed;
    }
    if (parse(2, nullptr) == Mode::Invalid)
        ++passed;
    else
        ++failed;
    std::ostringstream output;
    output << "{\"schema\":1,\"mode\":\"parser-self-test\",\"registryCalls\":0,\"passed\":" << passed
           << ",\"failed\":" << failed << "}\n";
    return emit(output.str(), failed ? 1 : 0);
}

bool predefined(const HKEY key) {
    return key == HKEY_CLASSES_ROOT || key == HKEY_CURRENT_USER || key == HKEY_LOCAL_MACHINE || key == HKEY_USERS ||
           key == HKEY_PERFORMANCE_DATA || key == HKEY_CURRENT_CONFIG || key == HKEY_DYN_DATA ||
           key == HKEY_CURRENT_USER_LOCAL_SETTINGS || key == HKEY_PERFORMANCE_TEXT || key == HKEY_PERFORMANCE_NLSTEXT;
}
bool validOwned(const HKEY key) {
    return key && key != reinterpret_cast<HKEY>(INVALID_HANDLE_VALUE) && !predefined(key);
}
struct Query final {
    bool attempted{false};
    std::uint32_t error{not_called}, type{}, bytes{};
    [[nodiscard]] bool absentIndicator() const { return attempted && error == ERROR_FILE_NOT_FOUND; }
    [[nodiscard]] bool linkIndicator() const { return attempted && error == ERROR_SUCCESS && type == REG_LINK; }
};
Query indicator(const HKEY key) {
    Query result;
    result.attempted = true;
    DWORD type = 0, bytes = 0;
    const auto error = RegQueryValueExW(key, L"SymbolicLinkValue", nullptr, &type, nullptr, &bytes);
    result.error = static_cast<std::uint32_t>(error);
    if (error == ERROR_SUCCESS) {
        result.type = type;
        result.bytes = bytes;
    }
    // No value data is requested, allocated, copied or used as a target path.
    // ERROR_FILE_NOT_FOUND is only an observation, never proof of key type.
    return result;
}
struct Ledger final {
    struct Key final {
        HKEY value{};
        bool close_attempted{false};
        std::uint32_t close_error{not_called};
    };
    std::array<Key, 16> keys{};
    std::size_t opened{}, closed{};
    bool overflow{false}, unexpected_handle{false}, unexpected_predefined{false}, close_called{false};
    bool own(const HKEY key) {
        if (!validOwned(key)) {
            unexpected_handle = true;
            unexpected_predefined = unexpected_predefined || predefined(key);
            return false;
        }
        if (opened >= keys.size()) {
            // Retain an unexpected extra native resource until process exit.
            overflow = true;
            return false;
        }
        keys[opened++].value = key;
        return true;
    }
    bool closeAll() {
        if (close_called)
            return opened == closed && !overflow && !unexpected_handle;
        close_called = true;
        for (std::size_t index = opened; index > 0; --index) {
            auto& key = keys[index - 1];
            key.close_attempted = true;
            key.close_error = static_cast<std::uint32_t>(RegCloseKey(key.value));
            if (key.close_error == ERROR_SUCCESS) {
                key.value = nullptr;
                ++closed;
            }
        }
        // No retry or destructor cleanup. Failed closes stay unknown/retained.
        return opened == closed && !overflow && !unexpected_handle;
    }
};
struct Step final {
    const char* name{};
    bool native{false}, attempted{false}, handle_returned{false}, opened{false};
    ULONG options{}, attributes{};
    NTSTATUS status{};
    std::uint32_t error{not_called};
    HKEY key{}; // Process-local only: never serialized or treated as identity.
    Query query{};
};
Step win32Open(Ledger& ledger, const char* label, const HKEY parent, const wchar_t* component) {
    Step result{.name = label, .attempted = true, .options = REG_OPTION_OPEN_LINK};
    HKEY key = nullptr;
    const auto error = RegOpenKeyExW(parent, component, REG_OPTION_OPEN_LINK, read_access, &key);
    result.error = static_cast<std::uint32_t>(error);
    result.handle_returned = key != nullptr;
    const bool owned = key && ledger.own(key);
    result.opened = error == ERROR_SUCCESS && owned;
    if (result.opened) {
        result.key = key;
        result.query = indicator(key);
    }
    return result;
}
Step nativeOpen(Ledger& ledger, const char* label, const HKEY parent, const wchar_t* component, const ULONG options) {
    Step result{.name = label, .native = true, .options = options, .attributes = no_reparse};
    if (!validOwned(parent))
        return result; // Never pass a predefined pseudo-handle as RootDirectory.
    const std::wstring_view name(component);
    if (name.empty() || name.size() > 255 || name.find_first_of(L"\\/") != std::wstring_view::npos)
        return result;
    UNICODE_STRING unicode{};
    unicode.Length = static_cast<USHORT>(name.size() * sizeof(wchar_t));
    unicode.MaximumLength = unicode.Length;
    unicode.Buffer = const_cast<PWSTR>(name.data());
    OBJECT_ATTRIBUTES attributes{};
    attributes.Length = sizeof(attributes);
    attributes.RootDirectory = parent;
    attributes.ObjectName = &unicode;
    attributes.Attributes = no_reparse; // No control ever follows a link intentionally.
    HANDLE key = nullptr;
    result.attempted = true;
    result.status = NtOpenKeyEx(&key, read_access, &attributes, options);
    result.error = RtlNtStatusToDosError(result.status);
    result.handle_returned = key != nullptr;
    const auto typed = static_cast<HKEY>(key);
    const bool owned = typed && ledger.own(typed);
    result.opened = result.status == 0 && owned;
    if (result.opened) {
        result.key = typed;
        result.query = indicator(typed);
    }
    return result;
}
bool nativeRejectedLink(const Step& step) {
    return step.attempted && !step.opened && !step.handle_returned &&
           (step.status == type_mismatch || step.status == reparse_encountered);
}
void writeQuery(std::ostringstream& output, const Query& query) {
    output << "{\"attempted\":" << boolean(query.attempted) << ",\"error\":";
    if (query.attempted)
        output << query.error;
    else
        output << "null";
    output << ",\"type\":";
    if (query.attempted && query.error == ERROR_SUCCESS)
        output << query.type;
    else
        output << "null";
    output << ",\"bytes\":";
    if (query.attempted && query.error == ERROR_SUCCESS)
        output << query.bytes;
    else
        output << "null";
    output << '}';
}
void writeStep(std::ostringstream& output, const Step& step) {
    output << "{\"name\":\"" << step.name << "\",\"api\":\"" << (step.native ? "NtOpenKeyEx" : "RegOpenKeyExW")
           << "\",\"attempted\":" << boolean(step.attempted) << ",\"openOptions\":" << step.options
           << ",\"objectAttributes\":" << step.attributes << ",\"handleReturned\":" << boolean(step.handle_returned)
           << ",\"opened\":" << boolean(step.opened) << ",\"ntStatus\":";
    if (step.native && step.attempted)
        output << static_cast<std::int64_t>(step.status);
    else
        output << "null";
    output << ",\"ntStatusBits\":";
    if (step.native && step.attempted)
        output << static_cast<std::uint32_t>(step.status);
    else
        output << "null";
    output << ",\"win32Error\":";
    if (step.attempted)
        output << step.error;
    else
        output << "null";
    output << ",\"symbolicLinkValueIndicator\":";
    writeQuery(output, step.query);
    output << '}';
}

int readonlyPreflight() {
    Ledger ledger;
    std::array<Step, 9> steps{
        Step{.name = "hkcu_software"},
        Step{.name = "hkcu_microsoft"},
        Step{.name = "classes_open_link"},
        Step{.name = "nt_hkcu_microsoft", .native = true, .attributes = no_reparse},
        Step{.name = "nt_classes_options_zero", .native = true, .attributes = no_reparse},
        Step{.name = "nt_classes_open_link", .native = true, .options = REG_OPTION_OPEN_LINK, .attributes = no_reparse},
        Step{.name = "local_settings_software"},
        Step{.name = "local_settings_microsoft"},
        Step{.name = "nt_local_settings_microsoft", .native = true, .attributes = no_reparse},
    };
    steps[0] = win32Open(ledger, steps[0].name, HKEY_CURRENT_USER, L"Software");
    if (steps[0].opened && steps[0].query.absentIndicator()) {
        steps[1] = win32Open(ledger, steps[1].name, steps[0].key, L"Microsoft");
        steps[2] = win32Open(ledger, steps[2].name, steps[0].key, L"Classes");
        if (steps[1].opened && steps[1].query.absentIndicator())
            steps[3] = nativeOpen(ledger, steps[3].name, steps[0].key, L"Microsoft", 0);
        if (steps[2].opened && steps[2].query.linkIndicator()) {
            steps[4] = nativeOpen(ledger, steps[4].name, steps[0].key, L"Classes", 0);
            steps[5] = nativeOpen(ledger, steps[5].name, steps[0].key, L"Classes", REG_OPTION_OPEN_LINK);
        }
    }
    steps[6] = win32Open(ledger, steps[6].name, HKEY_CURRENT_USER_LOCAL_SETTINGS, L"Software");
    if (steps[6].opened && steps[6].query.absentIndicator()) {
        steps[7] = win32Open(ledger, steps[7].name, steps[6].key, L"Microsoft");
        if (steps[7].opened && steps[7].query.absentIndicator())
            steps[8] = nativeOpen(ledger, steps[8].name, steps[6].key, L"Microsoft", 0);
    }
    const bool normal_controls =
        steps[3].opened && steps[3].query.absentIndicator() && steps[8].opened && steps[8].query.absentIndicator();
    const bool source_link_observed = steps[2].opened && steps[2].query.linkIndicator();
    const bool options_zero_rejected_link = source_link_observed && nativeRejectedLink(steps[4]);
    const bool open_link_observed = steps[5].opened && steps[5].query.linkIndicator();
    const bool open_link_rejected = nativeRejectedLink(steps[5]);
    const bool native_open_link_recognized = open_link_observed || open_link_rejected;
    const bool closed = ledger.closeAll();
    const bool available =
        normal_controls && source_link_observed && options_zero_rejected_link && native_open_link_recognized && closed;
    std::ostringstream output;
    output << "{\"schema\":1,\"mode\":\"registry-readonly-preflight\",\"readOnly\":true,"
              "\"registryValueDataRead\":false,\"ordinaryKeyTypeProven\":false,\"securityPolicyApproved\":false,"
              "\"qualification\":\""
           << (available ? "limited-observations-available" : "unavailable")
           << "\",\"available\":" << boolean(available) << ",\"normalControlsOpened\":" << boolean(normal_controls)
           << ",\"sourceLinkIndicatorObserved\":" << boolean(source_link_observed)
           << ",\"optionsZeroRejectedKnownLink\":" << boolean(options_zero_rejected_link)
           << ",\"openLinkReturnedSourceIndicator\":" << boolean(open_link_observed)
           << ",\"openLinkRejectedKnownLink\":" << boolean(open_link_rejected) << ",\"steps\":[";
    for (std::size_t index = 0; index < steps.size(); ++index) {
        if (index)
            output << ',';
        writeStep(output, steps[index]);
    }
    output << "],\"resources\":{\"opened\":" << ledger.opened << ",\"closed\":" << ledger.closed
           << ",\"allClosed\":" << boolean(closed)
           << ",\"predefinedClosed\":false,\"unexpectedPredefined\":" << boolean(ledger.unexpected_predefined)
           << ",\"unexpectedHandle\":" << boolean(ledger.unexpected_handle)
           << ",\"overflow\":" << boolean(ledger.overflow) << ",\"closeResults\":[";
    for (std::size_t index = 0; index < ledger.opened; ++index) {
        if (index)
            output << ',';
        output << "{\"attempted\":" << boolean(ledger.keys[index].close_attempted)
               << ",\"error\":" << ledger.keys[index].close_error << '}';
    }
    output << "]}}\n";
    return emit(output.str(), available ? 0 : 1);
}
} // namespace

int main(const int argc, char** argv) {
    // The self-test entry performs no Registry calls and validates CLI
    // admission independently of the manual read-only Windows diagnostic.
    if (argc == 2 && argv && argv[1] && std::string_view(argv[1]) == "--self-test")
        return parserSelfTest();
    if (parse(argc, argv) != Mode::ReadOnly)
        return emit("{\"schema\":1,\"readOnly\":true,\"registryCalls\":0,\"error\":\"invalid-arguments\"}\n", 2);
    return readonlyPreflight();
}
