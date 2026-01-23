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

// ---- your structs/interfaces ----
struct FileItem {
    std::string id;
    std::string serverFileId; // empty if not remote
    std::string parentId;     // "root" for top
    std::string name;
    bool isDir = false;

    std::int64_t size = 0;
    std::int64_t mtimeMs = 0;

    std::string localPath;      // empty if not local
    bool isPlaceholder = true;  // true if not downloaded / remote-only
    bool pinnedOffline = false;
};

class IFileRepository {
public:
    virtual ~IFileRepository() = default;

    virtual std::vector<FileItem> listChildren(const std::string& parentId) = 0;
    virtual std::optional<FileItem> getById(const std::string& id) = 0;

    virtual std::string createFolder(const std::string& parentId, const std::string& name) = 0;
    virtual std::string createFile(const std::string& parentId, const std::string& name) = 0;
    virtual void rename(const std::string& id, const std::string& newName) = 0;
    virtual void move(const std::string& id, const std::string& newParentId) = 0;
    virtual void remove(const std::string& id) = 0;

    // NOTE: your original signature copies string; kept as-is to match.
    virtual void updateServerFileId(const std::string& id, const std::string serverFileId) = 0;
    virtual void updateLocalPath(const std::string& id, const std::string localPath) = 0;
};

// ---- UUID interface (inject from your project) ----
class IUuidGenerator {
public:
    virtual ~IUuidGenerator() = default;
    virtual std::string newUuid() = 0;
};

// -------------------- implementation --------------------

class SqliteFileRepository final : public IFileRepository {
public:
    SqliteFileRepository(std::string dbPath, IUuidGenerator& uuidGen)
        : dbPath_(std::move(dbPath)), uuidGen_(uuidGen), db_(nullptr) {
        open();

        exec("PRAGMA foreign_keys = ON;");
        exec("PRAGMA journal_mode = WAL;");
        exec("PRAGMA synchronous = NORMAL;");
        exec("PRAGMA busy_timeout = 5000;");
    }

    ~SqliteFileRepository() override { closeNoThrow(); }

    SqliteFileRepository(const SqliteFileRepository&) = delete;
    SqliteFileRepository& operator=(const SqliteFileRepository&) = delete;

    SqliteFileRepository(SqliteFileRepository&& other) noexcept
        : dbPath_(std::move(other.dbPath_)), uuidGen_(other.uuidGen_), db_(other.db_) {
        other.db_ = nullptr;
    }

    SqliteFileRepository& operator=(SqliteFileRepository&& other) noexcept {
        if (this != &other) {
            closeNoThrow();
            dbPath_ = std::move(other.dbPath_);
            // uuidGen_ is a reference, cannot be reseated; keep current reference.
            db_ = other.db_;
            other.db_ = nullptr;
        }
        return *this;
    }

public:
    std::vector<FileItem> listChildren(const std::string& parentId) override {
        const char* sql_root =
            "SELECT id, server_file_id, parent_id, name, is_dir, size, mtime_ms, "
            "       local_path, is_placeholder, pinned_offline "
            "FROM items "
            "WHERE parent_id IS NULL AND deleted = 0 "
            "ORDER BY is_dir DESC, name ASC;";

        const char* sql_child =
            "SELECT id, server_file_id, parent_id, name, is_dir, size, mtime_ms, "
            "       local_path, is_placeholder, pinned_offline "
            "FROM items "
            "WHERE parent_id = ?1 AND deleted = 0 "
            "ORDER BY is_dir DESC, name ASC;";

        Stmt stmt(db_, isRoot(parentId) ? sql_root : sql_child);
        if (!isRoot(parentId)) {
            bindText(stmt.get(), 1, parentId);
        }

        std::vector<FileItem> out;
        while (sqlite3_step(stmt.get()) == SQLITE_ROW) {
            out.push_back(readFileItemRow(stmt.get()));
        }
        return out;
    }

    std::optional<FileItem> getById(const std::string& id) override {
        const char* sql =
            "SELECT id, server_file_id, parent_id, name, is_dir, size, mtime_ms, "
            "       local_path, is_placeholder, pinned_offline "
            "FROM items WHERE id = ?1;";

        Stmt stmt(db_, sql);
        bindText(stmt.get(), 1, id);

        int rc = sqlite3_step(stmt.get());
        if (rc == SQLITE_ROW) return readFileItemRow(stmt.get());
        if (rc == SQLITE_DONE) return std::nullopt;

        throw std::runtime_error(std::string("getById: sqlite3_step failed: ") + sqlite3_errmsg(db_));
    }

    std::string createFolder(const std::string& parentId, const std::string& name) override {
        return createItem(parentId, name, /*isDir=*/true);
    }

    std::string createFile(const std::string& parentId, const std::string& name) override {
        return createItem(parentId, name, /*isDir=*/false);
    }

    void rename(const std::string& id, const std::string& newName) override {
        const char* sql =
            "UPDATE items "
            "SET name = ?1, dirty = 1, updated_at_ms = (unixepoch()*1000) "
            "WHERE id = ?2;";

        Stmt stmt(db_, sql);
        bindText(stmt.get(), 1, newName);
        bindText(stmt.get(), 2, id);

        stepMustDone(stmt.get(), "rename");
        ensureChanged("rename");
    }

    void move(const std::string& id, const std::string& newParentId) override {
        const char* sql_root =
            "UPDATE items "
            "SET parent_id = NULL, dirty = 1, updated_at_ms = (unixepoch()*1000) "
            "WHERE id = ?1;";

        const char* sql_child =
            "UPDATE items "
            "SET parent_id = ?1, dirty = 1, updated_at_ms = (unixepoch()*1000) "
            "WHERE id = ?2;";

        if (isRoot(newParentId)) {
            Stmt stmt(db_, sql_root);
            bindText(stmt.get(), 1, id);
            stepMustDone(stmt.get(), "move(root)");
        } else {
            Stmt stmt(db_, sql_child);
            bindText(stmt.get(), 1, newParentId);
            bindText(stmt.get(), 2, id);
            stepMustDone(stmt.get(), "move");
        }
        ensureChanged("move");
    }
bool isDirById(const std::string& id) {
    const char* sql = "SELECT is_dir FROM items WHERE id = ?1;";
    Stmt stmt(db_, sql);
    bindText(stmt.get(), 1, id);

    int rc = sqlite3_step(stmt.get());
    if (rc == SQLITE_ROW) {
        return sqlite3_column_int(stmt.get(), 0) != 0;
    }
    if (rc == SQLITE_DONE) {
        throw std::runtime_error("remove: id not found");
    }
    throw std::runtime_error(std::string("remove: sqlite3_step failed: ") + sqlite3_errmsg(db_));
}
void remove(const std::string& id) override {
    const bool dir = isDirById(id);

    if (!dir) {
        // File: tombstone only itself
        const char* sql =
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
    const char* sql =
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


    void updateServerFileId(const std::string& id, const std::string serverFileId) override {
        const char* sql =
            "UPDATE items "
            "SET server_file_id = ?1, updated_at_ms = (unixepoch()*1000) "
            "WHERE id = ?2;";

        Stmt stmt(db_, sql);
        if (serverFileId.empty()) bindNull(stmt.get(), 1);
        else bindText(stmt.get(), 1, serverFileId);
        bindText(stmt.get(), 2, id);

        stepMustDone(stmt.get(), "updateServerFileId");
        ensureChanged("updateServerFileId");
    }

    void updateLocalPath(const std::string& id, const std::string localPath) override {
        const char* sql =
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
    class Stmt {
    public:
        Stmt(sqlite3* db, const char* sql) : stmt_(nullptr), db_(db) {
            int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt_, nullptr);
            if (rc != SQLITE_OK) {
                throw std::runtime_error(std::string("sqlite3_prepare_v2 failed: ") + sqlite3_errmsg(db_));
            }
        }
        ~Stmt() { if (stmt_) sqlite3_finalize(stmt_); }
        sqlite3_stmt* get() const { return stmt_; }
    private:
        sqlite3_stmt* stmt_;
        sqlite3* db_;
    };

private:
    void open() {
        if (db_) return;

        sqlite3* tmp = nullptr;
        int rc = sqlite3_open(dbPath_.c_str(), &tmp);
        if (rc != SQLITE_OK) {
            std::string msg = "Cannot open DB: ";
            msg += (tmp ? sqlite3_errmsg(tmp) : "unknown error");
            if (tmp) sqlite3_close(tmp);
            throw std::runtime_error(msg);
        }
        db_ = tmp;
    }

    void closeNoThrow() noexcept {
        if (db_) {
            sqlite3_close(db_);
            db_ = nullptr;
        }
    }

    void exec(const char* sql) {
        char* err = nullptr;
        int rc = sqlite3_exec(db_, sql, nullptr, nullptr, &err);
        if (rc != SQLITE_OK) {
            std::string msg = "sqlite3_exec failed: ";
            msg += (err ? err : "(no error)");
            if (err) sqlite3_free(err);
            msg += "\nSQL was:\n";
            msg += sql;
            throw std::runtime_error(msg);
        }
    }

    static void bindText(sqlite3_stmt* st, int idx, const std::string& s) {
        int rc = sqlite3_bind_text(st, idx, s.c_str(), -1, SQLITE_TRANSIENT);
        if (rc != SQLITE_OK) throw std::runtime_error("sqlite3_bind_text failed");
    }

    static void bindNull(sqlite3_stmt* st, int idx) {
        int rc = sqlite3_bind_null(st, idx);
        if (rc != SQLITE_OK) throw std::runtime_error("sqlite3_bind_null failed");
    }

    static void bindInt(sqlite3_stmt* st, int idx, int v) {
        int rc = sqlite3_bind_int(st, idx, v);
        if (rc != SQLITE_OK) throw std::runtime_error("sqlite3_bind_int failed");
    }

    void stepMustDone(sqlite3_stmt* st, const char* where) {
        int rc = sqlite3_step(st);
        if (rc != SQLITE_DONE) {
            throw std::runtime_error(std::string(where) + ": sqlite3_step failed: " + sqlite3_errmsg(db_));
        }
    }

    void ensureChanged(const char* where) {
        if (sqlite3_changes(db_) <= 0) {
            throw std::runtime_error(std::string(where) + ": no rows affected (id not found?)");
        }
    }

    static bool isRoot(const std::string& parentId) {
        return parentId.empty() || parentId == "root";
    }

    static std::string parentApiFromDb(sqlite3_stmt* st, int col) {
        const unsigned char* txt = sqlite3_column_text(st, col);
        if (!txt) return "root";
        std::string s(reinterpret_cast<const char*>(txt));
        if (s.empty()) return "root";
        return s;
    }

    static std::string colTextOrEmpty(sqlite3_stmt* st, int col) {
        const unsigned char* t = sqlite3_column_text(st, col);
        return t ? std::string(reinterpret_cast<const char*>(t)) : std::string();
    }

    FileItem readFileItemRow(sqlite3_stmt* st) {
        FileItem it;
        it.id = colTextOrEmpty(st, 0);
        it.serverFileId = colTextOrEmpty(st, 1);
        it.parentId = parentApiFromDb(st, 2);
        it.name = colTextOrEmpty(st, 3);
        it.isDir = sqlite3_column_int(st, 4) != 0;
        it.size = static_cast<std::int64_t>(sqlite3_column_int64(st, 5));
        it.mtimeMs = static_cast<std::int64_t>(sqlite3_column_int64(st, 6));
        it.localPath = colTextOrEmpty(st, 7);
        it.isPlaceholder = sqlite3_column_int(st, 8) != 0;
        it.pinnedOffline = sqlite3_column_int(st, 9) != 0;
        return it;
    }

    std::string createItem(const std::string& parentId, const std::string& name, bool isDir) {
        const char* sql_root =
            "INSERT INTO items (id, server_file_id, parent_id, name, is_dir, dirty, updated_at_ms) "
            "VALUES (?1, NULL, NULL, ?2, ?3, 1, (unixepoch()*1000));";

        const char* sql_child =
            "INSERT INTO items (id, server_file_id, parent_id, name, is_dir, dirty, updated_at_ms) "
            "VALUES (?1, NULL, ?2, ?3, ?4, 1, (unixepoch()*1000));";

        const std::string id = uuidGen_.newUuid();

        if (isRoot(parentId)) {
            Stmt stmt(db_, sql_root);
            bindText(stmt.get(), 1, id);
            bindText(stmt.get(), 2, name);
            bindInt(stmt.get(), 3, isDir ? 1 : 0);
            stepMustDone(stmt.get(), isDir ? "createFolder(root)" : "createFile(root)");
        } else {
            Stmt stmt(db_, sql_child);
            bindText(stmt.get(), 1, id);
            bindText(stmt.get(), 2, parentId);
            bindText(stmt.get(), 3, name);
            bindInt(stmt.get(), 4, isDir ? 1 : 0);
            stepMustDone(stmt.get(), isDir ? "createFolder" : "createFile");
        }

        return id;
    }

private:
    std::string dbPath_;
    IUuidGenerator& uuidGen_;
    sqlite3* db_;
};
#endif