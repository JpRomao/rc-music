#pragma once

#include <QByteArray>
#include <QByteArrayView>
#include <QString>

#include <cstdint>
#include <memory>

namespace music_player::sandbox_probe {
// Test-only transport. This class never creates a directory, replaces a file,
// repairs an ACL or removes anything. Construction does no native I/O.
class AppContainerObserverFiles final {
  public:
    static constexpr qsizetype maximum_file_size = 16 * 1024;
    enum class Slot { Request, Ready, Result };
    enum class OpenStatus { Opened, Unsafe, IoError };
    enum class ReadStatus { Missing, Busy, Ready, Unsafe, IoError, TooLarge };
    enum class WriteStatus { Written, Exists, Unsafe, IoError, TooLarge };
    struct Config final {
        QString directory{};
        // Empty bootstraps from the validated directory owner, never from JSON.
        QByteArray driver_sid{};
    };
    struct OpenResult final {
        OpenStatus status{OpenStatus::IoError};
        std::uint32_t error{};
    };
    class ReadLease final {
      public:
        ~ReadLease();
        ReadLease(const ReadLease&) = delete;
        ReadLease& operator=(const ReadLease&) = delete;

      private:
        struct Impl;
        explicit ReadLease(std::unique_ptr<Impl> impl);
        std::unique_ptr<Impl> impl_;
        friend class AppContainerObserverFiles;
    };
    struct ReadResult final {
        ReadStatus status{ReadStatus::IoError};
        QByteArray bytes{};
        // Retains the validated file and directory pins while bytes are used.
        std::shared_ptr<const ReadLease> lease{};
        std::uint32_t error{};
    };
    struct WriteResult final {
        WriteStatus status{WriteStatus::IoError};
        std::uint32_t error{};
    };
    explicit AppContainerObserverFiles(Config config);
    ~AppContainerObserverFiles();
    AppContainerObserverFiles(const AppContainerObserverFiles&) = delete;
    AppContainerObserverFiles& operator=(const AppContainerObserverFiles&) = delete;
    [[nodiscard]] OpenResult open();
    // Empty until Opened; immutable thereafter. This is not profile authority.
    [[nodiscard]] const QByteArray& driverSid() const noexcept;
    [[nodiscard]] ReadResult read(Slot slot) const;
    [[nodiscard]] WriteResult writeNew(Slot slot, QByteArrayView bytes) const;

  private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
};
} // namespace music_player::sandbox_probe
