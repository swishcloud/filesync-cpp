#include <monitor.h>
#include <internal.h>
HANDLE h;
const int buffer_len = 1 * 1024;
FILE_NOTIFY_INFORMATION lpBuffer[buffer_len];
LPDWORD lpBytesReturned;
OVERLAPPED overlapped;

void LpoverlappedCompletionRoutine(
    DWORD dwErrorCode,
    DWORD dwNumberOfBytesTransfered,
    LPOVERLAPPED lpOverlapped)
{
    std::cout << "triggered" << std::endl;
    for (auto i = 0; i < buffer_len; i++)
    {
        auto info = lpBuffer[i];
        switch (info.Action)
        {
        case 1:
            filesync::print_debug(common::string_format("added:%s", info.FileName));
            break;
        case 2:
            filesync::print_debug(common::string_format("removed:%s", info.FileName));
            break;

        case 3:
            filesync::print_debug(common::string_format("modified:%s", info.FileName));
            break;

        case 4:
            filesync::print_debug(common::string_format("the old name:%s", info.FileName));
            break;

        case 5:
            filesync::print_debug(common::string_format("the new name:%s", info.FileName));
            break;

        default:
            i = buffer_len - 1;
            break;
        }
    }
    int a = sizeof(lpBuffer[0]);
    if (ReadDirectoryChangesW(h, lpBuffer, buffer_len, true, FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_SIZE, lpBytesReturned, &overlapped, &LpoverlappedCompletionRoutine) == 0)
    {
        filesync::throw_exception(common::GetLastErrorMsg("ReadDirectoryChangesW"));
    }
}

void filesync::monitor::watch()
{
    //get file handle
    h = CreateFileA("C:/Users/bigyasuo/Desktop/test_file_watch", FILE_LIST_DIRECTORY, FILE_SHARE_DELETE | FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, NULL);
    if (h == INVALID_HANDLE_VALUE)
    {
        filesync::throw_exception(common::GetLastErrorMsg("filesync::monitor::watch()"));
    }
    if (ReadDirectoryChangesW(h, lpBuffer, buffer_len, true,
                              FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_SIZE, lpBytesReturned, &overlapped, &LpoverlappedCompletionRoutine) == 0)
    {
        filesync::throw_exception(common::GetLastErrorMsg("ReadDirectoryChangesW"));
    }
}