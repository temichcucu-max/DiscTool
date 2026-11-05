#include "disktool/core/cleanup.h"

#include "disktool/core/tree_builder.h"

#include <cstddef>
#include <functional>
#include <map>
#include <set>
#include <system_error>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace disktool::core {
namespace {

using PathSet = std::set<fs::path>;
using FilesByDirectory = std::map<fs::path, std::vector<fs::path>>;

bool isInsideRoot(const fs::path& path, const fs::path& root)
{
    const auto relative = path.lexically_relative(root);
    if (relative.empty() || relative.is_absolute()) {
        return false;
    }

    for (const auto& component : relative) {
        if (component == fs::path("..")) {
            return false;
        }
    }
    return true;
}

}  // namespace

CleanupResult findEmptyEntries(const ScanResult& scan,
                               const fs::path& root,
                               CleanupOptions options)
{
    CleanupResult result;

    if (!options.includeEmptyFiles && !options.includeEmptyDirectories) {
        return result;
    }

    FilesByDirectory emptyFilesByDirectory;
    PathSet directoriesWithBlockingContent;
    PathSet symlinkPaths;

    for (const auto& entry : scan.entries) {
        if (!isInsideRoot(entry.path, root)) {
            continue;
        }

        std::error_code linkError;
        const bool isLink = fs::is_symlink(entry.path, linkError) && !linkError;
        if (isLink) {
            // Never offer a link itself for deletion, and treat it as content
            // so its parent is not reported as an empty directory.
            symlinkPaths.insert(entry.path);
            directoriesWithBlockingContent.insert(entry.path.parent_path());
            continue;
        }

        if (entry.isDirectory) {
            continue;
        }

        if (entry.size == 0 && options.includeEmptyFiles) {
            emptyFilesByDirectory[entry.path.parent_path()].push_back(entry.path);
        } else {
            // Non-empty files always block. When empty files are not part of
            // this cleanup, they block directory emptiness as well.
            directoriesWithBlockingContent.insert(entry.path.parent_path());
        }
    }

    if (!options.includeEmptyDirectories) {
        // No directory may cover its empty files, so return every empty file.
        for (auto& [directory, files] : emptyFilesByDirectory) {
            (void)directory;
            for (const auto& file : files) {
                result.emptyFiles.push_back(CleanupItem{file, false, 0});
            }
        }
        return result;
    }

    auto tree = buildTree(scan, root);
    if (!tree) {
        return result;
    }

    // A node has blocking content when any file that should survive cleanup
    // lives in its subtree, or when it contains a symlink.
    std::map<const DirNode*, bool> hasContent;
    const std::function<bool(const DirNode&)> computeContent =
        [&](const DirNode& node) -> bool {
        bool content = directoriesWithBlockingContent.count(node.path) > 0
            || symlinkPaths.count(node.path) > 0;

        for (const auto& child : node.children) {
            if (computeContent(*child)) {
                content = true;
            }
        }

        hasContent[&node] = content;
        return content;
    };
    computeContent(*tree);

    const std::function<void(const DirNode&, bool)> collect =
        [&](const DirNode& node, bool isRoot) {
        if (symlinkPaths.count(node.path) > 0) {
            return;
        }

        const bool content = hasContent[&node];

        if (!isRoot && !content) {
            // Topmost empty directory. Its nested empty directories and empty
            // files are removed together with it, so do not emit them again.
            result.emptyDirectories.push_back(
                CleanupItem{node.path, true, 0});
            return;
        }

        if (options.includeEmptyFiles) {
            const auto files = emptyFilesByDirectory.find(node.path);
            if (files != emptyFilesByDirectory.end()) {
                for (const auto& file : files->second) {
                    result.emptyFiles.push_back(CleanupItem{file, false, 0});
                }
            }
        }

        for (const auto& child : node.children) {
            collect(*child, false);
        }
    };

    collect(*tree, true);

    for (const auto& item : result.emptyFiles) {
        result.totalWastedBytes += item.size;
    }

    return result;
}

}  // namespace disktool::core
