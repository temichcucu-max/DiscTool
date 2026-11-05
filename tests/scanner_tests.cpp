#include "disktool/core/scanner.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <system_error>

namespace fs = std::filesystem;
using disktool::core::ScanOptions;
using disktool::core::Scanner;

namespace {

class TempDir {
public:
    TempDir()
    {
        static std::uintmax_t counter = 0;
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        m_path = fs::temp_directory_path()
            / ("disktool_scanner_test_" + std::to_string(stamp) + "_"
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

fs::path makeNestedTree(const fs::path& root)
{
    fs::create_directories(root / "sub" / "nested");
    writeFile(root / "a.txt", "hello");             // 5 bytes
    writeFile(root / "b.bin", "abc");               // 3 bytes
    writeFile(root / "sub" / "c.txt", "1234567");   // 7 bytes
    writeFile(root / "sub" / "nested" / "d.txt", "xy");  // 2 bytes
    return root;
}

}  // namespace

TEST_CASE("Scanner counts files, directories and bytes", "[scanner]")
{
    TempDir temp;
    const auto root = makeNestedTree(temp.path() / "root");

    const auto result = Scanner{}.scan(root);

    CHECK(result.errors.empty());
    CHECK(result.totalFiles == 4);
    CHECK(result.totalDirectories == 2);
    CHECK(result.totalBytes == 17);
    CHECK(result.entries.size() == 6);
}

TEST_CASE("Scanner honours maxDepth", "[scanner]")
{
    TempDir temp;
    const auto root = makeNestedTree(temp.path() / "root");

    SECTION("depth 1 sees only direct children")
    {
        ScanOptions options;
        options.maxDepth = 1;

        const auto result = Scanner(options).scan(root);

        CHECK(result.errors.empty());
        CHECK(result.totalFiles == 2);
        CHECK(result.totalDirectories == 1);
        CHECK(result.totalBytes == 8);
        CHECK(result.entries.size() == 3);
    }

    SECTION("depth 2 excludes files below the limit")
    {
        ScanOptions options;
        options.maxDepth = 2;

        const auto result = Scanner(options).scan(root);

        CHECK(result.errors.empty());
        CHECK(result.totalFiles == 3);
        CHECK(result.totalDirectories == 2);
        CHECK(result.totalBytes == 15);
        CHECK(result.entries.size() == 5);
    }

    SECTION("depth 0 returns an empty result for an existing root")
    {
        ScanOptions options;
        options.maxDepth = 0;

        const auto result = Scanner(options).scan(root);

        CHECK(result.errors.empty());
        CHECK(result.entries.empty());
        CHECK(result.totalFiles == 0);
        CHECK(result.totalDirectories == 0);
        CHECK(result.totalBytes == 0);
    }
}

TEST_CASE("Scanner reports a missing root without throwing", "[scanner]")
{
    TempDir temp;
    const auto missing = temp.path() / "does-not-exist";

    const auto result = Scanner{}.scan(missing);

    CHECK(result.entries.empty());
    CHECK(result.totalFiles == 0);
    CHECK(result.totalDirectories == 0);
    CHECK(result.totalBytes == 0);
    REQUIRE(result.errors.size() == 1);
    CHECK(result.errors.front().find("does-not-exist") != std::string::npos);
}

TEST_CASE("Scanner skips dot names unless includeHidden is set", "[scanner]")
{
    TempDir temp;
    const auto root = temp.path() / "root";
    fs::create_directories(root);
    writeFile(root / "visible.txt", "12");
    writeFile(root / ".hidden", "1234");

    SECTION("hidden entries are skipped by default")
    {
        const auto result = Scanner{}.scan(root);

        CHECK(result.errors.empty());
        CHECK(result.totalFiles == 1);
        CHECK(result.totalBytes == 2);
        CHECK(result.entries.size() == 1);
    }

    SECTION("hidden entries are included on request")
    {
        ScanOptions options;
        options.includeHidden = true;

        const auto result = Scanner(options).scan(root);

        CHECK(result.errors.empty());
        CHECK(result.totalFiles == 2);
        CHECK(result.totalBytes == 6);
        CHECK(result.entries.size() == 2);
    }
}
