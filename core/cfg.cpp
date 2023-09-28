#include <cfg.h>
#include <filesystem>
#include <regex>
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
}
common::error filesync::CONFIG::load()
{
	// check if datapath directory exists
	if (!std::filesystem::exists(datapath))
	{
		std::error_code ec;
		filesync::print_info(common::string_format("create directory '%s'", datapath.string().c_str()));
		if (!std::filesystem::create_directories(datapath, ec))
		{
			return common::error(ec.message());
		}
	}

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
		this->server_ip = "192.168.1.1";
		this->server_port = 2002;
		this->server_tcp_port = 2003;
	}

	save();
	return NULL;
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
	return path.string();
}

filesync::PartitionConf filesync::PartitionConf::create(bool debug_mode, std::string partition_id, bool reuse)
{
	std::string partition_folder_name = common::string_format("partition_%s%s", debug_mode ? "debug_" : "", partition_id.c_str());
	auto folder_path = std::filesystem::path(datapath).append(partition_folder_name);
	std::filesystem::create_directories(folder_path);
	PartitionConf conf;
	conf.db_path = std::filesystem::path(folder_path).append("filesync.db").u8string();
	conf.partition_cfg_path = std::filesystem::path(folder_path).append("cfg").u8string();
	// if resue is false delete the file
	if (!reuse && !std::filesystem::remove(conf.partition_cfg_path))
	{
		std::string err = common::string_format("Failed to delete file ''", conf.partition_cfg_path.c_str());
		common::print_info(err);
		EXCEPTION(err);
	}
	conf.load();
	return conf;
}
void filesync::PartitionConf::load()
{
	// read cofigurations from partition_cfg_path
	std::ifstream in(partition_cfg_path);
	if (in.is_open())
	{
		json j = json::parse(in);
		this->commit_id = j["commit_id"].get<std::string>();
		this->sync_path = PATH(j["sync_path"].get<std::string>(), true);
		auto monitor_paths = !j["monitor_paths"].is_null() ? j["monitor_paths"].get<std::string>() : "";
		std::smatch m;
		std::regex reg{"/[^:]+"};
		while (std::regex_search(monitor_paths, m, reg))
		{
			this->monitor_paths.push_back(m[0].str());
			monitor_paths = m.suffix();
		}
	}
}

void filesync::PartitionConf::save()
{
	json j;
	j["commit_id"] = this->commit_id;
	j["sync_path"] = this->sync_path.string();
	std::string monitor_paths;
	for (auto v : this->monitor_paths)
	{
		monitor_paths += v.string() + ":";
	}
	monitor_paths = monitor_paths.substr(0, monitor_paths.size() - 1);
	j["monitor_paths"] = monitor_paths;
	std::ofstream out(this->partition_cfg_path);
	if (out.bad())
	{
		filesync::EXCEPTION(common::string_format("can not open file %s", this->partition_cfg_path));
	}
	out << j;
	out.close();
}
void filesync::PartitionConf::deleteFile()
{
	if (!std::filesystem::remove(db_path))
	{
		filesync::throw_exception(common::string_format("Failed to delete the db file %s", db_path.c_str()));
	}
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