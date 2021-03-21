#include <cfg.h>
#include <filesystem>
filesync::CONFIG::~CONFIG()
{
}
filesync::CONFIG::CONFIG()
{
	char *tmp = getenv("development");
	debug_mode = tmp && strcmp(tmp, "true") == 0;
	if (debug_mode)
	{
		filesync::print_info("debug mode on");
	}
	load();
}
void filesync::CONFIG::load()
{
	std::ifstream in(path());
	if (in.is_open())
	{
		std::string json_str;
		in >> json_str;
		in.close();
		json j = json::parse(json_str);
		this->server_ip = j["server_ip"].is_null() ? std::string{} : j["server_ip"].get<std::string>();
		this->server_port = j["server_port"].is_null() ? 0 : j["server_port"].get<int>();
		this->server_tcp_port = j["server_tcp_port"].is_null() ? 0 : j["server_tcp_port"].get<int>();
	}
	else
	{
		this->server_ip = "192.168.29.4";
		this->server_port = 2002;
		this->server_tcp_port = 8081;
	}

	save();
}
void filesync::CONFIG::save()
{
	json j;
	j["server_ip"] = this->server_ip;
	j["server_port"] = this->server_port;
	j["server_tcp_port"] = this->server_tcp_port;

	auto json_str = j.dump();
	std::ofstream out(this->path());
	if (!out.is_open())
	{
		filesync::EXCEPTION(common::string_format("can not open file %s", this->path().c_str()));
	}
	if (out.bad())
	{
		filesync::EXCEPTION(common::string_format("can not open file %s", this->path().c_str()));
	}
	out << json_str;
	if (out.bad())
	{
		filesync::EXCEPTION(common::string_format("failed to write file %s", this->path()));
	}
	out.flush();
	out.close();
	filesync::print_debug(common::string_format("saved config:%s", this->path().c_str()));
}
std::string filesync::CONFIG::path()
{
	auto path = std::filesystem::path(datapath).append(this->debug_mode ? "cfg_debug" : "cfg");
	auto parent = filesync::get_parent_dir(path.string().c_str());
	if (!std::filesystem::exists(parent))
	{
		assert(std::filesystem::create_directories(parent));
	}
	return path.string();
}

void filesync::PartitionConf::init(bool debug_mode)
{
	std::string partition_folder_name = common::string_format("partition_%s%s", debug_mode ? "debug_" : "", partition_id.c_str());
	auto folder_path = std::filesystem::path(datapath).append(partition_folder_name);
	std::filesystem::create_directories(folder_path);

	this->db_path = std::filesystem::path(folder_path).append("filesync.db").u8string();
	this->partition_cfg_path = std::filesystem::path(folder_path).append("cfg").u8string();

	//read cofigurations from partition_cfg_path
	std::ifstream in(partition_cfg_path);
	if (in.is_open())
	{
		std::string json_str;
		in >> json_str;
		in.close();
		json j = json::parse(json_str);
		this->commit_id = j["commit_id"].get<std::string>();
		this->sync_path = j["sync_path"].get<std::string>();
	}
}

void filesync::PartitionConf::save()
{
	json j;
	j["commit_id"] = this->commit_id;
	j["sync_path"] = this->sync_path;
	std::ofstream out(this->partition_cfg_path);
	if (out.bad())
	{
		filesync::EXCEPTION(common::string_format("can not open file %s", this->partition_cfg_path));
	}
	out << j;
	out.close();
}
std::string filesync::PartitionConf::get_tmp_dir(std::error_code &ec)
{
	std::filesystem::path tmp_dir = std::filesystem::temp_directory_path() / "filesync";
	if (!std::filesystem::exists(tmp_dir))
	{
		std::filesystem::create_directory(tmp_dir, ec);
	}
	return tmp_dir.string();
}