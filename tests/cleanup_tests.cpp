#include "disktool/core/cleanup.h"
#include "disktool/core/scanner.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <set>
#include <stdexcept>
#include <string>
#include <system_error>

namespace fs = std::filesystem;

using disktool::core::CleanupOptions;
using disktool::core::CleanupResult;
using disktool::core::ScanResult;
using disktool::core::Scanner;
using disktool::core::findEmptyEntries;

namespace {

class TempDir {
public:
    TempDir()
    {
        static std::uintmax_t counter = 0;
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        m_path = fs::temp_directory_path()
            / ("disktool_cleanup_test_" + std::to_string(stamp) + "_"
               + std::to_string(++counter));
        fs::create_directories(m_path);
    }

    ~TempDir()
    {
        std::error_code error;
        fs::remove_all(m_path, error);
    }

    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
    TempDir(TempDir&&) = delete;
    TempDir& operator=(TempDir&&) = delete;

    [[nodiscard]] const fs::path& path() const { return m_path; }

private:
    fs::path m_path;
};

void writeFile(const fs::path& path, const std::string& content)
{
    fs::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary);
    stream << content;
    if (!stream) {
        throw std::runtime_error("failed to write test file: " + path.string());
    }
}

bool createDirectoryLink(const fs::path& link, const fs::path& target,
                         std::error_code& error)
{
    error.clear();
    fs::create_directory_symlink(target, link, error);
    if (!error) {
        return true;
    }

#ifdef _WIN32
    const std::wstring command = L"cmd /c mklink /J \"" + link.wstring() + L"\" \""
        + target.wstring() + L"\" >nul 2>&1";
    if (_wsystem(command.c_str()) == 0) {
        std::error_code checkError;
        if (fs::is_directory(link, checkError) && !checkError) {
            error.clear();
            return true;
        }
    }
#endif

    return false;
}

CleanupResult find(const fs::path& root, CleanupOptions options = {})
{
    const auto scan = Scanner{}.scan(root);
    REQUIRE(scan.errors.empty());
    return findEmptyEntries(scan, root, options);
}

std::set<fs::path> pathsOf(
    const std::vector<disktool::core::CleanupItem>& items)
{
    std::set<fs::path> paths;
    for (const auto& item : items) {
        paths.insert(item.path);
    }
    return paths;
}

}  // namespace

TEST_CASE("findEmptyEntries finds empty files and topmost empty directories",
          "[cleanup]")
{
    TempDir temp;
    const auto root = temp.path() / "root";

    writeFile(root / "zero.txt", "");
    writeFile(root / "data.txt", "0123456789");
    writeFile(root / "withEmpty" / "only-empty.txt", "");
    fs::create_directories(root / "withEmpty" / "nestedEmpty");
    fs::create_directories(root / "emptyDir" / "nested");

    const auto result = find(root);

    CHECK(pathsOf(result.emptyFiles) == std::set<fs::path>{root / "zero.txt"});

    const std::set<fs::path> expectedDirectories{
        root / "withEmpty", root / "emptyDir"};
    CHECK(pathsOf(result.emptyDirectories) == expectedDirectories);

    // Nested empty directories are covered by their topmost parent.
    CHECK(pathsOf(result.emptyDirectories).count(
              root / "withEmpty" / "nestedEmpty")
          == 0);
    CHECK(pathsOf(result.emptyDirectories).count(root / "emptyDir" / "nested")
          == 0);

    CHECK(result.totalWastedBytes == 0);
    for (const auto& item : result.emptyFiles) {
        CHECK_FALSE(item.isDirectory);
        CHECK(item.size == 0);
    }
    for (const auto& item : result.emptyDirectories) {
        CHECK(item.isDirectory);
        CHECK(item.size == 0);
    }
}

TEST_CASE("findEmptyEntries can return only files", "[cleanup]")
{
    TempDir temp;
    const auto root = temp.path() / "root";

    writeFile(root / "zero.txt", "");
    writeFile(root / "data.txt", "data");
    writeFile(root / "withEmpty" / "only-empty.txt", "");
    fs::create_directories(root / "emptyDir");

    CleanupOptions options;
    options.includeEmptyDirectories = false;

    const auto result = find(root, options);

    CHECK(result.emptyDirectories.empty());
    CHECK(pathsOf(result.emptyFiles)
          == std::set<fs::path>{root / "zero.txt",
                                root / "withEmpty" / "only-empty.txt"});
}

TEST_CASE("findEmptyEntries can return only truly empty directories",
          "[cleanup]")
{
    TempDir temp;
    const auto root = temp.path() / "root";

    writeFile(root / "zero.txt", "");
    writeFile(root / "withEmpty" / "only-empty.txt", "");
    fs::create_directories(root / "emptyDir" / "nested");

    CleanupOptions options;
    options.includeEmptyFiles = false;

    const auto result = find(root, options);

    CHECK(result.emptyFiles.empty());
    // withEmpty contains a zero-byte file, which is content when empty files
    // are not part of this cleanup.
    CHECK(pathsOf(result.emptyDirectories)
          == std::set<fs::path>{root / "emptyDir"});
}

TEST_CASE("findEmptyEntries returns nothing for an empty scan", "[cleanup]")
{
    const fs::path root = "C:/virtual/empty-root";

    const auto result = findEmptyEntries(ScanResult{}, root);

    CHECK(result.emptyFiles.empty());
    CHECK(result.emptyDirectories.empty());
    CHECK(result.totalWastedBytes == 0);
}

TEST_CASE("findEmptyEntries ignores entries outside the root", "[cleanup]")
{
    const fs::path root = "C:/virtual/root";

    ScanResult scan;
    scan.entries.push_back(
        disktool::core::FileEntry{root / "inside.txt", 0, {}, false});
    scan.entries.push_back(disktool::core::FileEntry{
        root.parent_path() / "outside.txt", 0, {}, false});

    CleanupOptions options;
    options.includeEmptyDirectories = false;

    const auto result = findEmptyEntries(scan, root, options);

    CHECK(pathsOf(result.emptyFiles) == std::set<fs::path>{root / "inside.txt"});
}

TEST_CASE("findEmptyEntries ignores symlinks and keeps their parent",
          "[cleanup]")
{
    TempDir temp;
    const auto root = temp.path() / "root";
    const auto target = temp.path() / "target";
    fs::create_directories(root / "onlyLink");
    fs::create_directories(target);

    std::error_code linkError;
    if (!createDirectoryLink(root / "onlyLink" / "link", target, linkError)) {
        SKIP("cannot create a directory link: " + linkError.message());
    }

    const auto result = find(root);

    CHECK(result.emptyFiles.empty());
    CHECK(result.emptyDirectories.empty());
}
