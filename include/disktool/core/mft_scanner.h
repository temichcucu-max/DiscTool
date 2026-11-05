#pragma once

#include "disktool/core/scanner.h"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace disktool::core {

struct MftScanResult {
    /// Entries below the requested root. The root itself is not included.
    /// The USN journal exposes names, parent references and attributes, so
    /// file sizes and modification times are not available here (size == 0,
    /// modified == file_time_type{}). Use the regular Scanner when metadata is
    /// required.
    std::vector<FileEntry> entries;
    std::vector<std::string> errors;
    /// false when the volume is not NTFS, the process lacks Administrator
    /// rights, the drive does not exist, or enumeration failed.
    bool success = false;
};

struct MftScanOptions {
    /// Checked while enumerating the MFT and while rebuilding paths. When set,
    /// the scan stops with an "cancelled by user" error.
    std::atomic_bool* cancelToken = nullptr;

    /// Called periodically with the number of records processed so far.
    /// Counters cover the whole volume, not only the requested subtree, and
    /// are therefore an upper bound for the final result.
    std::function<void(std::uintmax_t files, std::uintmax_t dirs)> onProgress;
};

/// Fast NTFS reader based on the USN change journal (FSCTL_ENUM_USN_DATA).
///
/// Requires a local NTFS volume and Administrator privileges because the raw
/// volume handle is opened. On other file systems or without elevation the
/// result has success == false and an explanatory error, so the caller can
/// fall back to Scanner without special-casing error codes.
///
/// `rootDrive` can be a drive root ("C:\\") or any existing directory on an
/// NTFS volume. The whole volume is enumerated, then entries are filtered to
/// the requested subtree. Paths are reconstructed from
/// FileReferenceNumber / ParentFileReferenceNumber.
class MftScanner {
public:
    [[nodiscard]] MftScanResult scanVolume(
        const std::filesystem::path& rootDrive,
        MftScanOptions options = {}) const;
};

}  // namespace disktool::core
