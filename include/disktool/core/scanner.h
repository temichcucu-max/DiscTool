#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <limits>
#include <string>
#include <vector>

namespace disktool::core {

/// One entry found while scanning. The scan root itself is not included.
struct FileEntry {
    std::filesystem::path path;
    std::uintmax_t size = 0;
    std::filesystem::file_time_type modified{};
    bool isDirectory = false;
};

struct ScanOptions {
    /// Follow directory symlinks. Disabled by default so symlink loops cannot
    /// hang the scanner.
    bool followSymlinks = false;

    /// Detect directory cycles while following links (symlinks, junctions,
    /// other reparse points). Every directory is compared with already visited
    /// ones via std::filesystem::equivalent; a repeat is reported in
    /// ScanResult::errors and is not descended into. Only has an effect when
    /// followSymlinks is true.
    bool detectSymlinkCycles = true;

    /// Include names starting with '.'. This is the only hidden-file heuristic
    /// available with plain std::filesystem; Windows hidden attributes are not
    /// inspected.
    bool includeHidden = false;

    /// Maximum entry depth. Direct children of the root have depth 1,
    /// maxDepth == 0 returns an empty result (after validating the root).
    std::uintmax_t maxDepth = std::numeric_limits<std::uintmax_t>::max();

    /// If not nullptr, the scanner checks this flag in its loop and stops
    /// early when it is true. The returned ScanResult contains the entries
    /// collected so far and an error "cancelled by user".
    std::atomic_bool* cancelToken = nullptr;

    /// Called from the scanner thread periodically (roughly every 1000
    /// entries) and once at the end, including cancellation and errors.
    /// files/dirs are the counters accumulated so far. May be nullptr.
    /// Must not throw; exceptions are swallowed by the scanner.
    std::function<void(std::uintmax_t files, std::uintmax_t dirs)> onProgress;

    /// Called from the scanner thread with up to 1000 newly found entries at a
    /// time (and once more for the final partial batch). The callback runs
    /// before ScanResult::entries is returned, which lets the UI show results
    /// while the scan is still running. May be nullptr. Must not throw.
    std::function<void(const std::vector<FileEntry>&)> onBatch;
};

struct ScanResult {
    std::vector<FileEntry> entries;
    /// Human-readable UTF-8 messages, one per failed operation.
    std::vector<std::string> errors;
    std::uintmax_t totalFiles = 0;
    std::uintmax_t totalDirectories = 0;
    std::uintmax_t totalBytes = 0;
    std::chrono::milliseconds duration{0};
};

/// Synchronous recursive directory scanner built on std::filesystem.
/// It never throws for filesystem failures: problems are reported in
/// ScanResult::errors instead. If root is not a directory, the result is empty
/// and contains a single "not a directory" error.
class Scanner {
public:
    explicit Scanner(ScanOptions options = {});

    [[nodiscard]] ScanResult scan(const std::filesystem::path& root) const;

private:
    ScanOptions m_options;
};

}  // namespace disktool::core
