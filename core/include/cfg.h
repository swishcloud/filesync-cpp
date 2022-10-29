#ifndef CFG_H
#define CFG_H
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <common.h>
#include <internal.h>
using json = nlohmann::json;
namespace filesync
{
#ifdef __linux__
	const std::filesystem::path datapath = "/var/FILESYNC";
#else
	const std::filesystem::path datapath = std::string(getenv("APPDATA")) + "/FILESYNC";
#endif
	class CONFIG
	{
	private:
	public:
		std::string server_ip;
		int server_port;
		int server_tcp_port;
		bool debug_mode;
		CONFIG();
		// CONFIG(const CONFIG &) = delete;
		~CONFIG();
		void save();
		common::error load();
		std::string path();
	};
	class PartitionConf
	{
	public:
		std::string max_commit_id;
		std::string first_commit_id;
		std::string partition_id;
		std::string commit_id;
		std::string db_path;
		std::string partition_cfg_path;
		PATH sync_path;
		std::vector<PATH> monitor_paths;
		void init(bool debug_mode);
		void save();
		std::string get_tmp_dir(std::error_code &ec);
	};
} // namespace filesync
#endif