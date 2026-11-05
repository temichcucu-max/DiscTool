#include "disktool/core/mft_scanner.h"

#include <algorithm>
#include <cstddef>
#include <cwctype>
#include <exception>
#include <iterator>
#include <limits>
#include <string>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winioctl.h>
#endif

namespace fs = std::filesystem;

namespace disktool::core {

#ifdef _WIN32
namespace {

constexpr std::uint64_t kRecordNumberMask = 0x0000FFFFFFFFFFFFull;
constexpr std::uint64_t kRootRecordNumber = 5;
constexpr std::size_t kMaxPathDepth = 512;

struct Node {
    std::uint64_t parent = 0;
    std::wstring name;
    std::uint32_t attributes = 0;
};

struct VolumeHandle {
    HANDLE handle = INVALID_HANDLE_VALUE;

    VolumeHandle() = default;
    VolumeHandle(const VolumeHandle&) = delete;
    VolumeHandle& operator=(const VolumeHandle&) = delete;

    ~VolumeHandle()
    {
        if (handle != INVALID_HANDLE_VALUE) {
            CloseHandle(handle);
        }
    }
};

std::string toUtf8(const std::wstring& text)
{
    if (text.empty()) {
        return {};
    }

    const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(),
                                         static_cast<int>(text.size()),
                                         nullptr, 0, nullptr, nullptr);
    if (size <= 0) {
        return {};
    }

    std::string result(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(),
                        static_cast<int>(text.size()), result.data(), size,
                        nullptr, nullptr);
    return result;
}

std::string win32Message(DWORD code)
{
    LPWSTR buffer = nullptr;
    const DWORD length = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM
            | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, code, 0, reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);

    std::wstring message;
    if (length > 0 && buffer != nullptr) {
        message.assign(buffer, length);
        while (!message.empty()
               && (message.back() == L'\r' || message.back() == L'\n'
                   || message.back() == L' ')) {
            message.pop_back();
        }
    }
    if (buffer != nullptr) {
        LocalFree(buffer);
    }

    if (message.empty()) {
        message = L"unknown error";
    }
    return toUtf8(message);
}

std::string win32Error(const char* operation, DWORD code)
{
    std::string message = operation;
    message += ": ";
    message += win32Message(code);
    message += " (code ";
    message += std::to_string(code);
    message += ')';

    if (code == ERROR_ACCESS_DENIED) {
        message += " - Administrator privileges are required";
    }
    return message;
}

void notifyProgress(const MftScanOptions& options, std::uintmax_t files,
                    std::uintmax_t dirs) noexcept
{
    if (!options.onProgress) {
        return;
    }
    try {
        options.onProgress(files, dirs);
    } catch (...) {
        // A progress callback must never break the scan.
    }
}

bool isCancelled(const MftScanOptions& options) noexcept
{
    return options.cancelToken != nullptr && options.cancelToken->load();
}

bool startsWithIgnoreCase(const std::wstring& text, const std::wstring& prefix)
{
    if (text.size() < prefix.size()) {
        return false;
    }

    return CompareStringOrdinal(text.c_str(), static_cast<int>(prefix.size()),
                                prefix.c_str(), static_cast<int>(prefix.size()),
                                TRUE)
        == CSTR_EQUAL;
}

/// True when path is a strict descendant of root. Both are absolute native
/// paths; the comparison is case-insensitive like the Windows file system.
bool isInsideRoot(const std::wstring& path, const std::wstring& root)
{
    if (path.size() <= root.size() || !startsWithIgnoreCase(path, root)) {
        return false;
    }

    return root.back() == L'\\' || path[root.size()] == L'\\';
}

std::wstring normalizedRootText(const fs::path& root)
{
    fs::path normalized = root.lexically_normal();
    normalized.make_preferred();
    std::wstring text = normalized.wstring();

    // Keep "C:" for a drive root, but drop trailing separators elsewhere so
    // "C:\Users\" and "C:\Users" compare identically.
    while (text.size() > 2 && text.back() == L'\\') {
        text.pop_back();
    }
    return text;
}

/// Rebuilds the full path by walking ParentFileReferenceNumber up to record 5
/// (the NTFS root directory). Returns false for orphaned records and cycles.
bool buildPath(std::uint64_t reference,
               const std::unordered_map<std::uint64_t, Node>& nodes,
               const std::wstring& volumeRoot, std::wstring& result)
{
    const std::wstring* parts[kMaxPathDepth];
    std::size_t depth = 0;
    std::uint64_t current = reference;

    // FileReferenceNumber stores the 48-bit MFT record number in its low bits
    // and the sequence number in the high 16 bits. The root is record 5, but
    // its full FRN is e.g. 0x0005000000000005, so always compare the masked
    // record number here.
    while ((current & kRecordNumberMask) != kRootRecordNumber) {
        if (depth >= kMaxPathDepth) {
            return false;
        }

        const auto found = nodes.find(current);
        if (found == nodes.end()) {
            return false;
        }

        const Node& node = found->second;
        parts[depth++] = &node.name;

        if (node.parent == current || node.parent == 0) {
            return false;  // cycle or orphan
        }
        current = node.parent;
    }

    if (depth == 0) {
        return false;  // the root itself is not an entry
    }

    result = volumeRoot;  // ends with a separator
    for (std::size_t index = depth; index-- > 0;) {
        result += *parts[index];
        if (index != 0) {
            result += L'\\';
        }
    }
    return true;
}

MftScanResult scanVolumeWindows(const fs::path& rootDrive,
                                const MftScanOptions& options)
{
    MftScanResult result;

    if (rootDrive.empty()) {
        result.errors.push_back("MFT scanner: empty path");
        return result;
    }

    std::error_code pathError;
    const fs::path absolute = fs::absolute(rootDrive, pathError);
    if (pathError) {
        result.errors.push_back("MFT scanner: " + pathError.message());
        return result;
    }

    if (!fs::is_directory(absolute, pathError) || pathError) {
        result.errors.push_back("MFT scanner: not a directory: "
                                + toUtf8(absolute.wstring()));
        return result;
    }

    const std::wstring rootName = absolute.root_name().wstring();
    if (rootName.size() != 2 || rootName[1] != L':'
        || !std::iswalpha(rootName[0])) {
        result.errors.push_back(
            "MFT scanner: only local drive letters are supported");
        return result;
    }

    const std::wstring volumeRoot = rootName + L"\\";
    WCHAR fileSystem[MAX_PATH + 1]{};
    if (!GetVolumeInformationW(volumeRoot.c_str(), nullptr, 0, nullptr, nullptr,
                               nullptr, fileSystem,
                               static_cast<DWORD>(std::size(fileSystem)))) {
        result.errors.push_back(win32Error("GetVolumeInformationW",
                                           GetLastError()));
        return result;
    }

    if (_wcsicmp(fileSystem, L"NTFS") != 0) {
        result.errors.push_back("MFT scanner: volume " + toUtf8(rootName)
                                + " is not NTFS (found "
                                + toUtf8(fileSystem) + ")");
        return result;
    }

    const std::wstring volumePath = L"\\\\.\\" + rootName;
    VolumeHandle volume;
    volume.handle = CreateFileW(
        volumePath.c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, 0, nullptr);

    if (volume.handle == INVALID_HANDLE_VALUE) {
        result.errors.push_back(
            win32Error("CreateFileW on the NTFS volume", GetLastError()));
        return result;
    }

    try {
        std::unordered_map<std::uint64_t, Node> nodes;

        // Use the MFT size as a rough capacity hint. Failure is harmless.
        NTFS_VOLUME_DATA_BUFFER volumeData{};
        DWORD bytesReturned = 0;
        if (DeviceIoControl(volume.handle, FSCTL_GET_NTFS_VOLUME_DATA, nullptr,
                            0, &volumeData, sizeof(volumeData),
                            &bytesReturned, nullptr)
            && volumeData.BytesPerFileRecordSegment > 0) {
            const auto estimate = static_cast<std::uint64_t>(
                volumeData.MftValidDataLength.QuadPart)
                / volumeData.BytesPerFileRecordSegment;
            nodes.reserve(static_cast<std::size_t>(
                std::min<std::uint64_t>(estimate, 8'000'000)));
        }

        MFT_ENUM_DATA_V0 enumData{};
        enumData.StartFileReferenceNumber = 0;
        enumData.LowUsn = 0;
        enumData.HighUsn = std::numeric_limits<LONGLONG>::max();

        std::vector<BYTE> buffer(256 * 1024);
        std::uintmax_t files = 0;
        std::uintmax_t dirs = 0;
        std::uintmax_t unsupportedVersion = 0;
        bool cancelled = false;
        bool enumerationFailed = false;

        for (;;) {
            if (isCancelled(options)) {
                cancelled = true;
                break;
            }

            bytesReturned = 0;
            const BOOL ok = DeviceIoControl(
                volume.handle, FSCTL_ENUM_USN_DATA, &enumData,
                sizeof(enumData), buffer.data(),
                static_cast<DWORD>(buffer.size()), &bytesReturned, nullptr);

            if (!ok) {
                const DWORD error = GetLastError();
                if (error == ERROR_HANDLE_EOF || error == ERROR_NO_MORE_FILES) {
                    break;
                }
                result.errors.push_back(
                    win32Error("FSCTL_ENUM_USN_DATA", error));
                enumerationFailed = true;
                break;
            }

            if (bytesReturned < sizeof(USN)) {
                break;
            }

            const USN nextStart =
                *reinterpret_cast<const USN*>(buffer.data());
            const BYTE* cursor = buffer.data() + sizeof(USN);
            const BYTE* const end = buffer.data() + bytesReturned;
            std::uintmax_t processed = 0;

            while (cursor < end) {
                if ((processed & 0x3FF) == 0 && isCancelled(options)) {
                    cancelled = true;
                    break;
                }
                ++processed;

                if (static_cast<std::size_t>(end - cursor)
                    < offsetof(USN_RECORD_V2, FileName)) {
                    break;
                }

                const auto* record =
                    reinterpret_cast<const USN_RECORD_V2*>(cursor);
                const std::size_t recordLength = record->RecordLength;
                if (recordLength
                        < offsetof(USN_RECORD_V2, FileName)
                            + record->FileNameLength
                    || recordLength
                        > static_cast<std::size_t>(end - cursor)) {
                    break;  // malformed buffer; the next call starts over
                }

                if (record->MajorVersion != 2) {
                    ++unsupportedVersion;
                    cursor += recordLength;
                    continue;
                }

                const std::uint64_t reference = record->FileReferenceNumber;
                const std::uint64_t recordNumber =
                    reference & kRecordNumberMask;

                // Records 0..15 are NTFS metadata ($MFT, $LogFile, $Extend,
                // ...) and are not addressable as normal user paths. The root
                // (5) is excluded as well: Scanner never returns its root.
                if (recordNumber < 16) {
                    cursor += recordLength;
                    continue;
                }

                const std::wstring name(
                    record->FileName,
                    static_cast<std::size_t>(record->FileNameLength)
                        / sizeof(WCHAR));
                if (name.empty()) {
                    cursor += recordLength;
                    continue;
                }

                const bool isDirectory =
                    (record->FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
                if (isDirectory) {
                    ++dirs;
                } else {
                    ++files;
                }

                nodes.try_emplace(
                    reference,
                    Node{record->ParentFileReferenceNumber, name,
                         record->FileAttributes});

                cursor += recordLength;
            }

            notifyProgress(options, files, dirs);

            if (cancelled) {
                break;
            }

            if (bytesReturned == 0 || nextStart == 0
                || static_cast<std::uint64_t>(nextStart)
                    == enumData.StartFileReferenceNumber) {
                break;
            }
            enumData.StartFileReferenceNumber =
                static_cast<std::uint64_t>(nextStart);
        }

        if (cancelled) {
            result.errors.push_back("cancelled by user");
            return result;
        }

        if (enumerationFailed) {
            return result;
        }

        if (unsupportedVersion > 0) {
            result.errors.push_back(
                "MFT scanner: skipped "
                + std::to_string(unsupportedVersion)
                + " records with an unsupported USN version");
        }

        const std::wstring rootText = normalizedRootText(absolute);
        result.entries.reserve(nodes.size());

        std::vector<FileEntry> batch;
        if (options.onBatch) {
            batch.reserve(1000);
        }
        const auto flushBatch = [&options, &batch] {
            if (batch.empty() || !options.onBatch) {
                return;
            }
            try {
                options.onBatch(batch);
            } catch (...) {
                // A batch callback must never break the scan.
            }
            batch.clear();
        };

        std::uintmax_t visited = 0;
        for (const auto& [reference, node] : nodes) {
            if ((visited & 0x3FF) == 0 && isCancelled(options)) {
                result.errors.push_back("cancelled by user");
                result.entries.clear();
                return result;
            }
            ++visited;

            std::wstring fullPath;
            if (!buildPath(reference, nodes, volumeRoot, fullPath)) {
                continue;  // orphaned record (for example under $Extend)
            }

            if (!isInsideRoot(fullPath, rootText)) {
                continue;
            }

            FileEntry entry;
            entry.path = fs::path(std::move(fullPath));
            entry.isDirectory = (node.attributes & FILE_ATTRIBUTE_DIRECTORY)
                != 0;

            if (options.onBatch) {
                batch.push_back(entry);
                if (batch.size() >= 1000) {
                    flushBatch();
                }
            }
            result.entries.push_back(std::move(entry));
        }

        flushBatch();
        notifyProgress(options, files, dirs);
        result.success = true;
    } catch (const std::exception& exception) {
        result.errors.push_back(std::string("MFT scanner: ") + exception.what());
    } catch (...) {
        result.errors.push_back("MFT scanner: unknown error");
    }

    return result;
}

}  // namespace
#endif  // _WIN32

MftScanResult MftScanner::scanVolume(const fs::path& rootDrive,
                                     MftScanOptions options) const
{
#ifdef _WIN32
    return scanVolumeWindows(rootDrive, options);
#else
    (void)rootDrive;
    (void)options;

    MftScanResult result;
    result.errors.push_back("MFT scanning is only available on Windows");
    return result;
#endif
}

}  // namespace disktool::core
