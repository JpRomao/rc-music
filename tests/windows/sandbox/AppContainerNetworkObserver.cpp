#include "AppContainerNetworkObserver.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <fwpmu.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <list>
#include <mutex>
#include <utility>

namespace music_player::sandbox_probe {
namespace {
using Observer = AppContainerNetworkObserver;
using Reason = Observer::Reason;
using State = Observer::State;
constexpr std::size_t maximum_app_id_bytes = 65'536;

bool validSid(const std::span<const std::uint8_t> sid) {
    return sid.size() >= 8 && sid[0] == 1 && sid[1] <= 15 && sid.size() == 8 + 4 * std::size_t{sid[1]};
}
bool profileSid(const std::span<const std::uint8_t> sid) {
    return validSid(sid) && sid.size() == 40 && sid[1] == 8 &&
           std::all_of(sid.begin() + 2, sid.begin() + 7, [](auto byte) { return byte == 0; }) && sid[7] == 15 &&
           sid[8] == 2 && sid[9] == 0 && sid[10] == 0 && sid[11] == 0;
}
bool validExecutable(const std::wstring_view path) {
    if (path.size() < 4 || path.size() > 32767 ||
        !((path[0] >= L'A' && path[0] <= L'Z') || (path[0] >= L'a' && path[0] <= L'z')) || path[1] != L':' ||
        (path[2] != L'/' && path[2] != L'\\'))
        return false;
    for (std::size_t i = 0; i < path.size(); ++i) {
        const auto unit = static_cast<std::uint32_t>(path[i]);
        if (unit == 0 || unit > 0xffff || (unit >= 0xdc00 && unit <= 0xdfff))
            return false;
        if (unit >= 0xd800 && unit <= 0xdbff) {
            if (++i == path.size() || path[i] < 0xdc00 || path[i] > 0xdfff)
                return false;
        }
    }
    return true;
}
bool validKind(const Observer::EvidenceKind kind) {
    return kind == Observer::EvidenceKind::CapabilityDrop || kind == Observer::EvidenceKind::ClassifyIsolation;
}
bool validConfig(const Observer::Config& config) {
    return validExecutable(config.executable) && profileSid(config.package_sid) && validSid(config.user_sid) &&
           config.remote_port != 0 && config.begin_filetime <= config.end_filetime &&
           config.end_filetime - config.begin_filetime <= Observer::maximum_window_ticks &&
           validKind(config.evidence_kind);
}
bool same(const std::span<const std::uint8_t> left, const std::span<const std::uint8_t> right) {
    return left.size() == right.size() && std::equal(left.begin(), left.end(), right.begin());
}
bool validAppId(const Observer::Bytes& bytes) {
    return !bytes.empty() && bytes.size() <= maximum_app_id_bytes && bytes.size() % 2 == 0;
}
template <typename Report>
bool queryOptions(const Observer::Calls& calls, const Observer::Handle engine, Report& report,
                  const Observer::EvidenceKind kind) {
    report.query_error = ERROR_GEN_FAILURE;
    const auto collection = calls.get_collection(engine);
    report.query_error = collection.error;
    report.value_is_uint32 = collection.is_uint32;
    report.collection_value = collection.value;
    if (collection.error != ERROR_SUCCESS) {
        report.reason = Reason::CollectionQuery;
        return false;
    }
    if (!collection.is_uint32) {
        report.reason = Reason::CollectionType;
        return false;
    }
    if (collection.value != 1) {
        report.reason = Reason::CollectionDisabled;
        return false;
    }
    report.keyword_query_error = ERROR_GEN_FAILURE;
    const auto keywords = calls.get_keywords(engine);
    report.keyword_query_error = keywords.error;
    report.keywords_is_uint32 = keywords.is_uint32;
    report.keywords_value = keywords.value;
    if (keywords.error != ERROR_SUCCESS) {
        report.reason = Reason::KeywordQuery;
        return false;
    }
    if (!keywords.is_uint32) {
        report.reason = Reason::KeywordType;
        return false;
    }
    if (kind == Observer::EvidenceKind::CapabilityDrop && !(keywords.value & Observer::capability_drop_keyword)) {
        report.reason = Reason::KeywordDisabled;
        return false;
    }
    return true;
}
bool preflightCalls(const Observer::Calls& calls) {
    return calls.open && calls.get_collection && calls.get_keywords && calls.close;
}
struct Operation final {
    std::atomic_bool& busy;
    bool acquired;
    explicit Operation(std::atomic_bool& value) : busy(value), acquired(!busy.exchange(true)) {}
    ~Operation() {
        if (acquired)
            busy.store(false);
    }
};
} // namespace

struct AppContainerNetworkObserver::Impl final {
    Config config;
    Calls calls;
    Bytes app_id{};
    Handle engine{}, subscription{};
    mutable std::mutex mutex;
    std::atomic_bool busy{false};
    Report report{};
    bool accepting{false};
    Impl(Config value, Calls functions) : config(std::move(value)), calls(std::move(functions)) {
        report.evidence_kind = config.evidence_kind;
    }
    Report snapshot() const {
        const std::lock_guard lock(mutex);
        return report;
    }
    void publish(const Report& value, const bool accept = false) {
        const std::lock_guard lock(mutex);
        report = value;
        accepting = accept;
    }
    void disable() {
        const std::lock_guard lock(mutex);
        accepting = false;
    }
    Report disableAndRead() {
        const std::lock_guard lock(mutex);
        accepting = false;
        return report;
    }
    void started(const std::uint32_t error) {
        const std::lock_guard lock(mutex);
        report.subscribe_error = error;
        report.available = true;
        report.reason = Reason::None;
    }
    void retain(Report value, const Reason reason) {
        value.state = State::StopUnconfirmed;
        value.reason = reason;
        value.context_retained = true;
        value.matched = false;
        value.first_match.reset();
        value.rule_verified = false;
        publish(value);
    }
    void failed(Report value) {
        value.state = State::Unavailable;
        value.available = false;
        value.matched = false;
        value.first_match.reset();
        value.rule_verified = false;
        if (engine) {
            value.close_error = ERROR_GEN_FAILURE;
            try {
                value.close_error = calls.close(engine);
            } catch (...) {
            }
            if (value.close_error != ERROR_SUCCESS) {
                retain(value, Reason::Close);
                return;
            }
            engine = 0;
        }
        publish(value);
    }
    static void receive(void* context, const EventView& event) noexcept {
        try {
            static_cast<Impl*>(context)->consume(event);
        } catch (...) {
        }
    }
    void consume(const EventView& event) {
        const std::lock_guard lock(mutex);
        if (!accepting || report.matched || report.first_candidate ||
            (event.fields & required_fields) != required_fields || event.ip_version != 4 || event.protocol != 6 ||
            event.remote_ipv4 != loopback_ipv4 || event.remote_port != config.remote_port ||
            event.timestamp < config.begin_filetime || event.timestamp > config.end_filetime ||
            event.is_loopback != 1 || !event.filter_id || !same(event.app_id, app_id) ||
            !same(event.user_sid, config.user_sid) || !same(event.package_sid, config.package_sid))
            return;
        if (config.evidence_kind == EvidenceKind::CapabilityDrop) {
            if (event.kind != EventKind::CapabilityDrop || event.capability_id > 2)
                return;
            report.matched = true;
            report.first_match = Match{event.timestamp, event.filter_id, event.capability_id};
        } else if (event.kind == EventKind::ClassifyDrop && event.layer_id &&
                   event.ms_direction == classify_outbound_direction) {
            report.first_candidate =
                Match{event.timestamp, event.filter_id, not_called, event.layer_id, event.ms_direction};
        }
    }
    void verifyIsolationRule(Report& value) {
        if (config.evidence_kind != EvidenceKind::ClassifyIsolation || !value.first_candidate)
            return;
        const auto candidate = *value.first_candidate;
        // These are current-object reads on the subscription's engine, after
        // callback quiescence. They are not a historical policy snapshot.
        value.filter_result.error = ERROR_GEN_FAILURE;
        try {
            value.filter_result = calls.get_filter(engine, candidate.filter_id);
        } catch (...) {
            value.reason = Reason::Exception;
        }
        value.layer_result.error = ERROR_GEN_FAILURE;
        try {
            value.layer_result = calls.get_layer(engine, candidate.layer_id);
        } catch (...) {
            value.reason = Reason::Exception;
        }
        if (value.reason == Reason::Exception)
            return;
        const auto& filter = value.filter_result;
        const auto& layer = value.layer_result;
        if (filter.error != ERROR_SUCCESS || !filter.present) {
            value.reason = Reason::FilterQuery;
            return;
        }
        if (layer.error != ERROR_SUCCESS || !layer.present) {
            value.reason = Reason::LayerQuery;
            return;
        }
        value.rule_verified =
            filter.filter_id == candidate.filter_id && !filter.disabled && filter.action == FilterAction::Block &&
            filter.provider == ProviderIdentity::AppIsolation && filter.sublayer == SublayerIdentity::AppIsolation &&
            filter.layer == LayerIdentity::AleAuthConnectV4 && layer.layer == LayerIdentity::AleAuthConnectV4 &&
            layer.layer_id == candidate.layer_id;
        if (value.rule_verified) {
            value.matched = true;
            value.first_match = candidate;
        } else {
            value.reason = Reason::IsolationRule;
        }
    }
};
AppContainerNetworkObserver::AppContainerNetworkObserver(Config config, Calls calls)
    : impl_(std::make_unique<Impl>(std::move(config), std::move(calls))) {}
AppContainerNetworkObserver::~AppContainerNetworkObserver() {
    if (impl_->snapshot().state == State::Observing)
        static_cast<void>(stop());
    if (impl_->snapshot().context_retained)
        static_cast<void>(impl_.release());
}
AppContainerNetworkObserver::PreflightReport AppContainerNetworkObserver::preflight(Calls calls,
                                                                                    EvidenceKind kind) noexcept {
    PreflightReport report;
    report.evidence_kind = kind;
    struct Resources final {
        Calls calls;
        Handle engine{};
    };
    std::unique_ptr<Resources> resources;
    try {
        if (!validKind(kind)) {
            report.reason = Reason::InvalidConfiguration;
            return report;
        }
        if (!preflightCalls(calls)) {
            report.reason = Reason::MissingCall;
            return report;
        }
        resources = std::make_unique<Resources>(Resources{std::move(calls), 0});
        report.open_error = ERROR_GEN_FAILURE;
        const auto opened = resources->calls.open();
        report.open_error = opened.error;
        resources->engine = opened.handle;
        if (opened.error != ERROR_SUCCESS || !opened.handle)
            report.reason = Reason::Engine;
        else if (queryOptions(resources->calls, opened.handle, report, kind))
            report.available = true;
    } catch (...) {
        report.available = false;
        report.reason = Reason::Exception;
    }
    if (resources && resources->engine) {
        report.close_error = ERROR_GEN_FAILURE;
        try {
            report.close_error = resources->calls.close(resources->engine);
        } catch (...) {
        }
        if (report.close_error != ERROR_SUCCESS) {
            report.available = false;
            report.context_retained = true;
            report.reason = Reason::Close;
            static_cast<void>(resources.release());
        }
    }
    return report;
}
AppContainerNetworkObserver::PreflightReport AppContainerNetworkObserver::nativePreflight(EvidenceKind kind) noexcept {
    try {
        return preflight(systemCalls(), kind);
    } catch (...) {
        PreflightReport report;
        report.evidence_kind = kind;
        report.reason = Reason::Exception;
        return report;
    }
}
AppContainerNetworkObserver::Report AppContainerNetworkObserver::start() noexcept {
    auto& value = *impl_;
    const Operation operation(value.busy);
    if (!operation.acquired)
        return snapshot();
    auto report = snapshot();
    if (report.state != State::Idle)
        return report;
    bool subscribing = false;
    try {
        if (!validConfig(value.config)) {
            report.reason = Reason::InvalidConfiguration;
            value.failed(report);
            return snapshot();
        }
        if (!preflightCalls(value.calls) || !value.calls.app_id || !value.calls.subscribe || !value.calls.unsubscribe ||
            (value.config.evidence_kind == EvidenceKind::ClassifyIsolation &&
             (!value.calls.get_filter || !value.calls.get_layer))) {
            report.reason = Reason::MissingCall;
            value.failed(report);
            return snapshot();
        }
        report.app_id_error = ERROR_GEN_FAILURE;
        auto app = value.calls.app_id(value.config.executable);
        report.app_id_error = app.error;
        if (app.error != ERROR_SUCCESS || !validAppId(app.bytes)) {
            report.reason = Reason::AppId;
            value.failed(report);
            return snapshot();
        }
        value.app_id = std::move(app.bytes);
        report.open_error = ERROR_GEN_FAILURE;
        const auto opened = value.calls.open();
        report.open_error = opened.error;
        value.engine = opened.handle;
        if (opened.error != ERROR_SUCCESS || !opened.handle) {
            report.reason = Reason::Engine;
            value.failed(report);
            return snapshot();
        }
        if (!queryOptions(value.calls, value.engine, report, value.config.evidence_kind)) {
            value.failed(report);
            return snapshot();
        }
        const SubscriptionFilter filter{value.app_id,
                                        loopback_ipv4,
                                        6,
                                        value.config.remote_port,
                                        value.config.evidence_kind == EvidenceKind::CapabilityDrop
                                            ? EventKind::CapabilityDrop
                                            : EventKind::ClassifyDrop,
                                        value.config.begin_filetime,
                                        value.config.end_filetime};
        // All immutable identity storage and the accepting context exist before
        // Subscribe: the callback may run before that call returns.
        report.state = State::Observing;
        report.subscribe_error = ERROR_GEN_FAILURE;
        value.publish(report, true);
        subscribing = true;
        const auto subscribed = value.calls.subscribe(value.engine, filter, &Impl::receive, &value);
        value.subscription = subscribed.handle;
        report = snapshot();
        report.subscribe_error = subscribed.error;
        if (subscribed.error != ERROR_SUCCESS || !subscribed.handle) {
            value.disable();
            // A failed RPC, even without a handle, is not the callback barrier
            // provided by successful Unsubscribe. Preserve the published context.
            value.retain(report, Reason::Subscribe);
            return snapshot();
        }
        value.started(subscribed.error);
    } catch (...) {
        value.disable();
        if (subscribing)
            value.retain(snapshot(), Reason::Exception);
        else {
            report.reason = Reason::Exception;
            value.failed(report);
        }
    }
    return snapshot();
}
AppContainerNetworkObserver::Report AppContainerNetworkObserver::snapshot() const noexcept {
    try {
        return impl_->snapshot();
    } catch (...) {
        Report report;
        report.state = State::StopUnconfirmed;
        report.reason = Reason::Exception;
        return report;
    }
}
AppContainerNetworkObserver::Report AppContainerNetworkObserver::stop() noexcept {
    auto& value = *impl_;
    const Operation operation(value.busy);
    if (!operation.acquired)
        return snapshot();
    auto report = snapshot();
    if (report.state != State::Observing)
        return report;
    report = value.disableAndRead();
    // Never hold the callback's mutex while Unsubscribe waits for callbacks.
    report.unsubscribe_error = ERROR_GEN_FAILURE;
    try {
        report.unsubscribe_error = value.calls.unsubscribe(value.engine, value.subscription);
    } catch (...) {
    }
    if (report.unsubscribe_error != ERROR_SUCCESS) {
        value.retain(report, Reason::Unsubscribe);
        return snapshot();
    }
    value.subscription = 0;
    value.verifyIsolationRule(report);
    report.close_error = ERROR_GEN_FAILURE;
    try {
        report.close_error = value.calls.close(value.engine);
    } catch (...) {
    }
    if (report.close_error != ERROR_SUCCESS) {
        value.retain(report, Reason::Close);
        return snapshot();
    }
    value.engine = 0;
    report.state = State::Stopped;
    value.publish(report);
    return snapshot();
}

namespace {
struct WfpMemory final {
    void* pointer{};
    ~WfpMemory() {
        if (pointer)
            FwpmFreeMemory0(&pointer);
    }
};
Observer::OptionResult nativeOption(const Observer::Handle engine, const FWPM_ENGINE_OPTION option) {
    FWP_VALUE0* raw = nullptr;
    const auto error = FwpmEngineGetOption0(reinterpret_cast<HANDLE>(engine), option, &raw);
    const WfpMemory memory{raw};
    const bool typed = raw && raw->type == FWP_UINT32;
    return {error, typed, typed ? raw->uint32 : 0};
}
std::uint64_t currentFiletime() {
    FILETIME value{};
    GetSystemTimeAsFileTime(&value);
    return (std::uint64_t{value.dwHighDateTime} << 32) | value.dwLowDateTime;
}
Observer::GuidBytes guidBytes(const GUID& guid) {
    static_assert(sizeof(GUID) == 16);
    Observer::GuidBytes result{};
    std::copy_n(reinterpret_cast<const std::uint8_t*>(&guid), result.size(), result.begin());
    return result;
}
Observer::LayerIdentity layerIdentity(const GUID& guid) {
    if (IsEqualGUID(guid, FWPM_LAYER_ALE_AUTH_CONNECT_V4))
        return Observer::LayerIdentity::AleAuthConnectV4;
    if (IsEqualGUID(guid, FWPM_LAYER_ALE_AUTH_CONNECT_V6))
        return Observer::LayerIdentity::AleAuthConnectV6;
    return Observer::LayerIdentity::Unknown;
}
Observer::FilterResult nativeFilter(const Observer::Handle engine, const std::uint64_t id) {
    Observer::FilterResult result;
    result.query_begin_filetime = currentFiletime();
    FWPM_FILTER0* raw = nullptr;
    result.error = FwpmFilterGetById0(reinterpret_cast<HANDLE>(engine), id, &raw);
    const WfpMemory memory{raw};
    if (result.error == ERROR_SUCCESS && raw) {
        result.present = true;
        result.filter_id = raw->filterId;
        result.flags = raw->flags;
        result.disabled = (raw->flags & FWPM_FILTER_FLAG_DISABLED) != 0;
        result.action_type = raw->action.type;
        if (raw->action.type == FWP_ACTION_BLOCK)
            result.action = Observer::FilterAction::Block;
        else if (raw->action.type == FWP_ACTION_PERMIT)
            result.action = Observer::FilterAction::Permit;
        else if (raw->action.type == FWP_ACTION_CALLOUT_TERMINATING ||
                 raw->action.type == FWP_ACTION_CALLOUT_INSPECTION || raw->action.type == FWP_ACTION_CALLOUT_UNKNOWN)
            result.action = Observer::FilterAction::Callout;
        if (raw->providerKey) {
            result.provider_guid = guidBytes(*raw->providerKey);
            if (IsEqualGUID(*raw->providerKey, FWPM_PROVIDER_MPSSVC_APP_ISOLATION))
                result.provider = Observer::ProviderIdentity::AppIsolation;
            else if (IsEqualGUID(*raw->providerKey, FWPM_PROVIDER_MPSSVC_WF))
                result.provider = Observer::ProviderIdentity::WindowsFirewall;
        }
        result.sublayer_guid = guidBytes(raw->subLayerKey);
        if (IsEqualGUID(raw->subLayerKey, FWPM_SUBLAYER_MPSSVC_APP_ISOLATION))
            result.sublayer = Observer::SublayerIdentity::AppIsolation;
        else if (IsEqualGUID(raw->subLayerKey, FWPM_SUBLAYER_MPSSVC_WF))
            result.sublayer = Observer::SublayerIdentity::WindowsFirewall;
        result.layer = layerIdentity(raw->layerKey);
        result.layer_guid = guidBytes(raw->layerKey);
        result.filter_key = guidBytes(raw->filterKey);
    }
    result.query_end_filetime = currentFiletime();
    return result;
}
Observer::LayerResult nativeLayer(const Observer::Handle engine, const std::uint16_t id) {
    Observer::LayerResult result;
    result.query_begin_filetime = currentFiletime();
    FWPM_LAYER0* raw = nullptr;
    result.error = FwpmLayerGetById0(reinterpret_cast<HANDLE>(engine), id, &raw);
    const WfpMemory memory{raw};
    if (result.error == ERROR_SUCCESS && raw) {
        result.present = true;
        result.layer_id = raw->layerId;
        result.layer = layerIdentity(raw->layerKey);
        result.layer_guid = guidBytes(raw->layerKey);
    }
    result.query_end_filetime = currentFiletime();
    return result;
}
std::span<const std::uint8_t> nativeSid(const SID* sid) {
    if (!sid || !IsValidSid(const_cast<SID*>(sid)))
        return {};
    const auto length = GetLengthSid(const_cast<SID*>(sid));
    if (length < 8 || length > SECURITY_MAX_SID_SIZE)
        return {};
    return {reinterpret_cast<const std::uint8_t*>(sid), length};
}
struct NativeBridge final {
    Observer::Callback callback{};
    void* context{};
    Observer::Handle subscription{};
};
void CALLBACK nativeEvent(void* context, const FWPM_NET_EVENT2* event) noexcept {
    if (!context || !event ||
        !((event->type == FWPM_NET_EVENT_TYPE_CAPABILITY_DROP && event->capabilityDrop) ||
          (event->type == FWPM_NET_EVENT_TYPE_CLASSIFY_DROP && event->classifyDrop)))
        return;
    constexpr std::uint32_t needed = FWPM_NET_EVENT_FLAG_IP_VERSION_SET | FWPM_NET_EVENT_FLAG_IP_PROTOCOL_SET |
                                     FWPM_NET_EVENT_FLAG_REMOTE_ADDR_SET | FWPM_NET_EVENT_FLAG_REMOTE_PORT_SET |
                                     FWPM_NET_EVENT_FLAG_APP_ID_SET | FWPM_NET_EVENT_FLAG_USER_ID_SET |
                                     FWPM_NET_EVENT_FLAG_PACKAGE_ID_SET;
    const auto& header = event->header;
    if ((header.flags & needed) != needed || header.ipVersion != FWP_IP_VERSION_V4 || !header.appId.data ||
        !header.appId.size || header.appId.size > maximum_app_id_bytes)
        return;
    Observer::EventView view;
    view.fields = Observer::required_fields;
    view.timestamp = (std::uint64_t{header.timeStamp.dwHighDateTime} << 32) | header.timeStamp.dwLowDateTime;
    view.ip_version = 4;
    view.remote_ipv4 = header.remoteAddrV4;
    view.protocol = header.ipProtocol;
    view.remote_port = header.remotePort;
    view.app_id = {header.appId.data, header.appId.size};
    view.user_sid = nativeSid(header.userId);
    view.package_sid = nativeSid(header.packageSid);
    if (event->type == FWPM_NET_EVENT_TYPE_CAPABILITY_DROP) {
        const auto& drop = *event->capabilityDrop;
        view.kind = Observer::EventKind::CapabilityDrop;
        view.capability_id = static_cast<std::uint32_t>(drop.networkCapabilityId);
        view.filter_id = drop.filterId;
        view.is_loopback = drop.isLoopback;
    } else {
        const auto& drop = *event->classifyDrop;
        view.kind = Observer::EventKind::ClassifyDrop;
        view.filter_id = drop.filterId;
        view.is_loopback = drop.isLoopback;
        view.layer_id = drop.layerId;
        view.ms_direction = drop.msFwpDirection;
    }
    const auto& bridge = *static_cast<NativeBridge*>(context);
    bridge.callback(bridge.context, view);
}
struct NativeBindings final {
    std::mutex mutex;
    std::list<std::unique_ptr<NativeBridge>> bridges;
};
FILETIME filetime(const std::uint64_t value) { return {static_cast<DWORD>(value), static_cast<DWORD>(value >> 32)}; }
Observer::SubscribeResult nativeSubscribe(const std::shared_ptr<NativeBindings>& bindings,
                                          const Observer::Handle engine, const Observer::SubscriptionFilter& filter,
                                          const Observer::Callback callback, void* context) {
    if (!callback || !context ||
        (filter.kind != Observer::EventKind::CapabilityDrop && filter.kind != Observer::EventKind::ClassifyDrop) ||
        filter.app_id.empty() || filter.app_id.size() > maximum_app_id_bytes || filter.app_id.size() % 2 != 0 ||
        filter.remote_ipv4 != Observer::loopback_ipv4 || filter.protocol != 6 || !filter.remote_port ||
        filter.begin_filetime > filter.end_filetime ||
        filter.end_filetime - filter.begin_filetime > Observer::maximum_window_ticks)
        return {ERROR_INVALID_PARAMETER, 0};
    FWP_BYTE_BLOB app{static_cast<UINT32>(filter.app_id.size()), const_cast<UINT8*>(filter.app_id.data())};
    std::array<FWPM_FILTER_CONDITION0, 5> conditions{};
    for (auto& condition : conditions)
        condition.matchType = FWP_MATCH_EQUAL;
    conditions[0].fieldKey = FWPM_CONDITION_ALE_APP_ID;
    conditions[0].conditionValue.type = FWP_BYTE_BLOB_TYPE;
    conditions[0].conditionValue.byteBlob = &app;
    conditions[1].fieldKey = FWPM_CONDITION_IP_PROTOCOL;
    conditions[1].conditionValue.type = FWP_UINT8;
    conditions[1].conditionValue.uint8 = filter.protocol;
    conditions[2].fieldKey = FWPM_CONDITION_IP_REMOTE_ADDRESS;
    conditions[2].conditionValue.type = FWP_UINT32;
    conditions[2].conditionValue.uint32 = filter.remote_ipv4;
    conditions[3].fieldKey = FWPM_CONDITION_IP_REMOTE_PORT;
    conditions[3].conditionValue.type = FWP_UINT16;
    conditions[3].conditionValue.uint16 = filter.remote_port;
    conditions[4].fieldKey = FWPM_CONDITION_NET_EVENT_TYPE;
    conditions[4].conditionValue.type = FWP_UINT32;
    conditions[4].conditionValue.uint32 = filter.kind == Observer::EventKind::CapabilityDrop
                                              ? FWPM_NET_EVENT_TYPE_CAPABILITY_DROP
                                              : FWPM_NET_EVENT_TYPE_CLASSIFY_DROP;
    FWPM_NET_EVENT_ENUM_TEMPLATE0 selection{};
    selection.startTime = filetime(filter.begin_filetime);
    selection.endTime = filetime(filter.end_filetime);
    selection.numFilterConditions = static_cast<UINT32>(conditions.size());
    selection.filterCondition = conditions.data();
    FWPM_NET_EVENT_SUBSCRIPTION0 subscription{};
    subscription.enumTemplate = &selection;
    auto bridge = std::make_unique<NativeBridge>(NativeBridge{callback, context, 0});
    auto* address = bridge.get();
    {
        const std::lock_guard lock(bindings->mutex);
        bindings->bridges.push_back(std::move(bridge));
    }
    // The bridge is already owned before the SDK can publish its callback.
    HANDLE handle = nullptr;
    const auto error =
        FwpmNetEventSubscribe1(reinterpret_cast<HANDLE>(engine), &subscription, &nativeEvent, address, &handle);
    {
        const std::lock_guard lock(bindings->mutex);
        address->subscription = reinterpret_cast<Observer::Handle>(handle);
        // Preserve this bridge on every uncertain Subscribe result. The caller
        // also retains bindings/engine/context; EngineClose is not an ACK.
    }
    return {error, reinterpret_cast<Observer::Handle>(handle)};
}
} // namespace

AppContainerNetworkObserver::Calls AppContainerNetworkObserver::systemCalls() {
    const auto bindings = std::make_shared<NativeBindings>();
    return {
        .app_id =
            [](const std::wstring_view executable) {
                const std::wstring path{executable};
                FWP_BYTE_BLOB* raw = nullptr;
                const auto error = FwpmGetAppIdFromFileName0(path.c_str(), &raw);
                const WfpMemory memory{raw};
                AppIdResult result{error, {}};
                if (error == ERROR_SUCCESS && raw && raw->data && raw->size <= maximum_app_id_bytes)
                    result.bytes.assign(raw->data, raw->data + raw->size);
                return result;
            },
        .open =
            [] {
                HANDLE engine = nullptr;
                const auto error = FwpmEngineOpen0(nullptr, RPC_C_AUTHN_WINNT, nullptr, nullptr, &engine);
                return OpenResult{error, reinterpret_cast<Handle>(engine)};
            },
        .get_collection = [](const Handle engine) { return nativeOption(engine, FWPM_ENGINE_COLLECT_NET_EVENTS); },
        .get_keywords =
            [](const Handle engine) { return nativeOption(engine, FWPM_ENGINE_NET_EVENT_MATCH_ANY_KEYWORDS); },
        .subscribe = [bindings](const Handle engine, const SubscriptionFilter& filter, const Callback callback,
                                void* context) { return nativeSubscribe(bindings, engine, filter, callback, context); },
        .unsubscribe =
            [bindings](const Handle engine, const Handle subscription) {
                const auto error =
                    FwpmNetEventUnsubscribe0(reinterpret_cast<HANDLE>(engine), reinterpret_cast<HANDLE>(subscription));
                if (error == ERROR_SUCCESS) {
                    // The SDK guarantees callback completion before success returns.
                    const std::lock_guard lock(bindings->mutex);
                    bindings->bridges.remove_if(
                        [subscription](const auto& bridge) { return bridge->subscription == subscription; });
                }
                return static_cast<std::uint32_t>(error);
            },
        .close =
            [](const Handle engine) {
                return static_cast<std::uint32_t>(FwpmEngineClose0(reinterpret_cast<HANDLE>(engine)));
            },
        .get_filter = [](const Handle engine, const std::uint64_t id) { return nativeFilter(engine, id); },
        .get_layer = [](const Handle engine, const std::uint16_t id) { return nativeLayer(engine, id); }};
}
} // namespace music_player::sandbox_probe
