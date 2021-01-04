// filesync.h : Include file for standard system include files,
// or project specific include files.
#ifndef FILESYNC
#define FILESYNC
#pragma once

#include <iostream>
#include <filesystem>
#include <vector>
#include "db_manager.h"
#include <unordered_map>
#include <common.h>
#include <change_committer.h>
#include <cfg.h>
#include <memory>
#include <internal.h>
#include <monitor.h>
#include <queue>
#include <mutex>
// TODO: Reference additional headers your program requires here.
#define TOKEN_FILE_PATH "token"
namespace filesync
{
	enum class FileStatus
	{
		None = 0X00,
		Unknown = 0X01,
		Conflict = 0X02,
		Modified = 0X4,
		Added = 0X8,
		Online = 0X10,
		Synced = 0X20,
		OutOfDate
	};
	class ChangeCommitter;
	class FileSync;
	class tcp_client;
	struct File;
} // namespace filesync
class filesync::FileSync
{
private:
	class hasher
	{
	public:
		size_t operator()(const char *str) const
		{
			int hash = 0;
			for (int i = 0; i < strlen(str); i++)
			{
				auto c = str[i];
				hash += str[i] + c * 1000 * i;
			}
			return hash;
		};
	};
	class keyeq
	{
	public:
		bool operator()(const char *a, const char *b) const
		{
			return strcmp(a, b) == 0;
		};
	};
	bool need_sync_server = false;
	char *server_location;
	void find_all_files(char *path);
	void corret_path_sepatator(char *path);
	void to_relative_path(char *path);
	char *get_full_path(const char *path);
	char *get_relative_path(const char *server_path);
	char *get_relative_path_by_fulllpath(const char *path);
	std::filesystem::path relative_to_server_path(const char *relative_path);
	bool upload_file(std::ifstream &fs, const char *md5, long size);
	std::mutex _local_file_changes_mutex;
	std::queue<common::monitor::change *> _local_file_changes;

public:
	static void monitor_cb(common::monitor::change *c, void *obj);
	common::monitor::MONITOR *monitor;
	filesync::PartitionConf conf;
	filesync::db_manager db;
	CONFIG cfg;
	ChangeCommitter *committer;
	FileSync(char *server_location);
	~FileSync();
	bool download_file(File &file);
	std::vector<File> get_server_files(const char *path, const char *commit_id, const char *max_commit_id);
	void get_all_server_files();
	std::vector<filesync::File> files;
	std::unordered_map<const char *, int, hasher, keyeq> files_map;
	void check_sync_path();
	bool get_file_changes();
	void save_change(json change, const char *commit_id);
	void print();
	bool sync_server();
	bool sync_local_added_or_modified(const char *path);
	bool sync_local_deleted(const char *path);
	File local_file(std::string full_path, bool is_directory);
	File server_file(std::string server_path, std::string commit_id, bool is_directory);
	void add_local_file_change(common::monitor::change *change);
	common::monitor::change *get_local_file_change();
};
struct filesync::File
{
private:
public:
	FileStatus status;
	std::string full_path;
	std::string relative_path;
	std::string server_path;
	std::string commit_id;
	bool is_directory;
};
#endif