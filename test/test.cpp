#define BOOST_TEST_MODULE filesync_test
#include <boost/test/included/unit_test.hpp>
#include "filesync.h"
const char *cmd = "build/filesync/filesync";
const char *token = "zKH5-A7xziVT5vlY_xTwny8UFu-93QpBio-CUThBK2k.25Cd3JKeH_R28O3vGdtfffa-oDJ_f_T8rpWIQ1jU1yw";
BOOST_AUTO_TEST_CASE(begin_upload_test)
{
    const char *argv[] = {cmd, "upload", "--md5", "md5strxxx", "--location", "/", "--account", "test", "--path", "/home/debian/workspace/c++/filesync-cpp/core/db_manager.cpp"};
    int result = filesync::run(sizeof(argv) / sizeof(argv[0]), argv);
    BOOST_ASSERT(result == 0);
}