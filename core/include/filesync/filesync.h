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
#include <filesync/change_committer.h>
#include <filesync/cfg.h>
#include <memory>
#include <commotion/client/client.h>
#include <filesync/internal.h>
#include <monitor.h>
#include "filesync/webapi.h"
#include <queue>
#include <mutex>
#include <filesync/server.h>
#include <commotion/core/logger.h>
// TODO: Reference additional headers your program requires here.
namespace filesync
{
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
		std::string sha256;
		std::string token;
		std::string account;
		filesync::PATH location;
		size_t size;
		std::string serverIP;
		int port;
	};
	int run(int argc, const char *argv[]);
	// Overload operator<<
	std::ostream &operator<<(std::ostream &os, const ServerFile &file);

} // namespace filesync
class FS_CLIENT
{
private:
	class PrepareFileHandler : public MessageHandler
	{
	private:
		FS_CLIENT &fs_client;

	public:
		PrepareFileHandler(FS_CLIENT &fs_client) : fs_client(fs_client) {}
		int Handle(CLIENT *client, CORE::pMSG msg) override;
	};
	class PrepareFileFactory : public HandlerFactory
	{
	private:
	private:
		FS_CLIENT &fs_client;

	public:
		PrepareFileFactory(FS_CLIENT &fs_client) : fs_client(fs_client) {}
		MessageHandler *CreateHandler(CORE::pMSG msg)
		{
			return new PrepareFileHandler(fs_client);
		}
	};
	::CLIENT client;
	std::condition_variable cv;
	std::string serverId;
	std::mutex download_id_mutex;

public:
	std::string downloadId;
	std::string downloadSha256;
	std::condition_variable download_id_cv;
	FS_CLIENT(const std::string &serverIP, const int &serverPort, const std::string &serverId) : client(serverIP, serverPort), serverId(serverId)
	{
		delete client.sha256Generator;
		client.sha256Generator = new filesync::SHA256Generator();
		client.emplaceHandleFactory(static_cast<::MsgType>(filesync::MT_PrepareFile), new PrepareFileFactory(*this));
	}
	~FS_CLIENT()
	{
	}
	int login()
	{
		if (!client.connect() || !client.login("xx", "xx"))
		{
			std::cout << "failed to login in" << std::endl;
			return 0;
		}
		client.heartbeat();
		return 1;
	}
	int Upload(std::shared_ptr<std::istream> fs, const filesync::ServerFile &sf, const char *md5, size_t size, std::string _token);
	int DownloadFile(std::string server_path, std::string commit_id, std::string save_path, std::string token)
	{
		client.setHeartbeatTimeout(120);
		// send a MT_PrepareFile msg to get ready for downloading file, the server will response with a MT_RecordFile msg containing a downloading id when the file is ready
		const std::string targetId = serverId;										 // 16bytes
		const int file_type = commit_id.empty() ? 0 : 1;							 // 1 for normal file, 0 for share file
		const int dataLen = 1 + 16 + 16 + 1 + server_path.size() + 1 + token.size(); // target_id(16 bytes), file_type(1 byte),  commit_id(16 bytes),server_path_len(1 byte),server_path(variable length), token_len(1 byte), token(variable length)
		char buf[dataLen];
		int index = 0;
		memcpy(buf + index, targetId.c_str(), 16);
		index += 16;
		buf[index] = file_type;
		index += 1;
		std::unique_ptr<char[]> commitIdBytes(hexToBytes(filesync::stripHyphen(commit_id).c_str())); // 16 bytes
		memcpy(buf + index, commitIdBytes.get(), 16);
		index += 16;
		buf[index] = server_path.size();
		index += 1;
		memcpy(buf + index, server_path.c_str(), server_path.size());
		index += server_path.size();
		buf[index] = token.size();
		index += 1;
		memcpy(buf + index, token.c_str(), token.size());
		if (!client.sendMessage(static_cast<::MsgType>(filesync::MT_PrepareFile), buf, dataLen))
		{
			common::print_info("faild to send msg");
			return 0;
		}
		std::unique_lock<std::mutex> dlk(download_id_mutex);
		if (!download_id_cv.wait_for(dlk, std::chrono::seconds(30), [this]()
									 { return !downloadId.empty(); }))
		{
			common::print_info("failed to receive download id from server");
			return 0;
		}
		bool download_ok = false;
		std::mutex m;
		std::condition_variable cv;
		bool downloadEnded = false;
		client._file_downloaded_cb = [this, &cv, &download_ok, &downloadEnded](const char *filepath, bool success)
		{
			download_ok = success;
			downloadEnded = true;
			cv.notify_one();
		};
		client.requestFile(serverId.c_str(), downloadId.c_str(), save_path.c_str(), downloadSha256.c_str());
		std::unique_lock<std::mutex> downloadM(m);
		cv.wait(downloadM, [&downloadEnded]()
				{ return downloadEnded; });
		return download_ok;
	};
};
inline int FS_CLIENT::PrepareFileHandler::Handle(CLIENT *client, CORE::pMSG msg)
{
	// parse a MT_PrepareFile msg which contains targetId, downloading id and sha256
	char targetId[16];
	char downloadId[16];
	char sha256[32];
	int index = 0;
	memcpy(targetId, msg->data + index, 16);
	index += 16;
	memcpy(downloadId, msg->data + index, 16);
	index += 16;
	memcpy(sha256, msg->data + index, 32);
	std::unique_ptr<char[]> downloadIdHex(bytesTohex(downloadId, 16));
	const std::string downloadIdStr = std::string(downloadIdHex.get(), 32);
	common::print_debug(common::string_format("Received downloadId: %s", downloadIdStr.c_str()));
	fs_client.downloadId = downloadIdStr;
	fs_client.downloadSha256 = std::string(sha256, 32);
	fs_client.download_id_cv.notify_one();
	return 1;
}
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
	ILogger *logger;

public:
	static void monitor_cb(common::monitor::change *c, void *obj);
	common::monitor::MONITOR *monitor;
	std::string account;
	filesync::PartitionConf conf;
	filesync::db_manager *db;
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
inline int FS_CLIENT::Upload(std::shared_ptr<std::istream> fs, const filesync::ServerFile &sf, const char *md5, size_t size, std::string _token)
{
	if (sf.server_file_id.size() != 36 || sf.path.size() != 36 || strlen(md5) != 32)
	{
		std::cout << "invalid parameters" << std::endl;
		return 0;
	}

	const std::string targetId = serverId;																	  // 16bytes
	const std::unique_ptr<char[]> serverFileId(hexToBytes(filesync::stripHyphen(sf.server_file_id).c_str())); // 16bytes
	const std::unique_ptr<char[]> filepath(hexToBytes(filesync::stripHyphen(sf.path).c_str()));				  // 16bytes
	const std::string sha256 = std::string(md5) + md5;														  // 32bytes
	const std::string token = _token;																		  // variable length

	char sha256Bytes[32];
	std::string tmp(sha256);
	for (int i = 31; i >= 0; i--)
	{
		tmp.data()[(i + 1) * 2] = 0;
		sha256Bytes[i] = std::stoi(tmp.c_str() + i * 2, NULL, 16);
	}

	client.getUploadStreamCB = [&](const std::string &path, std::istream **is, std::string &_sha256,
								   size_t &_size)
	{
		*is = fs.get();
		_sha256 = sha256;
		_size = size;
	};
	int res = client.sendFile(targetId.c_str(), "-", sha256.c_str());
	std::cout << "uploading file " << (res ? "success" : "failed") << std::endl;
	if (!res)
	{
		return 0;
	}

	char buf[16 + 16 + 16 + 32 + token.size()];
	int index = 0;
	memcpy(buf + index, targetId.c_str(), 16);
	index += 16;
	memcpy(buf + index, serverFileId.get(), 16);
	index += 16;
	memcpy(buf + index, filepath.get(), 16);
	index += 16;
	memcpy(buf + index, sha256Bytes, 32);
	index += 32;
	memcpy(buf + index, token.c_str(), token.size());
	// send buf to server
	if (!client.sendMessage(static_cast<::MsgType>(filesync::MT_RecordFile), buf, sizeof(buf)))
	{
		return 0;
	}
	return 1;
}
// Overload operator<<
inline std::ostream &filesync::operator<<(std::ostream &os, const filesync::ServerFile &file)
{
	os << "ServerFile {\n"
	   << "  name: " << file.name << "\n"
	   << "  is_directory: " << std::boolalpha << file.is_directory << "\n"
	   << "  md5: " << file.md5 << "\n"
	   << "  size: " << file.size << "\n"
	   << "  uploaded_size: " << file.uploaded_size << "\n"
	   << "  commit_id: " << file.commit_id << "\n"
	   << "  path: " << file.path << "\n"
	   << "  server_file_id: " << file.server_file_id << "\n"
	   << "  is_completed: " << std::boolalpha << file.is_completed << "\n"
	   << "}";
	return os;
}
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
class FileUploader2 : public IFileUploader
{
private:
	std::shared_ptr<FS_CLIENT> client;

public:
	FileUploader2(std::shared_ptr<FS_CLIENT> &client) : client(client) {}

	bool upload_file(const filesync::ServerFile &sf, std::shared_ptr<std::istream> fs, const char *const md5, const size_t &size, const std::string &_token)
	{
		client->Upload(fs, sf, md5, size, _token);
		return true;
	}
};
class IFileDownloader
{
public:
	virtual ~IFileDownloader() = default;
	virtual int download_file(const std::string &server_path, const std::string &commit_id, const std::string &save_path, const std::string &token) = 0;
};
class FileDownloader2 : public IFileDownloader
{
private:
	std::shared_ptr<FS_CLIENT> client;

public:
	FileDownloader2(std::shared_ptr<FS_CLIENT> &client) : client(client)
	{
	}
	int download_file(const std::string &server_path, const std::string &commit_id, const std::string &save_path, const std::string &token)
	{
		return client->DownloadFile(server_path, commit_id, save_path, token);
	}
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
	int download_file(const std::string &server_path, const std::string &commit_id, const std::string &save_path, const std::string &token)
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
		return 1;
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
inline std::string getServerId()
{
	std::string targetId = "4669345BC5A04D88B700FC8B252368EB";
	char id[16];
	for (int i = targetId.size() - 2; i >= 0; i -= 2)
	{
		id[i / 2] = std::stoi(targetId.c_str() + i, NULL, 16);
		targetId.data()[i] = 0;
	}
	return std::string(id, 16);
}
inline std::string getToken(const std::string account, const bool debugMode)
{
#ifndef __APPLE__
	const std::string token_file_path = (std::filesystem::path(filesync::datapath) / common::string_format("token-%s", account.c_str())).string();
	auto cmd = common::string_format("filesync-go login --insecure --token_path \"%s\"", token_file_path.c_str());
	if (debugMode)
	{
		cmd = "development=true " + cmd;
	}
	system(cmd.c_str());
	std::ifstream in(token_file_path);
	std::string ss{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
	std::regex regex("accesstoken: (.+)");
	std::smatch m;
	if (std::regex_search(ss, m, regex))
	{
		return m[1].str();
	}
	return std::string();
#else
	return std::string();
#endif
}
#endif