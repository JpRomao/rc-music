// Separate diagnostic helper. CTest executes only the pure --self-test mode.
#include "AppContainerRegistryLocationNative.hpp"
#include "AppContainerRegistryLocationProtocol.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace {
namespace Protocol = music_player::sandbox_registry_location;
using Query = music_player::sandbox_probe::AppContainerRegistryLocationQuery;
enum class Mode { Refused, Help, SelfTest, Query };
Mode parseMode(const int argc, const wchar_t* const* argv) noexcept {
    if (argc != 2 || !argv || !argv[1])
        return Mode::Refused;
    const std::wstring_view argument{argv[1]};
    if (argument == L"--self-test")
        return Mode::SelfTest;
    if (argument == L"--help")
        return Mode::Help;
    if (argument == L"--query-context-registry")
        return Mode::Query;
    return Mode::Refused;
}
std::vector<std::uint8_t> exchange(const std::span<const std::uint8_t> bytes, const Query::Calls& calls) {
    const auto request = Protocol::decodeRequest(bytes);
    if (!request)
        return {};
    auto result = Query::execute(request->package_sid, calls);
    Protocol::Reply reply;
    reply.nonce = request->nonce;
    switch (result.outcome) {
    case Query::Outcome::RefusedToken:
        reply.outcome = Protocol::Outcome::RefusedToken;
        break;
    case Query::Outcome::NativeApiError:
        reply.outcome = Protocol::Outcome::NativeApiError;
        break;
    case Query::Outcome::CapturedName:
        reply.outcome = Protocol::Outcome::CapturedName;
        break;
    case Query::Outcome::CloseUnknown:
        reply.outcome = Protocol::Outcome::CloseUnknown;
        break;
    }
    if (result.token_returned) {
        const auto& token = result.token;
        if (token.package_sid.size() == request->package_sid.size()) {
            Protocol::ProfileSid sid{};
            std::copy(token.package_sid.begin(), token.package_sid.end(), sid.begin());
            // Reuse the pure canonical-SID check; a malformed diagnostic SID
            // cannot turn the refusal itself into a malformed reply.
            auto check = *request;
            check.package_sid = sid;
            if (!Protocol::encodeRequest(check).empty())
                reply.session_package_sid = sid;
        }
        if (token.token_query_called) {
            reply.token_errors.token_query_error = token.token_error;
            reply.token_errors.failed_information_class = token.failed_information_class;
        }
        if (token.token_close_called)
            reply.token_errors.process_token_close = token.token_close_error;
        if (token.thread_called)
            reply.token_errors.thread_token_open = token.thread_error;
        if (token.thread_close_called)
            reply.token_errors.thread_token_close = token.thread_close_error;
    }
    if (result.location_returned)
        reply.registry_location_hresult = result.location.hresult;
    if (result.size_returned) {
        reply.name_size_status = result.size_query.ntstatus;
        reply.name_size_win32 = result.size_query.win32_error;
        reply.name_size_required = result.size_query.bytes;
    }
    if (result.read_returned) {
        reply.name_read_status = result.read_query.ntstatus;
        reply.name_read_win32 = result.read_query.win32_error;
        reply.name_read_required = result.read_query.bytes;
    }
    if (result.close_returned)
        reply.key_close_error = result.close_error;
    reply.uncertain_output = result.location.uncertain_output || (result.location_called && !result.location_returned);
    reply.native_name = std::move(result.native_name);
    return Protocol::encodeReply(reply);
}
struct Fake final {
    Protocol::Request request{};
    Query::Token token{};
    Query::LocationResult location{0, 1, false};
    std::uint32_t close_error{};
    unsigned captures{}, opens{}, sizes{}, reads{}, closes{};
    bool location_throws{}, size_throws{}, close_throws{};
    std::u16string name{u"\\REGISTRY\\USER\\fixture"};
    Fake() {
        constexpr std::array<std::uint8_t, 12> prefix{1, 8, 0, 0, 0, 0, 0, 15, 2, 0, 0, 0};
        std::copy(prefix.begin(), prefix.end(), request.package_sid.begin());
        request.nonce.fill(9);
        token.complete = token.primary = token.app_container = true;
        token.package_sid.assign(request.package_sid.begin(), request.package_sid.end());
        token.capability_count = 0;
        token.integrity_rid = Query::low_integrity_rid;
        token.token_error = token.failed_information_class = token.token_close_error = 0;
        token.token_query_called = token.token_close_called = token.thread_called = true;
        token.thread_error = Query::no_thread_token;
    }
    Query::Calls calls() {
        return {[this] {
                    ++captures;
                    return token;
                },
                [this](const auto) {
                    ++opens;
                    if (location_throws)
                        throw std::runtime_error("location");
                    return location;
                },
                [this](const auto, const auto output) -> Query::NameResult {
                    const auto bytes = static_cast<std::uint32_t>(2 * name.size());
                    if (output.empty()) {
                        ++sizes;
                        if (size_throws)
                            throw std::runtime_error("size");
                        return {0xc0000023, 122, 4 + bytes};
                    }
                    ++reads;
                    if (output.size() < 4 + bytes)
                        throw std::runtime_error("bounded output");
                    std::memcpy(output.data(), &bytes, 4);
                    std::memcpy(output.data() + 4, name.data(), bytes);
                    return {0, 0, 4 + bytes};
                },
                [this](const auto) {
                    ++closes;
                    if (close_throws)
                        throw std::runtime_error("close");
                    return close_error;
                }};
    }
};
bool exchangeCase(const unsigned which) {
    Fake fake;
    if (which == 1)
        fake.token.capability_count = 1;
    if (which == 2)
        fake.location = {0x80070005, 0, false};
    if (which == 3)
        fake.close_error = 0xffffffff;
    if (which == 4)
        fake.location = {0x80070005, 0, true};
    fake.location_throws = which == 5;
    fake.size_throws = which == 6;
    fake.close_throws = which == 7;
    const auto calls = fake.calls();
    const auto bytes = exchange(Protocol::encodeRequest(fake.request), calls);
    const auto decoded = Protocol::decodeReply(bytes);
    if (!decoded || !Protocol::matchesRequest(fake.request, *decoded))
        return false;
    if (which == 0)
        return decoded->outcome == Protocol::Outcome::CapturedName && decoded->native_name == fake.name &&
               decoded->name_size_status == 0xc0000023U && decoded->key_close_error == 0U && fake.closes == 1;
    if (which == 1)
        return decoded->outcome == Protocol::Outcome::RefusedToken && !decoded->registry_location_hresult &&
               fake.opens == 0;
    if (which == 2)
        return decoded->outcome == Protocol::Outcome::NativeApiError &&
               decoded->registry_location_hresult == 0x80070005U && fake.closes == 0;
    if (which == 3)
        return decoded->outcome == Protocol::Outcome::CloseUnknown && decoded->key_close_error == 0xffffffffU &&
               decoded->native_name == fake.name && fake.closes == 1;
    if (which == 4)
        return decoded->outcome == Protocol::Outcome::CloseUnknown && decoded->uncertain_output && fake.closes == 0;
    if (which == 5)
        return decoded->outcome == Protocol::Outcome::CloseUnknown && decoded->uncertain_output &&
               !decoded->registry_location_hresult && fake.closes == 0;
    if (which == 6)
        return decoded->outcome == Protocol::Outcome::NativeApiError && !decoded->name_size_status &&
               decoded->key_close_error == 0U && fake.closes == 1;
    return decoded->outcome == Protocol::Outcome::CloseUnknown && !decoded->key_close_error &&
           decoded->native_name == fake.name && fake.closes == 1;
}
int selfTest() {
    struct Case final {
        int count;
        std::array<const wchar_t*, 4> arguments;
        Mode mode;
    };
    constexpr std::array cases{
        Case{2, {L"helper", L"--self-test"}, Mode::SelfTest},
        Case{2, {L"helper", L"--help"}, Mode::Help},
        Case{2, {L"helper", L"--query-context-registry"}, Mode::Query},
        Case{1, {L"helper"}, Mode::Refused},
        Case{2, {L"helper", L"--readonly"}, Mode::Refused},
        Case{2, {L"helper", L"--allow-profile-changes"}, Mode::Refused},
        Case{2, {L"helper", L"--query-context-registry "}, Mode::Refused},
        Case{2, {L"helper", L"--QUERY-context-registry"}, Mode::Refused},
        Case{3, {L"helper", L"--query-context-registry", L"HKCU"}, Mode::Refused},
        Case{3, {L"helper", L"--self-test", L"--query-context-registry"}, Mode::Refused},
        Case{2, {L"helper", nullptr}, Mode::Refused},
        Case{2, {L"helper", L""}, Mode::Refused},
    };
    unsigned passed{}, failed{};
    for (const auto& test : cases) {
        if (parseMode(test.count, test.arguments.data()) == test.mode)
            ++passed;
        else
            ++failed;
    }
    if (parseMode(2, nullptr) != Mode::Refused)
        ++failed;
    else
        ++passed;
    for (unsigned which = 0; which != 8; ++which) {
        if (exchangeCase(which))
            ++passed;
        else
            ++failed;
    }
    Fake malformed;
    const auto malformed_calls = malformed.calls();
    if (exchange(std::array<std::uint8_t, 1>{0}, malformed_calls).empty() && malformed.captures == 0)
        ++passed;
    else
        ++failed;
    // These three rejected entries return before a Windows API. The native
    // binding itself is linked, while its token/context-query paths stay idle.
    const auto inert = music_player::sandbox_probe::appContainerRegistryLocationNativeCalls();
    const auto denied_location = inert.get_location(0);
    if (denied_location.hresult == 0x80070057U && denied_location.owned_key == 0 && !denied_location.uncertain_output)
        ++passed;
    else
        ++failed;
    const auto denied_name = inert.query_name(1, {});
    if (denied_name.ntstatus == 0xc0000008U && denied_name.win32_error == 6 && denied_name.bytes == 0)
        ++passed;
    else
        ++failed;
    if (inert.close_key(1) == 6)
        ++passed;
    else
        ++failed;
    const int written = std::printf("{\"passed\":%u,\"failed\":%u,\"nativeCalls\":0}\n", passed, failed);
    return failed || written < 0 || std::fflush(stdout) != 0 ? 1 : 0;
}
int queryContext() {
    const auto input = GetStdHandle(STD_INPUT_HANDLE);
    const auto output = GetStdHandle(STD_OUTPUT_HANDLE);
    if (GetFileType(input) != FILE_TYPE_PIPE || GetFileType(output) != FILE_TYPE_PIPE)
        return 2;
    std::array<std::uint8_t, Protocol::request_frame_bytes + 1> request_bytes{};
    std::size_t used{};
    while (used < request_bytes.size()) {
        DWORD count{};
        if (!ReadFile(input, request_bytes.data() + used, static_cast<DWORD>(request_bytes.size() - used), &count,
                      nullptr)) {
            if (GetLastError() == ERROR_BROKEN_PIPE)
                break;
            return 2;
        }
        if (!count)
            break;
        if (count > request_bytes.size() - used)
            return 2;
        used += count;
    }
    const std::span<const std::uint8_t> request{request_bytes.data(), used};
    if (!Protocol::decodeRequest(request))
        return 2;
    // Factory binds only. The core checks the current primary token and lack
    // of thread token before GetAppContainerRegistryLocation may be called.
    const auto calls = music_player::sandbox_probe::appContainerRegistryLocationNativeCalls();
    const auto response = exchange(request, calls);
    if (response.empty())
        return 2;
    std::size_t sent{};
    while (sent < response.size()) {
        DWORD count{};
        if (!WriteFile(output, response.data() + sent, static_cast<DWORD>(response.size() - sent), &count, nullptr) ||
            !count || count > response.size() - sent)
            return 2;
        sent += count;
    }
    const auto decoded = Protocol::decodeReply(response);
    return decoded && decoded->outcome == Protocol::Outcome::CapturedName ? 0 : 1;
}
} // namespace
int wmain(const int argc, const wchar_t* const* argv) {
    try {
        switch (parseMode(argc, argv)) {
        case Mode::SelfTest:
            return selfTest();
        case Mode::Query:
            return queryContext();
        case Mode::Help:
            return std::puts("Test-only helper: --self-test or --query-context-registry (binary pipes).\n"
                             "Context query requires a separately authorized AppContainer launcher.\n"
                             "The Windows context API may initialize storage; no read-only guarantee is made.") < 0 ||
                           std::fflush(stdout) != 0
                       ? 1
                       : 0;
        case Mode::Refused:
            return 2;
        }
    } catch (...) {
        // Process/pipes termination is still the launcher's responsibility;
        // an absent reply does not authorize profile cleanup.
        return 2;
    }
    return 2;
}
