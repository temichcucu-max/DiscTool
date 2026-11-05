#include "disktool/core/scanner.h"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <system_error>

namespace fs = std::filesystem;
using disktool::core::ScanOptions;
using disktool::core::ScanResult;
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

std::string utf8(const char8_t* text)
{
    return reinterpret_cast<const char*>(text);
}

void writeManyFiles(const fs::path& root, int count)
{
    fs::create_directories(root);
    for (int index = 0; index < count; ++index) {
        std::ofstream stream(root / ("file_" + std::to_string(index) + ".dat"),
                             std::ios::binary);
        stream << 'x';
        if (!stream) {
            throw std::runtime_error("failed to create test files");
        }
    }
}bool isValidUtf8(const std::string& text)
{
    std::size_t index = 0;
    while (index < text.size()) {
        const auto lead = static_cast<unsigned char>(text[index]);
        std::size_t continuation = 0;
        if (lead < 0x80) {
            ++index;
            continue;
        }
        if ((lead & 0xE0) == 0xC0) {
            continuation = 1;
        } else if ((lead & 0xF0) == 0xE0) {
            continuation = 2;
        } else if ((lead & 0xF8) == 0xF0) {
            continuation = 3;
        } else {
            return false;
        }

        if (index + continuation >= text.size()) {
            return false;
        }
        for (std::size_t offset = 1; offset <= continuation; ++offset) {
            if ((static_cast<unsigned char>(text[index + offset]) & 0xC0) != 0x80) {
                return false;
            }
        }
        index += continuation + 1;
    }
    return true;
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
    // Without Developer Mode / SeCreateSymbolicLinkPrivilege a symlink cannot
    // be created, but a junction can. std::filesystem resolves both the same
    // way, so the cycle detection path is still exercised.
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

TEST_CASE("Scanner reports a regular file root as not a directory", "[scanner]")
{
    TempDir temp;
    const auto file = temp.path() / "file.txt";
    writeFile(file, "data");

    ScanResult result;
    CHECK_NOTHROW(result = Scanner{}.scan(file));

    CHECK(result.entries.empty());
    CHECK(result.totalFiles == 0);
    CHECK(result.totalDirectories == 0);
    CHECK(result.totalBytes == 0);
    REQUIRE(result.errors.size() == 1);
    CHECK(result.errors.front().find("file.txt") != std::string::npos);
    CHECK(result.errors.front().find("not a directory") != std::string::npos);
}

TEST_CASE("Scanner keeps UTF-8 paths in error messages", "[scanner]")
{
    TempDir temp;
    const auto folder = temp.path() / fs::path(u8"тест-папка");
    fs::create_directories(folder);
    fs::remove(folder);

    const auto result = Scanner{}.scan(folder);

    REQUIRE(result.errors.size() == 1);
    const auto& message = result.errors.front();
    CHECK(message.find(utf8(u8"тест-папка")) != std::string::npos);
    CHECK(isValidUtf8(message));
}

TEST_CASE("Scanner rejects files below maxDepth in a directory chain", "[scanner]")
{
    TempDir temp;
    const auto root = temp.path() / "root";
    fs::create_directories(root / "a" / "b" / "c");
    writeFile(root / "a" / "b" / "c" / "file.txt", "data");

    SECTION("depth 1 sees only a")
    {
        ScanOptions options;
        options.maxDepth = 1;

        const auto result = Scanner(options).scan(root);

        CHECK(result.errors.empty());
        CHECK(result.totalDirectories == 1);
        CHECK(result.totalFiles == 0);
        CHECK(result.entries.size() == 1);
    }

    SECTION("depth 2 sees a and b")
    {
        ScanOptions options;
        options.maxDepth = 2;

        const auto result = Scanner(options).scan(root);

        CHECK(result.errors.empty());
        CHECK(result.totalDirectories == 2);
        CHECK(result.totalFiles == 0);
        CHECK(result.entries.size() == 2);
    }

    SECTION("depth 3 sees a, b and c")
    {
        ScanOptions options;
        options.maxDepth = 3;

        const auto result = Scanner(options).scan(root);

        CHECK(result.errors.empty());
        CHECK(result.totalDirectories == 3);
        CHECK(result.totalFiles == 0);
        CHECK(result.entries.size() == 3);
    }

    SECTION("depth 4 adds file.txt")
    {
        ScanOptions options;
        options.maxDepth = 4;

        const auto result = Scanner(options).scan(root);

        CHECK(result.errors.empty());
        CHECK(result.totalDirectories == 3);
        CHECK(result.totalFiles == 1);
        CHECK(result.entries.size() == 4);
    }
}

TEST_CASE("Scanner detects directory cycles when following links", "[scanner]")
{
    TempDir temp;
    const auto root = temp.path() / "root";
    fs::create_directories(root / "sub");

    std::error_code linkError;
    if (!createDirectoryLink(root / "sub" / "loop", root, linkError)) {
        SKIP("cannot create a directory link: " + linkError.message());
    }

    ScanOptions options;
    options.followSymlinks = true;

    const auto result = Scanner(options).scan(root);

    bool hasCycleError = false;
    for (const auto& message : result.errors) {
        if (message.find("directory cycle detected") != std::string::npos) {
            hasCycleError = true;
            break;
        }
    }

    CHECK(hasCycleError);
    // The loop entry itself is reported as an error and not descended into,
    // so only "sub" is counted as a directory.
    CHECK(result.totalDirectories == 1);
}

TEST_CASE("Scanner stops early when the cancel token is set", "[scanner]")
{
    TempDir temp;
    const auto root = temp.path() / "root";
    writeManyFiles(root, 5000);

    std::atomic_bool cancel{true};
    ScanOptions options;
    options.cancelToken = &cancel;

    const auto result = Scanner(options).scan(root);

    constexpr std::uintmax_t expectedMaximum = 5000;

    bool hasCancelError = false;
    for (const auto& message : result.errors) {
        if (message.find("cancelled by user") != std::string::npos) {
            hasCancelError = true;
            break;
        }
    }

    CHECK(hasCancelError);
    CHECK(result.totalFiles < expectedMaximum);
    CHECK(result.entries.size() < expectedMaximum);
}

TEST_CASE("Scanner invokes the progress callback", "[scanner]")
{
    TempDir temp;
    const auto root = temp.path() / "root";
    writeManyFiles(root, 1100);

    std::uintmax_t calls = 0;
    std::uintmax_t lastFiles = 0;
    std::uintmax_t lastDirs = 0;

    ScanOptions options;
    options.onProgress = [&](std::uintmax_t files, std::uintmax_t dirs) {
        ++calls;
        lastFiles = files;
        lastDirs = dirs;
    };

    const auto result = Scanner(options).scan(root);

    // One periodic report around 1000 entries plus the final one.
    CHECK(calls >= std::uintmax_t{2});
    CHECK(lastFiles == result.totalFiles);
    CHECK(lastDirs == result.totalDirectories);
}


