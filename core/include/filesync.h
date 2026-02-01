// filesync.h : Include file for standard system include files,
// or project specific include files.
#ifndef FILESYNC
#define FILESYNC
// #pragma once

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
#include <server.h>
// TODO: Reference additional headers your program requires here.
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
	class ServerFile;
	struct FSConnectResult;
	// CMD STRUCTURE
	struct CMD_EXPORT_OPTION
	{
		std::string account;
		filesync::PATH path;
		std::string commit_id;
		std::string max_commit_id;
		filesync::PATH destination_folder;
	};
	struct CMD_SERVER_CLEAN_OPTION
	{
		std::string server_id;
		filesync::PATH files_path;
		filesync::PATH trash_dir;
	};
	struct CMD_UPLOAD_OPTION
	{
		std::string filename;
		filesync::PATH path;
		std::string md5;
		std::string token;
		std::string account;
		filesync::PATH location;
		size_t size;
	};
	int run(int argc, const char *argv[]);
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
	std::string get_relative_path_by_fulllpath(const char *path);
	std::filesystem::path relative_to_server_path(std::string relative_path);
	std::mutex _local_file_changes_mutex;
	std::queue<common::monitor::change *> _local_file_changes;
	void on_file_downloaded(PATH full_path, std::string md5);
	void on_file_uploaded(PATH full_path, std::string md5);

public:
	static void monitor_cb(common::monitor::change *c, void *obj);
	common::monitor::MONITOR *monitor;
	std::string account;
	filesync::PartitionConf conf;
	filesync::db_manager db;
	filesync::tcp_client *_tcp_client;
	CONFIG cfg;
	ChangeCommitter *committer;
	FileSync(char *server_location, CONFIG cfg);
	~FileSync();
	FSConnectResult connect(std::string serverip, std::string port);
	bool upload_file(std::shared_ptr<std::istream> fs, const char *md5, long size, std::string token);
	common::error download_file(std::string server_path, std::string commit_id, std::string save_path);
	std::vector<File> get_server_files(std::string path, std::string commit_id, std::string max_commit_id, bool *ok);
	std::string get_server_files(std::string path, std::string commit_id, std::string max_commit_id, std::function<void(ServerFile &file)> callback);
	bool get_all_server_files(int times);
	std::vector<filesync::File> files;
	std::unordered_map<const char *, int, hasher, keyeq> files_map;
	common::error check_sync_path();
	bool get_file_changes();
	void save_change(json change, const char *commit_id);
	void print();
	bool sync_server();
	bool sync_local_added_or_modified(const char *path);
	bool sync_local_deleted(const char *path);
	bool clear_synced_files(const char *path);
	File local_file(std::string full_path, bool is_directory);
	File server_file(std::string server_path, std::string commit_id, bool is_directory);
	void add_local_file_change(common::monitor::change *change);
	common::monitor::change *get_local_file_change();
	filesync::tcp_client *get_tcp_client();
	void destroy_tcp_client();
	bool monitor_path(PATH path);
	std::string get_token(std::string account);
};
struct filesync::File
{
private:
public:
	FileStatus status;
	std::string full_path;
	std::string relative_path;
	std::string server_path;
	std::string id;
	std::string commit_id;
	std::string name;
	std::string md5;
	size_t size;
	bool is_directory;
};
class filesync::ServerFile
{
private:
public:
	std::string name;
	bool is_directory;
	std::string md5;
	std::size_t size;
	std::size_t uploaded_size;
	std::string commit_id;
	std::string path;
	std::string server_file_id;
	bool is_completed;
};
class filesync::FSConnectResult
{
public:
	std::string max_commit_id;
	std::string first_commit_id;
	std::string partition_id;
};
class IConnectServer
{
public:
	virtual ~IConnectServer() = default;
	virtual filesync::FSConnectResult connect(const std::string &server_ip, const int &server_port, const std::string &token) = 0;
};
class ConnectServer : public IConnectServer
{
	filesync::FSConnectResult connect(const std::string &server_ip, const int &server_port, const std::string &token)
	{
		filesync::tcp_client tcp_client{server_ip, std::to_string(server_port)};
		if (!tcp_client.connect())
			throw common::exception("connect Web TCP Server failed");

		// connect web server
		XTCP::message msg;
		msg.msg_type = static_cast<int>(filesync::tcp::MsgType::Download_File);
		msg.addHeader({"token", token});
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
		filesync::FSConnectResult r;
		r.max_commit_id = reply.getHeaderValue<std::string>("max_commit_id");
		r.first_commit_id = reply.getHeaderValue<std::string>("first_commit_id");
		r.partition_id = reply.getHeaderValue<std::string>("partition_id");
		return r;
	}
};
class IFileUploader
{
public:
	virtual ~IFileUploader() = default;
	virtual bool upload_file(const filesync::ServerFile &sf, std::shared_ptr<std::istream> fs, const char *const md5, const size_t &size, const std::string &token) = 0;
};
class FileUploader : public IFileUploader
{
public:
	const std::string server_ip;
	const int server_port;
	FileUploader(const std::string &server_ip, const int &server_port) : server_ip(server_ip), server_port(server_port)
	{
	}
	bool upload_file(const filesync::ServerFile &sf, std::shared_ptr<std::istream> fs, const char *const md5, const size_t &size, const std::string &token)
	{
		XTCP::message reply;
		std::promise<common::error> promise;
		common::error err;
		XTCP::message msg;
		filesync::tcp_client tcp_client{server_ip, std::to_string(server_port)};
		if (!tcp_client.connect())
		{
			throw common::exception("connect server failed");
		}
		msg.msg_type = static_cast<int>(filesync::tcp::MsgType::UploadFile);
		msg.addHeader({"path", sf.path});
		msg.addHeader({"md5", md5});
		msg.addHeader({"file_size", size});
		msg.addHeader({"uploaded_size", sf.uploaded_size});
		msg.addHeader({"server_file_id", sf.server_file_id});
		msg.addHeader({TokenHeaderKey, token});
		msg.body_size = size - sf.uploaded_size;
		XTCP::send_message(&tcp_client.xclient.session, msg, err);
		if (err)
		{
			throw common::exception(common::string_format("UPLOAD failed %s", err.message()));
		}
		if (sf.uploaded_size > 0)
			fs->seekg(sf.uploaded_size, std::ios_base::beg);
		if (!fs)
		{
			throw common::exception(common::string_format("abnormal file stream"));
		}
		size_t written = 0;
		tcp_client.xclient.session.send_stream(
			fs, [&promise, &written, &msg](size_t written_size, XTCP::tcp_session *session, bool completed, common::error error, void *p)
			{
			written += written_size;
			auto percentage = (double)(written) / msg.body_size * 100;
			std::cout << common::string_format("\ruploading %d/%d bytes, %.2f%%", written, msg.body_size, percentage);
			if (error || completed)
			{
				promise.set_value(error);
			} },
			NULL);
		err = promise.get_future().get();
		if (err)
		{
			throw common::exception(err.message());
		}
		std::cout << std::endl;
		common::print_info(common::string_format("waiting for server replying..."));
		XTCP::read_message(&tcp_client.xclient.session, reply, err);
		if (err)
		{
			throw common::exception(common::string_format("UPLOAD failed %s", err.message()));
		}
		if (reply.msg_type == static_cast<int>(filesync::tcp::MsgType::Reply))
		{
			// this->on_file_uploaded(full_path, md5);
			return true;
		}
		else
		{
			throw common::exception(common::string_format("invalid msg type."));
		}
	}
};
class IFileDownloader
{
public:
	virtual ~IFileDownloader() = default;
	virtual void download_file(const std::string &server_path, const std::string &commit_id, const std::string &save_path, const std::string &token) = 0;
};
class FileDownloader : public IFileDownloader
{
public:
	const std::string server_ip;
	const std::string port;
	const int server_port;
	FileDownloader(const std::string &server_ip, const std::string &port, const int &server_port) : server_ip(server_ip), port(port), server_port(server_port)
	{
	}
	void download_file(const std::string &server_path, const std::string &commit_id, const std::string &save_path, const std::string &token)
	{
		filesync::tcp_client tcp_client{server_ip, std::to_string(server_port)};
		if (!tcp_client.connect())
		{
			throw common::exception("connect server failed");
		}
		std::string url_path = common::string_format("/api/file?path=%s&commit_id=%s", common::url_encode(server_path.c_str()).c_str(), commit_id.c_str());
		common::http_client c{server_ip, port, url_path.c_str(), token.c_str()};
		c.GET();
		if (c.error)
		{
			throw common::exception(c.error.message());
		}
		auto j = json::parse(c.resp_text);
		auto data = j["data"];
		if (!j["error"].is_null())
		{
			throw common::exception(j["error"].get<std::string>());
		}
		auto ip = data["Ip"];
		auto port = data["Port"];
		auto path = data["Path"];
		auto md5 = data["Md5"];
		size_t size = data["Size"].get<std::size_t>();

		XTCP::message msg;
		msg.msg_type = static_cast<int>(filesync::tcp::MsgType::Download_File);
		msg.addHeader({"path", path.get<std::string>()});
		msg.addHeader({TokenHeaderKey, token});
		common::error err;
		XTCP::send_message(&tcp_client.xclient.session, msg, err);
		if (err)
		{
			throw common::exception(common::string_format("DOWNLOAD failed %s", err.message()));
		}
		XTCP::message reply;
		XTCP::read_message(&tcp_client.xclient.session, reply, err);
		if (err)
		{
			throw common::exception(common::string_format("DOWNLOAD failed %s", err.message()));
		}
		if (!reply)
		{
			throw common::exception("file server have no response");
		}
		std::shared_ptr<std::ofstream> os{new std::ofstream{save_path, std::ios_base::binary}};
		if (os->bad())
		{
			throw common::exception(common::string_format("failed to create a file named %s", save_path.c_str()));
		}
		std::promise<common::error> dl_promise;
		size_t written{0};
		common::print_info(common::string_format("Downloading %s", server_path.c_str()));
		tcp_client.xclient.session.receive_stream(
			os, reply.body_size, [&written, &reply, &dl_promise](size_t read_size, XTCP::tcp_session *session, bool completed, common::error error, void *p)
			{
			written += read_size;
			auto percentage = (double)(written) / reply.body_size * 100;
			std::cout << common::string_format("\rreceived %d/%d bytes, %.2f%%", written, reply.body_size, percentage);
			if (completed || error)
			{
				dl_promise.set_value(error);
				std::cout << '\n';
			} },
			NULL);
		err = dl_promise.get_future().get();
		if (err)
		{
			throw common::exception(common::string_format("DOWNLOAD failed %s", err.message()));
		}
		os->flush();
		os->close();
		if (!filesync::compare_md5(common::file_md5(save_path.c_str()).c_str(), md5.get<std::string>().c_str()))
		{
			auto err = common::string_format("Downloaded a file but the MD5 value is wrong");
			throw common::exception(err);
		}
	}
};
class IWebAPI
{
public:
	virtual ~IWebAPI() = default;
	virtual std::vector<filesync::File> get_file_list(const std::string &path, const std::string &revision) = 0;
	virtual filesync::ServerFile get_file_info(const std::string &md5, const size_t &size) = 0;
};
class WebAPI : public IWebAPI
{
private:
	const std::string serverIP;
	const std::string port;
	const std::string token;
	const std::string maxid;

public:
	WebAPI(const std::string &serverIP, const std::string &port, const std::string &token, const std::string &maxid) : serverIP(serverIP), port(port), token(token), maxid(maxid)
	{
	}
	std::vector<filesync::File> get_file_list(const std::string &path, const std::string &revision)
	{

		std::vector<filesync::File> files;
		std::cout << "get_file_list:" << path << std::endl;
		std::string url_path = common::string_format("/api/files?path=%s&commit_id=%s&max=%s", common::url_encode(path.c_str()).c_str(), revision.c_str(), maxid.c_str());

		std::cout << "construct http_client" << std::endl;
		common::http_client c{serverIP, port, url_path, token};
		std::cout << "Send get request" << std::endl;
		c.GET();
		if (c.error)
		{
			throw std::runtime_error(common::string_format("api request error:", c.error.message()));
		}
		// std::cout << c.resp_text << std::endl;
		auto j = json::parse(c.resp_text);
		auto err = j["error"];
		if (!err.is_null())
		{
			throw std::runtime_error("server error:" + err.get<std::string>());
		}
		auto data = j["data"];
		for (auto item : data)
		{
			std::string file_server_path = common::string_format("%s%s%s", path.c_str(), path == "/" ? "" : "/", item["name"].get<std::string>().c_str());
			filesync::File f;
			f.is_directory = strcmp(item["type"].get<std::string>().c_str(), "2") == 0;
			f.commit_id = item["commit_id"];
			f.name = item["name"];
			f.id = item["id"];
			f.server_path = file_server_path;
			if (!f.is_directory)
			{
				f.size = std::stoull(item["size"].get<std::string>().c_str());
				f.md5 = item["md5"].get<std::string>().substr(0, 32);
			}
			files.push_back(f);
		}
		return files;
	}
	filesync::ServerFile get_file_info(const std::string &md5, const size_t &size)
	{
		std::string url_path = common::string_format("/api/file-info?md5=%s&size=%zu", md5.c_str(), size);
		common::http_client c{serverIP, port, url_path.c_str(), token};
		c.GET();
		if (c.error)
		{
			throw common::exception(c.error.message());
		}
		auto j = json::parse(c.resp_text);
		auto data = j["data"];
		if (!j["error"].is_null())
		{
			throw common::exception(common::string_format("Failed to get file info from web server:%s", j["error"].get<std::string>().c_str()));
		}
		filesync::ServerFile sf;
		sf.is_completed = data["is_completed"].get<std::string>() == "true";
		sf.path = data["path"].get<std::string>();
		sf.size = std::stoul(data["size"].get<std::string>());
		sf.uploaded_size = std::stoul(data["uploaded_size"].get<std::string>());
		sf.server_file_id = data["server_file_id"].get<std::string>();
		return sf;
	}
};

class ITokenStore
{
public:
	virtual ~ITokenStore() = default;
	virtual bool save(const nlohmann::json &tokenJson) = 0;
	virtual nlohmann::json load() = 0; // empty json {} if none / parse fail
	virtual void clear() = 0;
};

#endif