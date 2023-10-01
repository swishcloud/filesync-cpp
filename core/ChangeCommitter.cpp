#include <change_committer.h>
#include <filesync.h>
#include <http.h>
filesync::ChangeCommitter::ChangeCommitter(FileSync &fs) : fs{fs}
{
	// filesync::PathNode root;
	std::unique_ptr<PathNode> root{new filesync::PathNode{}};
	actionTreeRoot.swap(root);
	actionTreeRoot->path = filesync::PATH("/");
	actionTreeRoot->root = &(*actionTreeRoot);
}

filesync::ChangeCommitter::~ChangeCommitter()
{
}
filesync::ChangeCommitter *filesync::ChangeCommitter::add_action(PATH path, action_base *action)
{
	const int MAX_ACTION_LIMIT = 50;
	// this->actions.push_back(std::unique_ptr<action_base>{action});
	this->actionTreeRoot->AddAction(path, action);
	filesync::print_info(common::string_format("pending %d/%d actions", this->actionTreeRoot->Size(), MAX_ACTION_LIMIT));
	if (this->actionTreeRoot->Size() == MAX_ACTION_LIMIT)
	{
		if (!this->commit())
		{
			throw common::exception("commiting changes failed.");
		}
	}
	return this;
}
void filesync::ChangeCommitter::clear()
{
	this->actionTreeRoot->Free();
}
bool filesync::ChangeCommitter::commit(std::string token)
{
	json j_directories_arr = json::array();
	json j_file_arr = json::array();
	json j_delete_arr = json::array();
	for (auto &node : this->actionTreeRoot->GetAllChildren())
	{
		common::print_debug(common::string_format("PATH '%s':", node->path.string().c_str()));
		for (auto &action : node->actions)
		{
			if (action->type == 1)
			{
				j_file_arr.push_back(action->to_json());
				common::print_debug("file_actions");
			}
			else if (action->type == 2)
			{
				j_directories_arr.push_back(action->to_json());
				common::print_debug("directory_actions");
			}
			else if (action->type == 3)
			{
				j_delete_arr.push_back(action->to_json());
				common::print_debug("delete_by_path_actions");
			}
			else
			{
				throw new common::exception("unrecognized action type.");
			}
		}
	}
	std::vector<http::data_block> post_data;
	post_data.push_back({"directory_actions", j_directories_arr.dump().c_str()});
	post_data.push_back({"file_actions", j_file_arr.dump().c_str()});
	post_data.push_back({"delete_by_path_actions", j_delete_arr.dump().c_str()});
	std::string resp;
	bool ok = false;
	if (this->actionTreeRoot->Size() > 0)
	{
		common::http_client c{this->fs.cfg.server_ip.c_str(), common::string_format("%d", this->fs.cfg.server_port).c_str(), "/api/file", token != std::string{} ? token : filesync::get_token(fs.account)};
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
	else
	{
		ok = true;
	}

	this->actionTreeRoot->Free();
	return ok;
}
filesync::PathNode::PathNode()
{
	this->children = std::vector<std::unique_ptr<PathNode>>{};
	this->size = 0;
}
filesync::PathNode::~PathNode()
{
	// todo
}
filesync::PathNode *filesync::PathNode::Find(PATH path)
{
	if (this->path.string() == path.string())
	{
		return this;
	}
	PathNode *n;
	for (int i = 0; i < this->children.size(); i++)
	{
		n = this->children[i]->Find(path);
		if (n != NULL)
		{
			return n;
		}
	}

	if (this->path.string() == "/")
	{
		std::regex re{".*(?=/)"};
		std::smatch m;
		std::string str = path.string();
		std::regex_search(str, m, re);
		std::string p = m.str();
		if (p == "")
		{
			p = "/";
		}
		return Find(PATH(p));
	}
	else
	{
		return NULL;
	}
}
void filesync::PathNode::Print()
{
	common::print_info(this->path.string());
	for (int i = 0; i < this->children.size(); i++)
	{
		this->children[i]->Print();
	}
}
int filesync::PathNode::Size()
{
	return this->size;
}
void filesync::PathNode::AddAction(PATH path, action_base *action)
{
	filesync::PathNode *found = Find(path);
	auto node = found->_AddNode(path.string());
	node->actions.push_back(std::unique_ptr<action_base>(action));
}
filesync::PathNode *filesync::PathNode::_AddNode(std::string path)
{
	PathNode *n = new PathNode();
	n->path = path;
	n->actions = std::vector<std::unique_ptr<action_base>>{};
	n->root = root;
	for (auto i = this->children.begin(); i != this->children.end(); i++)
	{
		auto &child = *i;
		if (child->path.string().find(path) == 0)
		{
			n->children.push_back(std::move(child));
			i = --this->children.erase(i);
		}
	}
	this->children.push_back(std::unique_ptr<PathNode>(n));
	n->root->size++;
	return n;
}
std::vector<filesync::PathNode *> filesync::PathNode::GetAllChildren()
{
	std::vector<filesync::PathNode *> vector;
	for (auto i = this->children.begin(); i != this->children.end(); i++)
	{
		auto &child = *i;
		vector.push_back(&(*child));
		auto rec_children = child->GetAllChildren();
		for (auto j = rec_children.begin(); j != rec_children.end(); j++)
		{
			vector.push_back(*j);
		}
	}
	return vector;
}
void filesync::PathNode::Free()
{
	for (auto i = this->children.begin(); i != this->children.end(); i++)
	{
		auto &child = *i;
		child->Free();
		i = --this->children.erase(i);
		root->size--;
	}
}