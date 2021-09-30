﻿// filesync.cpp : Defines the entry point for the application.
//
#include <http.h>
#include "filesync.h"
#include <locale.h>
#include "db_manager.h"
#include <nlohmann/json.hpp>
#include <assert.h>
#include <regex>
#include <cfg.h>
#include <server.h>
#include "boost/algorithm/string.hpp"
#include "CLI11.hpp"
#include <boost/program_options.hpp>
using namespace nlohmann;
namespace po = boost::program_options;
std::string token_file_path;
namespace filesync
{
	PATH root_path;
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
		std::regex reg{".*\\S"};
		if (std::regex_search(str, m, reg))
			return m.str(0);
		else
			return std::string{};
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
void begin_sync(std::string account, bool get_all_server_files)
{
	common::print_info("login account:" + account);
	if (get_all_server_files)
	{
		common::print_info("will begin getting all server files in 3 seconds.Be patient...");
		std::this_thread::sleep_for(std::chrono::milliseconds(1000 * 3));
	}
	filesync::FileSync *filesync = new filesync::FileSync{common::strcpy("/")};
	token_file_path = (std::filesystem::path(filesync::datapath) / common::string_format("token-%s", account.c_str())).string();
	try
	{
		filesync->connect();
		filesync->check_sync_path();
		std::error_code ec;
		if (get_all_server_files && !std::filesystem::remove(filesync->conf.db_path.c_str(), ec))
		{
			delete filesync;
			common::print_info(ec.message());
			return;
		}
		filesync->db.init(filesync->conf.db_path.c_str());
		while (!filesync->get_all_server_files(get_all_server_files))
			;
		get_all_server_files = true;
		bool process_monitor = false;
		while (1)
		{
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
					if (procoss_monitor_failed)
					{
						filesync->add_local_file_change(local_change);
					}
					else
					{
						delete (local_change);
					}
				}
			}
			else
			{
				if (!filesync->sync_local_added_or_modified(filesync->conf.sync_path.string().c_str()))
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
			std::this_thread::sleep_for(std::chrono::milliseconds(5000));
		}
	}
	catch (const std::exception &ex)
	{
		common::print_info(ex.what());
	}
	delete (filesync);
}
void begin_listen(std::string listen_port, std::string files_path)
{
	filesync::FileSync *filesync = new filesync::FileSync{common::strcpy("/")};
	common::http_client http_client{filesync->cfg.server_ip, filesync->cfg.server_port};
#ifdef __linux__
	filesync::server s{(short)std::stoi(listen_port.c_str()), files_path, http_client};
#else
	filesync::server s{(short)std::stoi(listen_port.c_str()), files_path, http_client};
#endif
	s.listen();

	filesync::print_info("exited.");
}
void begin_export(filesync::PATH path, std::string commit_id, std::string max_commit_id, filesync::PATH destination_folder)
{
	common::print_info(common::string_format("exporting server directory %s with content since %s until %s to local directory %s", path.string().c_str(), std::string(commit_id) == "/" ? "first commit" : commit_id.c_str(), max_commit_id.c_str(), destination_folder.string().c_str()));
	commit_id = std::string(commit_id) == "/" ? "" : commit_id;
	if (!std::filesystem::exists(destination_folder.string()))
	{
		std::error_code ec;
		std::filesystem::create_directory(destination_folder.string(), ec);
		if (ec)
		{
			common::print_info(ec.message());
			return;
		}
	}
	else
	{
		if (!std::filesystem::is_directory(destination_folder.string()))
		{
			common::print_debug(common::string_format("the path %s already exists, but it's not a directory", destination_folder.string().c_str()));
			return;
		}
	}
	filesync::FileSync *filesync = new filesync::FileSync{common::strcpy("/")};
	filesync->connect();
	std::string first_server_path = path.string();
	std::string export_directory_path = destination_folder.string();
	export_directory_path = (std::filesystem::path{export_directory_path} / common::get_file_name(first_server_path)).string();
	common::makedir(export_directory_path);
	std::vector<std::string> failed_paths;
	auto error = filesync->get_server_files(path.string(), commit_id, max_commit_id, [&failed_paths, first_server_path, export_directory_path, filesync](filesync::ServerFile &file)
											{
												auto relative_path = common::get_relative_path(first_server_path, file.path);
												auto exported_path = (std::filesystem::path{export_directory_path} / relative_path).string();
												if (file.is_directory)
												{
													common::print_info(common::string_format("creating directory:%s", exported_path.c_str()));
													common::makedir(exported_path);
													return;
												}
												bool has_downloaded = false;
												if (std::filesystem::exists(exported_path))
												{
													auto md5 = common::file_md5(exported_path.c_str());
													if (filesync::compare_md5(md5.c_str(), file.md5.c_str()))
													{
														has_downloaded = true;
													}
												}
												if (has_downloaded)
												{
													common::print_info(common::string_format("%s already exists", exported_path.c_str()));
													return;
												}
												common::error err = filesync->download_file(file.path, file.commit_id, exported_path);
												if (err)
												{
													filesync->destroy_tcp_client();
													common::print_info(err.message());
													failed_paths.push_back(exported_path);
												}
											});
	if (!error.empty())
	{
		common::print_info(error);
		exit(1);
	}
	if (failed_paths.size() == 0)
	{
		common::print_info("Already exported all files.");
		return;
	}
	else
	{
		common::print_info("the following paths are not exported successfully:");
		for (auto i : failed_paths)
		{
			std::cout << "FAIL:" << i << std::endl;
		}
		return;
	}
}
void CMD_REPORT(CLI::App *parent)
{
	auto opt = std::shared_ptr<filesync::CMD_EXPORT_OPTION>{new filesync::CMD_EXPORT_OPTION};
	auto export_cmd = parent->add_subcommand("export", "export files");
	export_cmd->add_option("--account", opt->account, "your account name")->required();
	export_cmd->add_option("--path", opt->path, "the path to export")->required();
	export_cmd->add_option("--commit_id", opt->commit_id, "the commit id of a path")->required();
	export_cmd->add_option("--max_commit_id", opt->max_commit_id, "the max commit id of subfiles to query")->required();
	export_cmd->add_option("--dest", opt->destination_folder, "the destination folder where save exported files")->required();
	export_cmd->callback([opt]()
						 {
							 token_file_path = (std::filesystem::path(filesync::datapath) / common::string_format("token-%s", opt->account.c_str())).string();
							 begin_export(opt->path, opt->commit_id, opt->max_commit_id, opt->destination_folder);
						 });
}
int main(int argc, char *argv[])
{
	setlocale(LC_ALL, "zh_CN.UTF-8");
	CLI::App app("filesync tool");
	auto sync = app.add_subcommand("sync", "syncing everything");
	std::string account;
	bool fa = false;
	sync->add_option("--account", account, "your account name")->required();
	sync->add_option("--fa", fa, "force looking up all server files.this option is recommended for some unusual circumstance.");
	sync->callback([&account, &fa]()
				   { begin_sync(account, fa); });
	std::string listen_port, files_path;

	auto listen = app.add_subcommand("listen", "listen as a server node");
	listen->add_option("--listen_port", listen_port, "the tcp listen port")->required();
	listen->add_option("--files_path", files_path, "the path of files repo")->required();
	listen->callback([&listen_port, &files_path]()
					 { begin_listen(listen_port, files_path); });

	CMD_REPORT(&app);
	CLI11_PARSE(app, argc, argv);
	return 0;

	po::options_description desc("Allowed options");
	desc.add_options()("help", "produce help message")("sync", po::value<int>(), "set compression level");

	po::variables_map vm;
	po::store(po::parse_command_line(argc, argv, desc), vm);
	po::notify(vm);

	if (vm.count("help"))
	{
		std::cout << desc << "\n";
		return 1;
	}

	/*if (vm.count("compression"))
	{
		cout << "Compression level was set to "
			 << vm["compression"].as<int>() << ".\n";
	}
	else
	{
		cout << "Compression level was not set.\n";
	}*/

	//std::cout << "LC_ALL: " << setlocale(LC_ALL, NULL) << std::endl;

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
	/*for (int i = 0; i < argc; i++)
	{
		filesync::print_debug(argv[i]);
	}*/
	filesync::FileSync *filesync = new filesync::FileSync{common::strcpy("/")};
	token_file_path = (std::filesystem::path(filesync::datapath) / "token").string();
	if (argc > 1)
	{
		if (std::string(argv[1]) == "freespace")
		{
			filesync->connect();
			filesync->check_sync_path();
			filesync->db.init(filesync->conf.db_path.c_str());
			filesync->clear_synced_files(filesync->conf.sync_path.string().c_str());
			filesync::print_info(common::string_format("Freed some space from %s,exit...", filesync->conf.sync_path.string().c_str()));
			exit(0);
		}
	}

	return 0;
}
void filesync::FileSync::on_file_downloaded(PATH full_path, std::string md5)
{
	common::print_info(common::string_format("on_file_downloaded:%s", full_path.string().c_str()));
}
void filesync::FileSync::on_file_uploaded(PATH full_path, std::string md5)
{
	common::print_info(common::string_format("on_file_uploaded:%s", full_path.string().c_str()));
	create_file_action *action = new create_file_action();
	action->is_hidden = false;
	auto relative_path = get_relative_path_by_fulllpath(full_path.string().c_str());
	action->location = common::strcpy(get_parent_dir(relative_path.c_str()).c_str());
	action->md5 = common::strcpy(md5.c_str());
	action->name = file_name(relative_path.c_str());
	this->committer->add_action(action);
}
std::string filesync::FileSync::get_server_files(std::string path, std::string commit_id, std::string max_commit_id, std::function<void(ServerFile &file)> callback)
{
	std::string url_path = common::string_format("/api/files?path=%s&commit_id=%s&max=%s", common::url_encode(this->relative_to_server_path(path).string().c_str()).c_str(), commit_id.c_str(), max_commit_id.c_str());
	std::unique_ptr<char[]> token{get_token()};
	common::http_client c{this->cfg.server_ip.c_str(), common::string_format("%d", this->cfg.server_port).c_str(), url_path.c_str(), token.get()};
	while (1)
	{
		c.GET();
		if (c.error)
			common::print_info(c.error.message());
		else
			break;
	}
	auto j = json::parse(c.resp_text);
	auto err = j["error"];
	if (!err.is_null())
	{
		return "http resp:" + err.get<std::string>();
	}
	auto data = j["data"];
	for (auto item : data)
	{
		std::string file_server_path = common::string_format("%s%s%s", path.c_str(), path == "/" ? "" : "/", item["name"].get<std::string>().c_str());
		ServerFile f;
		f.is_directory = strcmp(item["type"].get<std::string>().c_str(), "2") == 0;
		if (!item["md5"].is_null())
			f.md5 = item["md5"].get<std::string>();
		f.name = item["name"].get<std::string>();
		if (!item["size"].is_null())
			f.size = common::to_size_t(item["size"].get<std::string>());
		f.commit_id = item["commit_id"].get<std::string>();
		f.path = file_server_path;
		callback(f);
		if (f.is_directory)
		{
			auto error = this->get_server_files(f.path.c_str(), f.commit_id.c_str(), max_commit_id, callback);
			if (!error.empty())
			{
				return error;
			}
		}
	}
	return "";
}
void filesync::FileSync::connect()
{
	try
	{
		tcp_client tcp_client{cfg.server_ip, common::string_format("%d", cfg.server_tcp_port)};
		if (!tcp_client.connect())
			throw common::exception("connect Web TCP Server failed");

		//connect web server
		XTCP::message msg;
		msg.msg_type = static_cast<int>(filesync::tcp::MsgType::Download_File);
		char *token = get_token();
		msg.addHeader({"token", token});
		delete[](token);
		common::error err;
		XTCP::send_message(&tcp_client.xclient.session, msg, err);
		if (err)
		{
			throw common::exception(err.message());
		}
		XTCP::message reply;
		XTCP::read_message(&tcp_client.xclient.session, reply, err);
		if (err)
		{
			throw common::exception(err.message());
		}
		if (!reply)
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
		//std::cout << "sync_server>" << full_path.get() << std::endl;

		//struct File object
		File f = this->server_file(file_name, commit_id, md5 == NULL);
		if (f.server_path == "/")
		{
			continue;
		}
		if (!this->monitor_path(f.server_path))
		{
			common::print_debug(common::string_format("clearing Non-monitored path in db:%s", f.server_path.c_str()));
			this->db.delete_file_hard(f.server_path.c_str());
			continue;
		}
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
				if (std::filesystem::is_regular_file(full_path.get()))
				{
					common::print_info(common::string_format("there is already a file with path:%s", full_path.get()));
					return false;
				}
				if (!std::filesystem::exists(full_path.get()))
					assert(std::filesystem::create_directories(f.full_path));
				this->db.update_local_md5(f.server_path.c_str(), DIRECTORY_MD5);
			}
		}
		else
		{
			//check if the file has not been downloaded or the file is out of date, if it's in either case then download/re-download the file.
			if (local_md5 == NULL || !compare_md5(md5, local_md5))
			{
				bool has_downloaded = false;
				if (std::filesystem::exists(f.full_path))
				{
					auto file_md5 = common::file_md5(f.full_path.c_str());
					if (filesync::compare_md5(file_md5.c_str(), md5))
					{
						has_downloaded = true;
					}
				}
				if (has_downloaded)
				{
					this->db.update_local_md5(f.server_path.c_str(), md5);
				}
				else
				{
					common::error err = this->download_file(f.server_path, f.commit_id, f.full_path);
					if (err)
					{
						destroy_tcp_client();
						common::print_info(common::string_format("Downloading failed:%s", err.message()));
						errs.push_back(err.message());
					}
					else
					{
						this->db.update_local_md5(f.server_path.c_str(), md5);
					}
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
	auto file_db = db.get_file(relative_path.c_str());
	//filesync::format_path(path);

	if (std::filesystem::exists(path))
	{
		if (std::filesystem::is_directory(path))
		{
			std::vector<std::string> files_cstr;
			common::find_files(path, files_cstr);
			for (std::string v : files_cstr)
			{
				if (!this->sync_local_added_or_modified(filesync::format_path(v.c_str()).c_str()))
				{
					return false;
				}
			}
			if (relative_path == "/")
			{
				return true;
			}
			if (!this->monitor_path(relative_path))
			{
				common::print_debug(common::string_format("sync_local_added_or_modified->Skip Non-monitored path:%s", relative_path.c_str()));
				return true;
			}
			if (file_db.get()->count == 0)
			{
				filesync::create_directory_action *action = new filesync::create_directory_action();
				action->is_hidden = false;
				action->path = common::strcpy(relative_path.c_str());
				this->committer->add_action(action);
			}
		}
		else
		{
			if (!this->monitor_path(relative_path))
			{
				common::print_debug(common::string_format("sync_local_added_or_modified->Skip Non-monitored path:%s", relative_path.c_str()));
				return true;
			}
			try
			{
				std::string md5;
				bool need_upload = false;
				if (file_db.get()->count == 0)
				{ //this is a added local file to be upload.
					md5 = common::file_md5(path);
					need_upload = true;
				}
				else
				{
					md5 = common::file_md5(path);
					const char *server_md5 = file_db.get()->get_value("md5");
					const char *local_md5 = file_db.get()->get_value("local_md5");
					if (!compare_md5(md5.c_str(), local_md5 ? local_md5 : server_md5))
					{ //this file has been modified locally.
						need_upload = true;
					}
				}
				if (need_upload)
				{
					auto file_size = std::filesystem::file_size(path);
					auto r = this->upload_file(path, md5.c_str(), file_size);
					std::cout << "UPLOAD " << (r ? "OK" : "Failed") << " :" << path << std::endl;
					if (!r)
					{
						errs.push_back(common::string_format("failed to upload %s", path));
						this->get_tcp_client()->xclient.session.close();
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
	return errs.size() == 0;
}
bool filesync::FileSync::sync_local_deleted(const char *path)
{
	std::unique_ptr<filesync::sqlite_query_result> u_files;
	filesync::sqlite_query_result *files;

	u_files = this->db.get_files();
	files = u_files.get();
	//todo: change to the following commented code after fixed the bug that win_monitor can't get exact deleted path in callback
	/*if (path == NULL)
	{
		u_files = this->db.get_files();
		files = u_files.get();
	}
	else
	{
		auto relative_path = this->get_relative_path_by_fulllpath(path);
		u_files = this->db.get_file(relative_path.c_str());
		files = u_files.get();
	}*/

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
		auto relative_path = this->get_relative_path_by_fulllpath(full_path);
		if (relative_path == "/")
		{
			continue;
		}
		if (!this->monitor_path(relative_path))
		{
			common::print_debug(common::string_format("Skip Non-monitored path:%s", relative_path.c_str()));
			continue;
		}
		if (local_md5 && !std::filesystem::exists(full_path))
		{ //this file has been deleted locally.
			delete_by_path_action *action = new delete_by_path_action();
			action->commit_id = common::strcpy(commit_id);
			action->file_type = is_directory ? 2 : 1;
			action->path = common::strcpy(file_name);
			this->committer->add_action(action);
		}
		delete[](full_path);
	}
	return true;
}
bool filesync::FileSync::clear_synced_files(const char *path)
{
	std::vector<std::string> files;
	common::find_files(path, files, true, 1);
	for (std::string v : files)
	{
		v = format_path(v);
		auto relative_path = this->get_relative_path_by_fulllpath(v.c_str());
		auto file_db = db.get_file(relative_path.c_str());
		std::string md5 = common::file_md5(v.c_str());
		if (file_db.get()->count == 1)
		{
			const char *server_md5 = file_db.get()->get_value("md5");
			const char *local_md5 = file_db.get()->get_value("local_md5");
			if (compare_md5(md5.c_str(), server_md5))
			{
				this->db.update_local_md5(relative_path.c_str(), NULL);
				if (!std::filesystem::remove(v.c_str()))
				{
					throw common::exception(common::string_format("Failed to delete file %s", v.c_str()));
				}
			}
		}
	}

	std::vector<std::string> directories;
	common::find_files(path, directories, true, 2);
	for (std::string v : directories)
	{
		v = format_path(v);
		auto relative_path = this->get_relative_path_by_fulllpath(v.c_str());
		auto file_db = db.get_file(relative_path.c_str());
		if (file_db.get()->count == 1)
		{
			if (std::filesystem::is_empty(v))
			{
				this->db.update_local_md5(relative_path.c_str(), NULL);
				if (!std::filesystem::remove(v))
					throw common::exception(common::string_format("Failed to delete file %s", v.c_str()));
			}
		}
	}
	return true;
}
bool filesync::FileSync::upload_file(std::string full_path, const char *md5, long size)
{
	XTCP::message reply;
	std::promise<common::error> promise;
	std::shared_ptr<std::istream> fs;
	common::error err;
	XTCP::message msg;
	tcp_client *tcp_client = this->get_tcp_client();
	std::string url_path = common::string_format("/api/file-info?md5=%s&size=%d", md5, size);
	char *token = filesync::get_token();
	common::http_client c{this->cfg.server_ip.c_str(), common::string_format("%d", this->cfg.server_port).c_str(), url_path.c_str(), token};
	c.GET();
	delete[](token);
	if (c.error)
	{
		common::print_info(c.error.message());
		return false;
	}
	auto j = json::parse(c.resp_text);
	auto data = j["data"];
	if (!j["error"].is_null())
	{
		common::print_info(common::string_format("Failed to get file info from web server:%s", j["error"].get<std::string>().c_str()));
		return false;
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
		this->on_file_uploaded(full_path, md5);
		return true;
	}
	msg.msg_type = static_cast<int>(filesync::tcp::MsgType::UploadFile);
	msg.addHeader({"path", path});
	msg.addHeader({"md5", md5});
	msg.addHeader({"file_size", (size_t)std::stoi(file_size)});
	msg.addHeader({"uploaded_size", (size_t)std::stoi(uploaded_size)});
	msg.addHeader({"server_file_id", server_file_id});
	token = filesync::get_token();
	msg.addHeader({TokenHeaderKey, token});
	delete[](token);
	msg.body_size = std::stoi(file_size) - std::stoi(uploaded_size);
	XTCP::send_message(&tcp_client->xclient.session, msg, err);
	if (err)
	{
		common::print_info(common::string_format("UPLOAD failed %s", err.message()));
		return false;
	}
	fs = std::shared_ptr<std::istream>{new std::ifstream(full_path, std::ios::binary)};
	fs->seekg(std::stoi(uploaded_size), std::ios_base::beg);
	if (!*fs)
	{
		common::print_info(common::string_format("can not open %s", full_path.c_str()));
		return false;
	}
	common::print_info(common::string_format("uploading file %s...", full_path.c_str()));
	tcp_client->xclient.session.send_stream(
		std::shared_ptr<std::istream>{new std::ifstream(full_path, std::ios::binary)}, [&promise](size_t written_size, XTCP::tcp_session *session, bool completed, common::error error, void *p)
		{
			if (error || completed)
			{
				promise.set_value(error);
			}
		},
		NULL);
	err = promise.get_future().get();
	if (err)
	{
		common::print_info(err.message());
		return false;
	}
	common::print_info(common::string_format("waiting for server replying..."));
	XTCP::read_message(&tcp_client->xclient.session, reply, err);
	if (err)
	{
		common::print_info(common::string_format("UPLOAD failed %s", err.message()));
		return false;
	}
	if (reply.msg_type == static_cast<int>(filesync::tcp::MsgType::Reply))
	{
		this->on_file_uploaded(full_path, md5);
		return true;
	}
	else
	{
		filesync::print_info(common::string_format("invalid msg type."));
		return false;
	}
}
bool filesync::FileSync::clear_errs()
{
	this->errs.clear();
	return true;
}
filesync::tcp_client *filesync::FileSync::get_tcp_client()
{
	if (!this->_tcp_client || this->_tcp_client->closed)
	{
		while (1)
		{
			//create the tcp client
			common::print_debug("creating a tcp client...");
			delete _tcp_client;
			//todo: make tcp server port configurable
			//_tcp_client = new filesync::tcp_client{cfg.server_ip, common::string_format("%d", 8008)};
			_tcp_client = new filesync::tcp_client{this->cfg.server_ip.c_str(), common::string_format("%d", 8008)};
			if (!_tcp_client->connect())
			{
				common::print_info("connect server failed,connecting in 10s");
				std::this_thread::sleep_for(std::chrono::seconds(10));
				continue;
			}
			else
			{
				break;
			}
		}
	}
	return this->_tcp_client;
}
void filesync::FileSync::destroy_tcp_client()
{
	common::print_debug("destroying tcp client.");
	delete _tcp_client;
	_tcp_client = NULL;
};
bool filesync::FileSync::monitor_path(PATH path)
{
	for (auto v : this->conf.monitor_paths)
	{
		if (path.string().find(v.string(), 0) == 0)
		{
			if (path.size() > v.size())
			{
				auto c = path.string().c_str()[v.size()];
				if ('\\' != c && '/' != c)
				{
					return false;
				}
			}
			return true;
		}
	}
	return false;
}
common::error filesync::FileSync::download_file(std::string server_path, std::string commit_id, std::string save_path)
{
	tcp_client *tcp_client = this->get_tcp_client();
	std::string url_path = common::string_format("/api/file?path=%s&commit_id=%s", common::url_encode(server_path.c_str()).c_str(), commit_id.c_str());
	std::unique_ptr<char[]> token{filesync::get_token()};
	common::http_client c{this->cfg.server_ip.c_str(), common::string_format("%d", this->cfg.server_port).c_str(), url_path.c_str(), token.get()};
	c.GET();
	if (c.error)
	{
		return c.error;
	}
	auto j = json::parse(c.resp_text);
	auto data = j["data"];
	if (!j["error"].is_null())
	{
		return j["error"].get<std::string>();
	}
	auto ip = data["Ip"];
	auto port = data["Port"];
	auto path = data["Path"];
	auto md5 = data["Md5"];
	size_t size = data["Size"].get<std::size_t>();

	XTCP::message msg;
	msg.msg_type = static_cast<int>(filesync::tcp::MsgType::Download_File);
	msg.addHeader({"path", path.get<std::string>()});
	msg.addHeader({TokenHeaderKey, token.get()});
	common::error err;
	XTCP::send_message(&tcp_client->xclient.session, msg, err);
	if (err)
	{
		return common::string_format("DOWNLOAD failed %s", err.message());
	}
	XTCP::message reply;
	XTCP::read_message(&tcp_client->xclient.session, reply, err);
	if (err)
	{
		return common::string_format("DOWNLOAD failed %s", err.message());
	}
	if (!reply)
	{
		return "file server have no response";
	}
	std::error_code ec;
	std::string tmp_dir = this->conf.get_tmp_dir(ec);
	if (ec)
	{
		return ec.message();
	}
	std::string tmp_path = (std::filesystem::path(tmp_dir) / trim_trailing_space(md5.get<std::string>())).string();
	std::shared_ptr<std::ofstream> os{new std::ofstream{tmp_path, std::ios_base::binary}};
	if (os->bad())
	{
		return common::string_format("failed to create a file named %s", tmp_path.c_str());
	}
	common::print_debug(common::string_format("temporary file:%s", tmp_path.c_str()));
	std::promise<common::error> dl_promise;
	size_t written{0};
	common::print_info(common::string_format("Downloading %s", save_path.c_str()));
	tcp_client->xclient.session.receive_stream(
		os, reply.body_size, [&written, &reply, &dl_promise](size_t read_size, XTCP::tcp_session *session, bool completed, common::error error, void *p)
		{
			written += read_size;
			auto percentage = (double)(written) / reply.body_size * 100;
			std::cout << common::string_format("\rreceived %d/%d bytes, %.2f%%", written, reply.body_size, percentage);
			if (completed || error)
			{
				dl_promise.set_value(error);
				std::cout << '\n';
			}
		},
		NULL);
	err = dl_promise.get_future().get();
	if (err)
	{
		return common::string_format("DOWNLOAD failed %s", err.message());
	}
	os->flush();
	os->close();
	if (!filesync::compare_md5(common::file_md5(tmp_path.c_str()).c_str(), md5.get<std::string>().c_str()))
	{
		auto err = common::string_format("Download a file with wrong MD5,deleting it...");
		common::print_info(err);
		if (!std::filesystem::remove(tmp_path))
		{
			filesync::print_info(common::string_format("WARNING!failed to delete the temp file %s", tmp_path.c_str()));
		}
		return err;
	}
	try
	{
		common::makedir(filesync::get_parent_dir(save_path.c_str()));
		common::movebycmd(tmp_path, save_path);
	}
	catch (std::filesystem::filesystem_error &e)
	{
		return common::string_format("failed to rename the file with error:%s", e.what());
	}
	this->on_file_downloaded(save_path, md5.get<std::string>());
	return NULL;
}
/*
bool filesync::FileSync::download_file(File &file)
{
	tcp_client *tcp_client = this->get_tcp_client();
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
	size_t size = data["Size"].get<std::size_t>();
	auto db_file = this->db.get_file(file.server_path.c_str());
	assert(db_file.get()->count > 0);
	bool is_downloaded = false;

	if (std::filesystem::exists(file.full_path) && compare_md5(md5.get<std::string>().c_str(), common::file_md5(file.full_path.c_str()).c_str()))
	{
		is_downloaded = true;
	}
	else
	{
		XTCP::message msg;
		msg.msg_type = static_cast<int>(filesync::tcp::MsgType::Download_File);
		msg.addHeader({"path", s_path.get<std::string>()});
		token = filesync::get_token();
		msg.addHeader({TokenHeaderKey, token});
		delete[](token);
		XTCP::send_message(&tcp_client->xclient.session, msg, NULL);
		XTCP::message reply;
		XTCP::read_message(&tcp_client->xclient.session, reply, NULL);
		if (!reply)
		{
			return false;
		}
		std::promise<bool> promise;
		std::future<bool> future = promise.get_future();
		std::error_code ec;
		std::string tmp_dir = this->conf.get_tmp_dir(ec);
		if (ec)
		{
			common::print_info(ec.message());
			return false;
		}
		std::string tmp_path = (std::filesystem::path(tmp_dir) / trim_trailing_space(md5.get<std::string>())).string();
		std::shared_ptr<std::ofstream> os{new std::ofstream{tmp_path, std::ios_base::binary}};
		if (os->bad())
		{
			filesync::print_info(common::string_format("failed to create a file named %s", tmp_path.c_str()));
			return false;
		}
		filesync::print_info(common::string_format("Downloading file %s", file.server_path.c_str()));
		common::print_debug(common::string_format("temporary file:%s", tmp_path.c_str()));
		std::promise<bool> dl_promise;
		tcp_client->xclient.session.receive_stream(
			os, reply.body_size, [&dl_promise](size_t read_size, XTCP::tcp_session *session, bool completed, const char *error, void *p) {
				if (completed)
				{
					dl_promise.set_value(true);
				}
				else if (error)
				{
					common::print_info(error);
					dl_promise.set_value(false);
				}
			},
			NULL);
		if (!dl_promise.get_future().get())
		{
			return false;
		}
		os->flush();
		os->close();
		if (!filesync::compare_md5(common::file_md5(tmp_path.c_str()).c_str(), md5.get<std::string>().c_str()))
		{
			filesync::print_info(common::string_format("Download a file with wrong MD5,deleting it...", file.server_path.c_str()));
			if (!std::filesystem::remove(tmp_path))
			{
				filesync::print_info(common::string_format("WARNING!failed to delete the temp file %s", tmp_path.c_str()));
			}
			return false;
		}
		try
		{
			std::filesystem::rename(tmp_path, file.full_path);
		}
		catch (std::filesystem::filesystem_error &e)
		{
			filesync::print_info(common::string_format("failed to rename the file with error:%s", e.what()));
		}
		is_downloaded = true;
	}
	if (is_downloaded)
	{
		filesync::print_info(common::string_format("downloaded file:%s", file.full_path.c_str()));
		this->db.update_local_md5(file.server_path.c_str(), md5.get<std::string>().c_str());
	}
	return is_downloaded;
}
*/
std::vector<filesync::File> filesync::FileSync::get_server_files(std::string path, std::string commit_id, std::string max_commit_id, bool *ok)
{
	std::vector<filesync::File> files;
	std::cout << path << std::endl;
	std::string url_path = common::string_format("/api/files?path=%s&commit_id=%s&max=%s", common::url_encode(this->relative_to_server_path(path).string().c_str()).c_str(), commit_id.c_str(), max_commit_id.c_str());

	std::unique_ptr<char[]> token{get_token()};
	common::http_client c{this->cfg.server_ip.c_str(), common::string_format("%d", this->cfg.server_port).c_str(), url_path.c_str(), token.get()};
	while (1)
	{
		c.GET();
		if (c.error)
			common::print_info(c.error.message());
		else
			break;
	}
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
		std::string file_server_path = common::string_format("%s%s%s", path.c_str(), path == "/" ? "" : "/", item["name"].get<std::string>().c_str());
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
bool filesync::FileSync::get_all_server_files(bool force_getting_all)
{
	this->need_sync_server = true;
	if (this->conf.commit_id.empty() || force_getting_all)
	{
		this->db.add_file("/", NULL, this->conf.first_commit_id.c_str());
		bool ok = false;
		auto files = this->get_server_files("/", "", this->conf.max_commit_id.c_str(), &ok);
		//auto files = this->get_server_files("/", "", "cd8b07e5-8742-4818-9331-80a098613467", &ok);
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
	std::string source_path = change["Source_Path"].get<std::string>();
	if (!this->monitor_path(path) && !this->monitor_path(source_path))
	{
		return;
	}
	std::string md5;
	if (!change["Md5"].is_null())
	{
		md5 = change["Md5"].get<std::string>();
	}
	switch (change_type)
	{
	case 1: //add
	case 6: //modified
		this->db.add_file(path.c_str(), md5.c_str(), commit_id);
		break;
	case 2: //delete
		this->db.delete_file(path.c_str());
		break;
	case 3: //move
	case 4: //rename
		this->db.move(source_path.c_str(), path.c_str(), commit_id);
		//need call add_file in case the source_path is not recorded
		this->db.add_file(path.c_str(), md5.c_str(), commit_id);
		break;
	case 5: //copy
		this->db.copy(source_path.c_str(), path.c_str(), commit_id);
		break;
	default:
		EXCEPTION("unknown change type.");
		break;
	}
	this->need_sync_server = true;
}
void filesync::FileSync::check_sync_path()
{
start:
	if (this->conf.sync_path.string().empty())
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
		this->conf.sync_path = PATH(typed, true);
		this->conf.save();
	}
	root_path = conf.sync_path;
	assert(this->monitor == NULL);
#ifdef __linux__
	this->monitor = new common::monitor::linux_monitor();
#else
	this->monitor = new common::monitor::win_monitor(this->conf.sync_path.string());
#endif
	monitor->watch(this->conf.sync_path.string());
	monitor->read_async(&filesync::FileSync::monitor_cb, this);
}
bool filesync::FileSync::get_file_changes()
{
start:
	common::print_info("checking server changes");
	std::string url_path = common::string_format("/api/commit/changes?commit_id=%s", this->conf.commit_id.c_str());
	char *token = filesync::get_token();
	common::http_client c{this->cfg.server_ip.c_str(), common::string_format("%d", this->cfg.server_port).c_str(), url_path.c_str(), std::string(token)};
	c.GET();
	delete[](token);
	if (c.error)
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
	this->_tcp_client = NULL;
	this->corret_path_sepatator(this->server_location);
}
filesync::FileSync::~FileSync()
{
	delete (this->committer);
	delete[](this->server_location);
	delete (this->monitor);
	delete this->_tcp_client;

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
std::string filesync::FileSync::get_relative_path_by_fulllpath(const char *c_path)
{
	PATH path = std::string(c_path);
	size_t len = root_path.string().size(), result_path_len = path.string().size() - len;
	if (result_path_len == 0)
	{
		return "/";
	}
	char *buf = new char[result_path_len + 1];
	if (memcmp(path.string().c_str(), root_path.string().c_str(), len) == 0)
	{
		memcpy(buf, path.string().c_str() + len, result_path_len);
		buf[result_path_len] = '\0';
		std::string path = buf;
		delete[] buf;
		return path;
	}
	else
	{
		delete[](buf);
		return std::string{};
	}
}
char *filesync::FileSync::get_full_path(const char *path)
{
	if (std::string(path) == "/")
		return common::strcpy(root_path.string().c_str());
	int size = root_path.string().size() + strlen(path);
	char *full_path = new char[size + 1];
	memcpy(full_path, root_path.string().c_str(), root_path.string().size());
	memcpy(full_path + root_path.string().size(), path, strlen(path));
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
std::filesystem::path filesync::FileSync::relative_to_server_path(std::string relative_path)
{
	return std::filesystem::path(this->server_location) / relative_path;
}
char *filesync::get_token()
{
#ifdef __linux__

#endif
	system(common::string_format("filesync-go login --insecure --token_path \"%s\"", token_file_path.c_str()).c_str());
	std::ifstream in(token_file_path);
	std::string ss{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
	std::regex regex("accesstoken: (.+)");
	std::smatch m;
	if (std::regex_search(ss, m, regex))
	{
		auto token = common::strcpy(m[1].str().c_str());
		return token;
	}
	return NULL;
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