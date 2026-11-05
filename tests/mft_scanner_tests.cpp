#include "disktool/core/mft_scanner.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace fs = std::filesystem;

using disktool::core::MftScanner;

namespace {

#ifdef _WIN32
/// Returns a drive letter that is not present in the system, so the scanner
/// can be exercised without depending on a particular machine layout.
bool findMissingDrive(wchar_t& letter)
{
    const DWORD mask = GetLogicalDrives();
    if (mask == 0) {
        return false;
    }

    for (wchar_t candidate = L'Z'; candidate >= L'D'; --candidate) {
        const auto index = static_cast<unsigned>(candidate - L'A');
        if ((mask & (1u << index)) == 0) {
            letter = candidate;
            return true;
        }
    }
    return false;
}
#endif

}  // namespace

TEST_CASE("MftScanner rejects a drive that does not exist", "[mft]")
{
#ifdef _WIN32
    wchar_t letter = 0;
    if (!findMissingDrive(letter)) {
        SKIP("all drive letters are in use");
    }

    const fs::path root = std::wstring(1, letter) + L":\\";
    const auto result = MftScanner{}.scanVolume(root);

    CHECK_FALSE(result.success);
    CHECK(result.entries.empty());
    CHECK_FALSE(result.errors.empty());
#else
    const auto result = MftScanner{}.scanVolume("Z:\\");
    CHECK_FALSE(result.success);
    CHECK(result.entries.empty());
    CHECK_FALSE(result.errors.empty());
#endif
}

TEST_CASE("MftScanner rejects an empty path", "[mft]")
{
    const auto result = MftScanner{}.scanVolume(fs::path{});

    CHECK_FALSE(result.success);
    CHECK(result.entries.empty());
    CHECK_FALSE(result.errors.empty());
}

TEST_CASE("MftScanner rejects a path that is not a directory", "[mft]")
{
    static std::uintmax_t counter = 0;
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path file = fs::temp_directory_path()
        / ("disktool_mft_test_" + std::to_string(stamp) + "_"
           + std::to_string(++counter) + ".tmp");

    {
        std::ofstream stream(file, std::ios::binary);
        stream << "not a directory";
    }

    const auto result = MftScanner{}.scanVolume(file);

    std::error_code error;
    fs::remove(file, error);

    CHECK_FALSE(result.success);
    CHECK(result.entries.empty());
    CHECK_FALSE(result.errors.empty());
}
