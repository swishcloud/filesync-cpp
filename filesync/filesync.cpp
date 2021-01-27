// filesync.cpp : Defines the entry point for the application.
//
#include <http.h>
#include <tcp.h>
#include "filesync.h"
#include <locale.h>
#include "db_manager.h"
#include <nlohmann/json.hpp>
#include <assert.h>
#include <regex>
#include <server.h>
#include "boost/algorithm/string.hpp"
using namespace nlohmann;
namespace filesync
{
	char *root_path;
	std::string get_parent_dir(const char *filename)
	{
		std::cmatch m{};
		std::regex reg{".*[/\\\\]"};
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
		return common::strcpy(m[0].str().c_str());
	}
} // namespace filesync

int main(int argc, char *argv[])
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
	for (int i = 0; i < argc; i++)
	{
		filesync::print_debug(argv[i]);
	}
	filesync::FileSync *filesync = new filesync::FileSync{common::strcpy("/")};
	if (argc > 1)
	{
		if (std::string(argv[1]) == "listen")
		{
			if(argc<4){
				filesync::print_info(common::string_format("missing a paramter for server listening port, or a paramter for server files location"));
return 1;
			}
	common::http_client http_client{filesync->cfg.server_ip, filesync->cfg.server_port};
			#ifdef __linux__
	filesync::server server{std::stoi(argv[2]),argv[3], http_client};
#else
	filesync::server server{std::stoi(argv[2]), argv[3], http_client};
#endif
	server.listen();
			while (getchar())
			{
				/* code */
			}
		}
		else if (std::string(argv[1]) == "sync")
		{
			if (argc < 3)
			{
				filesync::print_info("missing the sync path.");
				return 1;
			}
			filesync::print_info(common::string_format("syncing all files within folder %s", argv[2]));
			filesync::tcp_client tcp_client{"192.168.1.1", "8008"};
			tcp_client.connect();
			while (getchar())
			{
				/* code */
			}
		}
	}
	try
	{
		filesync->connect();
		filesync->check_sync_path();
		filesync->db.init(filesync->conf.db_path.c_str());
		while (!filesync->get_all_server_files())
			;
		bool process_monitor = false;
		while (1)
		{
			std::cout << "-----begin syncing" << std::endl;
			if (!filesync->clear_errs())
			{
				continue;
			}
			if (!filesync->get_file_changes())
			{
				continue;
			}
			if (!filesync->sync_server())
			{
				continue;
			}

			bool procoss_monitor_failed = false;
			if (process_monitor)
			{
				while (auto local_change = filesync->get_local_file_change())
				{
					std::this_thread::sleep_for(std::chrono::milliseconds(100));
					local_change->path;
					std::replace(local_change->path.begin(), local_change->path.end(), '\\', '/');

					if (!filesync->sync_local_added_or_modified(local_change->path.c_str()))
					{
						procoss_monitor_failed = true;
					}
					if (!filesync->sync_local_deleted(local_change->path.c_str()))
					{
						procoss_monitor_failed = true;
					}
					if(procoss_monitor_failed){ 
						filesync->add_local_file_change(local_change);
					}else{
					delete (local_change);
					}
				}
			}
			else
			{
				if (!filesync->sync_local_added_or_modified(filesync->conf.sync_path.c_str()))
				{
					continue;
				}
				if (!filesync->sync_local_deleted(NULL))
				{
					continue;
				}
				process_monitor = true;
				filesync::print_info("begin processing  monitor...");
			}
			if (!procoss_monitor_failed)
				filesync->committer->commit();
			std::cout << "-----end syncing" << std::endl;
			std::this_thread::sleep_for(std::chrono::milliseconds(5000));
		}
	}
	catch (const std::exception &ex)
	{
		std::cout << ex.what() << "";
	}
	delete (filesync);
	return 0;
}
void filesync::FileSync::connect()
{
	try
	{
		delete tcp_client;
		tcp_client = new filesync::tcp_client{cfg.server_ip, common::string_format("%d", cfg.server_tcp_port)};
		if(!tcp_client->connect())
		throw common::exception("connect server failed");

		//connect web server
		common::socket::message msg;
		msg.msg_type = static_cast<int>(MsgType::Download_File);
		char *token = get_token();
		msg.addHeader({"token", token});
		delete[](token);
		this->tcp_client->session_->send_message(msg);
		auto reply = this->tcp_client->session_->read_message();

		if (reply.msg_type == 0)
		{
			throw common::exception("connection failed");
		}
		conf.max_commit_id = reply.getHeaderValue<std::string>("max_commit_id");
		conf.first_commit_id = reply.getHeaderValue<std::string>("first_commit_id");
		conf.partition_id = reply.getHeaderValue<std::string>("partition_id");

		cfg.save();
		conf.init(cfg.debug_mode);
		if (!std::filesystem::exists(conf.db_path))
		{
			conf.commit_id.clear();
		}

		//create the tcp client
		delete tcp_client;
		tcp_client = new filesync::tcp_client{"localhost", "8008"};
		if(!tcp_client->connect())
		throw common::exception("connect server failed");
	}
	catch (const std::exception &e)
	{
		std::cout << e.what() << std::endl;
		int delay = 10000;
		std::cout << "reconnect in " << delay / 1000 << "s" << std::endl;
		std::this_thread::sleep_for(std::chrono::milliseconds(delay));
		connect();
	}
}
bool filesync::FileSync::sync_server()
{
	if (!this->need_sync_server)
	{
		//todo:uncomment this. filesync::print_info("Server have no changes.");
		return true;
	}
	std::vector<std::string> errs;
	auto u_files = this->db.get_files();
	auto files = u_files.get();
	for (int i = 0; i < files->count; i++)
	{
		const char *file_name = files->get_value("name", i);
		const char *md5 = files->get_value("md5", i);
		const char *local_md5 = files->get_value("local_md5", i);
		const char *is_deleted_str = files->get_value("is_deleted", i);
		const char *commit_id = files->get_value("commit_id", i);
		bool is_deleted = is_deleted_str[0] == '1';
		std::unique_ptr<char[]> full_path{get_full_path(file_name)};
		std::cout << "sync_server>" << full_path.get() << std::endl;

		//struct File object
		File f = this->server_file(file_name, commit_id, md5 == NULL);

		//check if server has delete the file, if yes then delete the file locally, and hard delete the file from db
		if (is_deleted)
		{
			std::error_code err;
			if (std::filesystem::remove_all(full_path.get(), err) == -1)
			{ //return 0 if full_path not exists, return -1 on error.
				std::cout << err.message() << std::endl;
				std::string err = common::string_format("Failed to remove file:%s", full_path.get());
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
				if (!std::filesystem::exists(full_path.get()))
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
	this->need_sync_server = errs.size() != 0;
	return errs.size() == 0;
}
bool filesync::FileSync::sync_local_added_or_modified(const char *path)
{
	auto relative_path = this->get_relative_path_by_fulllpath(path);
	auto file_db = db.get_file(relative_path);
	//filesync::format_path(path);

	if (std::filesystem::exists(path))
	{
		if (std::filesystem::is_directory(path))
		{
			std::vector<std::string> files_cstr;
			common::find_files(path, files_cstr);
			for (std::string v : files_cstr)
			{
				this->sync_local_added_or_modified(filesync::format_path(v.c_str()).c_str());
			}

			if (file_db.get()->count == 0)
			{
				filesync::create_directory_action *action=new filesync::create_directory_action();
				action->is_hidden = false;
				action->path = common::strcpy(relative_path);
				this->committer->add_action(action);
			}
		}
		else
		{
			try
			{
				std::string md5;
				bool need_upload = false;
				if (file_db.get()->count == 0)
				{ //this is a added local file to be upload.
					md5 = file_md5(path);
					need_upload = true;
				}
				else
				{
					md5 = file_md5(path);
					if (!compare_md5(md5.c_str(), file_db.get()->get_value("local_md5")))
					{ //this file has been modified locally.
						need_upload = true;
					}
				}

				if (need_upload)
				{
					auto file_size = std::filesystem::file_size(path);
					auto r = this->upload_file(path, md5.c_str(), file_size);
					std::cout << "UPLOAD " << (r ? "OK" : "Failed") << " :" << path << std::endl;
					if (r)
					{
						create_file_action *action=new create_file_action();
						action->is_hidden = false;
						action->location = common::strcpy(get_parent_dir(relative_path).c_str());
						action->md5 = common::strcpy(md5.c_str());
						action->name = file_name(relative_path);
						this->committer->add_action(action);
					}
					else
					{
						errs.push_back(common::string_format("failed to upload %s", path));
					}
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
	delete[](relative_path);
	return errs.size() == 0;
}
bool filesync::FileSync::sync_local_deleted(const char *path)
{
	std::unique_ptr<filesync::sqlite_query_result> u_files;
	filesync::sqlite_query_result *files;
	if (path == NULL)
	{
		u_files = this->db.get_files();
		files = u_files.get();
	}
	else
	{
		auto relative_path = this->get_relative_path_by_fulllpath(path);
		u_files = this->db.get_file(relative_path);
		delete[] relative_path;
		files = u_files.get();
	}

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
			delete_by_path_action *action=new delete_by_path_action();
			action->commit_id = common::strcpy(commit_id);
			action->file_type = is_directory ? 2 : 1;
			action->path = common::strcpy(file_name);
			this->committer->add_action(action);
		}
		delete[](full_path);
	}
	return true;
}
bool filesync::FileSync::upload_file(std::string full_path, const char *md5, long size)
{
	std::string url_path = common::string_format("/api/file-info?md5=%s&size=%d", md5, size);
	char *token = filesync::get_token();
	common::http_client c{this->cfg.server_ip.c_str(), common::string_format("%d", this->cfg.server_port).c_str(), url_path.c_str(), token};
	c.GET();
	delete[](token);
	if (!c.error.empty())

	{
		return false;
	}
	auto j = json::parse(c.resp_text);
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

	common::socket::message msg;
	msg.msg_type = static_cast<int>(MsgType::UploadFile);
	msg.addHeader({"path", path});
	msg.addHeader({"md5", md5});
	msg.addHeader({"file_size", (size_t)std::stoi(file_size)});
	msg.addHeader({"uploaded_size", (size_t)std::stoi(uploaded_size)});
	msg.addHeader({"server_file_id", server_file_id});
	token = filesync::get_token();
	msg.addHeader({TokenHeaderKey, token});
	delete[](token);
	msg.body_size = std::stoi(file_size) - std::stoi(uploaded_size);
	if (!this->tcp_client->session_->send_message(msg))
	{
		filesync::print_info(common::string_format("failed to send message"));
		return false;
	}
	this->tcp_client->session_->send_file(full_path, std::stoi(uploaded_size));
	auto reply = this->tcp_client->session_->read_message();
	if (reply.msg_type == static_cast<int>(filesync::MsgType::Reply))
	{
		return true;
	}
	else
	{
		filesync::print_info(common::string_format("Failed to upload %s", full_path.c_str()));
		return false;
	}
}
bool filesync::FileSync::upload_file_v2(const char *ip, unsigned short port, std::ifstream &fs, const char *path, const char *md5, long file_size, long uploaded_size, const char *server_file_id)
{
	return false;
}
bool filesync::FileSync::clear_errs()
{
	this->errs.clear();
	if (this->tcp_client->session_==NULL)
	{
		delete this->tcp_client;
		this->tcp_client = new filesync::tcp_client{"localhost", "8008"};
		return this->tcp_client->connect();
	}
	return true;
}
bool filesync::FileSync::download_file(File &file)
{
	std::string url_path = common::string_format("/api/file?path=%s&commit_id=%s", url_encode(file.server_path.c_str()).c_str(), file.commit_id.c_str());
	char *token = filesync::get_token();
	common::http_client c{this->cfg.server_ip.c_str(), common::string_format("%d", this->cfg.server_port).c_str(), url_path.c_str(), token};
	c.GET();
	delete[](token);
	if (!c.error.empty())
	{
		return false;
	}
	auto j = json::parse(c.resp_text);
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
	assert(db_file.get()->count > 0);
	bool is_downloaded = false;

	common::socket::message msg;
	msg.msg_type = static_cast<int>(MsgType::Download_File);
	msg.addHeader({"path", s_path.get<std::string>()});
	token = filesync::get_token();
	msg.addHeader({TokenHeaderKey, token});
	delete[](token);
	this->tcp_client->session_->send_message(msg);
	auto reply = this->tcp_client->session_->read_message();
	if (!reply)
	{
		return false;
	}
	std::promise<bool> promise;
	std::future<bool> future = promise.get_future();
	std::string tmp_path = (std::filesystem::temp_directory_path() / trim_trailing_space(md5.get<std::string>())).string();
	std::ofstream os{tmp_path, std::ios_base::binary};
	if (os.bad())
	{
		filesync::print_info(common::string_format("failed to create a file named %s", tmp_path.c_str()));
		return false;
	}
	if(compare_md5(md5.get<std::string>().c_str(),filesync::file_md5(file.full_path.c_str()).c_str())){
		is_downloaded=true;
	}else{
	filesync::print_info(common::string_format("Downloading file %s", file.server_path.c_str()));
	this->tcp_client->session_->async_read_chunk(reply.body_size, [&promise, tmp_path, this, &os, md5, &file](int len, std::string error, bool finished) {
		if (!error.empty())
		{
			filesync::print_info(common::string_format("failed to download file %s", file.server_path.c_str()));
			promise.set_value(false);
			return;
		}
		os.write(this->tcp_client->session_->buf.get(), len);
		if (os.bad())
		{
			filesync::print_info(common::string_format("failed to download file %s", file.server_path.c_str()));
			promise.set_value(false);
			return;
		}
		if (finished)
		{
			os.close();
			if (!filesync::compare_md5(filesync::file_md5(tmp_path.c_str()).c_str(), md5.get<std::string>().c_str()))
			{
				filesync::print_info(common::string_format("Download a file with wrong MD5,deleting it...", file.server_path.c_str()));
				if (!std::filesystem::remove(tmp_path))
				{
					filesync::print_info(common::string_format("WARNING!failed to delete the temp file %s", tmp_path.c_str()));
				}
				promise.set_value(false);
				return;
			}
			try
			{
				std::filesystem::rename(tmp_path, file.full_path);
				promise.set_value(true);
			}
			catch (std::filesystem::filesystem_error &e)
			{
				filesync::print_info(common::string_format("failed to rename the file with error:%s", e.what()));
				promise.set_value(false);
			}
		}
	});
	is_downloaded = future.get();
	}
	if (is_downloaded)
	{
		filesync::print_info(common::string_format("downloaded file:%s", file.full_path.c_str()));
		this->db.update_local_md5(file.server_path.c_str(), md5.get<std::string>().c_str());
	}
	return is_downloaded;
}
std::vector<filesync::File> filesync::FileSync::get_server_files(const char *path, const char *commit_id, const char *max_commit_id, bool *ok)
{
	std::vector<filesync::File> files;
	std::cout << path << std::endl;
	std::string url_path = common::string_format("/api/files?path=%s&commit_id=%s&max=%s", url_encode(this->relative_to_server_path(path).string().c_str()).c_str(), commit_id, max_commit_id);

	std::unique_ptr<char[]> token{get_token()};
	common::http_client c{this->cfg.server_ip.c_str(), common::string_format("%d", this->cfg.server_port).c_str(), url_path.c_str(), token.get()};
	do
	{
		c.GET();
	} while (!c.error.empty());
	auto j = json::parse(c.resp_text);
	auto err = j["error"];
	if (!err.is_null())
	{
		std::cout << err << std::endl;
		*ok = false;
		return files;
	}
	auto data = j["data"];
	for (auto item : data)
	{
		std::string file_server_path = common::string_format("%s%s%s", path, strcmp(path, "/") == 0 ? "" : "/", item["name"].get<std::string>().c_str());
		filesync::File f;
		f = this->server_file(file_server_path, item["commit_id"], strcmp(item["type"].get<std::string>().c_str(), "2") == 0);
		if (f.is_directory)
		{
			this->get_server_files(f.relative_path.c_str(), f.commit_id.c_str(), max_commit_id, ok);
			if (!ok)
			{
				return files;
			}
		}
		std::string md5{};
		if (!f.is_directory)
		{
			md5 = item["md5"].get<std::string>().c_str();
		}
		print_info(common::string_format("server file:%s", f.server_path.c_str()));
		this->db.add_file(f.server_path.c_str(), md5.c_str(), f.commit_id.c_str());
	}
	*ok = true;
	return files;
}
bool filesync::FileSync::get_all_server_files()
{
	this->need_sync_server = true;
	if (this->conf.commit_id.empty())
	{
		this->db.add_file("/", NULL, this->conf.first_commit_id.c_str());
		bool ok = false;
		auto files = this->get_server_files("/", "", this->conf.max_commit_id.c_str(), &ok);
		this->conf.commit_id = this->conf.max_commit_id;
		this->conf.save();
		return ok;
	}
	else
	{
		return true;
	}
}
void filesync::FileSync::save_change(json change, const char *commit_id)
{
	std::string id = change["Id"].get<std::string>();
	int change_type = change["ChangeType"].get<int>();
	std::string path = change["Path"].get<std::string>();
	/*//Skip if the change path is not under the syncing server location
	if (strcmp(this-/home/bigyasuo/Desktop/test_sync>server_location, path.substr(0, strlen(this->server_location)).c_str())!=0) {
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
	this->need_sync_server = true;
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
				EXCEPTION(err.message());
			}
		}
		this->conf.sync_path = typed;
		this->conf.save();
	}
	root_path = common::strcpy(conf.sync_path.c_str());
	this->corret_path_sepatator(root_path);
	assert(this->monitor == NULL);
#ifdef __linux__
	this->monitor = new common::monitor::linux_monitor();
#else
	this->monitor = new common::monitor::win_monitor(this->conf.sync_path);
#endif
	monitor->watch(this->conf.sync_path);
	monitor->read_async(&filesync::FileSync::monitor_cb, this);
}
bool filesync::FileSync::get_file_changes()
{
start:
	std::string url_path = common::string_format("/api/commit/changes?commit_id=%s", this->conf.commit_id.c_str());
	char *token = filesync::get_token();
	common::http_client c{this->cfg.server_ip.c_str(), common::string_format("%d", this->cfg.server_port).c_str(), url_path.c_str(), std::string(token)};
	c.GET();
	delete[](token);
	if (!c.error.empty())
	{
		print_info("http get failed");
		return false;
	}
	auto j = json::parse(c.resp_text);
	auto err = j["error"];
	auto data = j["data"];
	if (!err.is_null())
	{
		print_info(common::string_format("%s", err.get<std::string>().c_str()));
		return false;
	}
	if (data.is_null())
	{
		//std::cout << "WARNING:"
		//		  << "not got file changes." << std::endl;
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
	this->monitor = NULL;
	this->tcp_client = NULL;
	this->corret_path_sepatator(this->server_location);
}
filesync::FileSync::~FileSync()
{
	delete (this->committer);
	delete[](this->server_location);
	delete (this->monitor);
	delete this->tcp_client;

	this->committer = NULL;
	this->server_location = NULL;
	this->monitor = NULL;

	for (auto i : this->files_map)
	{
		delete[](i.first);
	}

	while (auto local_change = this->get_local_file_change())
	{
		delete (local_change);
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
		return new char[2]{'/', '\0'};
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
		delete[](buf);
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
		return common::strcpy(server_path);
	}
	auto r = server_path + strlen(this->server_location);
	char *result = common::strcpy(r);
	if (result[0] == '\0')
	{
		delete[](result);
		return common::strcpy("/");
	}
	else
	{
		return result;
	}
}
std::filesystem::path filesync::FileSync::relative_to_server_path(const char *relative_path)
{
	return std::filesystem::path(this->server_location) / relative_path;
}
char *filesync::get_token()
{
#ifdef __linux__
	system("filesync_old login --insecure");
#endif
	FILE *f = fopen(TOKEN_FILE_PATH, "rb");
	if (!f)
	{
		EXCEPTION("the token file does not exist");
	}
	int size = 1000;
	char *buf = new char[((long)size)];
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
	auto full_path = this->get_full_path(server_path.c_str());
	f.full_path = full_path;
	delete[] full_path;
	f.is_directory = is_directory;
	auto relative_path = this->get_relative_path_by_fulllpath(f.full_path.c_str());
	f.relative_path = relative_path;
	delete[] relative_path;
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
	auto str = common::string_format("EXCEPTION:%s", err.c_str());
	EXCEPTION(str);
}
void filesync::FileSync::monitor_cb(common::monitor::change *c, void *obj)
{
	auto filesync = (filesync::FileSync *)obj;
	std::replace(c->path.begin(), c->path.end(), '\\', '/');
	std::cout << "Entered the callback:" << c->path << std::endl;
	filesync->add_local_file_change(c);
}
void filesync::FileSync::add_local_file_change(common::monitor::change *change)
{
	std::lock_guard<std::mutex> guard(_local_file_changes_mutex);
	this->_local_file_changes.push(change);
}
common::monitor::change *filesync::FileSync::get_local_file_change()
{
	std::lock_guard<std::mutex> guard(_local_file_changes_mutex);
	if (this->_local_file_changes.empty())
	{
		return NULL;
	}
	auto change = this->_local_file_changes.front();
	this->_local_file_changes.pop();
	return change;
}