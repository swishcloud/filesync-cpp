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
		std::vector<create_directory_action> create_directory_actions;
		std::vector<create_file_action> create_file_actions;
		std::vector<delete_by_path_action> delete_by_path_actions;

	public:
		ChangeCommitter(FileSync &fs);
		~ChangeCommitter();
		ChangeCommitter *add_create_directory_action(create_directory_action &action);
		ChangeCommitter *add_create_file_action(create_file_action &action);
		ChangeCommitter *add_delete_by_path_action(delete_by_path_action &action);
		bool commit();
	};

} // namespace filesync
#endif