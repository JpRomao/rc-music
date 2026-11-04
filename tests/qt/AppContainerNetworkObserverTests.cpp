#include "../windows/sandbox/AppContainerNetworkObserver.hpp"

#include <QTest>

#include <algorithm>
#include <stdexcept>
#include <utility>

using Observer = music_player::sandbox_probe::AppContainerNetworkObserver;
namespace {
Observer::Bytes packageSid() {
    Observer::Bytes value(40, 0);
    value[0] = 1;
    value[1] = 8;
    value[7] = 15;
    value[8] = 2;
    value[12] = 7;
    return value;
}
Observer::Bytes userSid() { return {1, 2, 0, 0, 0, 0, 0, 5, 21, 0, 0, 0, 42, 0, 0, 0}; }
Observer::Config configuration() {
    return {L"C:/owned private/runtime/worker.exe", packageSid(), userSid(), 12345, 100'000'000, 150'000'000};
}
struct Fake final {
    std::vector<std::string> log;
    Observer::AppIdResult application{0, {'w', 0, 'o', 0, 'r', 0, 'k', 0, 0, 0}};
    Observer::OpenResult engine{0, 11};
    Observer::OptionResult collection{0, true, 1}, keywords{0, true, Observer::capability_drop_keyword};
    Observer::SubscribeResult subscription{0, 22};
    std::uint32_t unsubscribe_error{}, close_error{};
    Observer::Callback callback{};
    void* context{};
    std::function<void()> during_subscribe{}, during_unsubscribe{};
    bool throw_unsubscribe{false};
    Observer::Bytes filter_app;
    Observer::SubscriptionFilter filter{};
    Observer::FilterResult filter_result{0,
                                         true,
                                         123,
                                         false,
                                         Observer::FilterAction::Block,
                                         Observer::ProviderIdentity::AppIsolation,
                                         Observer::SublayerIdentity::AppIsolation,
                                         Observer::LayerIdentity::AleAuthConnectV4};
    Observer::LayerResult layer_result{0, true, 48, Observer::LayerIdentity::AleAuthConnectV4};
    Observer::Handle filter_engine{}, layer_engine{};
    std::uint64_t queried_filter{};
    std::uint16_t queried_layer{};
    bool throw_filter{false}, throw_layer{false};
    std::function<void()> during_filter{}, during_layer{};
    void deliver(const Observer::EventView& value) {
        if (callback)
            callback(context, value);
    }
};
Observer::Calls calls(const std::shared_ptr<Fake>& fake) {
    return {
        .app_id =
            [fake](std::wstring_view) {
                fake->log.push_back("app-id");
                return fake->application;
            },
        .open =
            [fake] {
                fake->log.push_back("open");
                return fake->engine;
            },
        .get_collection =
            [fake](Observer::Handle) {
                fake->log.push_back("collection");
                return fake->collection;
            },
        .get_keywords =
            [fake](Observer::Handle) {
                fake->log.push_back("keywords");
                return fake->keywords;
            },
        .subscribe =
            [fake](Observer::Handle, const Observer::SubscriptionFilter& filter, Observer::Callback cb, void* context) {
                fake->log.push_back("subscribe");
                fake->filter_app.assign(filter.app_id.begin(), filter.app_id.end());
                fake->filter = filter;
                fake->filter.app_id = fake->filter_app;
                fake->callback = cb;
                fake->context = context;
                if (fake->during_subscribe)
                    fake->during_subscribe();
                return fake->subscription;
            },
        .unsubscribe =
            [fake](Observer::Handle, Observer::Handle) {
                fake->log.push_back("unsubscribe");
                if (fake->throw_unsubscribe)
                    throw std::runtime_error("unsubscribe uncertain");
                if (fake->during_unsubscribe)
                    fake->during_unsubscribe();
                if (fake->unsubscribe_error == 0) {
                    fake->callback = nullptr;
                    fake->context = nullptr;
                }
                return fake->unsubscribe_error;
            },
        .close =
            [fake](Observer::Handle) {
                fake->log.push_back("close");
                return fake->close_error;
            },
        .get_filter =
            [fake](Observer::Handle engine, std::uint64_t id) {
                fake->log.push_back("filter");
                fake->filter_engine = engine;
                fake->queried_filter = id;
                if (fake->during_filter)
                    fake->during_filter();
                if (fake->throw_filter)
                    throw std::runtime_error("filter query");
                return fake->filter_result;
            },
        .get_layer =
            [fake](Observer::Handle engine, std::uint16_t id) {
                fake->log.push_back("layer");
                fake->layer_engine = engine;
                fake->queried_layer = id;
                if (fake->during_layer)
                    fake->during_layer();
                if (fake->throw_layer)
                    throw std::runtime_error("layer query");
                return fake->layer_result;
            }};
}
struct Harness final {
    Observer::Config config{configuration()};
    std::shared_ptr<Fake> fake{std::make_shared<Fake>()};
    std::unique_ptr<Observer> observer;
    void construct() { observer = std::make_unique<Observer>(config, calls(fake)); }
    Observer::EventView event() const {
        return {Observer::EventKind::CapabilityDrop,
                Observer::required_fields,
                120'000'000,
                4,
                Observer::loopback_ipv4,
                6,
                config.remote_port,
                fake->application.bytes,
                config.user_sid,
                config.package_sid,
                0,
                123,
                1};
    }
    void classify() { config.evidence_kind = Observer::EvidenceKind::ClassifyIsolation; }
    Observer::EventView classifyEvent() const {
        auto result = event();
        result.kind = Observer::EventKind::ClassifyDrop;
        result.capability_id = Observer::not_called;
        result.layer_id = 48;
        result.ms_direction = Observer::classify_outbound_direction;
        return result;
    }
};
} // namespace

class AppContainerNetworkObserverTests final : public QObject {
    Q_OBJECT
  private slots:
    void constructionAndIdleDestructionAreInert() {
        Harness h;
        h.construct();
        QCOMPARE(h.observer->snapshot().state, Observer::State::Idle);
        QVERIFY(!h.observer->snapshot().matched);
        h.observer.reset();
        QVERIFY(h.fake->log.empty());
    }
    void preflightQueriesCollectionAndCapabilityDropKeywordWithoutSubscription() {
        auto fake = std::make_shared<Fake>();
        auto injected = calls(fake);
        injected.app_id = {};
        injected.subscribe = {};
        injected.unsubscribe = {};
        const auto report = Observer::preflight(std::move(injected));
        QVERIFY(report.available);
        QCOMPARE(report.open_error, 0U);
        QCOMPARE(report.query_error, 0U);
        QCOMPARE(report.keyword_query_error, 0U);
        QCOMPARE(report.close_error, 0U);
        QVERIFY(report.value_is_uint32 && report.keywords_is_uint32);
        QCOMPARE(report.collection_value, 1U);
        QCOMPARE(report.keywords_value, Observer::capability_drop_keyword);
        QVERIFY((fake->log == std::vector<std::string>{"open", "collection", "keywords", "close"}));
    }
    void preflightPreservesAccessDeniedDisabledTypeAndCloseFailures_data() {
        QTest::addColumn<int>("kind");
        for (int i = 0; i < 8; ++i)
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void preflightPreservesAccessDeniedDisabledTypeAndCloseFailures() {
        QFETCH(int, kind);
        auto fake = std::make_shared<Fake>();
        if (kind == 0)
            fake->engine = {5, 0};
        if (kind == 1)
            fake->collection.error = 5;
        if (kind == 2)
            fake->collection.is_uint32 = false;
        if (kind == 3)
            fake->collection.value = 0;
        if (kind == 4)
            fake->keywords.error = 5;
        if (kind == 5)
            fake->keywords.is_uint32 = false;
        if (kind == 6)
            fake->keywords.value = 0;
        if (kind == 7)
            fake->close_error = 5;
        const auto report = Observer::preflight(calls(fake));
        QVERIFY(!report.available);
        QVERIFY(report.reason != Observer::Reason::None);
        QVERIFY(std::find(fake->log.begin(), fake->log.end(), "subscribe") == fake->log.end());
        if (kind == 0) {
            QCOMPARE(report.open_error, 5U);
            QCOMPARE(report.close_error, Observer::not_called);
        }
        if (kind == 1)
            QCOMPARE(report.query_error, 5U);
        if (kind == 4)
            QCOMPARE(report.keyword_query_error, 5U);
        if (kind == 7) {
            QCOMPARE(report.close_error, 5U);
            QVERIFY(report.context_retained);
        }
    }
    void startUsesTheExactAppIdAndOnlyDocumentedSubscriptionFilters() {
        Harness h;
        h.construct();
        const auto report = h.observer->start();
        QCOMPARE(report.state, Observer::State::Observing);
        QVERIFY(report.available && !report.matched);
        QVERIFY((h.fake->log == std::vector<std::string>{"app-id", "open", "collection", "keywords", "subscribe"}));
        QVERIFY(h.fake->filter_app == h.fake->application.bytes);
        QCOMPARE(h.fake->filter.remote_ipv4, Observer::loopback_ipv4);
        QCOMPARE(h.fake->filter.remote_port, h.config.remote_port);
        QCOMPARE(h.fake->filter.protocol, std::uint8_t{6});
        QCOMPARE(h.fake->filter.kind, Observer::EventKind::CapabilityDrop);
        const auto count = h.fake->log.size();
        QCOMPARE(h.observer->start().state, Observer::State::Observing);
        QCOMPARE(h.fake->log.size(), count);
        QCOMPARE(h.observer->stop().state, Observer::State::Stopped);
    }
    void invalidConfigurationIsRejectedBeforeAnyCall_data() {
        QTest::addColumn<int>("kind");
        for (int i = 0; i < 15; ++i)
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void invalidConfigurationIsRejectedBeforeAnyCall() {
        QFETCH(int, kind);
        Harness h;
        switch (kind) {
        case 0:
            h.config.executable = L"relative.exe";
            break;
        case 1:
            h.config.executable[3] = L'\0';
            break;
        case 2:
            h.config.executable = L"C:/" + std::wstring(32765, L'x');
            break;
        case 3:
            h.config.executable.push_back(static_cast<wchar_t>(0xd800));
            break;
        case 4:
            h.config.package_sid.resize(39);
            break;
        case 5:
            h.config.package_sid[0] = 2;
            break;
        case 6:
            h.config.package_sid[1] = 7;
            break;
        case 7:
            h.config.package_sid[7] = 5;
            break;
        case 8:
            h.config.package_sid[8] = 3;
            break;
        case 9:
            h.config.user_sid.clear();
            break;
        case 10:
            h.config.user_sid[0] = 2;
            break;
        case 11:
            h.config.user_sid[1] = 15;
            break;
        case 12:
            h.config.remote_port = 0;
            break;
        case 13:
            h.config.begin_filetime = h.config.end_filetime + 1;
            break;
        case 14:
            h.config.end_filetime = h.config.begin_filetime + Observer::maximum_window_ticks + 1;
            break;
        }
        h.construct();
        const auto report = h.observer->start();
        QCOMPARE(report.state, Observer::State::Unavailable);
        QCOMPARE(report.reason, Observer::Reason::InvalidConfiguration);
        QVERIFY(!report.available && !report.matched);
        QVERIFY(h.fake->log.empty());
    }
    void startPreservesNativeFailuresAndClosesOnlyItsOpenedEngine_data() {
        QTest::addColumn<int>("kind");
        for (int i = 0; i < 5; ++i)
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void startPreservesNativeFailuresAndClosesOnlyItsOpenedEngine() {
        QFETCH(int, kind);
        Harness h;
        if (kind == 0)
            h.fake->application.error = 5;
        if (kind == 1)
            h.fake->engine = {5, 0};
        if (kind == 2)
            h.fake->collection.error = 5;
        if (kind == 3)
            h.fake->keywords.error = 5;
        if (kind == 4)
            h.fake->subscription = {5, 0};
        h.construct();
        const auto report = h.observer->start();
        QCOMPARE(report.state, kind == 4 ? Observer::State::StopUnconfirmed : Observer::State::Unavailable);
        QVERIFY(!report.available && !report.matched);
        if (kind == 0)
            QCOMPARE(report.app_id_error, 5U);
        if (kind == 1)
            QCOMPARE(report.open_error, 5U);
        if (kind == 2)
            QCOMPARE(report.query_error, 5U);
        if (kind == 3)
            QCOMPARE(report.keyword_query_error, 5U);
        if (kind == 4)
            QCOMPARE(report.subscribe_error, 5U);
        QCOMPARE(std::count(h.fake->log.begin(), h.fake->log.end(), "close"), kind == 2 || kind == 3 ? 1 : 0);
        if (kind == 4)
            QVERIFY(report.context_retained);
    }
    void appIdBlobMustBeBoundedAndUtf16Aligned_data() {
        QTest::addColumn<int>("bytes");
        QTest::addColumn<bool>("valid");
        QTest::newRow("empty") << 0 << false;
        QTest::newRow("odd") << 3 << false;
        QTest::newRow("minimum") << 2 << true;
        QTest::newRow("inclusive") << 65536 << true;
        QTest::newRow("over") << 65538 << false;
    }
    void appIdBlobMustBeBoundedAndUtf16Aligned() {
        QFETCH(int, bytes);
        QFETCH(bool, valid);
        Harness h;
        h.fake->application.bytes.assign(static_cast<std::size_t>(bytes), 0);
        h.construct();
        const auto report = h.observer->start();
        QCOMPARE(report.available, valid);
        if (!valid) {
            QCOMPARE(report.reason, Observer::Reason::AppId);
            QVERIFY((h.fake->log == std::vector<std::string>{"app-id"}));
        }
    }
    void exactCapabilityDropIsAcceptedAtBothInclusiveTimeEdges_data() {
        QTest::addColumn<int>("capability");
        QTest::addColumn<bool>("last");
        for (int cap = 0; cap < 3; ++cap)
            for (bool last : {false, true})
                QTest::newRow(qPrintable(QString::number(cap) + (last ? "-last" : "-first"))) << cap << last;
    }
    void exactCapabilityDropIsAcceptedAtBothInclusiveTimeEdges() {
        QFETCH(int, capability);
        QFETCH(bool, last);
        Harness h;
        h.construct();
        QVERIFY(h.observer->start().available);
        auto event = h.event();
        event.capability_id = static_cast<std::uint32_t>(capability);
        event.timestamp = last ? h.config.end_filetime : h.config.begin_filetime;
        h.fake->deliver(event);
        const auto report = h.observer->snapshot();
        QVERIFY(report.matched && report.first_match);
        QCOMPARE(report.first_match->timestamp, event.timestamp);
        QCOMPARE(report.first_match->filter_id, event.filter_id);
        QCOMPARE(report.first_match->capability_id, event.capability_id);
    }
    void mismatchedOrIncompleteEventsNeverBecomeEvidence_data() {
        QTest::addColumn<int>("kind");
        for (int i = 0; i < 25; ++i)
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void mismatchedOrIncompleteEventsNeverBecomeEvidence() {
        QFETCH(int, kind);
        Harness h;
        h.construct();
        QVERIFY(h.observer->start().available);
        auto event = h.event();
        auto different_app = h.fake->application.bytes;
        auto different_user = h.config.user_sid;
        auto different_package = h.config.package_sid;
        if (kind < 7)
            event.fields &= ~(1U << kind);
        switch (kind) {
        case 7:
            event.kind = Observer::EventKind::ClassifyDrop;
            break;
        case 8:
            event.kind = Observer::EventKind::ClassifyAllow;
            break;
        case 9:
            event.kind = Observer::EventKind::CapabilityAllow;
            break;
        case 10:
            event.kind = Observer::EventKind::Other;
            break;
        case 11:
            different_app[0] ^= 1;
            event.app_id = different_app;
            break;
        case 12:
            different_user.back() ^= 1;
            event.user_sid = different_user;
            break;
        case 13:
            different_package.back() ^= 1;
            event.package_sid = different_package;
            break;
        case 14:
            event.timestamp = h.config.begin_filetime - 1;
            break;
        case 15:
            event.timestamp = h.config.end_filetime + 1;
            break;
        case 16:
            event.ip_version = 6;
            break;
        case 17:
            event.remote_ipv4 ^= 1;
            break;
        case 18:
            ++event.remote_port;
            break;
        case 19:
            event.protocol = 17;
            break;
        case 20:
            event.capability_id = 3;
            break;
        case 21:
            event.capability_id = Observer::not_called;
            break;
        case 22:
            event.filter_id = 0;
            break;
        case 23:
            event.is_loopback = 0;
            break;
        case 24:
            event.is_loopback = 2;
            break;
        }
        h.fake->deliver(event);
        QVERIFY(!h.observer->snapshot().matched);
        QVERIFY(!h.observer->snapshot().first_match);
    }
    void callbackDuringSubscribeIsSafeAndOnlyTheFirstMatchingMetadataIsRetained() {
        Harness h;
        h.fake->during_subscribe = [&] { h.fake->deliver(h.event()); };
        h.construct();
        QVERIFY(h.observer->start().matched);
        auto second = h.event();
        second.filter_id = 456;
        for (int i = 0; i < 100; ++i)
            h.fake->deliver(second);
        QCOMPARE(h.observer->snapshot().first_match->filter_id, std::uint64_t{123});
        h.fake->during_unsubscribe = [&] { h.fake->deliver(second); };
        const auto report = h.observer->stop();
        QCOMPARE(report.state, Observer::State::Stopped);
        QVERIFY(report.available && report.matched);
        QCOMPARE(report.first_match->filter_id, std::uint64_t{123});
        const auto count = h.fake->log.size();
        QCOMPARE(h.observer->stop().state, Observer::State::Stopped);
        h.observer.reset();
        QCOMPARE(h.fake->log.size(), count);
    }
    void failedUnsubscribePreservesCallbackContextAndNeverRetriesOnDestruction_data() {
        QTest::addColumn<bool>("throws");
        QTest::newRow("raw-access-denied") << false;
        QTest::newRow("exception") << true;
    }
    void failedSubscribeWithNoHandlePreservesThePublishedContextWithoutClosingOrRetrying() {
        Harness h;
        h.fake->subscription = {1726, 0}; // A failed RPC is not a callback-quiescence barrier.
        h.fake->during_subscribe = [&] { h.fake->deliver(h.event()); };
        h.construct();
        const auto event = h.event();
        const auto report = h.observer->start();
        // Stop here in Red before invoking any callback after destruction.
        QCOMPARE(report.state, Observer::State::StopUnconfirmed);
        QVERIFY(report.context_retained && !report.available && !report.matched && !report.first_match);
        QCOMPARE(report.reason, Observer::Reason::Subscribe);
        QCOMPARE(report.subscribe_error, 1726U);
        QCOMPARE(report.unsubscribe_error, Observer::not_called);
        QCOMPARE(report.close_error, Observer::not_called);
        h.fake->deliver(event);
        QVERIFY(!h.observer->snapshot().matched);
        QCOMPARE(h.observer->stop().state, Observer::State::StopUnconfirmed);
        const std::weak_ptr<Fake> preserved = h.fake;
        h.observer.reset();
        h.fake.reset();
        const auto retained = preserved.lock();
        QVERIFY(retained); // Calls/configuration lifetime is proved before dereferencing the late callback.
        retained->deliver(event);
        QCOMPARE(std::count(retained->log.begin(), retained->log.end(), "subscribe"), 1);
        QCOMPARE(std::count(retained->log.begin(), retained->log.end(), "unsubscribe"), 0);
        QCOMPARE(std::count(retained->log.begin(), retained->log.end(), "close"), 0);
    }
    void failedUnsubscribePreservesCallbackContextAndNeverRetriesOnDestruction() {
        QFETCH(bool, throws);
        Harness h;
        h.fake->unsubscribe_error = 5;
        h.fake->throw_unsubscribe = throws;
        h.construct();
        QVERIFY(h.observer->start().available);
        h.fake->deliver(h.event());
        QVERIFY(h.observer->snapshot().matched);
        const auto report = h.observer->stop();
        QCOMPARE(report.state, Observer::State::StopUnconfirmed);
        QVERIFY(report.context_retained && !report.matched);
        QCOMPARE(report.unsubscribe_error, throws ? 31U : 5U);
        QCOMPARE(report.close_error, Observer::not_called);
        h.fake->deliver(h.event());
        QVERIFY(!h.observer->snapshot().matched);
        h.observer.reset();
        h.fake->deliver(h.event()); // Context must remain alive, even after the wrapper is gone.
        QCOMPARE(std::count(h.fake->log.begin(), h.fake->log.end(), "unsubscribe"), 1);
        QCOMPARE(std::count(h.fake->log.begin(), h.fake->log.end(), "close"), 0);
    }
    void closeFailureRemainsUnconfirmedAndTheDestructorDoesNotRetry() {
        Harness h;
        h.fake->close_error = 5;
        h.construct();
        QVERIFY(h.observer->start().available);
        const auto report = h.observer->stop();
        QCOMPARE(report.state, Observer::State::StopUnconfirmed);
        QCOMPARE(report.unsubscribe_error, 0U);
        QCOMPARE(report.close_error, 5U);
        QVERIFY(report.context_retained && !report.matched);
        h.observer.reset();
        QCOMPARE(std::count(h.fake->log.begin(), h.fake->log.end(), "close"), 1);
    }
    void classifyPreflightRecordsKeywordsWithoutRequiringTheCapabilityBit_data() {
        QTest::addColumn<quint32>("keywords");
        for (const auto value : {0U, 2U, Observer::capability_drop_keyword, Observer::not_called})
            QTest::newRow(qPrintable(QString::number(value))) << value;
    }
    void classifyPreflightRecordsKeywordsWithoutRequiringTheCapabilityBit() {
        QFETCH(quint32, keywords);
        auto fake = std::make_shared<Fake>();
        fake->keywords.value = keywords;
        const auto report = Observer::preflight(calls(fake), Observer::EvidenceKind::ClassifyIsolation);
        QVERIFY(report.available);
        QCOMPARE(report.reason, Observer::Reason::None);
        QCOMPARE(report.evidence_kind, Observer::EvidenceKind::ClassifyIsolation);
        QCOMPARE(report.keywords_value, keywords);
        QCOMPARE(report.keyword_query_error, 0U);
        QVERIFY((fake->log == std::vector<std::string>{"open", "collection", "keywords", "close"}));
    }
    void classifyPreflightStillRejectsUnknownOrDisabledCollection_data() {
        QTest::addColumn<int>("kind");
        for (int i = 0; i < 6; ++i)
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void classifyPreflightStillRejectsUnknownOrDisabledCollection() {
        QFETCH(int, kind);
        auto fake = std::make_shared<Fake>();
        if (kind == 0)
            fake->collection.error = 5;
        if (kind == 1)
            fake->collection.is_uint32 = false;
        if (kind == 2)
            fake->collection.value = 0;
        if (kind == 3)
            fake->keywords.error = 5;
        if (kind == 4)
            fake->keywords.is_uint32 = false;
        if (kind == 5)
            fake->close_error = 5;
        const auto report = Observer::preflight(calls(fake), Observer::EvidenceKind::ClassifyIsolation);
        QVERIFY(!report.available);
        QVERIFY(report.reason != Observer::Reason::None);
        QCOMPARE(report.evidence_kind, Observer::EvidenceKind::ClassifyIsolation);
        QVERIFY(std::find(fake->log.begin(), fake->log.end(), "subscribe") == fake->log.end());
    }
    void unknownEvidenceKindIsRejectedBeforeAnyCall() {
        Harness h;
        h.config.evidence_kind = static_cast<Observer::EvidenceKind>(99);
        h.construct();
        QCOMPARE(h.observer->start().reason, Observer::Reason::InvalidConfiguration);
        QVERIFY(h.fake->log.empty());
        const auto report = Observer::preflight(calls(h.fake), h.config.evidence_kind);
        QVERIFY(!report.available);
        QCOMPARE(report.reason, Observer::Reason::InvalidConfiguration);
        QVERIFY(h.fake->log.empty());
    }
    void capabilityEvidenceDoesNotRequireOrQueryIsolationRulePorts() {
        Harness h;
        auto injected = calls(h.fake);
        injected.get_filter = {};
        injected.get_layer = {};
        h.observer = std::make_unique<Observer>(h.config, std::move(injected));
        QVERIFY(h.observer->start().available);
        h.fake->deliver(h.event());
        QVERIFY(h.observer->snapshot().matched);
        const auto report = h.observer->stop();
        QVERIFY(report.matched && report.first_match);
        QVERIFY(!report.first_candidate && !report.rule_verified);
        QCOMPARE(report.evidence_kind, Observer::EvidenceKind::CapabilityDrop);
        QCOMPARE(report.filter_result.error, Observer::not_called);
        QCOMPARE(report.layer_result.error, Observer::not_called);
        QVERIFY(std::find(h.fake->log.begin(), h.fake->log.end(), "filter") == h.fake->log.end());
    }
    void classifyRequiresBothRulePortsBeforeOpeningTheEngine_data() {
        QTest::addColumn<bool>("filterMissing");
        QTest::newRow("filter") << true;
        QTest::newRow("layer") << false;
    }
    void classifyRequiresBothRulePortsBeforeOpeningTheEngine() {
        QFETCH(bool, filterMissing);
        Harness h;
        h.classify();
        auto injected = calls(h.fake);
        if (filterMissing)
            injected.get_filter = {};
        else
            injected.get_layer = {};
        h.observer = std::make_unique<Observer>(h.config, std::move(injected));
        const auto report = h.observer->start();
        QCOMPARE(report.reason, Observer::Reason::MissingCall);
        QVERIFY(!report.available && !report.matched);
        QVERIFY(h.fake->log.empty());
    }
    void classifyUsesItsOwnSubscriptionAndNeverQueriesWithoutACandidate() {
        Harness h;
        h.classify();
        h.fake->keywords.value = 0;
        h.construct();
        const auto started = h.observer->start();
        QVERIFY(started.available);
        QCOMPARE(started.evidence_kind, Observer::EvidenceKind::ClassifyIsolation);
        QCOMPARE(h.fake->filter.kind, Observer::EventKind::ClassifyDrop);
        QCOMPARE(h.fake->filter.remote_ipv4, Observer::loopback_ipv4);
        QCOMPARE(h.fake->filter.protocol, std::uint8_t{6});
        QCOMPARE(h.fake->filter.remote_port, h.config.remote_port);
        QCOMPARE(h.fake->filter.begin_filetime, h.config.begin_filetime);
        QCOMPARE(h.fake->filter.end_filetime, h.config.end_filetime);
        QVERIFY(h.fake->filter_app == h.fake->application.bytes);
        const auto stopped = h.observer->stop();
        QCOMPARE(stopped.state, Observer::State::Stopped);
        QVERIFY(!stopped.matched && !stopped.rule_verified && !stopped.first_candidate);
        QCOMPARE(stopped.filter_result.error, Observer::not_called);
        QCOMPARE(stopped.layer_result.error, Observer::not_called);
    }
    void classifyCandidateBecomesEvidenceOnlyAfterQuiescenceAndExactRuleVerification_data() {
        QTest::addColumn<bool>("last");
        QTest::addColumn<bool>("duringSubscribe");
        for (bool last : {false, true})
            for (bool during : {false, true})
                QTest::newRow(qPrintable(QString::number(last) + "-" + QString::number(during))) << last << during;
    }
    void classifyCandidateBecomesEvidenceOnlyAfterQuiescenceAndExactRuleVerification() {
        QFETCH(bool, last);
        QFETCH(bool, duringSubscribe);
        Harness h;
        h.classify();
        auto event = h.classifyEvent();
        event.timestamp = last ? h.config.end_filetime : h.config.begin_filetime;
        if (duringSubscribe)
            h.fake->during_subscribe = [&] { h.fake->deliver(event); };
        h.construct();
        QVERIFY(h.observer->start().available);
        if (!duringSubscribe)
            h.fake->deliver(event);
        const auto candidate = h.observer->snapshot();
        QVERIFY(candidate.first_candidate);
        QVERIFY(!candidate.matched && !candidate.first_match && !candidate.rule_verified);
        QCOMPARE(candidate.first_candidate->filter_id, event.filter_id);
        QCOMPARE(candidate.first_candidate->timestamp, event.timestamp);
        QCOMPARE(candidate.first_candidate->layer_id, event.layer_id);
        QCOMPARE(candidate.first_candidate->ms_direction, Observer::classify_outbound_direction);
        QCOMPARE(candidate.first_candidate->capability_id, Observer::not_called);
        QCOMPARE(candidate.filter_result.error, Observer::not_called);
        auto second = event;
        second.filter_id = 456;
        h.fake->deliver(second);
        h.fake->during_unsubscribe = [&] { h.fake->deliver(second); };
        bool provedDuringQuery = false;
        h.fake->during_filter = [&] {
            provedDuringQuery = h.observer->snapshot().matched;
            h.fake->deliver(second);
        };
        const auto report = h.observer->stop();
        QVERIFY(!provedDuringQuery);
        QCOMPARE(report.state, Observer::State::Stopped);
        QCOMPARE(report.reason, Observer::Reason::None);
        QVERIFY(report.available && report.matched && report.rule_verified && report.first_match);
        QVERIFY(!report.context_retained);
        QCOMPARE(report.first_match->filter_id, std::uint64_t{123});
        QCOMPARE(report.first_match->capability_id, Observer::not_called);
        QCOMPARE(report.filter_result.error, 0U);
        QCOMPARE(report.layer_result.error, 0U);
        QCOMPARE(h.fake->queried_filter, std::uint64_t{123});
        QCOMPARE(h.fake->queried_layer, std::uint16_t{48});
        QCOMPARE(h.fake->filter_engine, Observer::Handle{11});
        QCOMPARE(h.fake->layer_engine, Observer::Handle{11});
        QVERIFY((h.fake->log == std::vector<std::string>{"app-id", "open", "collection", "keywords", "subscribe",
                                                         "unsubscribe", "filter", "layer", "close"}));
        const auto count = h.fake->log.size();
        QVERIFY(h.observer->stop().matched);
        h.observer.reset();
        QCOMPARE(h.fake->log.size(), count);
    }
    void classifyMismatchedOrIncompleteEventsNeverBecomeCandidates_data() {
        QTest::addColumn<int>("kind");
        for (int i = 0; i < 29; ++i)
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void classifyMismatchedOrIncompleteEventsNeverBecomeCandidates() {
        QFETCH(int, kind);
        Harness h;
        h.classify();
        h.construct();
        QVERIFY(h.observer->start().available);
        auto event = h.classifyEvent();
        auto different_app = h.fake->application.bytes;
        auto different_user = h.config.user_sid;
        auto different_package = h.config.package_sid;
        if (kind < 7)
            event.fields &= ~(1U << kind);
        switch (kind) {
        case 7:
            event.kind = Observer::EventKind::CapabilityDrop;
            break;
        case 8:
            event.kind = Observer::EventKind::CapabilityAllow;
            break;
        case 9:
            event.kind = Observer::EventKind::ClassifyAllow;
            break;
        case 10:
            event.kind = Observer::EventKind::Other;
            break;
        case 11:
            different_app[0] ^= 1;
            event.app_id = different_app;
            break;
        case 12:
            different_user.back() ^= 1;
            event.user_sid = different_user;
            break;
        case 13:
            different_package.back() ^= 1;
            event.package_sid = different_package;
            break;
        case 14:
            event.timestamp = h.config.begin_filetime - 1;
            break;
        case 15:
            event.timestamp = h.config.end_filetime + 1;
            break;
        case 16:
            event.ip_version = 6;
            break;
        case 17:
            event.remote_ipv4 ^= 1;
            break;
        case 18:
            ++event.remote_port;
            break;
        case 19:
            event.protocol = 17;
            break;
        case 20:
            event.filter_id = 0;
            break;
        case 21:
            event.is_loopback = 0;
            break;
        case 22:
            event.is_loopback = 2;
            break;
        case 23:
            event.layer_id = 0;
            break;
        case 24:
            event.ms_direction = 0;
            break;
        case 25:
            event.ms_direction = 1;
            break;
        case 26:
            event.ms_direction = 0x3900;
            break;
        case 27:
            event.ms_direction = 0x3902;
            break;
        case 28:
            event.ms_direction = Observer::not_called;
            break;
        }
        h.fake->deliver(event);
        QVERIFY(!h.observer->snapshot().first_candidate);
        const auto report = h.observer->stop();
        QVERIFY(!report.matched && !report.first_match && !report.rule_verified);
        QCOMPARE(report.filter_result.error, Observer::not_called);
        QCOMPARE(report.layer_result.error, Observer::not_called);
    }
    void classifyRequiresTheConcreteEnabledAppIsolationBlockingRule_data() {
        QTest::addColumn<int>("kind");
        for (int i = 0; i < 19; ++i)
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void classifyRequiresTheConcreteEnabledAppIsolationBlockingRule() {
        QFETCH(int, kind);
        Harness h;
        h.classify();
        auto& filter = h.fake->filter_result;
        auto& layer = h.fake->layer_result;
        switch (kind) {
        case 0:
            filter.error = 5;
            break;
        case 1:
            filter.present = false;
            break;
        case 2:
            filter.filter_id = 456;
            break;
        case 3:
            filter.disabled = true;
            break;
        case 4:
            filter.action = Observer::FilterAction::Unknown;
            break;
        case 5:
            filter.action = Observer::FilterAction::Permit;
            break;
        case 6:
            filter.action = Observer::FilterAction::Callout;
            break;
        case 7:
            filter.provider = Observer::ProviderIdentity::Unknown;
            break;
        case 8:
            filter.provider = Observer::ProviderIdentity::WindowsFirewall;
            break;
        case 9:
            filter.sublayer = Observer::SublayerIdentity::Unknown;
            break;
        case 10:
            filter.sublayer = Observer::SublayerIdentity::WindowsFirewall;
            break;
        case 11:
            filter.layer = Observer::LayerIdentity::Unknown;
            break;
        case 12:
            filter.layer = Observer::LayerIdentity::AleAuthConnectV6;
            break;
        case 13:
            layer.error = 5;
            break;
        case 14:
            layer.present = false;
            break;
        case 15:
            layer.layer = Observer::LayerIdentity::Unknown;
            break;
        case 16:
            layer.layer = Observer::LayerIdentity::AleAuthConnectV6;
            break;
        case 17:
            layer.layer_id = 49;
            break;
        case 18:
            filter.provider = static_cast<Observer::ProviderIdentity>(99);
            break;
        }
        h.construct();
        QVERIFY(h.observer->start().available);
        h.fake->deliver(h.classifyEvent());
        const auto report = h.observer->stop();
        QCOMPARE(report.state, Observer::State::Stopped);
        QVERIFY(report.available && report.first_candidate);
        QVERIFY(!report.matched && !report.first_match && !report.rule_verified && !report.context_retained);
        QVERIFY(report.reason != Observer::Reason::None);
        QCOMPARE(report.filter_result.error, filter.error);
        QCOMPARE(report.layer_result.error, layer.error);
        QCOMPARE(report.close_error, 0U);
        QCOMPARE(h.fake->queried_filter, std::uint64_t{123});
        QCOMPARE(h.fake->queried_layer, std::uint16_t{48});
    }
    void classifyQueryExceptionsAreInconclusiveAndStillCloseTheEngine_data() {
        QTest::addColumn<bool>("filterThrows");
        QTest::newRow("filter") << true;
        QTest::newRow("layer") << false;
    }
    void classifyQueryExceptionsAreInconclusiveAndStillCloseTheEngine() {
        QFETCH(bool, filterThrows);
        Harness h;
        h.classify();
        h.fake->throw_filter = filterThrows;
        h.fake->throw_layer = !filterThrows;
        h.construct();
        QVERIFY(h.observer->start().available);
        h.fake->deliver(h.classifyEvent());
        const auto report = h.observer->stop();
        QCOMPARE(report.state, Observer::State::Stopped);
        QCOMPARE(report.reason, Observer::Reason::Exception);
        QVERIFY(report.first_candidate && !report.matched && !report.first_match && !report.rule_verified);
        QCOMPARE(report.filter_result.error, filterThrows ? 31U : 0U);
        QCOMPARE(report.layer_result.error, filterThrows ? 0U : 31U);
        QCOMPARE(report.close_error, 0U);
        QCOMPARE(std::count(h.fake->log.begin(), h.fake->log.end(), "filter"), 1);
        QCOMPARE(std::count(h.fake->log.begin(), h.fake->log.end(), "layer"), 1);
    }
    void classifyUnconfirmedCleanupNeverPublishesVerifiedEvidence_data() {
        QTest::addColumn<bool>("unsubscribeFails");
        QTest::newRow("unsubscribe") << true;
        QTest::newRow("close") << false;
    }
    void classifyUnconfirmedCleanupNeverPublishesVerifiedEvidence() {
        QFETCH(bool, unsubscribeFails);
        Harness h;
        h.classify();
        h.fake->unsubscribe_error = unsubscribeFails ? 5 : 0;
        h.fake->close_error = unsubscribeFails ? 0 : 5;
        h.construct();
        QVERIFY(h.observer->start().available);
        h.fake->deliver(h.classifyEvent());
        const auto report = h.observer->stop();
        QCOMPARE(report.state, Observer::State::StopUnconfirmed);
        QVERIFY(report.context_retained && !report.matched && !report.first_match && !report.rule_verified);
        QCOMPARE(report.filter_result.error, unsubscribeFails ? Observer::not_called : 0U);
        QCOMPARE(report.layer_result.error, unsubscribeFails ? Observer::not_called : 0U);
        const auto count = h.fake->log.size();
        h.observer.reset();
        QCOMPARE(h.fake->log.size(), count);
    }
    void classifyNativeLayerIdsAreNotHardcodedOrTruncated() {
        Harness h;
        h.classify();
        h.fake->layer_result.layer_id = 65535;
        h.construct();
        QVERIFY(h.observer->start().available);
        auto event = h.classifyEvent();
        event.layer_id = 65535;
        h.fake->deliver(event);
        const auto report = h.observer->stop();
        QVERIFY(report.matched && report.rule_verified);
        QCOMPARE(h.fake->queried_layer, std::uint16_t{65535});
        QCOMPARE(report.first_match->layer_id, std::uint16_t{65535});
    }
    void classifyRuleMetadataPreservesRawGuidBytesWithoutApprovingUnknownIdentities() {
        Harness h;
        h.classify();
        h.fake->filter_result.provider = Observer::ProviderIdentity::Unknown;
        h.fake->filter_result.provider_guid.fill(0x11);
        h.fake->filter_result.sublayer_guid.fill(0x22);
        h.fake->filter_result.layer_guid.fill(0x33);
        h.fake->layer_result.layer_guid.fill(0x44);
        h.fake->filter_result.filter_key.fill(0x55);
        h.fake->filter_result.flags = 0x66;
        h.fake->filter_result.action_type = 0x77;
        h.fake->filter_result.query_begin_filetime = 151'000'000;
        h.fake->filter_result.query_end_filetime = 151'000'010;
        h.fake->layer_result.query_begin_filetime = 151'000'020;
        h.fake->layer_result.query_end_filetime = 151'000'030;
        h.construct();
        QVERIFY(h.observer->start().available);
        h.fake->deliver(h.classifyEvent());
        const auto report = h.observer->stop();
        QVERIFY(!report.matched && !report.rule_verified);
        QVERIFY(report.filter_result.provider_guid == h.fake->filter_result.provider_guid);
        QVERIFY(report.filter_result.sublayer_guid == h.fake->filter_result.sublayer_guid);
        QVERIFY(report.filter_result.layer_guid == h.fake->filter_result.layer_guid);
        QVERIFY(report.layer_result.layer_guid == h.fake->layer_result.layer_guid);
        QVERIFY(report.filter_result.filter_key == h.fake->filter_result.filter_key);
        QCOMPARE(report.filter_result.flags, 0x66U);
        QCOMPARE(report.filter_result.action_type, 0x77U);
        QCOMPARE(report.filter_result.query_begin_filetime, std::uint64_t{151'000'000});
        QCOMPARE(report.filter_result.query_end_filetime, std::uint64_t{151'000'010});
        QCOMPARE(report.layer_result.query_begin_filetime, std::uint64_t{151'000'020});
        QCOMPARE(report.layer_result.query_end_filetime, std::uint64_t{151'000'030});
    }
    void activeDestructionStopsOnceAndInjectedExceptionsDoNotEscape() {
        Harness h;
        h.construct();
        QVERIFY(h.observer->start().available);
        h.observer.reset();
        QCOMPARE(std::count(h.fake->log.begin(), h.fake->log.end(), "unsubscribe"), 1);
        QCOMPARE(std::count(h.fake->log.begin(), h.fake->log.end(), "close"), 1);
        auto injected = calls(h.fake);
        injected.open = []() -> Observer::OpenResult { throw std::runtime_error("injected open"); };
        Observer observer(configuration(), std::move(injected));
        QVERIFY(!observer.start().available);
        QVERIFY(!Observer::preflight({}).available);
    }
};
QTEST_GUILESS_MAIN(AppContainerNetworkObserverTests)
#include "AppContainerNetworkObserverTests.moc"
