#include "AppContainerRegistryLocationQuery.hpp"

#include <QTest>

#include <algorithm>
#include <array>
#include <cstring>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

using Query = music_player::sandbox_probe::AppContainerRegistryLocationQuery;
namespace {
constexpr std::uint32_t buffer_too_small = 0xc0000023U;
constexpr Query::Handle key = 17;
std::array<std::uint8_t, 40> sid() {
    std::array<std::uint8_t, 40> value{};
    value[0] = 1;
    value[1] = 8;
    value[7] = 15;
    value[8] = 2;
    for (std::size_t index = 0; index < 7; ++index)
        value[12 + 4 * index] = static_cast<std::uint8_t>(index + 1);
    return value;
}
Query::Token validToken() {
    Query::Token token;
    token.complete = token.primary = token.app_container = true;
    const auto expected = sid();
    token.package_sid.assign(expected.begin(), expected.end());
    token.capability_count = 0;
    token.integrity_rid = Query::low_integrity_rid;
    token.token_error = token.failed_information_class = token.token_close_error = 0;
    token.thread_error = Query::no_thread_token;
    token.token_query_called = token.token_close_called = token.thread_called = true;
    return token;
}
std::vector<std::uint8_t> nameInformation(const std::u16string& name) {
    const auto count = static_cast<std::uint32_t>(2 * name.size());
    std::vector<std::uint8_t> value(4 + count);
    for (unsigned index = 0; index < 4; ++index)
        value[index] = static_cast<std::uint8_t>(count >> (8 * index));
    for (std::size_t index = 0; index < name.size(); ++index) {
        value[4 + 2 * index] = static_cast<std::uint8_t>(name[index]);
        value[5 + 2 * index] = static_cast<std::uint8_t>(name[index] >> 8);
    }
    return value;
}
struct State final {
    Query::Token token{validToken()};
    Query::LocationResult location{0, key, false};
    std::u16string name{u"\\REGISTRY\\USER\\fixture\\AppContainer\\Storage"};
    std::vector<std::string> events;
    std::uint32_t access{}, close_error{};
    std::uint32_t size_status{buffer_too_small}, size_win32{122};
    std::optional<std::uint32_t> size_bytes{};
    std::function<void()> on_capture{};
    bool throw_location{}, throw_size{}, throw_read{}, throw_close{};
};
Query::Calls calls(const std::shared_ptr<State>& state) {
    return {.capture_token =
                [state] {
                    state->events.emplace_back("token");
                    if (state->on_capture)
                        state->on_capture();
                    return state->token;
                },
            .get_location =
                [state](const std::uint32_t access) {
                    state->events.emplace_back("location");
                    state->access = access;
                    if (state->throw_location)
                        throw std::runtime_error("location output unknown");
                    return state->location;
                },
            .query_name =
                [state](const Query::Handle handle, const std::span<std::uint8_t> output) {
                    if (handle != key)
                        throw std::runtime_error("unexpected opaque key");
                    const auto bytes = nameInformation(state->name);
                    if (output.empty()) {
                        state->events.emplace_back("size");
                        if (state->throw_size)
                            throw std::runtime_error("size unknown");
                        return Query::NameResult{state->size_status, state->size_win32,
                                                 state->size_bytes.value_or(static_cast<std::uint32_t>(bytes.size()))};
                    }
                    state->events.emplace_back("read");
                    if (state->throw_read)
                        throw std::runtime_error("read unknown");
                    if (output.size() != bytes.size())
                        throw std::runtime_error("unbounded or incorrect buffer");
                    std::copy(bytes.begin(), bytes.end(), output.begin());
                    return Query::NameResult{0, 0, static_cast<std::uint32_t>(bytes.size())};
                },
            .close_key =
                [state](const Query::Handle handle) {
                    if (handle != key)
                        throw std::runtime_error("unowned close");
                    state->events.emplace_back("close");
                    if (state->throw_close)
                        throw std::runtime_error("close unknown");
                    return state->close_error;
                }};
}
} // namespace

class AppContainerRegistryLocationQueryTests final : public QObject {
    Q_OBJECT
  private slots:
    void oneReadOnlyLocationSizeReadAndExplicitCloseCaptureACountedName() {
        const auto state = std::make_shared<State>();
        const auto result = Query::execute(sid(), calls(state));
        QCOMPARE(result.outcome, Query::Outcome::CapturedName);
        QCOMPARE(result.reason, Query::Reason::None);
        QCOMPARE(result.native_name, state->name);
        QCOMPARE(state->access, Query::key_read);
        QCOMPARE(state->events, (std::vector<std::string>{"token", "location", "size", "read", "close"}));
        QVERIFY(result.token_called && result.token_returned && result.location_called && result.location_returned);
        QVERIFY(result.size_called && result.size_returned && result.read_called && result.read_returned);
        QVERIFY(result.close_called && result.close_returned);
        QCOMPARE(result.close_error, 0U);
        QCOMPARE(result.size_query.ntstatus, buffer_too_small);
        QCOMPARE(result.size_query.win32_error, 122U);
    }
    void anUnqualifiedTokenIsRefusedBeforeTheContextualApi() {
        const auto state = std::make_shared<State>();
        state->token.capability_count = 1;
        const auto result = Query::execute(sid(), calls(state));
        QCOMPARE(result.outcome, Query::Outcome::RefusedToken);
        QCOMPARE(state->events, (std::vector<std::string>{"token"}));
        QVERIFY(result.token_called && result.token_returned);
        QVERIFY(!result.location_called && !result.close_called);
        QVERIFY(result.native_name.empty());
    }
    void nameQueryFailureStillClosesTheTransferredKeyExactlyOnce() {
        const auto state = std::make_shared<State>();
        state->size_status = 0xc0000022U;
        state->size_win32 = 5;
        const auto result = Query::execute(sid(), calls(state));
        QCOMPARE(result.outcome, Query::Outcome::NativeApiError);
        QCOMPARE(state->events, (std::vector<std::string>{"token", "location", "size", "close"}));
        QVERIFY(result.size_called && result.size_returned && !result.read_called);
        QVERIFY(result.close_called && result.close_returned);
        QCOMPARE(result.size_query.ntstatus, 0xc0000022U);
        QCOMPARE(result.size_query.win32_error, 5U);
        QCOMPARE(result.close_error, 0U);
    }
    void anUncertainApiOutputIsNotQueriedOrClosed() {
        const auto state = std::make_shared<State>();
        state->location = {0x80070005U, 0, true};
        const auto result = Query::execute(sid(), calls(state));
        QCOMPARE(result.outcome, Query::Outcome::CloseUnknown);
        QCOMPARE(state->events, (std::vector<std::string>{"token", "location"}));
        QVERIFY(result.location_returned);
        QCOMPARE(result.location.hresult, 0x80070005U);
        QVERIFY(result.location.uncertain_output);
        QVERIFY(!result.size_called && !result.close_called);
    }
    void callbackThrowsKeepAttemptedAndReturnedFactsSeparate_data() {
        QTest::addColumn<QString>("phase");
        for (const auto* phase : {"location", "size", "read", "close"})
            QTest::newRow(phase) << QString::fromLatin1(phase);
    }
    void callbackThrowsKeepAttemptedAndReturnedFactsSeparate() {
        QFETCH(QString, phase);
        const auto state = std::make_shared<State>();
        state->throw_location = phase == "location";
        state->throw_size = phase == "size";
        state->throw_read = phase == "read";
        state->throw_close = phase == "close";
        const auto result = Query::execute(sid(), calls(state));
        QVERIFY(result.location_called);
        QCOMPARE(result.location_returned, !state->throw_location);
        if (state->throw_location) {
            QCOMPARE(result.outcome, Query::Outcome::CloseUnknown);
            QVERIFY(!result.size_called && !result.close_called);
        } else {
            QVERIFY(result.size_called);
            QCOMPARE(result.size_returned, !state->throw_size);
            QCOMPARE(result.read_called, !state->throw_size);
            QCOMPARE(result.read_returned, !state->throw_size && !state->throw_read);
            QVERIFY(result.close_called);
            QCOMPARE(result.close_returned, !state->throw_close);
            QCOMPARE(result.outcome,
                     state->throw_close ? Query::Outcome::CloseUnknown : Query::Outcome::NativeApiError);
            QCOMPARE(std::count(state->events.begin(), state->events.end(), "close"), 1);
        }
    }
    void closeFailurePreservesTheAlreadyCapturedNameWithoutAcknowledgingClosure_data() {
        QTest::addColumn<QString>("mode");
        for (const auto* mode : {"error-five", "raw-ffffffff", "throw"})
            QTest::newRow(mode) << QString::fromLatin1(mode);
    }
    void closeFailurePreservesTheAlreadyCapturedNameWithoutAcknowledgingClosure() {
        QFETCH(QString, mode);
        const auto state = std::make_shared<State>();
        state->throw_close = mode == "throw";
        state->close_error = mode == "raw-ffffffff" ? 0xffffffffU : 5;
        const auto result = Query::execute(sid(), calls(state));
        QCOMPARE(result.outcome, Query::Outcome::CloseUnknown);
        QCOMPARE(result.native_name, state->name);
        QVERIFY(result.read_called && result.read_returned && result.close_called);
        QCOMPARE(result.close_returned, !state->throw_close);
        if (result.close_returned)
            QCOMPARE(result.close_error, state->close_error);
        QCOMPARE(std::count(state->events.begin(), state->events.end(), "close"), 1);
    }
    void expectedSidIsOwnedBeforeACallbackCanChangeTheCallersBuffer() {
        const auto state = std::make_shared<State>();
        auto expected = sid();
        state->token.package_sid[12] ^= 1;
        state->on_capture = [&] { expected[12] ^= 1; };
        const auto result = Query::execute(expected, calls(state));
        QCOMPARE(result.outcome, Query::Outcome::RefusedToken);
        QCOMPARE(state->events, (std::vector<std::string>{"token"}));
        QVERIFY(result.token_returned);
        QVERIFY(!result.location_called);
    }
    void eachRequiredTokenPropertyGuardsTheContextualApi_data() {
        QTest::addColumn<QString>("property");
        for (const auto* property : {"complete", "primary", "app-container", "elevated", "ui-access", "integrity",
                                     "query-error", "query-uncalled", "process-close-error", "process-close-uncalled",
                                     "thread-error", "thread-uncalled", "thread-close-present", "sid"})
            QTest::newRow(property) << QString::fromLatin1(property);
    }
    void eachRequiredTokenPropertyGuardsTheContextualApi() {
        QFETCH(QString, property);
        const auto state = std::make_shared<State>();
        auto& token = state->token;
        if (property == "complete")
            token.complete = false;
        else if (property == "primary")
            token.primary = false;
        else if (property == "app-container")
            token.app_container = false;
        else if (property == "elevated")
            token.elevated = true;
        else if (property == "ui-access")
            token.ui_access = true;
        else if (property == "integrity")
            token.integrity_rid = 0x2000;
        else if (property == "query-error")
            token.token_error = 5;
        else if (property == "query-uncalled")
            token.token_query_called = false;
        else if (property == "process-close-error")
            token.token_close_error = 5;
        else if (property == "process-close-uncalled")
            token.token_close_called = false;
        else if (property == "thread-error")
            token.thread_error = 0;
        else if (property == "thread-uncalled")
            token.thread_called = false;
        else if (property == "thread-close-present")
            token.thread_close_called = true;
        else
            token.package_sid[12] ^= 1;
        const auto result = Query::execute(sid(), calls(state));
        QCOMPARE(result.outcome, Query::Outcome::RefusedToken);
        QCOMPARE(state->events, (std::vector<std::string>{"token"}));
        QVERIFY(!result.location_called && !result.close_called);
    }
    void sizingBoundsRefuseBeforeTheDataRead_data() {
        QTest::addColumn<quint32>("bytes");
        QTest::newRow("zero") << quint32{0};
        QTest::newRow("short") << quint32{4};
        QTest::newRow("odd") << quint32{7};
        QTest::newRow("too-large") << quint32{Query::maximum_query_bytes + 2};
    }
    void sizingBoundsRefuseBeforeTheDataRead() {
        QFETCH(quint32, bytes);
        const auto state = std::make_shared<State>();
        state->size_bytes = bytes;
        const auto result = Query::execute(sid(), calls(state));
        QCOMPARE(result.outcome, Query::Outcome::NativeApiError);
        QCOMPARE(result.reason, Query::Reason::Bounds);
        QVERIFY(result.size_returned && !result.read_called && result.close_returned);
        QCOMPARE(state->events, (std::vector<std::string>{"token", "location", "size", "close"}));
    }
    void unicodeAndTheInclusiveCountedNameBoundAreAccepted_data() {
        QTest::addColumn<bool>("maximum");
        QTest::newRow("unicode") << false;
        QTest::newRow("maximum") << true;
    }
    void unicodeAndTheInclusiveCountedNameBoundAreAccepted() {
        QFETCH(bool, maximum);
        const auto state = std::make_shared<State>();
        state->name = maximum ? std::u16string(Query::maximum_name_units, u'x')
                              : std::u16string{u"\\REGISTRY\\USER\\音-\U0001f3b5"};
        state->size_status = 0x80000005U;
        state->size_win32 = 234;
        const auto result = Query::execute(sid(), calls(state));
        QCOMPARE(result.outcome, Query::Outcome::CapturedName);
        QCOMPARE(result.native_name, state->name);
        QCOMPARE(result.read_query.bytes, 4U + 2U * static_cast<std::uint32_t>(state->name.size()));
        QCOMPARE(result.close_error, 0U);
    }
};
QTEST_GUILESS_MAIN(AppContainerRegistryLocationQueryTests)
#include "AppContainerRegistryLocationQueryTests.moc"
