#ifndef DB_MANAGER
#define DB_MANAGER
#include <iostream>
#include <sqlite3.h>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <memory>
using json = nlohmann::json;
namespace filesync
{
    class sqlite_callback_result;
    class db_manager;
    class sqlite_query_result;
    class action_base
    {
    public:
        int type;
        virtual json to_json() = 0;
    };
    struct create_file_action : action_base
    {
        char *name;
        char *md5;
        char *location;
        bool is_hidden;

    public:
        create_file_action();
        // create_file_action(create_file_action &&action);
        ~create_file_action();

        json to_json() override;
    };
    struct create_directory_action : action_base
    {
        char *path;
        bool is_hidden;

    public:
        create_directory_action();
        // create_directory_action(create_directory_action &&action);
        ~create_directory_action();
        json to_json() override;
    };
    struct delete_by_path_action : action_base
    {
        char *path;
        char *commit_id;
        int file_type;

    public:
        delete_by_path_action();
        // delete_by_path_action(delete_by_path_action &&action);
        ~delete_by_path_action();
        json to_json() override;
    };
    struct move_action : action_base
    {
        char *id;
        char *destinationPath;

    public:
        move_action();
        ~move_action();
        json to_json() override;
    };
    struct rename_action : action_base
    {
        char *id;
        char *newName;

    public:
        rename_action();
        ~rename_action();
        json to_json() override;
    };
} // namespace filesync
class filesync::db_manager
{
public:
    db_manager();
    ~db_manager();
    static int sqlite_callback(void *, int n, char **value, char **column);
    std::unique_ptr<filesync::sqlite_query_result> sqlite3_query(sqlite3_stmt *stmt);
    std::unique_ptr<filesync::sqlite_query_result> get_file(const char *filename, bool count_deleted = false);
    std::unique_ptr<filesync::sqlite_query_result> fuzzily_query(const char *filename);
    std::unique_ptr<filesync::sqlite_query_result> get_files();
    bool move(const char *source_path, const char *dest_path, const char *commit_id);
    bool copy(const char *source_path, const char *dest_path, const char *commit_id);
    bool add_file(const char *filename, const char *md5, const char *id);
    bool delete_file(const char *filename);
    bool delete_file_hard(const char *filename);
    bool restore_file(const char *filename);
    bool update_commit_id(const char *filename, const char *commit_id);
    bool update_md5(const char *filename, const char *md5);
    bool update_local_md5(const char *filename, const char *local_md5);
    bool initialize_db();
    bool clear_files();
    bool opendb();
    bool closedb();
    void init(const char *db_path);

private:
    char *db_file_name;
    sqlite3 *db;
};
class filesync::sqlite_callback_result
{
public:
    int count;
    int column_count;
    std::vector<char **> value;
    char **column;
    const char *get_value(const char *colume_name, int row = 0);
};

class filesync::sqlite_query_result
{
private:
    sqlite3_stmt *stmt;

public:
    sqlite_query_result(sqlite3_stmt *stmt);
    int count{};
    int column_count{};
    const char **column{};
    std::vector<char **> value{};
    const char *get_value(const char *colume_name, int row = 0);
    ~sqlite_query_result();
};
#include <sqlite3.h>
#include <string>
#include <stdexcept>
#include <utility>

class SqliteDbSchema
{
public:
    explicit SqliteDbSchema(std::string dbPath)
        : dbPath_(std::move(dbPath)), db_(nullptr) {}

    ~SqliteDbSchema()
    {
        closeNoThrow();
    }

    // Non-copyable
    SqliteDbSchema(const SqliteDbSchema &) = delete;
    SqliteDbSchema &operator=(const SqliteDbSchema &) = delete;

    // Movable
    SqliteDbSchema(SqliteDbSchema &&other) noexcept
        : dbPath_(std::move(other.dbPath_)), db_(other.db_)
    {
        other.db_ = nullptr;
    }

    SqliteDbSchema &operator=(SqliteDbSchema &&other) noexcept
    {
        if (this != &other)
        {
            closeNoThrow();
            dbPath_ = std::move(other.dbPath_);
            db_ = other.db_;
            other.db_ = nullptr;
        }
        return *this;
    }

    // Open DB connection (safe to call multiple times)
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

        // Recommended pragmas for safety / concurrency
        exec("PRAGMA foreign_keys = ON;");
        exec("PRAGMA journal_mode = WAL;");
        exec("PRAGMA synchronous = NORMAL;");
        exec("PRAGMA busy_timeout = 5000;");
    }

    void close()
    {
        if (!db_)
            return;
        sqlite3_close(db_);
        db_ = nullptr;
    }

    // Create tables + indexes
    void createSchema()
    {
        open();

        exec(
            "CREATE TABLE IF NOT EXISTS items ("
            "    id                 TEXT PRIMARY KEY,"
            "    server_file_id     TEXT UNIQUE,"
            "    parent_id          TEXT NULL,"
            "    name               TEXT NOT NULL,"
            "    is_dir             INTEGER NOT NULL DEFAULT 0,"
            "    size               INTEGER NOT NULL DEFAULT 0,"
            "    mtime_ms           INTEGER NOT NULL DEFAULT 0,"
            "    local_path         TEXT NOT NULL DEFAULT '',"
            "    is_placeholder     INTEGER NOT NULL DEFAULT 1,"
            "    pinned_offline     INTEGER NOT NULL DEFAULT 0,"
            "    deleted            INTEGER NOT NULL DEFAULT 0,"
            "    deleted_at_ms      INTEGER NOT NULL DEFAULT 0,"
            "    server_rev         TEXT,"
            "    local_server_rev   TEXT,"
            "    server_mtime_ms    INTEGER NOT NULL DEFAULT 0,"
            "    server_parent_id_snapshot TEXT NULL,"
            "    server_name_snapshot      TEXT NOT NULL DEFAULT '',"
            "    dirty              INTEGER NOT NULL DEFAULT 0,"
            "    sync_stage         INTEGER NOT NULL DEFAULT 0,"
            "    inflight_op_id     TEXT,"
            "    inflight_since_ms  INTEGER NOT NULL DEFAULT 0,"
            "    last_error         TEXT NOT NULL DEFAULT '',"
            "    retry_count        INTEGER NOT NULL DEFAULT 0,"
            "    conflict           INTEGER NOT NULL DEFAULT 0,"
            "    created_at_ms      INTEGER NOT NULL DEFAULT (unixepoch()*1000),"
            "    updated_at_ms      INTEGER NOT NULL DEFAULT (unixepoch()*1000),"
            "    CHECK (is_dir IN (0,1)),"
            "    CHECK (is_placeholder IN (0,1)),"
            "    CHECK (pinned_offline IN (0,1)),"
            "    CHECK (deleted IN (0,1)),"
            "    CHECK (dirty IN (0,1)),"
            "    CHECK (conflict IN (0,1)),"
            "    CHECK (sync_stage >= 0),"
            "    CHECK (retry_count >= 0)"
            ");");

        exec(
            "CREATE INDEX IF NOT EXISTS idx_items_parent_deleted "
            "ON items(parent_id, deleted);");

        exec(
            "CREATE INDEX IF NOT EXISTS idx_items_dirty_stage "
            "ON items(dirty, sync_stage);");

        exec(
            "CREATE UNIQUE INDEX IF NOT EXISTS ux_items_sibling_name_alive "
            "ON items(parent_id, name) "
            "WHERE deleted = 0;");

        exec(
            "CREATE INDEX IF NOT EXISTS idx_items_server_file_id "
            "ON items(server_file_id);");
    }

    sqlite3 *handle() const noexcept { return db_; }

private:
    void exec(const char *sql)
    {
        char *err = nullptr;
        int rc = sqlite3_exec(db_, sql, nullptr, nullptr, &err);
        if (rc != SQLITE_OK)
        {
            std::string msg = "SQLite exec failed: ";
            msg += (err ? err : "(no error message)");
            msg += "\nSQL was:\n";
            msg += sql;
            if (err)
                sqlite3_free(err);
            throw std::runtime_error(msg);
        }
    }

    void closeNoThrow() noexcept
    {
        if (db_)
        {
            sqlite3_close(db_);
            db_ = nullptr;
        }
    }

private:
    std::string dbPath_;
    sqlite3 *db_;
};

#endif