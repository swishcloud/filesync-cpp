#ifndef FILE_REPOSITORY_H
#define FILE_REPOSITORY_H
#include <string>
#include <vector>
#include <optional>
#include <sqlite3.h>
#include <cstdint>
#include <string>
#include <vector>
#include <optional>
#include <stdexcept>
#include <utility>
#include <iostream>
enum class SyncStage : int
{
    Idle = 0, // no pending sync work (clean or already handled)

    PullNeeded = 1, // server has newer metadata/content
    Pulling = 2,    // currently downloading metadata/content

    PushNeeded = 3, // local changes need to be uploaded
    Pushing = 4,    // currently uploading

    Conflict = 5, // conflict detected, requires resolution

    Error = 6, // last sync attempt failed (retryable)
    Cancelled = 7
};

// ---- your structs/interfaces ----
struct FileItem
{
    std::string id;
    std::string serverFileId; // empty if not remote
    std::string parentId;     // "root" for top
    std::string name;
    bool isDir = false;

    std::int64_t size = 0;
    std::int64_t mtimeMs = 0;

    std::string localPath;     // empty if not local
    bool isPlaceholder = true; // true if not downloaded / remote-only
    bool pinnedOffline = false;
    bool is_deleted = false;
    bool isDirty = false;
    std::string serverRev; // server revision / etag / version
    std::string localServerRev;
    std::string server_parent_id_snapshot;
    std::string server_name_snapshot;
    SyncStage syncStage;
};

class IFileRepository
{
public:
    virtual ~IFileRepository() = default;

    virtual std::vector<FileItem> listChildren(const std::string &parentId) = 0;
    virtual std::optional<FileItem> getById(const std::string &id) = 0;
    virtual std::string getFullPath(const std::string &id) = 0;

    virtual std::string createFolder(const std::string &parentId, const std::string &name) = 0;
    virtual std::string createFile(const std::string &parentId, const std::string &name) = 0;
    virtual void rename(const std::string &id, const std::string &newName) = 0;
    virtual void move(const std::string &id, const std::string &newParentId) = 0;
    virtual void remove(const std::string &id) = 0;

    // NOTE: your original signature copies string; kept as-is to match.
    virtual void updateServerFileId(const std::string &id, const std::string serverFileId) = 0;
    virtual void updateLocalPath(const std::string &id, const std::string localPath) = 0;
    virtual void markDirty(const std::string &id) = 0;
    // Return all items that need to be pushed to server
    virtual std::vector<FileItem> listDirtyItems() = 0;

    // Clear dirty flag after server confirms success
    virtual void clearDirty(const std::string &id) = 0;
    virtual void upsertFromServer(
        const std::string &serverFileId,
        const std::string &parentServerFileId, // empty => root
        const std::string &name,
        bool isDir,
        std::int64_t size,
        std::int64_t mtimeMs,
        const std::string &serverRev) = 0;

    virtual void applyServerDelete(const std::string &serverFileId) = 0;

    // ack from server: clear dirty by server id
    virtual void clearDirtyByServerId(const std::string &serverFileId) = 0;

    // mapping helper
    virtual std::optional<std::string>
    getLocalIdByServerId(const std::string &serverFileId) = 0;
    // Find a non-deleted child by name under a parent
    // parentId: local id, use "root" for top-level
    virtual std::optional<FileItem>
    findChildByName(const std::string &parentId,
                    const std::string &name) = 0;
    virtual bool setSyncStage(
        const std::string &id,
        SyncStage stage) = 0;
    virtual std::vector<FileItem>
    listItemsNeedingDownload() = 0;
    virtual bool requestDownload(const std::string &id) = 0;
    virtual bool cancelDownload(const std::string &id) = 0;
    virtual bool recoverPullingToPullNeeded() = 0;
    virtual bool markDownloaded(
        const std::string &id,
        const std::string &localPath,
        std::int64_t size,
        std::int64_t mtimeMs) = 0;
    virtual bool markDirectoryChildrenUpToDate(
        const std::string &dirId,
        const std::string &serverRev) = 0;
};

// ---- UUID interface (inject from your project) ----
class IUuidGenerator
{
public:
    virtual ~IUuidGenerator() = default;
    virtual std::string newUuid() = 0;
};

// -------------------- implementation --------------------

class SqliteFileRepository final : public IFileRepository
{
public:
    SqliteFileRepository(std::string dbPath, IUuidGenerator &uuidGen)
        : dbPath_(std::move(dbPath)), uuidGen_(uuidGen), db_(nullptr)
    {
        open();

        exec("PRAGMA foreign_keys = ON;");
        exec("PRAGMA journal_mode = WAL;");
        exec("PRAGMA synchronous = NORMAL;");
        exec("PRAGMA busy_timeout = 5000;");
    }

    ~SqliteFileRepository() override { closeNoThrow(); }

    SqliteFileRepository(const SqliteFileRepository &) = delete;
    SqliteFileRepository &operator=(const SqliteFileRepository &) = delete;

    SqliteFileRepository(SqliteFileRepository &&other) noexcept
        : dbPath_(std::move(other.dbPath_)), uuidGen_(other.uuidGen_), db_(other.db_)
    {
        other.db_ = nullptr;
    }

    SqliteFileRepository &operator=(SqliteFileRepository &&other) noexcept
    {
        if (this != &other)
        {
            closeNoThrow();
            dbPath_ = std::move(other.dbPath_);
            // uuidGen_ is a reference, cannot be reseated; keep current reference.
            db_ = other.db_;
            other.db_ = nullptr;
        }
        return *this;
    }

public:
    std::vector<FileItem> listChildren(const std::string &parentId) override
    {
        const char *sql_root =
            "SELECT id, server_file_id, parent_id, name, is_dir, size, mtime_ms, "
            "       local_path, is_placeholder, pinned_offline,server_rev,local_server_rev,deleted,dirty,server_parent_id_snapshot,server_name_snapshot,sync_stage "
            "FROM items "
            "WHERE parent_id IS NULL AND deleted = 0 "
            "ORDER BY is_dir DESC, name ASC;";

        const char *sql_child =
            "SELECT id, server_file_id, parent_id, name, is_dir, size, mtime_ms, "
            "       local_path, is_placeholder, pinned_offline,server_rev,local_server_rev,deleted,dirty,server_parent_id_snapshot,server_name_snapshot,sync_stage "
            "FROM items "
            "WHERE parent_id = ?1 AND deleted = 0 "
            "ORDER BY is_dir DESC, name ASC;";

        Stmt stmt(db_, isRoot(parentId) ? sql_root : sql_child);
        if (!isRoot(parentId))
        {
            bindText(stmt.get(), 1, parentId);
        }

        std::vector<FileItem> out;
        while (sqlite3_step(stmt.get()) == SQLITE_ROW)
        {
            out.push_back(readFileItemRow(stmt.get()));
        }
        return out;
    }

    std::optional<FileItem> getById(const std::string &id) override
    {
        const char *sql =
            "SELECT id, server_file_id, parent_id, name, is_dir, size, mtime_ms, "
            "       local_path, is_placeholder, pinned_offline ,server_rev,local_server_rev,deleted,dirty,server_parent_id_snapshot,server_name_snapshot,sync_stage "
            "FROM items WHERE id = ?1;";

        Stmt stmt(db_, sql);
        bindText(stmt.get(), 1, id);

        int rc = sqlite3_step(stmt.get());
        if (rc == SQLITE_ROW)
            return readFileItemRow(stmt.get());
        if (rc == SQLITE_DONE)
            return std::nullopt;

        throw std::runtime_error(std::string("getById: sqlite3_step failed: ") + sqlite3_errmsg(db_));
    }
    std::string getFullPath(const std::string &id) override
    {
        if (id == "root" || id.empty())
            return "/";

        // Build path like "/a/b/c" (root is parent_id NULL)
        // Requires SQLite that supports recursive CTEs (3.8.3+) and window/order in aggregate is available.
        // To be safe across versions, we order in a subquery before GROUP_CONCAT.
        const char *sql =
            "WITH RECURSIVE chain(id, parent_id, name, depth) AS ("
            "    SELECT i.id, i.parent_id, i.name, 0"
            "    FROM items i"
            "    WHERE i.id = ?1"
            "    UNION ALL"
            "    SELECT p.id, p.parent_id, p.name, c.depth + 1"
            "    FROM items p"
            "    JOIN chain c ON p.id = c.parent_id"
            "    WHERE c.parent_id IS NOT NULL"
            ")"
            "SELECT "
            "    CASE "
            "      WHEN COUNT(*) = 0 THEN '' "
            "      ELSE '/' || GROUP_CONCAT(name, '/') "
            "    END AS full_path "
            "FROM ("
            "    SELECT name "
            "    FROM chain "
            "    ORDER BY depth DESC"
            ");";

        Stmt stmt(db_, sql);
        bindText(stmt.get(), 1, id);

        int rc = sqlite3_step(stmt.get());
        if (rc == SQLITE_DONE)
        {
            throw std::runtime_error("getFullPath: id not found");
        }
        if (rc != SQLITE_ROW)
        {
            throw std::runtime_error(std::string("getFullPath: sqlite3_step failed: ") + sqlite3_errmsg(db_));
        }

        const unsigned char *txt = sqlite3_column_text(stmt.get(), 0);
        if (!txt)
            return std::string(); // shouldn't happen
        return std::string(reinterpret_cast<const char *>(txt));
    }

    std::string createFolder(const std::string &parentId, const std::string &name) override
    {
        return createItem(parentId, name, /*isDir=*/true);
    }

    std::string createFile(const std::string &parentId, const std::string &name) override
    {
        return createItem(parentId, name, /*isDir=*/false);
    }

    void rename(const std::string &id, const std::string &newName) override
    {
        const char *sql =
            "UPDATE items "
            "SET name = ?1, dirty = 1, updated_at_ms = (unixepoch()*1000) "
            "WHERE id = ?2;";

        Stmt stmt(db_, sql);
        bindText(stmt.get(), 1, newName);
        bindText(stmt.get(), 2, id);

        stepMustDone(stmt.get(), "rename");
        ensureChanged("rename");
    }

    void move(const std::string &id, const std::string &newParentId) override
    {
        const char *sql_root =
            "UPDATE items "
            "SET parent_id = NULL, dirty = 1, updated_at_ms = (unixepoch()*1000) "
            "WHERE id = ?1;";

        const char *sql_child =
            "UPDATE items "
            "SET parent_id = ?1, dirty = 1, updated_at_ms = (unixepoch()*1000) "
            "WHERE id = ?2;";

        if (isRoot(newParentId))
        {
            Stmt stmt(db_, sql_root);
            bindText(stmt.get(), 1, id);
            stepMustDone(stmt.get(), "move(root)");
        }
        else
        {
            Stmt stmt(db_, sql_child);
            bindText(stmt.get(), 1, newParentId);
            bindText(stmt.get(), 2, id);
            stepMustDone(stmt.get(), "move");
        }
        ensureChanged("move");
    }
    bool isDirById(const std::string &id)
    {
        const char *sql = "SELECT is_dir FROM items WHERE id = ?1;";
        Stmt stmt(db_, sql);
        bindText(stmt.get(), 1, id);

        int rc = sqlite3_step(stmt.get());
        if (rc == SQLITE_ROW)
        {
            return sqlite3_column_int(stmt.get(), 0) != 0;
        }
        if (rc == SQLITE_DONE)
        {
            throw std::runtime_error("remove: id not found");
        }
        throw std::runtime_error(std::string("remove: sqlite3_step failed: ") + sqlite3_errmsg(db_));
    }
    void remove(const std::string &id) override
    {
        const bool dir = isDirById(id);

        if (!dir)
        {
            // File: tombstone only itself
            const char *sql =
                "UPDATE items "
                "SET deleted = 1, "
                "    deleted_at_ms = (unixepoch() * 1000), "
                "    dirty = 1, "
                "    updated_at_ms = (unixepoch() * 1000) "
                "WHERE id = ?1;";

            Stmt stmt(db_, sql);
            bindText(stmt.get(), 1, id);

            stepMustDone(stmt.get(), "remove(file)");
            ensureChanged("remove(file)");
            return;
        }

        // Directory: tombstone folder + all descendants
        const char *sql =
            "WITH RECURSIVE tree(id) AS ("
            "    SELECT id FROM items WHERE id = ?1 "
            "    UNION ALL "
            "    SELECT i.id FROM items i JOIN tree t ON i.parent_id = t.id "
            ") "
            "UPDATE items "
            "SET deleted = 1, "
            "    deleted_at_ms = (unixepoch() * 1000), "
            "    dirty = 1, "
            "    updated_at_ms = (unixepoch() * 1000) "
            "WHERE id IN (SELECT id FROM tree);";

        Stmt stmt(db_, sql);
        bindText(stmt.get(), 1, id);

        stepMustDone(stmt.get(), "remove(dir cascade)");
        ensureChanged("remove(dir cascade)");
    }

    void updateServerFileId(const std::string &id, const std::string serverFileId) override
    {
        const char *sql =
            "UPDATE items "
            "SET server_file_id = ?1, updated_at_ms = (unixepoch()*1000) "
            "WHERE id = ?2;";

        Stmt stmt(db_, sql);
        if (serverFileId.empty())
            bindNull(stmt.get(), 1);
        else
            bindText(stmt.get(), 1, serverFileId);
        bindText(stmt.get(), 2, id);

        stepMustDone(stmt.get(), "updateServerFileId");
        ensureChanged("updateServerFileId");
    }

    void updateLocalPath(const std::string &id, const std::string localPath) override
    {
        const char *sql =
            "UPDATE items "
            "SET local_path = ?1, updated_at_ms = (unixepoch()*1000) "
            "WHERE id = ?2;";

        Stmt stmt(db_, sql);
        bindText(stmt.get(), 1, localPath);
        bindText(stmt.get(), 2, id);

        stepMustDone(stmt.get(), "updateLocalPath");
        ensureChanged("updateLocalPath");
    }

private:
    class Stmt
    {
    public:
        Stmt(sqlite3 *db, const char *sql) : stmt_(nullptr), db_(db)
        {
            int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt_, nullptr);
            if (rc != SQLITE_OK)
            {
                throw std::runtime_error(std::string("sqlite3_prepare_v2 failed: ") + sqlite3_errmsg(db_));
            }
        }
        ~Stmt()
        {
            if (stmt_)
                sqlite3_finalize(stmt_);
        }
        sqlite3_stmt *get() const { return stmt_; }

    private:
        sqlite3_stmt *stmt_;
        sqlite3 *db_;
    };

private:
    void open()
    {
        if (db_)
            return;

        sqlite3 *tmp = nullptr;
        int rc = sqlite3_open(dbPath_.c_str(), &tmp);
        if (rc != SQLITE_OK)
        {
            std::string msg = "Cannot open DB: ";
            msg += (tmp ? sqlite3_errmsg(tmp) : "unknown error");
            if (tmp)
                sqlite3_close(tmp);
            throw std::runtime_error(msg);
        }
        db_ = tmp;
    }

    void closeNoThrow() noexcept
    {
        if (db_)
        {
            sqlite3_close(db_);
            db_ = nullptr;
        }
    }

    void exec(const char *sql)
    {
        char *err = nullptr;
        int rc = sqlite3_exec(db_, sql, nullptr, nullptr, &err);
        if (rc != SQLITE_OK)
        {
            std::string msg = "sqlite3_exec failed: ";
            msg += (err ? err : "(no error)");
            if (err)
                sqlite3_free(err);
            msg += "\nSQL was:\n";
            msg += sql;
            throw std::runtime_error(msg);
        }
    }

    static void bindText(sqlite3_stmt *st, int idx, const std::string &s)
    {
        int rc = sqlite3_bind_text(st, idx, s.c_str(), -1, SQLITE_TRANSIENT);
        if (rc != SQLITE_OK)
            throw std::runtime_error("sqlite3_bind_text failed");
    }

    static void bindNull(sqlite3_stmt *st, int idx)
    {
        int rc = sqlite3_bind_null(st, idx);
        if (rc != SQLITE_OK)
            throw std::runtime_error("sqlite3_bind_null failed");
    }

    static void bindInt(sqlite3_stmt *st, int idx, int v)
    {
        int rc = sqlite3_bind_int(st, idx, v);
        if (rc != SQLITE_OK)
            throw std::runtime_error("sqlite3_bind_int failed");
    }

    static void bindInt64(sqlite3_stmt *st, int idx, std::int64_t v)
    {
        int rc = sqlite3_bind_int64(st, idx, static_cast<sqlite3_int64>(v));
        if (rc != SQLITE_OK)
            throw std::runtime_error("sqlite3_bind_int64 failed");
    }

    void stepMustDone(sqlite3_stmt *st, const char *where)
    {
        int rc = sqlite3_step(st);
        if (rc != SQLITE_DONE)
        {
            throw std::runtime_error(std::string(where) + ": sqlite3_step failed: " + sqlite3_errmsg(db_));
        }
    }

    void ensureChanged(const char *where)
    {
        if (sqlite3_changes(db_) <= 0)
        {
            throw std::runtime_error(std::string(where) + ": no rows affected (id not found?)");
        }
    }

    static bool isRoot(const std::string &parentId)
    {
        return parentId.empty() || parentId == "root";
    }

    static std::string parentApiFromDb(sqlite3_stmt *st, int col)
    {
        const unsigned char *txt = sqlite3_column_text(st, col);
        if (!txt)
            return "root";
        std::string s(reinterpret_cast<const char *>(txt));
        if (s.empty())
            return "root";
        return s;
    }

    static std::string colTextOrEmpty(sqlite3_stmt *st, int col)
    {
        const unsigned char *t = sqlite3_column_text(st, col);
        return t ? std::string(reinterpret_cast<const char *>(t)) : std::string();
    }

    FileItem readFileItemRow(sqlite3_stmt *st)
    {
        FileItem it;
        it.id = colTextOrEmpty(st, 0);
        it.serverFileId = colTextOrEmpty(st, 1);
        it.parentId = parentApiFromDb(st, 2);
        it.name = colTextOrEmpty(st, 3);
        it.isDir = sqlite3_column_int(st, 4) != 0;
        it.size = sqlite3_column_int64(st, 5);
        it.mtimeMs = sqlite3_column_int64(st, 6);
        it.localPath = colTextOrEmpty(st, 7);
        it.isPlaceholder = sqlite3_column_int(st, 8) != 0;
        it.pinnedOffline = sqlite3_column_int(st, 9) != 0;

        it.serverRev = colTextOrEmpty(st, 10);
        it.localServerRev = colTextOrEmpty(st, 11);

        it.is_deleted = sqlite3_column_int(st, 12);
        it.isDirty = sqlite3_column_int(st, 13);
        it.server_parent_id_snapshot = colTextOrEmpty(st, 14);
        it.server_name_snapshot = colTextOrEmpty(st, 15);

        it.syncStage = static_cast<SyncStage>(sqlite3_column_int(st, 16));
        return it;
    }

    std::string createItem(const std::string &parentId, const std::string &name, bool isDir)
    {
        const char *sql_root =
            "INSERT INTO items (id, server_file_id, parent_id, name, is_dir, dirty, updated_at_ms) "
            "VALUES (?1, NULL, NULL, ?2, ?3, 1, (unixepoch()*1000));";

        const char *sql_child =
            "INSERT INTO items (id, server_file_id, parent_id, name, is_dir, dirty, updated_at_ms) "
            "VALUES (?1, NULL, ?2, ?3, ?4, 1, (unixepoch()*1000));";

        const std::string id = uuidGen_.newUuid();

        if (isRoot(parentId))
        {
            Stmt stmt(db_, sql_root);
            bindText(stmt.get(), 1, id);
            bindText(stmt.get(), 2, name);
            bindInt(stmt.get(), 3, isDir ? 1 : 0);
            stepMustDone(stmt.get(), isDir ? "createFolder(root)" : "createFile(root)");
        }
        else
        {
            Stmt stmt(db_, sql_child);
            bindText(stmt.get(), 1, id);
            bindText(stmt.get(), 2, parentId);
            bindText(stmt.get(), 3, name);
            bindInt(stmt.get(), 4, isDir ? 1 : 0);
            stepMustDone(stmt.get(), isDir ? "createFolder" : "createFile");
        }

        return id;
    }
    std::vector<FileItem> listDirtyItems() override
    {
        const char *sql =
            "SELECT id, server_file_id, parent_id, name, is_dir, size, mtime_ms, "
            "       local_path, is_placeholder, pinned_offline,server_rev,local_server_rev,deleted,dirty,server_parent_id_snapshot,server_name_snapshot,sync_stage "
            "FROM items "
            "WHERE dirty = 1 "
            "ORDER BY "
            "    parent_id IS NOT NULL, " // root items first
            "    is_dir DESC, "           // folders before files
            "    name ASC;";

        Stmt stmt(db_, sql);

        std::vector<FileItem> out;
        while (sqlite3_step(stmt.get()) == SQLITE_ROW)
        {
            out.push_back(readFileItemRow(stmt.get()));
        }
        return out;
    }

    void clearDirty(const std::string &id) override
    {
        const char *sql =
            "UPDATE items "
            "SET dirty = 0, "
            "    sync_stage = 0, "
            "    last_error = '', "
            "    retry_count = 0, "
            "    updated_at_ms = (unixepoch() * 1000) "
            "WHERE id = ?1;";

        Stmt stmt(db_, sql);
        bindText(stmt.get(), 1, id);

        stepMustDone(stmt.get(), "clearDirty");
        ensureChanged("clearDirty");
    }
    void markDirty(const std::string &id) override
    {
        const char *sql =
            "UPDATE items "
            "SET dirty = 1, "
            "    updated_at_ms = (unixepoch() * 1000) "
            "WHERE id = ?1;";

        Stmt stmt(db_, sql);
        bindText(stmt.get(), 1, id);

        stepMustDone(stmt.get(), "markDirty");
        ensureChanged("markDirty");
    }
    std::optional<std::string>
    getLocalIdByServerId(const std::string &serverFileId) override
    {
        const char *sql = "SELECT id FROM items WHERE server_file_id = ?1 LIMIT 1;";
        Stmt stmt(db_, sql);
        bindText(stmt.get(), 1, serverFileId);

        int rc = sqlite3_step(stmt.get());
        if (rc == SQLITE_ROW)
        {
            const unsigned char *t = sqlite3_column_text(stmt.get(), 0);
            return t ? std::optional<std::string>(reinterpret_cast<const char *>(t)) : std::nullopt;
        }
        if (rc == SQLITE_DONE)
            return std::nullopt;

        throw std::runtime_error(std::string("getLocalIdByServerId: step failed: ") + sqlite3_errmsg(db_));
    }
    std::optional<FileItem>
    findChildByName(const std::string &parentId,
                    const std::string &name) override
    {
        const bool isRoot = (parentId == "root");

        const char *sql_root =
            "SELECT id, server_file_id, parent_id, name, is_dir, size, mtime_ms, "
            "       local_path, is_placeholder, pinned_offline, server_rev,local_server_rev,deleted,dirty,server_parent_id_snapshot,server_name_snapshot,sync_stage "
            "FROM items "
            "WHERE deleted = 0 "
            "  AND parent_id IS NULL "
            "  AND name = ?1 "
            "LIMIT 1;";

        const char *sql_child =
            "SELECT id, server_file_id, parent_id, name, is_dir, size, mtime_ms, "
            "       local_path, is_placeholder, pinned_offline, server_rev,local_server_rev,deleted,dirty,server_parent_id_snapshot,server_name_snapshot,sync_stage "
            "FROM items "
            "WHERE deleted = 0 "
            "  AND parent_id = ?1 "
            "  AND name = ?2 "
            "LIMIT 1;";

        Stmt stmt(db_, isRoot ? sql_root : sql_child);

        if (isRoot)
        {
            bindText(stmt.get(), 1, name);
        }
        else
        {
            bindText(stmt.get(), 1, parentId);
            bindText(stmt.get(), 2, name);
        }

        int rc = sqlite3_step(stmt.get());
        if (rc == SQLITE_ROW)
        {
            return readFileItemRow(stmt.get());
        }
        if (rc == SQLITE_DONE)
        {
            return std::nullopt;
        }

        throw std::runtime_error(
            std::string("findChildByName: sqlite3_step failed: ") +
            sqlite3_errmsg(db_));
    }
    std::optional<std::string>
    getParentLocalIdFromParentServerId(const std::string &parentServerFileId)
    {
        if (parentServerFileId.empty())
            return std::nullopt; // root => NULL in DB

        auto parentLocal = getLocalIdByServerId(parentServerFileId);
        if (!parentLocal)
        {
            throw std::runtime_error(
                "upsertFromServer: parent not found locally for parentServerFileId=" + parentServerFileId);
        }
        return parentLocal;
    }
    void upsertFromServer(
        const std::string &serverFileId,
        const std::string &parentServerFileId,
        const std::string &name,
        bool isDir,
        std::int64_t size,
        std::int64_t mtimeMs,
        const std::string &serverRev) override
    {
        const auto localIdOpt = getLocalIdByServerId(serverFileId);
        const auto parentLocalOpt = getParentLocalIdFromParentServerId(parentServerFileId);

        if (!localIdOpt)
        {
            // INSERT
            const std::string newLocalId = uuidGen_.newUuid();

            const char *sql_root =
                "INSERT INTO items ("
                "  id, server_file_id, parent_id, name, is_dir, size, mtime_ms, "
                "  local_path, is_placeholder, pinned_offline, "
                "  deleted, deleted_at_ms, "
                "  server_rev, server_mtime_ms, "
                "  dirty, sync_stage, inflight_op_id, inflight_since_ms, last_error, retry_count, conflict, "
                "  created_at_ms, updated_at_ms,server_parent_id_snapshot,server_name_snapshot"
                ") VALUES ("
                "  ?1, ?2, NULL, ?3, ?4, ?5, ?6, "
                "  '', 1, 0, "
                "  0, 0, "
                "  ?7, ?8, "
                "  0, 0, NULL, 0, '', 0, 0, "
                "  (unixepoch()*1000), (unixepoch()*1000), "
                "  ?9, ?10 "
                ");";

            const char *sql_child =
                "INSERT INTO items ("
                "  id, server_file_id, parent_id, name, is_dir, size, mtime_ms, "
                "  local_path, is_placeholder, pinned_offline, "
                "  deleted, deleted_at_ms, "
                "  server_rev, server_mtime_ms, "
                "  dirty, sync_stage, inflight_op_id, inflight_since_ms, last_error, retry_count, conflict, "
                "  created_at_ms, updated_at_ms,server_parent_id_snapshot,server_name_snapshot"
                ") VALUES ("
                "  ?1, ?2, ?3, ?4, ?5, ?6, ?7, "
                "  '', 1, 0, "
                "  0, 0, "
                "  ?8, ?9, "
                "  0, 0, NULL, 0, '', 0, 0, "
                "  (unixepoch()*1000), (unixepoch()*1000), "
                "  ?10, ?11 "
                ");";

            if (!parentLocalOpt)
            {
                Stmt stmt(db_, sql_root);
                bindText(stmt.get(), 1, newLocalId);
                bindText(stmt.get(), 2, serverFileId);
                bindText(stmt.get(), 3, name);
                bindInt(stmt.get(), 4, isDir ? 1 : 0);
                bindInt64(stmt.get(), 5, size);
                bindInt64(stmt.get(), 6, mtimeMs);
                bindText(stmt.get(), 7, serverRev);
                bindInt64(stmt.get(), 8, mtimeMs);
                bindText(stmt.get(), 9, parentServerFileId);
                bindText(stmt.get(), 10, name);
                stepMustDone(stmt.get(), "upsertFromServer(insert root)");
            }
            else
            {
                Stmt stmt(db_, sql_child);
                bindText(stmt.get(), 1, newLocalId);
                bindText(stmt.get(), 2, serverFileId);
                bindText(stmt.get(), 3, *parentLocalOpt);
                bindText(stmt.get(), 4, name);
                bindInt(stmt.get(), 5, isDir ? 1 : 0);
                bindInt64(stmt.get(), 6, size);
                bindInt64(stmt.get(), 7, mtimeMs);
                bindText(stmt.get(), 8, serverRev);
                bindInt64(stmt.get(), 9, mtimeMs);
                bindText(stmt.get(), 10, parentServerFileId);
                bindText(stmt.get(), 11, name);
                stepMustDone(stmt.get(), "upsertFromServer(insert child)");
            }
            return;
        }

        // UPDATE existing
        const std::string &localId = *localIdOpt;

        // If local dirty=1, we should not stomp local intent; mark conflict and only update server_* fields.
        {
            const char *sqlDirty = "SELECT dirty FROM items WHERE id = ?1;";
            Stmt stmt(db_, sqlDirty);
            bindText(stmt.get(), 1, localId);

            int rc = sqlite3_step(stmt.get());
            if (rc != SQLITE_ROW)
            {
                throw std::runtime_error("upsertFromServer(update): local row disappeared");
            }
            const bool localDirty = sqlite3_column_int(stmt.get(), 0) != 0;

            if (localDirty)
            {
                const char *sql =
                    "UPDATE items SET "
                    "  server_rev = ?1, "
                    "  server_mtime_ms = ?2, "
                    "  conflict = 1, "
                    "  updated_at_ms = (unixepoch()*1000), "
                    "  server_parent_id_snapshot = ?3, "
                    "  server_name_snapshot = ?4 "
                    "WHERE id = ?5;";

                Stmt u(db_, sql);
                bindText(u.get(), 1, serverRev);
                bindInt64(u.get(), 2, mtimeMs);
                bindText(u.get(), 3, parentServerFileId);
                bindText(u.get(), 4, name);
                bindText(u.get(), 5, localId);
                stepMustDone(u.get(), "upsertFromServer(update dirty=1 => conflict)");
                ensureChanged("upsertFromServer(update conflict)");
                return;
            }
        }

        // Local clean => apply server truth fully
        const char *sql_root =
            "UPDATE items SET "
            "  parent_id = NULL, "
            "  name = ?1, "
            "  is_dir = ?2, "
            "  size = ?3, "
            "  mtime_ms = ?4, "
            "  server_rev = ?5, "
            "  server_mtime_ms = ?6, "
            "  deleted = 0, "
            "  deleted_at_ms = 0, "
            "  conflict = 0, "
            "  updated_at_ms = (unixepoch()*1000) "
            "WHERE id = ?7;";

        const char *sql_child =
            "UPDATE items SET "
            "  parent_id = ?1, "
            "  name = ?2, "
            "  is_dir = ?3, "
            "  size = ?4, "
            "  mtime_ms = ?5, "
            "  server_rev = ?6, "
            "  server_mtime_ms = ?7, "
            "  deleted = 0, "
            "  deleted_at_ms = 0, "
            "  conflict = 0, "
            "  updated_at_ms = (unixepoch()*1000) "
            "WHERE id = ?8;";

        if (!parentLocalOpt)
        {
            Stmt u(db_, sql_root);
            bindText(u.get(), 1, name);
            bindInt(u.get(), 2, isDir ? 1 : 0);
            bindInt64(u.get(), 3, size);
            bindInt64(u.get(), 4, mtimeMs);
            bindText(u.get(), 5, serverRev);
            bindInt64(u.get(), 6, mtimeMs);
            bindText(u.get(), 7, localId);
            stepMustDone(u.get(), "upsertFromServer(update root)");
        }
        else
        {
            Stmt u(db_, sql_child);
            bindText(u.get(), 1, *parentLocalOpt);
            bindText(u.get(), 2, name);
            bindInt(u.get(), 3, isDir ? 1 : 0);
            bindInt64(u.get(), 4, size);
            bindInt64(u.get(), 5, mtimeMs);
            bindText(u.get(), 6, serverRev);
            bindInt64(u.get(), 7, mtimeMs);
            bindText(u.get(), 8, localId);
            stepMustDone(u.get(), "upsertFromServer(update child)");
        }

        ensureChanged("upsertFromServer(update)");
    }
    void applyServerDelete(const std::string &serverFileId) override
    {
        const auto localIdOpt = getLocalIdByServerId(serverFileId);
        if (!localIdOpt)
        {
            // Already not present locally => nothing to do (idempotent)
            return;
        }
        const std::string &localId = *localIdOpt;

        const char *sql =
            "WITH RECURSIVE tree(id) AS ("
            "  SELECT id FROM items WHERE id = ?1 "
            "  UNION ALL "
            "  SELECT i.id FROM items i JOIN tree t ON i.parent_id = t.id "
            ") "
            "UPDATE items SET "
            "  deleted = 1, "
            "  deleted_at_ms = (unixepoch()*1000), "
            "  dirty = 0, "
            "  sync_stage = 0, "
            "  inflight_op_id = NULL, "
            "  inflight_since_ms = 0, "
            "  last_error = '', "
            "  retry_count = 0, "
            "  conflict = 0, "
            "  updated_at_ms = (unixepoch()*1000) "
            "WHERE id IN (SELECT id FROM tree);";

        Stmt stmt(db_, sql);
        bindText(stmt.get(), 1, localId);

        stepMustDone(stmt.get(), "applyServerDelete(cascade)");
        ensureChanged("applyServerDelete(cascade)");
    }
    void clearDirtyByServerId(const std::string &serverFileId) override
    {
        const char *sql =
            "UPDATE items SET "
            "  dirty = 0, "
            "  sync_stage = 0, "
            "  inflight_op_id = NULL, "
            "  inflight_since_ms = 0, "
            "  last_error = '', "
            "  retry_count = 0, "
            "  updated_at_ms = (unixepoch()*1000) "
            "WHERE server_file_id = ?1;";

        Stmt stmt(db_, sql);
        bindText(stmt.get(), 1, serverFileId);

        stepMustDone(stmt.get(), "clearDirtyByServerId");
        // if no row changed, that's OK: might be a race or already cleared
    }
    bool setSyncStage(
        const std::string &id,
        SyncStage stage)
    {
        const char *sql =
            "UPDATE items "
            "SET sync_stage = ?, "
            "    updated_at_ms = (unixepoch()*1000) "
            "WHERE id = ?;";

        sqlite3_stmt *st = nullptr;
        if (sqlite3_prepare_v2(db_, sql, -1, &st, nullptr) != SQLITE_OK)
            return false;

        sqlite3_bind_int(st, 1, static_cast<int>(stage));
        sqlite3_bind_text(st, 2, id.c_str(), -1, SQLITE_TRANSIENT);

        bool ok = (sqlite3_step(st) == SQLITE_DONE);
        sqlite3_finalize(st);
        return ok;
    }
    std::vector<FileItem> listItemsNeedingDownload()
    {
        const char *sql = R"sql(
        SELECT
            id,                      -- 0
            server_file_id,          -- 1
            parent_id,               -- 2
            name,                    -- 3
            is_dir,                  -- 4
            size,                    -- 5
            mtime_ms,                -- 6
            local_path,              -- 7
            is_placeholder,          -- 8
            pinned_offline,          -- 9
            server_rev,              -- 10
            local_server_rev,        -- 11
            deleted,                 -- 12
            dirty,                   -- 13
            server_parent_id_snapshot,-- 14
            server_name_snapshot,    -- 15
            sync_stage               -- 16
        FROM items
        WHERE deleted = 0
          AND is_dir = 0
          AND server_file_id IS NOT NULL
          AND sync_stage = ?
        ORDER BY name ASC
    )sql";

        sqlite3_stmt *st = nullptr;
        std::vector<FileItem> out;

        if (sqlite3_prepare_v2(db_, sql, -1, &st, nullptr) != SQLITE_OK)
            return out;

        sqlite3_bind_int(st, 1, static_cast<int>(SyncStage::PullNeeded)); // usually 1

        while (sqlite3_step(st) == SQLITE_ROW)
        {
            out.push_back(readFileItemRow(st));
        }

        sqlite3_finalize(st);
        return out;
    }

    bool requestDownload(const std::string &id)
    {
        const char *sql =
            "UPDATE items "
            "SET sync_stage = ?, "
            "    is_placeholder = 1 "
            "WHERE id = ? "
            "  AND deleted = 0 "
            "  AND server_file_id IS NOT NULL;";

        sqlite3_stmt *st = nullptr;
        if (sqlite3_prepare_v2(db_, sql, -1, &st, nullptr) != SQLITE_OK)
            return false;

        sqlite3_bind_int(st, 1, static_cast<int>(SyncStage::PullNeeded));
        sqlite3_bind_text(st, 2, id.c_str(), -1, SQLITE_TRANSIENT);

        bool ok = (sqlite3_step(st) == SQLITE_DONE);
        sqlite3_finalize(st);
        return ok;
    }
    bool recoverPullingToPullNeeded()
    {
        const char *sql =
            "UPDATE items "
            "SET sync_stage = ? "
            "WHERE sync_stage = ?;";

        sqlite3_stmt *st = nullptr;
        if (sqlite3_prepare_v2(db_, sql, -1, &st, nullptr) != SQLITE_OK)
            return false;

        sqlite3_bind_int(st, 1, static_cast<int>(SyncStage::PullNeeded)); // 1
        sqlite3_bind_int(st, 2, static_cast<int>(SyncStage::Pulling));    // 2

        bool ok = (sqlite3_step(st) == SQLITE_DONE);
        sqlite3_finalize(st);
        return ok;
    }
    bool cancelDownload(const std::string &id)
    {
        const char *sql =
            "UPDATE items "
            "SET sync_stage = ?, "
            "    updated_at_ms = (unixepoch()*1000) "
            "WHERE id = ? "
            "  AND deleted = 0 "
            "  AND sync_stage = ?;"; // only cancel if currently Pulling

        sqlite3_stmt *st = nullptr;
        if (sqlite3_prepare_v2(db_, sql, -1, &st, nullptr) != SQLITE_OK)
            return false;

        sqlite3_bind_int(st, 1, static_cast<int>(SyncStage::Cancelled));
        sqlite3_bind_text(st, 2, id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(st, 3, static_cast<int>(SyncStage::Pulling));

        bool ok = (sqlite3_step(st) == SQLITE_DONE);
        sqlite3_finalize(st);
        return ok;
    }
    bool markDownloaded(
        const std::string &id,
        const std::string &localPath,
        std::int64_t size,
        std::int64_t mtimeMs)
    {
        const char *sql =
            "UPDATE items "
            "SET local_path = ?, "
            "    is_placeholder = 0, "
            "    size = ?, "
            "    mtime_ms = ?, "
            "    dirty = 0, "
            "    sync_stage = ?, "
            "    retry_count = 0, "
            "    last_error = '', "
            "    updated_at_ms = (unixepoch()*1000) "
            "WHERE id = ? "
            "  AND deleted = 0;";

        sqlite3_stmt *st = nullptr;
        if (sqlite3_prepare_v2(db_, sql, -1, &st, nullptr) != SQLITE_OK)
            return false;

        sqlite3_bind_text(st, 1, localPath.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(st, 2, static_cast<sqlite3_int64>(size));
        sqlite3_bind_int64(st, 3, static_cast<sqlite3_int64>(mtimeMs));
        sqlite3_bind_int(st, 4, static_cast<int>(SyncStage::Idle));
        sqlite3_bind_text(st, 5, id.c_str(), -1, SQLITE_TRANSIENT);

        bool ok = (sqlite3_step(st) == SQLITE_DONE);
        sqlite3_finalize(st);
        return ok;
    }
    bool markDirectoryChildrenUpToDate(
        const std::string &dirId,
        const std::string &serverRev)
    {
        const char *sql =
            "UPDATE items "
            "SET local_server_rev = ?, "
            "    sync_stage = ?, "
            "    updated_at_ms = (unixepoch()*1000) "
            "WHERE id = ? "
            "  AND is_dir = 1 "
            "  AND deleted = 0;";

        sqlite3_stmt *st = nullptr;
        if (sqlite3_prepare_v2(db_, sql, -1, &st, nullptr) != SQLITE_OK)
            return false;

        sqlite3_bind_text(st, 1, serverRev.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(st, 2, static_cast<int>(SyncStage::Idle));
        sqlite3_bind_text(st, 3, dirId.c_str(), -1, SQLITE_TRANSIENT);

        bool ok = (sqlite3_step(st) == SQLITE_DONE);
        sqlite3_finalize(st);
        return ok;
    }

private:
    std::string dbPath_;
    IUuidGenerator &uuidGen_;
    sqlite3 *db_;
};
#endif