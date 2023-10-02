#ifndef CHANGE_COMMITTER
#define CHANGE_COMMITTER
#include <vector>
#include <db_manager.h>
#include <filesync.h>
#include "internal.h"
namespace filesync
{
	class FileSync;
	struct create_directory_action;
	struct create_file_action;
	struct delete_by_path_action;

	struct PathNode;
	struct PathNode
	{
	private:
		/* data */
		PathNode *_AddNode(std::string path);
		int size;

	public:
		PATH path;
		PathNode *root;
		std::vector<std::unique_ptr<action_base>> actions;
		std::vector<std::unique_ptr<PathNode>> children;
		PathNode(/* args */);
		~PathNode();
		PathNode *Find(PATH path);
		void AddAction(PATH path, action_base *action);
		void Print();
		int Size();
		std::vector<PathNode *> GetAllChildren();
		void Free();
		std::string Dump();
	};

	class ChangeCommitter
	{
	private:
		std::unique_ptr<PathNode> actionTreeRoot;
		FileSync &fs;

	public:
		ChangeCommitter(FileSync &fs);
		~ChangeCommitter();
		ChangeCommitter *add_action(PATH path, action_base *action);
		void clear();
		bool commit(std::string token = std::string{});
		void Dump();
	};
} // namespace filesync
#endif