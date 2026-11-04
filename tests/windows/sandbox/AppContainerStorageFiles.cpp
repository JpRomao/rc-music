#include "AppContainerStorageFiles.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <winternl.h>

#include <QDebug>
#include <QDir>
#include <QScopeGuard>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

namespace music_player::sandbox_probe {
namespace {
using Files = AppContainerStorageFiles;
using Inventory = AppContainerStorageInventory;
using Security = AppContainerStorageSecurity;
constexpr DWORD sharing = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;
constexpr DWORD metadata_rights = FILE_READ_ATTRIBUTES | READ_CONTROL;
constexpr DWORD no_follow = FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS;
constexpr std::size_t maximum_handles = Inventory::maximum_objects * 8 + 512;
constexpr std::size_t maximum_nodes = Inventory::maximum_objects + 2;
constexpr std::uint64_t metadata_budget = Inventory::maximum_sd_bytes + 68;
struct Operation final {
    bool& busy;
    bool acquired;
    explicit Operation(bool& value) : busy(value), acquired(!value) {
        if (acquired)
            busy = true;
    }
    ~Operation() {
        if (acquired)
            busy = false;
    }
};
struct NativeInfo final {
    FILE_ID_INFO identity{};
    DWORD attributes{}, tag{}, links{};
    std::uint64_t size{};
    bool directory() const { return (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0; }
    bool reparse() const { return (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0; }
};
bool sameIdentity(const FILE_ID_INFO& left, const FILE_ID_INFO& right) {
    return left.VolumeSerialNumber == right.VolumeSerialNumber &&
           std::memcmp(left.FileId.Identifier, right.FileId.Identifier, sizeof(left.FileId.Identifier)) == 0;
}
bool sameInfo(const NativeInfo& left, const NativeInfo& right) {
    return sameIdentity(left.identity, right.identity) && left.attributes == right.attributes &&
           left.tag == right.tag && left.links == right.links && (left.directory() || left.size == right.size);
}
DWORD information(const HANDLE handle, NativeInfo& result) {
    if (GetFileType(handle) != FILE_TYPE_DISK)
        return ERROR_INVALID_DATA;
    BY_HANDLE_FILE_INFORMATION basic{};
    FILE_ATTRIBUTE_TAG_INFO tag{};
    if (!GetFileInformationByHandle(handle, &basic) ||
        !GetFileInformationByHandleEx(handle, FileIdInfo, &result.identity, sizeof(result.identity)) ||
        !GetFileInformationByHandleEx(handle, FileAttributeTagInfo, &tag, sizeof(tag)))
        return GetLastError();
    if (basic.dwFileAttributes != tag.FileAttributes)
        return ERROR_RETRY;
    result.attributes = tag.FileAttributes;
    result.tag = result.reparse() ? tag.ReparseTag : 0;
    result.links = basic.nNumberOfLinks;
    result.size = (std::uint64_t{basic.nFileSizeHigh} << 32) | basic.nFileSizeLow;
    return ERROR_SUCCESS;
}
std::wstring nativePath(const QString& path) {
    return (QStringLiteral("\\\\?\\") + QDir::toNativeSeparators(path)).toStdWString();
}
bool normalize(const QString& original, QString& result) {
    if (!original.isValidUtf16() || original.size() < 4 || original.size() > 30000 ||
        !((original[0] >= u'a' && original[0] <= u'z') || (original[0] >= u'A' && original[0] <= u'Z')) ||
        original[1] != u':' || (original[2] != u'/' && original[2] != u'\\'))
        return false;
    result = QDir::fromNativeSeparators(original);
    const auto components = result.mid(3).split('/');
    if (components.size() > 256)
        return false;
    for (const auto& component : components) {
        if (component.isEmpty() || component == "." || component == ".." || component.endsWith(' ') ||
            component.endsWith('.'))
            return false;
        for (const auto character : component)
            if (character.unicode() < 32 || QStringLiteral("<>:\"|?*").contains(character))
                return false;
    }
    return true;
}
bool component(const QString& name) {
    if (name.isEmpty() || name.size() > Inventory::maximum_name_units || !name.isValidUtf16() || name == "." ||
        name == "..")
        return false;
    for (const auto character : name)
        if (character.unicode() < 32 || QStringLiteral(":/\\").contains(character))
            return false;
    return true;
}
DWORD securityError(const Security::Result& result) {
    if (result.error == Security::Error::None)
        return ERROR_SUCCESS;
    if (result.error == Security::Error::SizeQuery && result.query_error != Security::not_called && result.query_error)
        return result.query_error;
    if (result.error == Security::Error::Read && result.read_error != Security::not_called && result.read_error)
        return result.read_error;
    if (result.error == Security::Error::Bounds)
        return ERROR_INSUFFICIENT_BUFFER;
    if (result.error == Security::Error::MissingCall)
        return ERROR_PROC_NOT_FOUND;
    return ERROR_INVALID_SECURITY_DESCR;
}
bool sameMetadata(const Inventory::Metadata& left, const Inventory::Metadata& right) {
    return left.identity == right.identity && left.type == right.type && left.link_count == right.link_count &&
           left.attributes == right.attributes && left.reparse_tag == right.reparse_tag &&
           left.reparse == right.reparse && left.owner_sid == right.owner_sid && left.raw_sd == right.raw_sd &&
           left.dacl == right.dacl && left.dacl_protected == right.dacl_protected &&
           left.sacl_not_queried == right.sacl_not_queried;
}
struct Entry final {
    QString name{};
    FILE_ID_128 id{};
    DWORD attributes{}, tag{};
};
bool sameEntry(const Entry& left, const Entry& right) {
    return left.name == right.name && left.attributes == right.attributes && left.tag == right.tag &&
           std::memcmp(left.id.Identifier, right.id.Identifier, sizeof(left.id.Identifier)) == 0;
}
struct Cursor final {
    std::size_t handle{};
    alignas(std::max_align_t) std::array<std::byte, 64 * 1024> buffer{};
    std::size_t offset{};
    bool started{false}, buffered{false}, ended{false};
};
struct Node final {
    Inventory::NodeRef ref{}, parent{};
    Inventory::Identity identity{};
    QString path{}, name{};
    std::size_t handle{};
    NativeInfo initial{};
    std::optional<Inventory::Metadata> captured{};
    std::unique_ptr<Cursor> cursor{};
    std::vector<Entry> entries{};
    std::vector<Inventory::NodeRef> children{};
};
struct Anchor final {
    QString path;
    std::size_t handle;
    FILE_ID_INFO identity;
};
struct Witness final {
    QString path;
    std::size_t handle;
};
struct OwnedHandle final {
    HANDLE value{INVALID_HANDLE_VALUE};
    bool attempted{false};
};
} // namespace

struct AppContainerStorageFiles::Impl final {
    RootPaths paths;
    Calls calls;
    Security security;
    bool busy{false}, configured{false}, valid_config{false}, revalidating{false};
    std::array<bool, 2> root_attempted{};
    std::array<Inventory::RootResult, 2> roots{};
    std::vector<OwnedHandle> handles{};
    std::vector<Anchor> anchors{};
    std::vector<Witness> witnesses{};
    std::vector<Node> nodes{};
    std::vector<FILE_ID_INFO> identities{};
    Inventory::ValidationResult validation{Inventory::Validation::Unchanged, 0};
    Inventory::CloseResult closure{};
    Impl(RootPaths captured_paths, Calls functions)
        : paths(std::move(captured_paths)), calls(std::move(functions)), security(calls.security) {}
    bool live() const { return closure.state == Inventory::CloseState::NotAttempted; }
    bool hasHandles() const {
        return std::any_of(handles.begin(), handles.end(),
                           [](const auto& handle) { return handle.value != INVALID_HANDLE_VALUE; });
    }
    HANDLE handle(const std::size_t index) const { return handles[index].value; }
    void unknown(const DWORD error) {
        if (validation.state == Inventory::Validation::Unchanged)
            validation = {Inventory::Validation::Unknown, error};
    }
    void changed(const DWORD error = 0) {
        if (validation.state == Inventory::Validation::Unchanged)
            validation = {Inventory::Validation::Changed, error};
    }
    bool configure() {
        if (!configured) {
            configured = true;
            QString package, ac;
            valid_config = calls.close_handle && normalize(paths.package_root, package) && normalize(paths.ac, ac);
            if (valid_config) {
                paths = {std::move(package), std::move(ac)};
                const auto drive = QDir::toNativeSeparators(paths.package_root.left(3)).toStdWString();
                const auto ac_drive = QDir::toNativeSeparators(paths.ac.left(3)).toStdWString();
                valid_config =
                    GetDriveTypeW(drive.c_str()) == DRIVE_FIXED && GetDriveTypeW(ac_drive.c_str()) == DRIVE_FIXED;
            }
        }
        return valid_config;
    }
    std::optional<std::size_t> slot() {
        for (std::size_t index = 0; index < handles.size(); ++index)
            if (handles[index].value == INVALID_HANDLE_VALUE) {
                handles[index].attempted = false;
                return index;
            }
        if (handles.size() >= maximum_handles)
            return {};
        handles.push_back({});
        return handles.size() - 1;
    }
    std::optional<std::size_t> open(const QString& path, const DWORD rights, DWORD& error,
                                    const std::optional<bool> directory = {}) {
        const auto index = slot();
        if (!index) {
            error = ERROR_TOO_MANY_OPEN_FILES;
            return {};
        }
        if (path.size() == 3 && path[1] == u':' && path[2] == u'/') {
            // Only the drive root uses an ambient path. Every filesystem
            // component below it is opened relative to a retained parent.
            if (calls.before_open)
                calls.before_open(path);
            handles[*index].value = CreateFileW(nativePath(path).c_str(), rights | FILE_LIST_DIRECTORY, sharing,
                                                nullptr, OPEN_EXISTING, no_follow, nullptr);
        } else {
            const auto separator = path.lastIndexOf('/');
            const auto parent_path = separator == 2 ? path.left(3) : path.left(separator);
            const auto name = path.mid(separator + 1);
            if (separator < 2 || !component(name)) {
                error = ERROR_INVALID_NAME;
                return {};
            }
            std::optional<std::size_t> parent_handle;
            std::optional<bool> expected_directory = directory;
            for (const auto& value : nodes) {
                if (value.path == parent_path && value.initial.directory() && !value.initial.reparse())
                    parent_handle = value.handle;
                if (!expected_directory && value.path == path)
                    expected_directory = value.initial.directory();
            }
            for (const auto& anchor : anchors) {
                if (!parent_handle && anchor.path == parent_path)
                    parent_handle = anchor.handle;
                if (!expected_directory && anchor.path == path)
                    expected_directory = true;
            }
            if (!parent_handle) {
                error = ERROR_PATH_NOT_FOUND;
                return {};
            }
            NativeInfo parent_info;
            error = information(handle(*parent_handle), parent_info);
            if (error || !parent_info.directory() || parent_info.reparse()) {
                if (!error)
                    error = ERROR_REPARSE_TAG_INVALID;
                return {};
            }
            const auto native_name = name.toStdWString();
            UNICODE_STRING unicode{};
            unicode.Length = static_cast<USHORT>(native_name.size() * sizeof(wchar_t));
            unicode.MaximumLength = unicode.Length;
            unicode.Buffer = const_cast<PWSTR>(native_name.data());
            OBJECT_ATTRIBUTES attributes{};
            attributes.Length = sizeof(attributes);
            attributes.RootDirectory = handle(*parent_handle);
            attributes.ObjectName = &unicode;
            attributes.Attributes = OBJ_DONT_REPARSE;
            IO_STATUS_BLOCK status_block{};
            HANDLE opened = nullptr;
            ULONG options = FILE_OPEN_REPARSE_POINT | FILE_SYNCHRONOUS_IO_NONALERT;
            if (expected_directory)
                options |= *expected_directory ? FILE_DIRECTORY_FILE : FILE_NON_DIRECTORY_FILE;
            // Test mutation happens after selecting the actual parent handle,
            // so a replaced path component cannot redirect this native open.
            if (calls.before_open)
                calls.before_open(path);
            const auto status = NtCreateFile(&opened, rights | SYNCHRONIZE, &attributes, &status_block, nullptr, 0,
                                             sharing, FILE_OPEN, options, nullptr, 0);
            if (opened && opened != INVALID_HANDLE_VALUE)
                handles[*index].value = opened;
            if (status < 0 || status == 0x103 || !opened || opened == INVALID_HANDLE_VALUE) {
                error = status == 0x103 ? ERROR_IO_PENDING : RtlNtStatusToDosError(status);
                if (!error)
                    error = ERROR_GEN_FAILURE;
                return {};
            }
        }
        if (handle(*index) == INVALID_HANDLE_VALUE) {
            error = GetLastError();
            return {};
        }
        error = ERROR_SUCCESS;
        return index;
    }
    std::optional<std::size_t> reopen(const std::size_t original, DWORD& error) {
        for (const auto& value : nodes)
            if (value.handle == original && value.initial.directory() && !value.initial.reparse())
                return open(value.path, metadata_rights | FILE_LIST_DIRECTORY, error, true);
        error = ERROR_DIRECTORY;
        return {};
    }
    bool closeHandle(const std::size_t index) noexcept {
        auto& owned = handles[index];
        if (owned.value == INVALID_HANDLE_VALUE)
            return true;
        if (owned.attempted)
            return false;
        owned.attempted = true;
        DWORD error = ERROR_GEN_FAILURE;
        try {
            error = calls.close_handle(reinterpret_cast<Handle>(owned.value));
        } catch (...) {
        }
        if (error) {
            closure = {Inventory::CloseState::Unknown, error};
            unknown(error);
            return false;
        }
        owned.value = INVALID_HANDLE_VALUE;
        return true;
    }
    bool missing(const DWORD error) const { return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND; }
    bool pinAncestors(const QString& path, DWORD& error) {
        QString prefix = path.left(3);
        const auto parts = path.mid(3).split('/');
        for (qsizetype index = 0; index < parts.size(); ++index) {
            if (index) {
                if (!prefix.endsWith('/'))
                    prefix += '/';
                prefix += parts[index - 1];
            }
            if (std::any_of(anchors.begin(), anchors.end(), [&](const auto& anchor) { return anchor.path == prefix; }))
                continue;
            const auto opened = open(prefix, metadata_rights | FILE_LIST_DIRECTORY, error, true);
            if (!opened)
                return false;
            NativeInfo info;
            error = information(handle(*opened), info);
            if (error || !info.directory() || info.reparse()) {
                if (!error)
                    error = ERROR_REPARSE_TAG_INVALID;
                return false;
            }
            anchors.push_back({prefix, *opened, info.identity});
        }
        return true;
    }
    Node* node(const Inventory::NodeRef ref) {
        return ref.value && ref.value <= nodes.size() ? &nodes[static_cast<std::size_t>(ref.value - 1)] : nullptr;
    }
    Inventory::Identity identity(const FILE_ID_INFO& native) {
        for (std::size_t index = 0; index < identities.size(); ++index)
            if (sameIdentity(identities[index], native))
                return {1, index + 1};
        identities.push_back(native);
        return {1, identities.size()};
    }
    std::optional<Inventory::NodeRef> add(const QString& path, const std::size_t native_handle, const NativeInfo& info,
                                          Inventory::NodeRef parent = {}, QString name = {}) {
        for (auto& existing : nodes)
            if (existing.path == path && sameIdentity(existing.initial.identity, info.identity)) {
                if (parent.value) {
                    existing.parent = parent;
                    existing.name = std::move(name);
                }
                return existing.ref;
            }
        if (nodes.size() >= maximum_nodes)
            return {};
        const Inventory::NodeRef ref{nodes.size() + 1};
        const auto id = identity(info.identity);
        nodes.push_back({ref, parent, id, path, std::move(name), native_handle, info});
        return ref;
    }
    bool bound(const QString& path, const FILE_ID_INFO& expected) {
        DWORD error = 0;
        const auto current = open(path, metadata_rights, error);
        if (!current) {
            if (missing(error))
                changed(error);
            else
                unknown(error);
            return false;
        }
        NativeInfo info;
        error = information(handle(*current), info);
        const bool matches = !error && sameIdentity(expected, info.identity);
        if (error)
            unknown(error);
        else if (!matches)
            changed();
        if (revalidating)
            return closeHandle(*current) && matches;
        if (!matches)
            return false;
        // Keep one captured name witness per binding, rather than one per
        // operation. Replaced witnesses receive an explicit, accounted close.
        const auto previous =
            std::find_if(witnesses.begin(), witnesses.end(), [&](const auto& witness) { return witness.path == path; });
        if (previous != witnesses.end()) {
            if (!closeHandle(previous->handle))
                return false;
            previous->handle = *current;
        } else {
            witnesses.push_back({path, *current});
        }
        return true;
    }
    bool ancestorsUnchanged() {
        for (const auto& anchor : anchors) {
            NativeInfo held;
            const auto error = information(handle(anchor.handle), held);
            if (error) {
                unknown(error);
                return false;
            }
            if (!held.directory() || held.reparse() || !sameIdentity(held.identity, anchor.identity)) {
                changed();
                return false;
            }
            if (!bound(anchor.path, anchor.identity))
                return false;
        }
        return true;
    }
    bool nodeBound(const Node& value) {
        if (value.parent.value) {
            const auto* parent = node(value.parent);
            if (!parent || !bound(parent->path, parent->initial.identity))
                return false;
        }
        return bound(value.path, value.initial.identity);
    }
    Inventory::RootResult root(const Inventory::Root requested) {
        const auto index = static_cast<std::size_t>(requested);
        if (index >= 2)
            return {{}, ERROR_INVALID_PARAMETER, Inventory::Presence::Unknown};
        if (!live())
            return {{}, ERROR_INVALID_HANDLE, Inventory::Presence::Unknown};
        if (root_attempted[index])
            return roots[index];
        root_attempted[index] = true;
        auto& result = roots[index];
        result = {{}, ERROR_INVALID_PARAMETER, Inventory::Presence::Unknown};
        if (!configure()) {
            unknown(result.error);
            return result;
        }
        const auto& path = index == 0 ? paths.package_root : paths.ac;
        DWORD error = 0;
        if (!pinAncestors(path, error)) {
            result.error = error;
            unknown(error);
            return result;
        }
        const auto opened = open(path, metadata_rights, error, true);
        if (!opened) {
            result = {{}, error, missing(error) ? Inventory::Presence::Missing : Inventory::Presence::Unknown};
            unknown(error);
            return result;
        }
        NativeInfo info;
        error = information(handle(*opened), info);
        if (error) {
            result.error = error;
            unknown(error);
            return result;
        }
        const auto ref = add(path, *opened, info);
        if (!ref) {
            result.error = ERROR_TOO_MANY_OPEN_FILES;
            unknown(result.error);
            return result;
        }
        result = {*ref, 0, Inventory::Presence::Present};
        return result;
    }
    Inventory::Metadata metadata(const Inventory::NodeRef ref, const std::uint64_t budget) {
        Inventory::Metadata result{.ref = ref, .error = ERROR_INVALID_HANDLE};
        auto* current = node(ref);
        if (!live() || !current)
            return result;
        if (!budget || budget > metadata_budget) {
            result.error = ERROR_INSUFFICIENT_BUFFER;
            return result;
        }
        if (!nodeBound(*current)) {
            result.error = validation.error ? validation.error : ERROR_INVALID_DATA;
            return result;
        }
        NativeInfo info;
        const auto native_error = information(handle(current->handle), info);
        if (native_error) {
            result.error = native_error;
            unknown(native_error);
            return result;
        }
        result.identity = current->identity;
        result.presence = Inventory::Presence::Present;
        result.type = info.directory() ? Inventory::Type::Directory : Inventory::Type::File;
        result.attributes = info.attributes;
        result.reparse_tag = info.tag;
        result.reparse = info.reparse();
        result.link_count = info.links;
        const auto security_result =
            security.read(Security::Object::File, reinterpret_cast<Handle>(handle(current->handle)), budget);
        result.error = securityError(security_result);
        if (result.error) {
            unknown(result.error);
            return result;
        }
        result.owner_sid = security_result.owner_sid;
        result.raw_sd = security_result.raw_sd;
        result.dacl = static_cast<Inventory::Dacl>(security_result.dacl);
        result.dacl_protected = security_result.dacl_protected;
        result.sacl_not_queried = security_result.sacl_not_queried;
        if (!sameInfo(current->initial, info) || (current->captured && !sameMetadata(*current->captured, result))) {
            changed();
            result.error = ERROR_INVALID_DATA;
            return result;
        }
        if (!current->captured)
            current->captured = result;
        return result;
    }
    DWORD entry(Cursor& cursor, Entry& result) {
        for (;;) {
            if (cursor.ended)
                return ERROR_NO_MORE_FILES;
            if (!cursor.buffered) {
                cursor.buffer.fill(std::byte{});
                const auto kind = cursor.started ? FileIdExtdDirectoryInfo : FileIdExtdDirectoryRestartInfo;
                cursor.started = true;
                if (!GetFileInformationByHandleEx(handle(cursor.handle), kind, cursor.buffer.data(),
                                                  static_cast<DWORD>(cursor.buffer.size()))) {
                    const auto error = GetLastError();
                    if (error != ERROR_NO_MORE_FILES)
                        qInfo() << "storage-files-enum-native-error" << error;
                    if (error == ERROR_NO_MORE_FILES)
                        cursor.ended = true;
                    return error;
                }
                cursor.offset = 0;
                cursor.buffered = true;
            }
            constexpr auto prefix = offsetof(FILE_ID_EXTD_DIR_INFO, FileName);
            const auto available = cursor.buffer.size() - cursor.offset;
            if (available < prefix)
                return ERROR_INVALID_DATA;
            const auto* record = reinterpret_cast<const FILE_ID_EXTD_DIR_INFO*>(cursor.buffer.data() + cursor.offset);
            const auto span = record->NextEntryOffset ? record->NextEntryOffset : available;
            if (span < prefix || span > available || record->FileNameLength % sizeof(wchar_t) ||
                record->FileNameLength > span - prefix ||
                record->FileNameLength / sizeof(wchar_t) > Inventory::maximum_name_units ||
                (record->NextEntryOffset && record->NextEntryOffset % alignof(FILE_ID_EXTD_DIR_INFO)))
                return ERROR_INVALID_DATA;
            result = {QString::fromWCharArray(record->FileName,
                                              static_cast<qsizetype>(record->FileNameLength / sizeof(wchar_t))),
                      record->FileId, record->FileAttributes,
                      (record->FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ? record->ReparsePointTag : 0};
            if (record->NextEntryOffset)
                cursor.offset += record->NextEntryOffset;
            else
                cursor.buffered = false;
            if (result.name == "." || result.name == "..")
                continue;
            return component(result.name) ? ERROR_SUCCESS : ERROR_INVALID_NAME;
        }
    }
    bool ensureCursor(Node& value, DWORD& error) {
        if (value.cursor)
            return true;
        if (!value.initial.directory() || value.initial.reparse()) {
            error = ERROR_DIRECTORY;
            return false;
        }
        const auto reopened = reopen(value.handle, error);
        if (!reopened)
            return false;
        NativeInfo info;
        error = information(handle(*reopened), info);
        if (error || !sameInfo(value.initial, info)) {
            if (!error)
                changed();
            return false;
        }
        value.cursor = std::make_unique<Cursor>();
        value.cursor->handle = *reopened;
        return true;
    }
    Inventory::ChildResult child(const Inventory::NodeRef parent_ref, const std::uint32_t index,
                                 const std::uint32_t units) {
        Inventory::ChildResult result{.error = ERROR_INVALID_PARAMETER};
        auto* parent = node(parent_ref);
        if (!live() || !parent) {
            result.error = ERROR_INVALID_HANDLE;
            return result;
        }
        if (!units || units > Inventory::maximum_name_units || index > parent->children.size())
            return result;
        if (!parent->initial.directory() || parent->initial.reparse()) {
            result.error = ERROR_DIRECTORY;
            return result;
        }
        if (!nodeBound(*parent)) {
            result.error = validation.error ? validation.error : ERROR_INVALID_DATA;
            return result;
        }
        if (index < parent->children.size())
            return {Inventory::ChildState::Child, 0, parent->entries[index].name, parent->children[index]};
        DWORD error = 0;
        if (!ensureCursor(*parent, error)) {
            result.error = error ? error : ERROR_INVALID_DATA;
            return result;
        }
        Entry item;
        error = entry(*parent->cursor, item);
        if (error == ERROR_NO_MORE_FILES)
            return {Inventory::ChildState::End, 0, {}, {}};
        if (error || item.name.size() > units) {
            result.error = error ? error : ERROR_MORE_DATA;
            unknown(result.error);
            return result;
        }
        if (parent->children.size() >= Inventory::maximum_objects) {
            result.error = ERROR_MORE_DATA;
            unknown(result.error);
            return result;
        }
        const auto child_path = parent->path + '/' + item.name;
        const auto opened = open(child_path, metadata_rights, error, (item.attributes & FILE_ATTRIBUTE_DIRECTORY) != 0);
        if (!opened) {
            result.error = error;
            unknown(error);
            return result;
        }
        NativeInfo info;
        error = information(handle(*opened), info);
        FILE_ID_INFO expected{parent->initial.identity.VolumeSerialNumber, item.id};
        if (error || !sameIdentity(expected, info.identity) || info.attributes != item.attributes ||
            info.tag != item.tag || !nodeBound(*parent)) {
            result.error = error ? error : ERROR_INVALID_DATA;
            if (error)
                unknown(error);
            else
                changed();
            return result;
        }
        const auto ref = add(child_path, *opened, info, parent_ref, item.name);
        if (!ref) {
            result.error = ERROR_TOO_MANY_OPEN_FILES;
            unknown(result.error);
            return result;
        }
        // add() may grow nodes, so no earlier Node pointer survives it.
        parent = node(parent_ref);
        parent->entries.push_back(item);
        parent->children.push_back(*ref);
        return {Inventory::ChildState::Child, 0, std::move(item.name), *ref};
    }
    bool enumerationUnchanged(Node& value) {
        if (!value.cursor)
            return true;
        if (!value.cursor->ended) {
            unknown(ERROR_IO_INCOMPLETE);
            return false;
        }
        DWORD error = 0;
        const auto fresh = reopen(value.handle, error);
        if (!fresh) {
            unknown(error);
            return false;
        }
        Cursor probe;
        probe.handle = *fresh;
        std::vector<Entry> current;
        for (std::uint32_t index = 0; index <= Inventory::maximum_objects; ++index) {
            Entry item;
            error = entry(probe, item);
            if (error == ERROR_NO_MORE_FILES)
                break;
            if (error || current.size() >= Inventory::maximum_objects) {
                unknown(error ? error : ERROR_MORE_DATA);
                break;
            }
            current.push_back(std::move(item));
        }
        const bool closed = closeHandle(*fresh);
        if (!closed || validation.state != Inventory::Validation::Unchanged)
            return false;
        if (!probe.ended || current.size() != value.entries.size()) {
            changed();
            return false;
        }
        auto expected = value.entries;
        const auto order = [](const Entry& left, const Entry& right) { return left.name < right.name; };
        std::sort(expected.begin(), expected.end(), order);
        std::sort(current.begin(), current.end(), order);
        if (!std::equal(expected.begin(), expected.end(), current.begin(), sameEntry)) {
            changed();
            return false;
        }
        return true;
    }
    Inventory::ValidationResult revalidate() {
        if (!live())
            return {Inventory::Validation::Unknown, ERROR_INVALID_HANDLE};
        if (validation.state != Inventory::Validation::Unchanged)
            return validation;
        if (!root_attempted[0] && !root_attempted[1])
            return {Inventory::Validation::Unknown, ERROR_INVALID_HANDLE};
        revalidating = true;
        const auto reset = qScopeGuard([&] { revalidating = false; });
        if (!ancestorsUnchanged())
            return validation;
        for (auto& value : nodes) {
            if (!nodeBound(value))
                return validation;
            NativeInfo info;
            const auto error = information(handle(value.handle), info);
            if (error) {
                unknown(error);
                return validation;
            }
            if (!sameInfo(value.initial, info)) {
                changed();
                return validation;
            }
            if (value.captured) {
                const auto current = metadata(value.ref, metadata_budget);
                if (current.error) {
                    unknown(current.error);
                    return validation;
                }
            }
            if (!enumerationUnchanged(value))
                return validation;
        }
        return validation;
    }
};
AppContainerStorageFiles::Calls AppContainerStorageFiles::systemCalls() {
    return {Security::systemCalls(), [](const Handle handle) -> std::uint32_t {
                if (CloseHandle(reinterpret_cast<HANDLE>(handle)))
                    return std::uint32_t{ERROR_SUCCESS};
                const auto error = GetLastError();
                return static_cast<std::uint32_t>(error ? error : DWORD{ERROR_GEN_FAILURE});
            }};
}
AppContainerStorageFiles::AppContainerStorageFiles(RootPaths roots)
    : AppContainerStorageFiles(std::move(roots), systemCalls()) {}
AppContainerStorageFiles::AppContainerStorageFiles(RootPaths roots, Calls calls)
    : impl_(std::make_unique<Impl>(std::move(roots), std::move(calls))) {}
AppContainerStorageFiles::~AppContainerStorageFiles() {
    if (impl_->hasHandles())
        static_cast<void>(impl_.release());
}
AppContainerStorageFiles::Inventory::RootResult AppContainerStorageFiles::root(const Inventory::Root requested) {
    const Operation operation(impl_->busy);
    if (!operation.acquired)
        return {{}, ERROR_BUSY, Inventory::Presence::Unknown};
    try {
        return impl_->root(requested);
    } catch (...) {
        impl_->unknown(ERROR_GEN_FAILURE);
        return {{}, ERROR_GEN_FAILURE, Inventory::Presence::Unknown};
    }
}
AppContainerStorageFiles::Inventory::Metadata
AppContainerStorageFiles::readMetadata(const Inventory::NodeRef ref, const std::uint64_t maximum_bytes) {
    const Operation operation(impl_->busy);
    if (!operation.acquired)
        return {.ref = ref, .error = ERROR_BUSY};
    try {
        return impl_->metadata(ref, maximum_bytes);
    } catch (...) {
        impl_->unknown(ERROR_GEN_FAILURE);
        return {.ref = ref, .error = ERROR_GEN_FAILURE};
    }
}
AppContainerStorageFiles::Inventory::ChildResult
AppContainerStorageFiles::nextChild(const Inventory::NodeRef parent, const std::uint32_t index,
                                    const std::uint32_t maximum_units) {
    const Operation operation(impl_->busy);
    if (!operation.acquired)
        return {.error = ERROR_BUSY};
    try {
        return impl_->child(parent, index, maximum_units);
    } catch (...) {
        impl_->unknown(ERROR_GEN_FAILURE);
        return {.error = ERROR_GEN_FAILURE};
    }
}
AppContainerStorageFiles::Inventory::ValidationResult AppContainerStorageFiles::revalidate() noexcept {
    const Operation operation(impl_->busy);
    if (!operation.acquired)
        return {Inventory::Validation::Unknown, ERROR_BUSY};
    try {
        return impl_->revalidate();
    } catch (...) {
        impl_->unknown(ERROR_GEN_FAILURE);
        return impl_->validation;
    }
}
AppContainerStorageFiles::Inventory::CloseResult AppContainerStorageFiles::close() noexcept {
    const Operation operation(impl_->busy);
    if (!operation.acquired)
        return {Inventory::CloseState::NotAttempted, ERROR_BUSY};
    if (!impl_->live())
        return impl_->closure;
    impl_->closure = {Inventory::CloseState::Unknown, ERROR_GEN_FAILURE};
    for (std::size_t index = 0; index < impl_->handles.size(); ++index)
        if (!impl_->closeHandle(index))
            return impl_->closure;
    impl_->closure = {Inventory::CloseState::Closed, 0};
    return impl_->closure;
}
AppContainerStorageFiles::Inventory::CloseResult AppContainerStorageFiles::closeResult() const noexcept {
    return impl_->closure;
}
} // namespace music_player::sandbox_probe
