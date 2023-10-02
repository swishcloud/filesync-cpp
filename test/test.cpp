#define BOOST_TEST_MODULE filesync_test
#include <boost/test/included/unit_test.hpp>
#include "filesync.h"
#include "change_committer.h"
const char *cmd = "build/filesync/filesync";
const char *token = "zKH5-A7xziVT5vlY_xTwny8UFu-93QpBio-CUThBK2k.25Cd3JKeH_R28O3vGdtfffa-oDJ_f_T8rpWIQ1jU1yw";
BOOST_AUTO_TEST_CASE(begin_upload_test)
{
    const char *argv[] = {cmd, "upload", "--md5", "md5strxxx", "--location", "/", "--account", "test", "--path", "/home/debian/workspace/c++/filesync-cpp/core/db_manager.cpp"};
    int result = filesync::run(sizeof(argv) / sizeof(argv[0]), argv);
    BOOST_ASSERT(result == 0);
}
BOOST_AUTO_TEST_CASE(PathNode_Test)
{
    filesync::PathNode root;
    root.root = &root;
    root.path = filesync::PATH("/");
    root.AddAction(filesync::PATH("/1"), new filesync::create_file_action{});
    root.AddAction(filesync::PATH("/2"), new filesync::create_file_action{});
    root.AddAction(filesync::PATH("/3"), new filesync::create_file_action{});
    root.AddAction(filesync::PATH("/1/2/4"), new filesync::create_file_action{});
    root.AddAction(filesync::PATH("/4"), new filesync::create_file_action{});
    root.Print();
    root.AddAction(filesync::PATH("/1/2"), new filesync::create_file_action{});
    common::print_debug("inserted /1/2");
    root.Print();
    BOOST_ASSERT(root.Size() > 0);
    std::string dump = root.Dump();
    common::print_info(dump);
    root.Free();
    BOOST_ASSERT(root.Size() == 0);
    // common::print_debug(common::string_format("found path:%s", found->path.string().c_str()));
}