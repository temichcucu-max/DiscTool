#include "disktool/core/scanner.h"
#include "disktool/core/tree_builder.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

using disktool::core::DirNode;
using disktool::core::FileEntry;
using disktool::core::ScanResult;
using disktool::core::Scanner;
using disktool::core::buildTree;
using disktool::core::sortBySizeDescending;

namespace {

class TempDir {
public:
    TempDir()
    {
        static std::uintmax_t counter = 0;
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        m_path = fs::temp_directory_path()
            / ("disktool_tree_test_" + std::to_string(stamp) + "_"
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

const DirNode* findChild(const DirNode& parent, const std::string& name)
{
    for (const auto& child : parent.children) {
        if (child->name == name) {
            return child.get();
        }
    }
    return nullptr;
}

std::vector<std::string> childNames(const DirNode& parent)
{
    std::vector<std::string> names;
    names.reserve(parent.children.size());
    for (const auto& child : parent.children) {
        names.push_back(child->name);
    }
    return names;
}

FileEntry directoryEntry(const fs::path& path)
{
    FileEntry entry;
    entry.path = path;
    entry.isDirectory = true;
    return entry;
}

FileEntry fileEntry(const fs::path& path, std::uintmax_t size)
{
    FileEntry entry;
    entry.path = path;
    entry.size = size;
    entry.isDirectory = false;
    return entry;
}

}  // namespace

TEST_CASE("buildTree aggregates own and cumulative sizes", "[tree_builder]")
{
    TempDir temp;
    const auto root = temp.path() / "root";

    writeFile(root / "a" / "b" / "file1.txt", std::string(30, '1'));
    writeFile(root / "a" / "file2.txt", std::string(20, '2'));
    writeFile(root / "file3.txt", std::string(10, '3'));

    const auto scan = Scanner{}.scan(root);
    REQUIRE(scan.errors.empty());

    const auto tree = buildTree(scan, root);
    REQUIRE(tree != nullptr);

    CHECK(tree->name == "root");
    CHECK(tree->path == root);
    CHECK(tree->ownSize == 10);
    CHECK(tree->cumulativeSize == 60);
    CHECK(tree->ownFileCount == 1);
    CHECK(tree->cumulativeFileCount == 3);
    REQUIRE(tree->children.size() == 1);

    const auto* a = findChild(*tree, "a");
    REQUIRE(a != nullptr);
    CHECK(a->path == root / "a");
    CHECK(a->ownSize == 20);
    CHECK(a->cumulativeSize == 50);
    CHECK(a->ownFileCount == 1);
    CHECK(a->cumulativeFileCount == 2);
    REQUIRE(a->children.size() == 1);

    const auto* b = findChild(*a, "b");
    REQUIRE(b != nullptr);
    CHECK(b->path == root / "a" / "b");
    CHECK(b->ownSize == 30);
    CHECK(b->cumulativeSize == 30);
    CHECK(b->ownFileCount == 1);
    CHECK(b->cumulativeFileCount == 1);
    CHECK(b->children.empty());
}

TEST_CASE("sortBySizeDescending orders children recursively", "[tree_builder]")
{
    const fs::path root = fs::path("C:/virtual/root");

    ScanResult scan;
    scan.entries.push_back(directoryEntry(root / "alpha"));
    scan.entries.push_back(directoryEntry(root / "beta"));
    scan.entries.push_back(directoryEntry(root / "gamma"));
    scan.entries.push_back(directoryEntry(root / "beta" / "nested"));
    scan.entries.push_back(directoryEntry(root / "beta" / "other"));
    scan.entries.push_back(fileEntry(root / "alpha" / "a.bin", 10));
    scan.entries.push_back(fileEntry(root / "beta" / "b.bin", 20));
    scan.entries.push_back(fileEntry(root / "beta" / "nested" / "n.bin", 10));
    scan.entries.push_back(fileEntry(root / "beta" / "other" / "o.bin", 5));
    scan.entries.push_back(fileEntry(root / "gamma" / "g.bin", 20));

    const auto tree = buildTree(scan, root);
    REQUIRE(tree != nullptr);

    CHECK(childNames(*tree) == std::vector<std::string>{"alpha", "beta", "gamma"});

    sortBySizeDescending(*tree);

    // beta = 20 + 10 + 5 = 35, alpha = 10, gamma = 20.
    CHECK(childNames(*tree) == std::vector<std::string>{"beta", "gamma", "alpha"});

    const auto* beta = findChild(*tree, "beta");
    REQUIRE(beta != nullptr);
    CHECK(childNames(*beta) == std::vector<std::string>{"nested", "other"});
}

TEST_CASE("buildTree returns an empty root for an empty scan", "[tree_builder]")
{
    const fs::path root = fs::path("C:/virtual/empty-root");

    const auto tree = buildTree(ScanResult{}, root);
    REQUIRE(tree != nullptr);

    CHECK(tree->name == "empty-root");
    CHECK(tree->path == root);
    CHECK(tree->ownSize == 0);
    CHECK(tree->cumulativeSize == 0);
    CHECK(tree->ownFileCount == 0);
    CHECK(tree->cumulativeFileCount == 0);
    CHECK(tree->children.empty());
}

TEST_CASE("buildTree aggregates a five-level directory chain", "[tree_builder]")
{
    TempDir temp;
    const auto root = temp.path() / "root";

    writeFile(root / "d1" / "d2" / "d3" / "d4" / "d5" / "deep.bin",
              std::string(64, 'd'));
    writeFile(root / "d1" / "d2" / "mid.bin", std::string(16, 'm'));

    const auto scan = Scanner{}.scan(root);
    REQUIRE(scan.errors.empty());

    const auto tree = buildTree(scan, root);
    REQUIRE(tree != nullptr);

    CHECK(tree->cumulativeSize == 80);
    CHECK(tree->ownSize == 0);
    CHECK(tree->cumulativeFileCount == 2);

    const auto* d1 = findChild(*tree, "d1");
    REQUIRE(d1 != nullptr);
    CHECK(d1->cumulativeSize == 80);
    CHECK(d1->ownSize == 0);

    const auto* d2 = findChild(*d1, "d2");
    REQUIRE(d2 != nullptr);
    CHECK(d2->cumulativeSize == 80);
    CHECK(d2->ownSize == 16);
    CHECK(d2->ownFileCount == 1);
    CHECK(d2->cumulativeFileCount == 2);

    const auto* d3 = findChild(*d2, "d3");
    REQUIRE(d3 != nullptr);
    CHECK(d3->cumulativeSize == 64);
    CHECK(d3->ownSize == 0);

    const auto* d4 = findChild(*d3, "d4");
    REQUIRE(d4 != nullptr);
    CHECK(d4->cumulativeSize == 64);

    const auto* d5 = findChild(*d4, "d5");
    REQUIRE(d5 != nullptr);
    CHECK(d5->cumulativeSize == 64);
    CHECK(d5->ownSize == 64);
    CHECK(d5->ownFileCount == 1);
}

TEST_CASE("buildTree ignores entries outside the root", "[tree_builder]")
{
    const fs::path root = fs::path("C:/virtual/root");

    ScanResult scan;
    scan.entries.push_back(fileEntry(root / "inside.bin", 7));
    scan.entries.push_back(fileEntry(root.parent_path() / "outside.bin", 100));
    scan.entries.push_back(fileEntry(fs::path("D:/elsewhere/file.bin"), 200));

    const auto tree = buildTree(scan, root);
    REQUIRE(tree != nullptr);

    CHECK(tree->ownSize == 7);
    CHECK(tree->cumulativeSize == 7);
    CHECK(tree->children.empty());
}
