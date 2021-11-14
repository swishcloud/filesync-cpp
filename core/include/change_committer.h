#ifndef CHANGE_COMMITTER
#define CHANGE_COMMITTER
#include <vector>
#include <db_manager.h>
#include <filesync.h>
namespace filesync
{
	class FileSync;
	struct create_directory_action;
	struct create_file_action;
	struct delete_by_path_action;
	class ChangeCommitter
	{
	private:
		FileSync &fs;
		std::vector<std::unique_ptr<action_base>> actions;

	public:
		ChangeCommitter(FileSync &fs);
		~ChangeCommitter();
		ChangeCommitter *add_action(action_base *action);
		bool commit(std::string token = std::string{});
	};

} // namespace filesync
#endif