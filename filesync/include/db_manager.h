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
	struct create_file_action
	{
		char *name;
		char *md5;
		char *location;
		bool is_hidden;

	public:
		create_file_action();
		create_file_action(create_file_action &&action);
		~create_file_action();

		json to_json();
	};
	struct create_directory_action
	{
		char *path;
		bool is_hidden;

	public:
		create_directory_action();
		create_directory_action(create_directory_action &&action);
		~create_directory_action();
		json to_json();
	};
	struct delete_by_path_action
	{
		char *path;
		char *commit_id;
		int file_type;

	public:
		delete_by_path_action();
		delete_by_path_action(delete_by_path_action &&action);
		~delete_by_path_action();
		json to_json();
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
#endif