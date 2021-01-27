#include <sqlite3.h>
#include <filesync.h>
#include <common.h>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
#include "db_manager.h"
#include <assert.h>
#include <regex>
using namespace nlohmann;
filesync::db_manager::db_manager() : db_file_name{NULL}, db{NULL}
{
}
filesync::db_manager::~db_manager()
{
	delete[](this->db_file_name);
	//delete (this->db);free(): invalid pointer
}
int filesync::db_manager::sqlite_callback(void *result, int n, char **value, char **column)
{
	/* filesync::sqlite_callback_result *result_p = (filesync::sqlite_callback_result *)result;
	if (result_p->column == NULL)
	{
		result_p->column = new char *[n];
		for (int i = 0; i < n; i++)
		{
			result_p->column[i] = common::strncpy(column[i]);
		}
	}
	char *c = column[1];
	char *v = value[1];
	char **value_copy = new char *[n];
	for (int i = 0; i < n; i++)
	{
		value_copy[i] = common::strncpy(value[i]);
	}
	result_p->value.push_back(value_copy);
	result_p->count++;
	result_p->column_count = n; */
	return 0;
}
std::unique_ptr<filesync::sqlite_query_result> filesync::db_manager::sqlite3_query(sqlite3_stmt *stmt)
{
	filesync::sqlite_query_result *result = new filesync::sqlite_query_result(stmt);
	while (1)
	{
		int rc = sqlite3_step(stmt);
		if (rc == SQLITE_DONE)
		{
			break;
		}
		else if (rc == SQLITE_ROW)
		{
			result->count++;
			if (!result->column)
			{
				result->column_count = sqlite3_column_count(stmt);
				result->column = new const char *[result->column_count];
				for (int i = 0; i < result->column_count; i++)
				{
					result->column[i] = sqlite3_column_name(stmt, i);
				}
			}
			char **row = new char *[result->column_count];
			for (int i = 0; i < result->column_count; i++)
			{
				auto v = sqlite3_column_text(stmt, i);
				row[i] = common::strcpy((const char *)v);
			}
			result->value.push_back(row);
		}
		else
		{
			filesync::EXCEPTION(sqlite3_errmsg(this->db));
		}
	}
	return std::unique_ptr<filesync::sqlite_query_result>(result);
}
std::unique_ptr<filesync::sqlite_query_result> filesync::db_manager::get_file(const char *filename, bool count_deleted)
{
	std::string sql = common::string_format("select * from file where name=?%s", count_deleted ? "" : " and is_deleted=0");
	sqlite3_stmt *stmt;
	int rc = sqlite3_prepare_v2(db, sql.c_str(), sql.size(), &stmt, NULL);
	assert(rc == SQLITE_OK);
	rc = sqlite3_bind_text(stmt, 1, filename, strlen(filename), NULL);
	assert(rc == SQLITE_OK);
	auto result = sqlite3_query(stmt);
	assert(result.get());
	if (result.get()->count > 1)
	{
		//std::cout << "duplicated records found,deleting..." << std::endl;
		filesync::EXCEPTION("duplicated records found,deleting...");
	}
	return result;
}
std::unique_ptr<filesync::sqlite_query_result> filesync::db_manager::fuzzily_query(const char *filename)
{
	auto fuzzy = common::string_format("%s/%%", filename);
	const char *sql = "select * from file where name=? or name like ?";
	sqlite3_stmt *stmt;
	int rc = sqlite3_prepare_v2(db, sql, strlen(sql), &stmt, NULL);
	assert(rc == SQLITE_OK);
	rc = sqlite3_bind_text(stmt, 1, filename, strlen(filename), NULL);
	assert(rc == SQLITE_OK);
	rc = sqlite3_bind_text(stmt, 2, fuzzy.c_str(), strlen(fuzzy.c_str()), NULL);
	assert(rc == SQLITE_OK);
	auto result = sqlite3_query(stmt);
	assert(result.get());
	return result;
}
std::unique_ptr<filesync::sqlite_query_result> filesync::db_manager::get_files()
{
	std::string sql = "select * from file";
	sqlite3_stmt *stmt;
	int rc = sqlite3_prepare_v2(db, sql.c_str(), sql.size(), &stmt, NULL);
	assert(rc == SQLITE_OK);
	auto result = sqlite3_query(stmt);
	assert(result.get());
	return result;
}
bool filesync::db_manager::move(const char *source_path, const char *dest_path, const char *id)
{
	auto r = fuzzily_query(source_path);
	for (int i = 0; i < r->count; i++)
	{
		auto file_name = r->get_value("name", i);
		auto md5 = r->get_value("md5", i);
		std::cout << file_name << std::endl;
		delete_file(file_name);
		auto dest = common::string_format("%s%s", dest_path, file_name + strlen(source_path));
		add_file(dest.c_str(), md5, id);
	}
	return true;
}
bool filesync::db_manager::add_file(const char *filename, const char *md5, const char *id)
{
	if (md5 && md5[0] == '\0')
	{
		md5 = NULL;
	}
	auto file = get_file(filename, true);
	if (file->count > 0)
	{
		if (strcmp(file->get_value("is_deleted"), "1") == 0)
		{
			if (!restore_file(filename))
			{
				return false;
			}
		}
		update_md5(filename, md5);
		update_commit_id(filename, id);
		return true;
	}
	else
	{
		std::string uuid = boost::uuids::to_string(boost::uuids::random_generator()());
		std::string sql = common::string_format("insert into file(id,name,md5,is_deleted,commit_id)values(?,?,?,0,?)");
		sqlite3_stmt *stmt;
		int rc = sqlite3_prepare_v2(db, sql.c_str(), strlen(sql.c_str()), &stmt, NULL);
		assert(rc == SQLITE_OK);
		rc = sqlite3_bind_text(stmt, 1, uuid.c_str(), uuid.size(), NULL);
		assert(rc == SQLITE_OK);
		rc = sqlite3_bind_text(stmt, 2, filename, strlen(filename), NULL);
		assert(rc == SQLITE_OK);
		rc = sqlite3_bind_text(stmt, 3, md5, md5 ? strlen(md5) : 0, NULL);
		assert(rc == SQLITE_OK);
		rc = sqlite3_bind_text(stmt, 4, id, strlen(id), NULL);
		assert(rc == SQLITE_OK);
		rc = sqlite3_step(stmt);
		assert(rc == SQLITE_DONE);
		//std::cout << "[SQL OK:]add file " << filename << std::endl;
		rc = sqlite3_finalize(stmt);
		assert(rc == SQLITE_OK);
		return true;
	}
}

bool filesync::db_manager::delete_file(const char *filename)
{
	auto fuzzy = common::string_format("%s/%%", filename);
	const char *sql = "update file set is_deleted=1 where name=? or name like ?";
	sqlite3_stmt *stmt;
	int rc = sqlite3_prepare_v2(db, sql, strlen(sql), &stmt, NULL);
	assert(rc == SQLITE_OK);
	rc = sqlite3_bind_text(stmt, 1, filename, strlen(filename), NULL);
	assert(rc == SQLITE_OK);
	rc = sqlite3_bind_text(stmt, 2, fuzzy.c_str(), strlen(fuzzy.c_str()), NULL);
	assert(rc == SQLITE_OK);
	rc = sqlite3_step(stmt);
	assert(rc == SQLITE_DONE);
	//std::cout << "[SQL OK:]delete_file " << fuzzy << std::endl;
	rc = sqlite3_finalize(stmt);
	assert(rc == SQLITE_OK);
	return true;
}
bool filesync::db_manager::restore_file(const char *filename)
{
	const char *sql = "update file set is_deleted=0 where name=?";
	sqlite3_stmt *stmt;
	int rc = sqlite3_prepare_v2(db, sql, strlen(sql), &stmt, NULL);
	assert(rc == SQLITE_OK);
	rc = sqlite3_bind_text(stmt, 1, filename, strlen(filename), NULL);
	assert(rc == SQLITE_OK);
	rc = sqlite3_step(stmt);
	assert(rc == SQLITE_DONE);
	//std::cout << "[SQL OK:]restore_file " << filename << std::endl;
	rc = sqlite3_finalize(stmt);
	assert(rc == SQLITE_OK);
	return true;
}
bool filesync::db_manager::update_commit_id(const char *filename, const char *commit_id)
{
	std::string uuid = boost::uuids::to_string(boost::uuids::random_generator()());
	std::string sql = common::string_format("update file set commit_id=? where name=?");
	sqlite3_stmt *stmt;
	int rc = sqlite3_prepare_v2(db, sql.c_str(), strlen(sql.c_str()), &stmt, NULL);
	assert(rc == SQLITE_OK);
	rc = sqlite3_bind_text(stmt, 1, commit_id, strlen(commit_id), NULL);
	assert(rc == SQLITE_OK);
	rc = sqlite3_bind_text(stmt, 2, filename, strlen(filename), NULL);
	assert(rc == SQLITE_OK);
	rc = sqlite3_step(stmt);
	assert(rc == SQLITE_DONE);
	rc = sqlite3_finalize(stmt);
	assert(rc == SQLITE_OK);
	return true;
}
bool filesync::db_manager::update_md5(const char *filename, const char *md5)
{
	if (md5 && md5[0] == '\0')
	{
		md5 = NULL;
	}
	std::string uuid = boost::uuids::to_string(boost::uuids::random_generator()());
	std::string sql = common::string_format("update file set md5=? where name=?");
	sqlite3_stmt *stmt;
	int rc = sqlite3_prepare_v2(db, sql.c_str(), strlen(sql.c_str()), &stmt, NULL);
	assert(rc == SQLITE_OK);
	rc = sqlite3_bind_text(stmt, 1, md5, md5 ? strlen(md5) : 0, NULL);
	assert(rc == SQLITE_OK);
	rc = sqlite3_bind_text(stmt, 2, filename, strlen(filename), NULL);
	assert(rc == SQLITE_OK);
	rc = sqlite3_step(stmt);
	assert(rc == SQLITE_DONE);
	//std::cout << "[SQL OK:]update_md5" << filename << std::endl;
	rc = sqlite3_finalize(stmt);
	assert(rc == SQLITE_OK);
	return true;
}
bool filesync::db_manager::update_local_md5(const char *filename, const char *local_md5)
{
	if (local_md5[0] == '\0')
	{
		local_md5 = NULL;
	}
	std::string uuid = boost::uuids::to_string(boost::uuids::random_generator()());
	std::string sql = common::string_format("update file set local_md5=? where name=?");
	sqlite3_stmt *stmt;
	int rc = sqlite3_prepare_v2(db, sql.c_str(), strlen(sql.c_str()), &stmt, NULL);
	assert(rc == SQLITE_OK);
	rc = sqlite3_bind_text(stmt, 1, local_md5, strlen(local_md5), NULL);
	assert(rc == SQLITE_OK);
	rc = sqlite3_bind_text(stmt, 2, filename, strlen(filename), NULL);
	assert(rc == SQLITE_OK);
	rc = sqlite3_step(stmt);
	assert(rc == SQLITE_DONE);
	//std::cout << "[SQL OK:]update_local_md5" << filename << std::endl;
	rc = sqlite3_finalize(stmt);
	assert(rc == SQLITE_OK);
	return true;
}
bool filesync::db_manager::initialize_db()
{
	const char *sql = "CREATE TABLE \"file\" ("
					  "\"id\"	TEXT NOT NULL,"
					  "\"commit_id\"	TEXT NOT NULL,"
					  "\"name\"	TEXT NOT NULL,"
					  "\"md5\"	TEXT,"
					  "\"is_deleted\"	INTEGER NOT NULL,"
					  "\"local_md5\"	TEXT,"
					  "PRIMARY KEY(\"id\")"
					  "); ";
	sqlite3_stmt *stmt;
	int rc = sqlite3_prepare_v2(db, sql, strlen(sql), &stmt, NULL);
	assert(rc == SQLITE_OK);
	rc = sqlite3_step(stmt);
	assert(rc == SQLITE_DONE);
	//std::cout << "[SQL OK:]initialize_db->" << this->db_file_name << std::endl;
	rc = sqlite3_finalize(stmt);
	assert(rc == SQLITE_OK);
	return true;
}
bool filesync::db_manager::delete_file_hard(const char *filename)
{
	const char *sql = "delete from file where name=?";
	sqlite3_stmt *stmt;
	int rc = sqlite3_prepare_v2(db, sql, strlen(sql), &stmt, NULL);
	assert(rc == SQLITE_OK);
	rc = sqlite3_bind_text(stmt, 1, filename, strlen(filename), NULL);
	assert(rc == SQLITE_OK);
	rc = sqlite3_step(stmt);
	assert(rc == SQLITE_DONE);
	//std::cout << "[SQL OK:]delete_file_hard " << filename << std::endl;
	rc = sqlite3_finalize(stmt);
	assert(rc == SQLITE_OK);
	return true;
}
bool filesync::db_manager::clear_files()
{
	const char *sql = "delete from file";
	sqlite3_stmt *stmt;
	int rc = sqlite3_prepare_v2(db, sql, strlen(sql), &stmt, NULL);
	assert(rc == SQLITE_OK);
	rc = sqlite3_step(stmt);
	assert(rc == SQLITE_DONE);
	//std::cout << "[SQL OK:]clear all files"  << std::endl;
	rc = sqlite3_finalize(stmt);
	assert(rc == SQLITE_OK);
	return true;
}
bool filesync::db_manager::opendb()
{
	if (sqlite3_open(this->db_file_name, &db))
	{
		fprintf(stderr, "Can't open database: %s\n", sqlite3_errmsg(db));
		sqlite3_close(db);
		return false;
	}
	return true;
}
bool filesync::db_manager::closedb()
{
	auto r = sqlite3_close(db);
	assert(r == SQLITE_OK);
	return true;
}

void filesync::db_manager::init(const char *db_path)
{
	this->db_file_name = common::strcpy(db_path);
	if (std::filesystem::exists(this->db_file_name))
	{
		this->opendb();
	}
	else
	{
		this->opendb();
		//this->db_manager.clear_files();
		this->initialize_db();
	}
}

const char *filesync::sqlite_callback_result::get_value(const char *colume_name, int row)
{
	int index = -1;
	for (int i = 0; i != this->column_count; i++)
	{
		if (strcmp(this->column[i], colume_name) == 0)
		{
			index = i;
			break;
		}
	}
	if (index == -1)
	{
		filesync::EXCEPTION("not found specified column name.");
	}
	if (row >= this->count)
	{
		filesync::EXCEPTION("row index is out of range.");
	}
	return this->value[row][index];
}
filesync::sqlite_query_result::sqlite_query_result(sqlite3_stmt *stmt) : stmt{stmt}
{
}
const char *filesync::sqlite_query_result::get_value(const char *colume_name, int row)
{
	int index = -1;
	for (int i = 0; i != this->column_count; i++)
	{
		if (strcmp(this->column[i], colume_name) == 0)
		{
			index = i;
			break;
		}
	}
	if (index == -1)
	{
		filesync::EXCEPTION("not found specified column name.");
	}
	if (row >= this->count)
	{
		filesync::EXCEPTION("row index is out of range.");
	}
	return this->value[row][index];
}

filesync::sqlite_query_result::~sqlite_query_result()
{
	//for (int i = 0; i < this->column_count; i++) {
	//	delete(this->column[i]);
	//}
	for (int i = 0; i < this->count; i++)
	{
		char **value = this->value[i];
		for (int j = 0; j < this->column_count; j++)
		{
			delete[](value[j]);
		}
		delete[](value);
	}
	/*for (int i = 0; i < this->column_count; i++)
	{
		delete[](this->column[i]);
	}*/
	delete[](this->column);
	assert(sqlite3_finalize(stmt) == SQLITE_OK);
}

json filesync::create_file_action::to_json()
{
	json j;
	j["name"] = this->name;
	j["md5"] = this->md5;
	j["location"] = this->location;
	j["isHidden"] = this->is_hidden;
	return j;
}

json filesync::create_directory_action::to_json()
{
	json j;
	j["path"] = this->path;
	j["isHidden"] = this->is_hidden;
	return j;
}
filesync::create_file_action::create_file_action()
{
	this->type=1;
}
filesync::create_file_action::~create_file_action()
{
	delete[](this->location);
	delete[](this->md5);
	delete[](this->name);

	this->location = NULL;
	this->md5 = NULL;
	this->name = NULL;
	;
}
/*filesync::create_file_action::create_file_action(create_file_action &&action)
{
	this->is_hidden = action.is_hidden;
	this->location = action.location;
	this->md5 = action.md5;
	this->name = action.name;

	action.location = NULL;
	action.md5 = NULL;
	action.name = NULL;
}*/
filesync::create_directory_action::create_directory_action()
{
	this->type=2;
}
filesync::create_directory_action::~create_directory_action()
{
	delete[](this->path);

	this->path = NULL;
}
/*filesync::create_directory_action::create_directory_action(create_directory_action &&action)
{
	this->is_hidden = action.is_hidden;
	this->path = action.path;

	action.path = NULL;
}*/
filesync::delete_by_path_action::delete_by_path_action()
{
	this->type=3;
}
/*filesync::delete_by_path_action::delete_by_path_action(delete_by_path_action &&action)
{
	this->commit_id = action.commit_id;
	this->file_type = action.file_type;
	this->path = action.path;

	action.commit_id = NULL;
	action.file_type = 0;
	action.path = NULL;
}*/
filesync::delete_by_path_action::~delete_by_path_action()
{
	delete[](this->commit_id);
	delete[](this->path);

	this->commit_id = NULL;
	this->file_type = 0;
	this->path = NULL;
}
json filesync::delete_by_path_action::to_json()
{
	json j;
	j["path"] = this->path;
	j["commit_id"] = this->commit_id;
	j["file_type"] = this->file_type;
	return j;
}