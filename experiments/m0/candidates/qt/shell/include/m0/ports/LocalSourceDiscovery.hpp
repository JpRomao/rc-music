#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

namespace music_player::m0 {

struct DiscoveryGeneration final {
    std::uint64_t value{0};

    friend bool operator==(const DiscoveryGeneration&, const DiscoveryGeneration&) = default;
};

struct LocalSourceDiscoveryRequest final {
    DiscoveryGeneration generation{};
    std::vector<std::filesystem::path> roots{};
};

enum class LocalSourceDiscoveryIssueCode {
    SourceNotFound,
    AccessDenied,
    UnsupportedFile,
    UnsupportedEntry,
    ReparsePoint,
    DepthLimitExceeded,
    FilesystemError,
};

struct LocalSourceDiscoveryIssue final {
    LocalSourceDiscoveryIssueCode code{LocalSourceDiscoveryIssueCode::FilesystemError};
    std::filesystem::path source_path{};
};

struct LocalSourceDiscoveryBatch final {
    DiscoveryGeneration generation{};
    std::uint64_t sequence{0};
    std::uint64_t inspected_count{0};
    std::vector<std::filesystem::path> accepted_paths{};
    std::vector<LocalSourceDiscoveryIssue> issues{};
};

struct LocalSourceDiscoverySummary final {
    std::uint64_t inspected_count{0};
    std::uint64_t accepted_count{0};
    std::uint64_t issue_count{0};
};

enum class DiscoveryCommandResult {
    Accepted,
    InvalidArgument,
    Busy,
    StaleGeneration,
    Shutdown,
};

enum class DiscoveryPollStatus {
    InvalidArgument,
    Pending,
    BatchReady,
    Completed,
    Cancelled,
    StaleGeneration,
    Shutdown,
    Idle,
};

struct DiscoveryPollResult final {
    DiscoveryPollStatus status{DiscoveryPollStatus::Idle};
    std::optional<LocalSourceDiscoveryBatch> batch{};
    std::optional<LocalSourceDiscoverySummary> summary{};
};

class LocalSourceDiscovery {
  public:
    virtual ~LocalSourceDiscovery() = default;

    virtual DiscoveryCommandResult start(LocalSourceDiscoveryRequest request) = 0;
    virtual DiscoveryCommandResult cancel(DiscoveryGeneration generation) = 0;
    [[nodiscard]] virtual DiscoveryPollResult poll(DiscoveryGeneration generation) = 0;
    virtual DiscoveryCommandResult shutdown() = 0;
};

} // namespace music_player::m0
