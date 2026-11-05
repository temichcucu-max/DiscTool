#pragma once

#include "disktool/core/scanner.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace disktool::core {

/// One directory in the aggregated scan tree.
///
/// Only directories are materialised as nodes: a file contributes its size and
/// one file count to the ownSize/ownFileCount of the directory that contains it.
/// This keeps cumulativeSize == ownSize + sum(children.cumulativeSize) exact.
/// isDirectory is reserved for a future version that exposes file leaves.
struct DirNode {
    std::filesystem::path path;
    std::string name;  ///< filename().u8string(), UTF-8
    std::uintmax_t ownSize = 0;  ///< bytes of files directly inside this directory
    std::uintmax_t cumulativeSize = 0;  ///< ownSize + all descendant files
    std::uintmax_t ownFileCount = 0;  ///< files directly inside this directory
    std::uintmax_t cumulativeFileCount = 0;  ///< ownFileCount + all descendants
    bool isDirectory = true;  ///< false only if file leaves are materialised
    std::vector<std::unique_ptr<DirNode>> children;
};

/// Builds a directory hierarchy from a flat ScanResult.
///
/// ScanResult does not contain the root itself (Scanner::scan excludes it), so
/// a root DirNode is created for `root` and filled from the entry paths.
/// Entries outside `root` and entries whose relative path contains ".." are
/// ignored. Missing intermediate directories are created defensively.
[[nodiscard]] std::unique_ptr<DirNode> buildTree(
    const ScanResult& scan,
    const std::filesystem::path& root);

/// Recursively sorts children by cumulativeSize descending (for the treemap).
/// Equal sizes are ordered by name to keep the result deterministic.
void sortBySizeDescending(DirNode& node);

}  // namespace disktool::core
