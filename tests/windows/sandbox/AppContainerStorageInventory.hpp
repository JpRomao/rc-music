#pragma once

#include <QByteArray>
#include <QString>

#include <array>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <vector>

namespace music_player::sandbox_probe {
// Test-only inventory model. No native factory, ownership grant, mutation,
// adoption or cleanup API is exposed by this interface.
class AppContainerStorageInventory final {
  public:
    static constexpr std::uint32_t not_called = std::numeric_limits<std::uint32_t>::max();
    static constexpr std::uint32_t maximum_objects = 512, maximum_depth = 16;
    static constexpr std::uint64_t maximum_sd_bytes = 64 * 1024, maximum_total_bytes = 4 * 1024 * 1024;
    static constexpr std::uint32_t maximum_name_units = 255;
    enum class Provenance { Unknown, LiveCreated, Loaded, Revoked };
    enum class Root { PackageRoot, AC, MappingsSID, StorageSID };
    enum class Presence { Unknown, Present, Missing };
    enum class Type { Unknown, Directory, File, RegistryKey };
    enum class Dacl { Unknown, Absent, Null, Present };
    struct NodeRef final {
        std::uint64_t value{};
        bool operator==(const NodeRef&) const = default;
    };
    // Session-local identity from the trusted port, not a caller path or grant.
    // No native FileID/MIC/security-descriptor validity is proved by this model.
    // A future Registry binding must not pretend HKEY numbers are persistent IDs.
    struct Identity final {
        std::uint64_t domain{}, object{};
        bool operator==(const Identity&) const = default;
    };
    struct Metadata final {
        NodeRef ref{};
        Identity identity{};
        Presence presence{Presence::Unknown};
        Type type{Type::Unknown};
        std::uint32_t error{not_called};
        std::uint32_t link_count{}, attributes{}, reparse_tag{};
        bool reparse{false}, registry_link{false};
        // The trusted port supplies already captured/structured owner+DACL data.
        // This model bounds/preserves bytes; it does not parse Win32 descriptors.
        QByteArray owner_sid{}, raw_sd{};
        Dacl dacl{Dacl::Unknown};
        bool dacl_protected{false};
        bool sacl_not_queried{true};
    };
    struct RootResult final {
        NodeRef ref{};
        std::uint32_t error{not_called};
        Presence presence{Presence::Unknown};
    };
    enum class ChildState { Unknown, Child, End };
    struct ChildResult final {
        ChildState state{ChildState::Unknown};
        std::uint32_t error{not_called};
        QString name{};
        NodeRef ref{};
    };
    enum class Validation { Unknown, Unchanged, Changed };
    struct ValidationResult final {
        Validation state{Validation::Unknown};
        std::uint32_t error{not_called};
    };
    enum class CloseState { NotAttempted, Closed, Unknown };
    struct CloseResult final {
        CloseState state{CloseState::NotAttempted};
        std::uint32_t error{not_called};
    };
    struct Limits final {
        std::uint32_t objects{maximum_objects}, depth{maximum_depth};
        std::uint64_t sd_bytes{maximum_sd_bytes}, total_bytes{maximum_total_bytes};
    };
    class ReadSession {
      public:
        virtual ~ReadSession() = default;
        [[nodiscard]] virtual RootResult root(Root root) = 0;
        // Bounds apply before allocating a native response as well as on return.
        [[nodiscard]] virtual Metadata readMetadata(NodeRef ref, std::uint64_t maximum_bytes) = 0;
        [[nodiscard]] virtual ChildResult nextChild(NodeRef parent, std::uint32_t index,
                                                    std::uint32_t maximum_units) = 0;
        [[nodiscard]] virtual ValidationResult revalidate() = 0;
        [[nodiscard]] virtual CloseResult close() = 0;
    };
    struct BeginResult final {
        Provenance provenance{Provenance::Unknown};
        std::uint32_t error{not_called};
        std::unique_ptr<ReadSession> session{};
    };
    class Source {
      public:
        virtual ~Source() = default;
        // Trusted future composition supplies provenance and already captured
        // roots atomically. Loaded records and SID values cannot mint this scope.
        [[nodiscard]] virtual BeginResult begin() = 0;
    };
    struct Object final {
        Root root{Root::PackageRoot};
        NodeRef parent{};
        QString name{}; // Bounded relative component; never an ambient path.
        std::uint32_t depth{};
        Metadata metadata{};
    };
    struct RootObservation final {
        Root root{Root::PackageRoot};
        RootResult result{};
        Presence presence{Presence::Unknown};
        std::uint32_t metadata_error{not_called};
        std::optional<std::uint32_t> object_index{};
    };
    enum class Status { Incomplete, Observed };
    enum class Reason { None, Limits, Source, Provenance, Root, Metadata, Name, Identity, Bounds, Changed, Exception };
    struct Report final {
        Status status{Status::Incomplete};
        Reason reason{Reason::None};
        Provenance provenance{Provenance::Unknown};
        std::uint32_t begin_error{not_called};
        std::array<RootObservation, 4> roots{};
        std::vector<Object> objects{};
        std::uint64_t total_bytes{}; // owner SID, raw SD and UTF-16 relative names.
        ValidationResult validation{};
    };
    class ReadLease final {
      public:
        // One trusted worker only, not thread-safe. Reentrant operations are
        // refused as BUSY without invoking the port or replacing cached state.
        ~ReadLease();
        ReadLease(const ReadLease&) = delete;
        ReadLease& operator=(const ReadLease&) = delete;
        [[nodiscard]] ValidationResult revalidate() noexcept;
        // Closed+error0 acknowledges only this read session's handles, never
        // worker stop, profile cleanup, native ownership or mutation authority.
        // NotAttempted+BUSY means no close was attempted; explicit close may be
        // called again after the current read operation has returned.
        [[nodiscard]] CloseResult close() noexcept;
        [[nodiscard]] CloseResult closeResult() const noexcept;

      private:
        friend class AppContainerStorageInventory;
        struct Impl;
        explicit ReadLease(std::unique_ptr<Impl> impl);
        std::unique_ptr<Impl> impl_;
    };
    struct Result final {
        Report report{};
        std::shared_ptr<ReadLease> lease{};
    };
    [[nodiscard]] static Result inspect(Source& source, Limits limits);
    [[nodiscard]] static Result inspect(Source& source);
};
} // namespace music_player::sandbox_probe
