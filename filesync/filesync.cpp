// filesync.cpp : Defines the entry point for the application.
//

#include "filesync.h"
#include <locale.h>
#include "db_manager.h"
#include <http.h>
#include <nlohmann/json.hpp>
#include <assert.h>

#include <tcp.h>
#include <regex>
using namespace nlohmann;
namespace filesync
{
	char *root_path;
	std::string get_parent_dir(const char *filename)
	{
		std::cmatch m{};
		std::regex reg{".*/"};
		if (!std::regex_search(filename, m, reg))
		{
			EXCEPTION("impossible exception.");
		}
		if (m[0].str().size() == 1)
		{
			return m[0].str();
		}
		std::string p = m[0].str().substr(0, m[0].str().size() - 1);
		return p;
	}
	std::string trim_trailing_space(std::string str)
	{
		std::smatch m{};
		std::regex reg{"([^ ]*) *$"};
		assert(std::regex_search(str, m, reg));
		return m[1].str();
	}

	std::string format_path(std::string str)
	{
		std::regex reg{"\\\\"};
		return std::regex_replace(str, reg, "/");
	}

	bool compare_md5(const char *a, const char *b)
	{
		if (a == NULL)
		{
			a = "";
		}
		if (b == NULL)
		{
			b = "";
		}
		return strcmp(trim_trailing_space(a).c_str(), trim_trailing_space(b).c_str()) == 0;
	}

	char *file_name(const char *path)
	{
		std::cmatch m{};
		std::regex reg{"[^/\\\\]*$"};
		assert(std::regex_search(path, m, reg));
		return common::strncpy(m[0].str().c_str());
	}
} // namespace filesync
int main()
{
	setlocale(LC_ALL, "zh_CN.UTF-8");
	std::cout << "LC_ALL: " << setlocale(LC_ALL, NULL) << std::endl;

#ifdef __linux__
#else
	if (!SetConsoleCP(CP_UTF8))
	{
		// An error occurred; handle it. Call GetLastError() for more information.
		// ...
		exit(1);
	}
	if (!SetConsoleOutputCP(CP_UTF8))
	{
		// An error occurred; handle it. Call GetLastError() for more information.
		// ...
		exit(1);
	}
#endif
	try
	{
		filesync::FileSync *filesync = new filesync::FileSync{"/"};
		filesync::connect(filesync);
		filesync->check_sync_path();
		filesync->db.init(filesync->conf.db_path.c_str());
		filesync->get_all_server_files();
		while (1)
		{
			std::cout << "-----begin syncing" << std::endl;
			if (!filesync->get_file_changes())
			{
				continue;
			}
			if (!filesync->sync_server())
			{
				continue;
			}
			if (!filesync->sync_local_added_or_modified(filesync->conf.sync_path.c_str()))
			{
				continue;
			}
			if (!filesync->sync_local_deleted())
			{
				continue;
			}
			filesync->committer->commit();
			std::cout << "-----end syncing" << std::endl;
		}
		delete (filesync);
	}
	catch (const std::exception &ex)
	{
		std::cout << ex.what() << "";
	}
	return 0;
}
bool filesync::FileSync::sync_server()
{
	std::vector<std::string> errs;
	auto files = this->db.get_files();
	for (int i = 0; i < files->count; i++)
	{
		const char *file_name = files->get_value("name", i);
		const char *md5 = files->get_value("md5", i);
		const char *local_md5 = files->get_value("local_md5", i);
		const char *is_deleted_str = files->get_value("is_deleted", i);
		const char *commit_id = files->get_value("commit_id", i);
		bool is_deleted = is_deleted_str[0] == '1';
		auto full_path = get_full_path(file_name);
		std::cout << "sync_server>" << full_path << std::endl;

		//struct File object
		File f = this->server_file(file_name, commit_id, md5 == NULL);

		//check if server has delete the file, if yes then delete the file locally, and hard delete the file from db
		if (is_deleted)
		{
			std::error_code err;
			if (std::filesystem::remove_all(full_path, err) == -1)
			{ //return 0 if full_path not exists, return -1 on error.
				std::cout << err.message() << std::endl;
				std::string err = common::string_format("Failed to remove file:%s", full_path);
				std::cout << err << std::endl;
				errs.push_back(err);
			}
			else
			{
				this->db.delete_file_hard(file_name);
			}
			continue;
		}

		//check if it's a directory, if yes then create it.
		if (f.is_directory)
		{
			if (!compare_md5(DIRECTORY_MD5, local_md5))
			{
				if (!std::filesystem::exists(full_path))
					assert(std::filesystem::create_directories(f.full_path));
				this->db.update_local_md5(f.server_path.c_str(), DIRECTORY_MD5);
			}
		}
		else
		{
			//check if the file has not been downloaded or the file has out of date, if it's in either case then download/re-download the file.
			if (local_md5 == NULL || !compare_md5(md5, local_md5))
			{
				if (!this->download_file(f))
				{
					std::string err = common::string_format("Failed to download file:%s", file_name);
					std::cout << err << std::endl;
					errs.push_back(err);
				}
			}
		}
	}
	delete (files);
	return errs.size() == 0;
}
bool filesync::FileSync::sync_local_added_or_modified(const char *path)
{
	std::vector<std::string> errs;
	auto relative_path = this->get_relative_path_by_fulllpath(path);
	auto file_db = db.get_file(relative_path);
	//filesync::format_path(path);

	if (std::filesystem::exists(path))
	{
		if (std::filesystem::is_directory(path))
		{
			std::vector<std::string> files_cstr;
			auto count = common::find_files(path, files_cstr);
			if (count == -1)
			{
				errs.push_back("finding files failed.");
				return false;
			}
			for (std::string v : files_cstr)
			{
				std::string p = common::string_format("%s/%s", path, v.c_str());
				this->sync_local_added_or_modified(filesync::format_path(p).c_str());
			}

			if (file_db->count == 0)
			{
				filesync::create_directory_action action;
				action.is_hidden = false;
				action.path = common::strncpy(relative_path);
				this->committer->add_create_directory_action(action);
			}
		}
		else
		{
			try
			{
				std::string md5;
				bool need_upload = false;
				if (file_db->count == 0)
				{ //this is a added local file to be upload.
					md5 = file_md5(path);
					need_upload = true;
				}
				else
				{
					md5 = file_md5(path);
					if (!compare_md5(md5.c_str(), file_db->get_value("local_md5")))
					{ //this file has been modified locally.
						need_upload = true;
					}
				}
				if (need_upload)
				{
					auto file_size = std::filesystem::file_size(path);
					std::ifstream fs{path, std::ios_base::binary};
					auto r = this->upload_file(fs, md5.c_str(), file_size);
					std::cout << "UPLOAD " << (r ? "OK" : "Failed") << " :" << path << std::endl;

					create_file_action action;
					action.is_hidden = false;
					action.location = common::strncpy(get_parent_dir(relative_path).c_str());
					action.md5 = common::strncpy(md5.c_str());
					action.name = file_name(relative_path);
					this->committer->add_create_file_action(action);
				}
			}
			catch (const std::exception &e)
			{
				std::string err = common::string_format("%s", e.what());
				std::cout << err << std::endl;
				errs.push_back(err);
			}
		}
	}
	delete (relative_path);
	delete (file_db);
	return errs.size() == 0;
}
bool filesync::FileSync::sync_local_deleted()
{
	auto files = this->db.get_files();
	for (int i = 0; i < files->count; i++)
	{
		const char *file_name = files->get_value("name", i);
		const char *md5 = files->get_value("md5", i);
		const char *local_md5 = files->get_value("local_md5", i);
		const char *is_deleted_str = files->get_value("is_deleted", i);
		const char *commit_id = files->get_value("commit_id", i);
		bool is_deleted = is_deleted_str[0] == '1';
		bool is_directory = md5 == NULL;
		auto full_path = get_full_path(file_name);

		if (!std::filesystem::exists(full_path))
		{ //this file has been deleted locally.
			delete_by_path_action action;
			action.commit_id = common::strncpy(commit_id);
			action.file_type = is_directory ? 2 : 1;
			action.path = common::strncpy(file_name);
			this->committer->add_delete_by_path_action(action);
		}
		delete (full_path);
	}
	delete (files);
	return true;
}
bool filesync::FileSync::upload_file(std::ifstream &fs, const char *md5, long size)
{
	std::string url_path = common::string_format("/api/file-info?md5=%s&size=%d", md5, size);
	std::string res;
	if (!filesync::http_get(this->cfg.server_ip.c_str(), common::string_format("%d", this->cfg.server_port).c_str(), url_path.c_str(), get_token(), res))
	{
		return false;
	}
	auto j = json::parse(res.c_str());
	auto err = j["error"];
	auto data = j["data"];
	if (!err.is_null())
	{
		std::cout << "Error:" << err << std::endl;
	}
	else
	{
		//std::cout << "OK:" << path << " "  << std::endl;
	}
	//std::cout << data << std::endl;
	auto is_completed = data["is_completed"].get<std::string>();
	auto ip = data["ip"].get<std::string>();
	auto port = data["port"].get<std::string>();
	auto s_path = data["path"].get<std::string>();
	auto path = data["path"].get<std::string>();
	auto file_size = data["size"].get<std::string>();
	auto uploaded_size = data["uploaded_size"].get<std::string>();
	auto server_file_id = data["server_file_id"].get<std::string>();
	if (strcmp(is_completed.c_str(), "false") != 0)
	{
		std::cout << "WARNING:"
				  << "the file has already uploaded,skiping uploading." << std::endl;
		return true;
	}
	filesync::upload_file(ip.c_str(), std::stoi(port), fs, path.c_str(), md5, std::stoi(file_size), std::stoi(uploaded_size), server_file_id.c_str());
	return true;
}
bool filesync::FileSync::download_file(File &file)
{
	std::string url_path = common::string_format("/api/file?path=%s&commit_id=%s", url_encode(file.server_path.c_str()).c_str(), file.commit_id.c_str());
	std::string res;
	if (!filesync::http_get(this->cfg.server_ip.c_str(), common::string_format("%d", this->cfg.server_port).c_str(), url_path.c_str(), get_token(), res))
	{
		return false;
	}
	else
	{
		//std::cout << res << std::endl;
	}
	auto j = json::parse(res.c_str());
	auto err = j["error"];
	auto data = j["data"];
	if (!err.is_null())
	{
		print_info(common::string_format("%s", err.get<std::string>().c_str()));
		return false;
	}
	auto ip = data["Ip"];
	auto port = data["Port"];
	auto s_path = data["Path"];
	auto md5 = data["Md5"];
	auto db_file = this->db.get_file(file.server_path.c_str());
	assert(db_file->count > 0);
	auto is_downloaded = filesync::download_file(ip.get<std::string>().c_str(), port, s_path.get<std::string>().c_str(), db_file->get_value("md5"), db_file->get_value("local_md5"), file);
	if (is_downloaded)
	{
		std::cout << "downloaded file:" << file.full_path << " " << std::endl;
		this->db.update_local_md5(file.server_path.c_str(), md5.get<std::string>().c_str());
	}
	delete (db_file);
	return is_downloaded;
}
std::vector<filesync::File> filesync::FileSync::get_server_files(const char *path, const char *commit_id, const char *max_commit_id)
{
	std::string url_path = common::string_format("/api/files?path=%s&commit_id=%s&max=%s", url_encode(this->relative_to_server_path(path).string().c_str()).c_str(), commit_id, max_commit_id);
	std::vector<filesync::File> files;
	std::string res;
	std::unique_ptr<char> token{get_token()};
	if (!filesync::http_get(this->cfg.server_ip.c_str(), common::string_format("%d", this->cfg.server_port).c_str(), url_path.c_str(), token.get(), res))
	{
		EXCEPTION("http get failed");
	}
	else
	{
		//std::cout << res << std::endl;
	}
	auto j = json::parse(res.c_str());
	auto err = j["error"];
	if (!err.is_null())
	{
		std::cout << err << std::endl;
		return files;
	}
	auto data = j["data"];
	for (auto item : data)
	{
		std::string file_server_path = common::string_format("%s%s%s", path, strcmp(path, "/") == 0 ? "" : "/", item["name"].get<std::string>().c_str());
		auto f = this->server_file(file_server_path, item["commit_id"], strcmp(item["type"].get<std::string>().c_str(), "2") == 0);
		if (f.is_directory)
		{
			this->get_server_files(f.relative_path.c_str(), f.commit_id.c_str(), max_commit_id);
		}
		std::string md5{};
		if (!f.is_directory)
		{
			md5 = item["md5"].get<std::string>().c_str();
		}
		print_info(common::string_format("server file:%s", f.server_path.c_str()));
		this->db.add_file(f.server_path.c_str(), md5.c_str(), f.commit_id.c_str());
	}
	return files;
}
void filesync::FileSync::get_all_server_files()
{
	if (this->conf.commit_id.empty())
	{
		this->db.add_file("/", NULL, this->conf.first_commit_id.c_str());
		auto files = this->get_server_files("/", "", this->conf.max_commit_id.c_str());
		this->conf.commit_id = this->conf.max_commit_id;
		this->conf.save();
	}
}
void filesync::FileSync::save_change(json change, const char *commit_id)
{
	std::string id = change["Id"].get<std::string>();
	int change_type = change["ChangeType"].get<int>();
	std::string path = change["Path"].get<std::string>();
	/*//Skip if the change path is not under the syncing server location
	if (strcmp(this->server_location, path.substr(0, strlen(this->server_location)).c_str())!=0) {
		std::cout<<"file change outside the syncing server location:"<<path<<std::endl;
		return;
	}*/
	std::string source_path = change["Source_Path"].get<std::string>();
	std::string md5;
	if (!change["Md5"].is_null())
	{
		md5 = change["Md5"].get<std::string>();
	}
	switch (change_type)
	{
	case 1: //add
		this->db.add_file(path.c_str(), md5.c_str(), commit_id);
		break;
	case 2: //delete
		this->db.delete_file(path.c_str());
		break;
	case 3: //move
		/*this->db.delete_file(source_path.c_str());
		this->db.add_file(path.c_str(), md5.c_str());*/
		this->db.move(source_path.c_str(), path.c_str(), commit_id);
		break;
	case 4: //rename
		/*this->db.delete_file(source_path.c_str());
		this->db.add_file(path.c_str(), md5.c_str());*/
		this->db.move(source_path.c_str(), path.c_str(), commit_id);
		break;
	case 5: //copy
		this->db.add_file(path.c_str(), md5.c_str(), commit_id);
		break;
	default:
		EXCEPTION("unknown change type.");
		break;
	}
	/*{{if eq .ChangeType 1}}

				add

				{{else if eq .ChangeType 2}}

				delete

				{{else if eq .ChangeType 3}}

				move

				{{else if eq .ChangeType 4}}

				rename

				{{else if eq .ChangeType 5}}

				copy

				{{end}}*/
}
void filesync::FileSync::check_sync_path()
{
start:
	if (this->conf.sync_path.empty())
	{
		std::cout << "please enter the path of sync location:";
		std::string typed;
		std::cin >> typed;
		if (!std::filesystem::is_directory(typed))
		{
			std::cout << "not found the path or it is not a directory." << std::endl;
			goto start;
		}
		/*if (!std::filesystem::is_empty(typed)) {
			std::cout << "the directory you specified is not a empty directory." << std::endl;
			goto start;
		}*/
		if (std::filesystem::exists(this->conf.db_path))
		{
			std::error_code err;
			std::cout << "deleting " << this->conf.db_path << std::endl;
			if (!std::filesystem::remove(this->conf.db_path, err))
			{
				std::cout << err.message() << std::endl;
				exit(1);
			}
		}
		this->conf.sync_path = typed;
		this->conf.save();
	}
	root_path = common::strncpy(conf.sync_path.c_str());
	this->corret_path_sepatator(root_path);
}
bool filesync::FileSync::get_file_changes()
{
start:
	std::string url_path = common::string_format("/api/commit/changes?commit_id=%s", this->conf.commit_id.c_str());
	std::string res;
	char *token = get_token();
	auto ok = filesync::http_get(this->cfg.server_ip.c_str(), common::string_format("%d", this->cfg.server_port).c_str(), url_path.c_str(), token, res);
	delete (token);
	token = NULL;
	if (!ok)
	{
		std::cout << "HTTP GET changes failed" << std::endl;
		return false;
	}
	auto j = json::parse(res.c_str());
	auto err = j["error"];
	auto data = j["data"];
	if (!err.is_null())
	{
		std::cout << "Error:" << err << std::endl;
		return false;
	}
	if (data.is_null())
	{
		std::cout << "WARNING:"
				  << "not got file changes." << std::endl;
		this->conf.max_commit_id = this->conf.commit_id;
		this->conf.save();
		return true;
	}
	std::string commit_id = data["Commit_id"].get<std::string>();
	print_info(common::string_format("change id:%s", commit_id.c_str()));
	for (auto change : data["Changes"])
	{
		this->save_change(change, commit_id.c_str());
	}
	this->conf.commit_id = commit_id;
	this->conf.save();
	goto start;
}

void filesync::FileSync::print()
{
	std::cout << "the files pending sync:" << std::endl;
	for (auto &f : this->files)
	{
		if (f.status == FileStatus::Added || f.status == FileStatus::Modified)
			std::cout << f.relative_path << std::endl;
	}
}

filesync::FileSync::FileSync(char *server_location)
{
	cfg.load();
	this->server_location = server_location;
	this->db = filesync::db_manager{};
	this->committer = new ChangeCommitter(*this);
	this->corret_path_sepatator(this->server_location);
}
filesync::FileSync::~FileSync()
{
	delete (this->committer);
	this->committer = NULL;

	delete (this->server_location);
	this->server_location = NULL;

	for (auto i : this->files_map)
	{
		delete (i.first);
	}
}

void filesync::FileSync::corret_path_sepatator(char *path)
{
	std::replace(path, path + strlen(path), '\\', '/');
}
void filesync::FileSync::to_relative_path(char *path)
{
	int len = strlen(root_path), result_path_len = strlen(path) - len;
	if (memcmp(path, root_path, len) == 0)
	{
		memcpy(path, path + len, result_path_len);
		path[result_path_len] = '\0';
		if (result_path_len == 0)
		{
			path[0] = '/';
			path[1] = '\0';
		}
		return;
	}
	path[0] = '\0';
}
char *filesync::FileSync::get_relative_path_by_fulllpath(const char *path)
{
	int len = strlen(root_path), result_path_len = strlen(path) - len;
	if (result_path_len == 0)
	{
		return new char{'/'};
	}
	char *buf = new char[result_path_len + 1];
	if (memcmp(path, root_path, len) == 0)
	{
		memcpy(buf, path + len, result_path_len);
		buf[result_path_len] = '\0';
		return buf;
	}
	else
	{
		delete (buf);
		return NULL;
	}
}
char *filesync::FileSync::get_full_path(const char *path)
{
	int size = strlen(root_path) + strlen(path);
	char *full_path = new char[size + 1];
	memcpy(full_path, root_path, strlen(root_path));
	memcpy(full_path + strlen(root_path), path, strlen(path));
	full_path[size] = '\0';
	return full_path;
}
char *filesync::FileSync::get_relative_path(const char *server_path)
{
	if (strlen(this->server_location) == 1)
	{
		return common::strncpy(server_path);
	}
	auto r = server_path + strlen(this->server_location);
	char *result = common::strncpy(r);
	return result[0] == '\0' ? common::strncpy("/") : result;
}
std::filesystem::path filesync::FileSync::relative_to_server_path(const char *relative_path)
{
	return std::filesystem::path(this->server_location) / relative_path;
}
char *filesync::get_token()
{
	FILE *f = fopen(TOKEN_FILE_PATH, "rb");
	if (!f)
	{
		std::cout << "the token file does not exist" << std::endl;
		return NULL;
	}
	int size = 1000;
	char *buf = new char[((long)size) + 1];
	char *res = fgets(buf, size, f);
	if (!res)
	{
		std::cout << "reading file failed" << std::endl;
		return NULL;
	}
	fclose(f);
	return buf;
}
filesync::File filesync::FileSync::local_file(std::string full_path, bool is_directory)
{
	auto f = filesync::File();
	f.full_path = full_path;
	f.is_directory = is_directory;
	f.relative_path = this->get_relative_path_by_fulllpath(full_path.c_str());
	f.server_path = this->relative_to_server_path(f.relative_path.c_str()).string();
	return f;
}
filesync::File filesync::FileSync::server_file(std::string server_path, std::string commit_id, bool is_directory)
{
	auto f = filesync::File();
	f.full_path = this->get_full_path(server_path.c_str());
	f.is_directory = is_directory;
	f.relative_path = this->get_relative_path_by_fulllpath(f.full_path.c_str());
	f.server_path = server_path;
	f.commit_id = commit_id;

	if (f.commit_id.empty())
	{
		throw_exception("commit_id not provided.");
	}

	return f;
}
void filesync::throw_exception(std::string err)
{
	auto str = common::string_format("EXCEPTION:%s", err.c_str()).c_str();
	std::cout << str << std::endl;
	EXCEPTION(str);
}