#pragma once

#include "disktool/core/mft_scanner.h"
#include "disktool/core/scanner.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <string>
#include <utility>

namespace disktool::app {

/// Result of one background scan. Either the regular directory walk or the
/// MFT/USN journal was used; an MFT failure is reported to the UI so it can
/// warn the user and restart with the regular scanner.
struct ScanJobResult {
    disktool::core::ScanResult scan;
    bool mftFailed = false;
    bool cancelled = false;
    std::string mftError;
};

/// Runs synchronously on a worker thread.
inline ScanJobResult runScanJob(
    const std::filesystem::path& root,
    disktool::core::ScanOptions scanOptions,
    bool useMft,
    disktool::core::MftScanOptions mftOptions)
{
    const auto started = std::chrono::steady_clock::now();

    if (!useMft) {
        return ScanJobResult{
            disktool::core::Scanner(std::move(scanOptions)).scan(root),
            false, false, {}};
    }

    auto mft = disktool::core::MftScanner{}.scanVolume(
        root, std::move(mftOptions));

    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started);

    if (!mft.success) {
        ScanJobResult outcome;
        outcome.mftFailed = true;
        outcome.cancelled = std::any_of(
            mft.errors.begin(), mft.errors.end(),
            [](const std::string& message) {
                return message.find("cancelled by user") != std::string::npos;
            });
        outcome.mftError = mft.errors.empty() ? "MFT scan failed"
                                              : mft.errors.front();
        outcome.scan.duration = elapsed;
        return outcome;
    }

    disktool::core::ScanResult scan;
    scan.entries = std::move(mft.entries);
    scan.errors = std::move(mft.errors);
    scan.duration = elapsed;

    for (const auto& entry : scan.entries) {
        if (entry.isDirectory) {
            ++scan.totalDirectories;
        } else {
            ++scan.totalFiles;
            scan.totalBytes += entry.size;
        }
    }

    ScanJobResult outcome;
    outcome.scan = std::move(scan);
    return outcome;
}

}  // namespace disktool::app
