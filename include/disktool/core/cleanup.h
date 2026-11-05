#pragma once

#include "disktool/core/scanner.h"

#include <cstdint>
#include <filesystem>
#include <vector>

namespace disktool::core {

struct CleanupItem {
    std::filesystem::path path;
    bool isDirectory = false;
    /// Files normally have size 0 here; the field is kept for future cleanup
    /// rules (temporary files, old downloads, etc.).
    std::uintmax_t size = 0;
};

struct CleanupResult {
    std::vector<CleanupItem> emptyFiles;
    std::vector<CleanupItem> emptyDirectories;
    std::uintmax_t totalWastedBytes = 0;
};

/// Which kinds of entries the caller wants to clean up.
struct CleanupOptions {
    bool includeEmptyFiles = true;
    bool includeEmptyDirectories = true;
};

/// Finds empty files (size == 0) and empty directories.
///
/// A directory counts as empty when it contains no non-empty files and all of
/// its subdirectories are empty as well. With includeEmptyFiles == true,
/// zero-byte files are treated as removable content, so a directory containing
/// only zero-byte files is also reported as empty. This lets one cleanup pass
/// remove both the empty files and the directories they leave behind.
///
/// Only the topmost empty directory of every empty subtree is returned, so a
/// parent and its nested empty directory are never deleted twice. The scan root
/// itself is never returned. Symbolic links and junctions are ignored and also
/// prevent their parent from being treated as empty.
[[nodiscard]] CleanupResult findEmptyEntries(
    const ScanResult& scan,
    const std::filesystem::path& root,
    CleanupOptions options = {});

}  // namespace disktool::core
