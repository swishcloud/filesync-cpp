#include <boost/test/unit_test.hpp>
#include "filesync.h"
#include <string>
#include <vector>
#include <optional>

// Your project headers (adjust paths/names as needed)
// #include "SqliteDbSchema.h"
// #include "SqliteFileRepository.h"
// #include "UuidGenerator.h"

static void dumpTree(IFileRepository& repo, const std::string& parentId, int depth = 0)
{
    const auto items = repo.listChildren(parentId);

    for (const auto& it : items) {
        std::string indent(depth * 2, ' ');
        BOOST_TEST_MESSAGE(
            indent +
            (it.isDir ? "[DIR]  " : "[FILE] ") +
            it.name +
            " (id=" + it.id +
            ", parent=" + it.parentId +
            ", serverFileId=" + (it.serverFileId.empty() ? "(empty)" : it.serverFileId) +
            ", localPath=" + (it.localPath.empty() ? "(empty)" : it.localPath) +
            ", placeholder=" + std::string(it.isPlaceholder ? "true" : "false") +
            ", pinned=" + std::string(it.pinnedOffline ? "true" : "false") +
            ")"
        );

        if (it.isDir) {
            dumpTree(repo, it.id, depth + 1);
        }
    }
}

static bool containsName(const std::vector<FileItem>& items, const std::string& name, bool isDir)
{
    for (const auto& it : items) {
        if (it.name == name && it.isDir == isDir) return true;
    }
    return false;
}

static std::optional<FileItem> findByName(const std::vector<FileItem>& items, const std::string& name, bool isDir)
{
    for (const auto& it : items) {
        if (it.name == name && it.isDir == isDir) return it;
    }
    return std::nullopt;
}

BOOST_AUTO_TEST_CASE(DB_Full_CRUD_Test)
{
    filesync::UuidGenerator generator;
    const std::string dbpath = (std::filesystem::temp_directory_path() /
              ("filesync_test_" + generator.newUuid() + ".db")).string();


    // 1) Create schema
    SqliteDbSchema schema(dbpath);
    schema.createSchema();

    // 2) Construct repo with clear lifetime (no temporary binding)
    SqliteFileRepository repoImpl(dbpath, generator);
    IFileRepository& repo = repoImpl;

    // -------------------------
    // A. CREATE + LIST + GET
    // -------------------------
    const std::string folder123 = repo.createFolder("root", "folder123");
    const std::string folderABC = repo.createFolder("root", "folderABC");

    const std::string rootFile = repo.createFile("root", "root_file.txt");
    const std::string file1    = repo.createFile(folder123, "file1.txt");
    const std::string file2    = repo.createFile(folder123, "file2.txt");

    const std::string subdir   = repo.createFolder(folder123, "sub");
    const std::string deepFile = repo.createFile(subdir, "deep.txt");
    (void)deepFile;

    BOOST_TEST_MESSAGE("==== TREE AFTER CREATE ====");
    dumpTree(repo, "root");

    // listChildren(root)
    {
        auto rootItems = repo.listChildren("root");
        BOOST_TEST(containsName(rootItems, "folder123", true));
        BOOST_TEST(containsName(rootItems, "folderABC", true));
        BOOST_TEST(containsName(rootItems, "root_file.txt", false));
    }

    // getById(file2)
    {
        auto opt = repo.getById(file2);
        BOOST_TEST(static_cast<bool>(opt));
        BOOST_TEST(opt->id == file2);
        BOOST_TEST(opt->name == "file2.txt");
        BOOST_TEST(opt->parentId == folder123);
        BOOST_TEST(!opt->isDir);
    }

    // -------------------------
    // B. UPDATE LOCAL FIELDS
    // -------------------------
    repo.updateLocalPath(file2, "/tmp/folder123/file2.txt");
    repo.updateServerFileId(file2, "server-id-file2");

    {
        auto opt = repo.getById(file2);
        BOOST_TEST(static_cast<bool>(opt));
        BOOST_TEST(opt->localPath == "/tmp/folder123/file2.txt");
        BOOST_TEST(opt->serverFileId == "server-id-file2");
    }

    // -------------------------
    // C. RENAME
    // -------------------------
    repo.rename(file1, "file1_renamed.txt");

    {
        auto items = repo.listChildren(folder123);
        BOOST_TEST(!containsName(items, "file1.txt", false));
        BOOST_TEST(containsName(items, "file1_renamed.txt", false));

        auto renamed = findByName(items, "file1_renamed.txt", false);
        BOOST_TEST(static_cast<bool>(renamed));
        BOOST_TEST(renamed->id == file1); // same id after rename
    }

    // -------------------------
    // D. MOVE
    // -------------------------
    repo.move(file2, folderABC);

    {
        auto items123 = repo.listChildren(folder123);
        BOOST_TEST(!containsName(items123, "file2.txt", false)); // moved out

        auto itemsABC = repo.listChildren(folderABC);
        BOOST_TEST(containsName(itemsABC, "file2.txt", false));  // moved in

        auto opt = repo.getById(file2);
        BOOST_TEST(static_cast<bool>(opt));
        BOOST_TEST(opt->parentId == folderABC);
    }

    // Move folder "sub" from folder123 to root; its child should follow
    repo.move(subdir, "root");

    {
        auto rootItems = repo.listChildren("root");
        BOOST_TEST(containsName(rootItems, "sub", true)); // now at root

        auto subItems = repo.listChildren(subdir);
        BOOST_TEST(containsName(subItems, "deep.txt", false));
    }

    BOOST_TEST_MESSAGE("==== TREE AFTER RENAME/MOVE ====");
    dumpTree(repo, "root");

    // -------------------------
    // E. REMOVE (tombstone) + verify it's hidden from listing
    // -------------------------
    repo.remove(rootFile);       // remove a root file
    repo.remove(folderABC);      // remove a folder (tombstone)

    BOOST_TEST_MESSAGE("==== TREE AFTER REMOVE ====");
    dumpTree(repo, "root");

    // root_file.txt should be gone; folderABC should be gone from root listing
    {
        auto rootItems = repo.listChildren("root");
        BOOST_TEST(!containsName(rootItems, "root_file.txt", false));
        BOOST_TEST(!containsName(rootItems, "folderABC", true));
    }

    // getById still returns tombstoned item (soft delete)
    {
        auto opt = repo.getById(folderABC);
        BOOST_TEST(static_cast<bool>(opt));
        BOOST_TEST(opt->name == "folderABC");
        BOOST_TEST(opt->isDir);
    }

    // listChildren on a deleted folder should return empty (because listChildren filters deleted=0)
    {
        auto items = repo.listChildren(folderABC);
        BOOST_TEST(items.empty());
    }

    // -------------------------
    // F. Move something to root and then delete it (extra coverage)
    // -------------------------
    // file2 was moved under folderABC earlier; folderABC is now deleted.
    // In a Dropbox-style client, you'd resolve this later; for now, just ensure getById still works.
    {
        auto opt = repo.getById(file2);
        BOOST_TEST(static_cast<bool>(opt));
        BOOST_TEST(opt->name == "file2.txt");
    }

    // Delete a renamed file
    repo.remove(file1);

    {
        auto items123 = repo.listChildren(folder123);
        BOOST_TEST(!containsName(items123, "file1_renamed.txt", false));
    }

    BOOST_TEST_MESSAGE("==== FINAL TREE ====");
    dumpTree(repo, "root");
}
