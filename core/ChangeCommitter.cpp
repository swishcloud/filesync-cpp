#include <change_committer.h>
#include <filesync.h>
#include <http.h>
filesync::ChangeCommitter::ChangeCommitter(FileSync &fs) : fs{fs}
{
}

filesync::ChangeCommitter::~ChangeCommitter()
{
}
filesync::ChangeCommitter *filesync::ChangeCommitter::add_action(action_base *action)
{
	const int MAX_ACTION_LIMIT = 50;
	this->actions.push_back(std::unique_ptr<action_base>{action});
	filesync::print_info(common::string_format("pending %d/%d actions", actions.size(), MAX_ACTION_LIMIT));
	if (this->actions.size() == MAX_ACTION_LIMIT)
	{
		if (!this->commit())
		{
			throw common::exception("commiting changes failed.");
		}
	}
	return this;
}
bool filesync::ChangeCommitter::commit(std::string token)
{
	json j_directories_arr = json::array();
	json j_file_arr = json::array();
	json j_delete_arr = json::array();
	for (auto &action : this->actions)
	{
		if (action->type == 1)
		{
			j_file_arr.push_back(action->to_json());
		}
		else if (action->type == 2)
		{
			j_directories_arr.push_back(action->to_json());
		}
		else if (action->type == 3)
		{
			j_delete_arr.push_back(action->to_json());
		}
		else
		{
			throw new common::exception("unrecognized action type.");
		}
	}
	std::vector<http::data_block> post_data;
	post_data.push_back({"directory_actions", j_directories_arr.dump().c_str()});
	post_data.push_back({"file_actions", j_file_arr.dump().c_str()});
	post_data.push_back({"delete_by_path_actions", j_delete_arr.dump().c_str()});
	std::string resp;
	bool ok = false;
	if (this->actions.size() > 0)
	{
		common::http_client c{this->fs.cfg.server_ip.c_str(), common::string_format("%d", this->fs.cfg.server_port).c_str(), "/api/file", token != std::string{} ? token : fs.get_token()};
		c.POST(post_data);
		if (c.error)
		{
			common::print_info(c.error.message());
			return false;
		}
		auto j = json::parse(c.resp_text);
		if (!j["error"].is_null())
		{
			common::print_info(common::string_format("error posting files to server:%s", j["error"].get<std::string>().c_str()));
		}
		else
		{
			ok = true;
		}
	}

	this->actions.clear();
	return ok;
}