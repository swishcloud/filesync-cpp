#include <change_committer.h>
#include <filesync.h>
#include <http.h>
filesync::ChangeCommitter::ChangeCommitter(FileSync &fs) : fs{fs}
{
}

filesync::ChangeCommitter::~ChangeCommitter()
{
}
filesync::ChangeCommitter *filesync::ChangeCommitter::add_create_directory_action(create_directory_action &action)
{
	this->create_directory_actions.push_back(std::move(action));
	return this;
}
filesync::ChangeCommitter *filesync::ChangeCommitter::add_create_file_action(create_file_action &action)
{
	this->create_file_actions.push_back(std::move(action));
	return this;
}
filesync::ChangeCommitter *filesync::ChangeCommitter::add_delete_by_path_action(delete_by_path_action &action)
{
	this->delete_by_path_actions.push_back(std::move(action));
	return this;
}
void filesync::ChangeCommitter::commit()
{
	//limite 100 actions
	const int max_action_number = 50;

	json j_directories_arr = json::array();
	json j_file_arr = json::array();
	json j_delete_arr = json::array();
	int n = 0;
	for (auto &action : this->create_directory_actions)
	{
		if (n == max_action_number)
		{
			continue;
		}
		j_directories_arr.push_back(action.to_json());
		n++;
	}
	for (auto &action : this->create_file_actions)
	{
		if (n == max_action_number)
		{
			continue;
		}
		j_file_arr.push_back(action.to_json());
		n++;
	}
	for (auto &action : this->delete_by_path_actions)
	{
		if (n == max_action_number)
		{
			continue;
		}
		j_delete_arr.push_back(action.to_json());
		n++;
	}

	http::http_client http_client;
	std::vector<http::data_block> post_data;
	post_data.push_back({"directory_actions", j_directories_arr.dump().c_str()});
	post_data.push_back({"file_actions", j_file_arr.dump().c_str()});
	post_data.push_back({"delete_by_path_actions", j_delete_arr.dump().c_str()});
	std::string resp;
	char *token = filesync::get_token();
	if (this->create_directory_actions.size() + this->create_file_actions.size() + this->delete_by_path_actions.size() > 0 && http_client.post(this->fs.cfg.server_ip.c_str(), common::string_format("%d", this->fs.cfg.server_port).c_str(), "/api/file", post_data, token, resp))
	{
		json result = json::parse(resp);
		if (!result["error"].is_null())
		{
			std::cout << "error posting files to server:" << result["error"] << std::endl;
		}
	}
	delete (token);

	this->create_directory_actions.clear();
	this->create_file_actions.clear();
	this->delete_by_path_actions.clear();
}