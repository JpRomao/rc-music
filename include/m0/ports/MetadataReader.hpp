#pragma once

#include "m0/ports/MetadataExtractor.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>

namespace music_player::m0 {

struct MetadataGeneration final {
    std::uint64_t value{0};

    friend bool operator==(const MetadataGeneration&, const MetadataGeneration&) = default;
};

struct MetadataReadRequest final {
    MetadataGeneration generation{};
    std::filesystem::path source_path{};
};

struct MetadataReadCompletion final {
    MetadataGeneration generation{};
    std::filesystem::path source_path{};
    MetadataReadResult result;
};

enum class MetadataCommandResult {
    Accepted,
    InvalidArgument,
    StaleGeneration,
    Shutdown,
};

enum class MetadataPollStatus {
    InvalidArgument,
    Pending,
    Completed,
    Cancelled,
    Expired,
    StaleGeneration,
    Shutdown,
    Idle,
};

struct MetadataPollResult final {
    MetadataPollStatus status{MetadataPollStatus::Idle};
    std::optional<MetadataReadCompletion> completion{};
};

class MetadataReader {
  public:
    virtual ~MetadataReader() = default;

    virtual MetadataCommandResult startRead(MetadataReadRequest request) = 0;
    virtual MetadataCommandResult cancelRead(MetadataGeneration generation) = 0;
    [[nodiscard]] virtual MetadataPollResult poll(MetadataGeneration generation) = 0;
    [[nodiscard]] virtual std::optional<MetadataReadCompletion> tryTakeCompletion() = 0;
    virtual MetadataCommandResult shutdown() = 0;
};

} // namespace music_player::m0
