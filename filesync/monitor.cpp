#include <monitor.h>
#include <internal.h>
#include <assert.h>
HANDLE h;
const int buffer_len = 64 * 1024;
//FILE_NOTIFY_INFORMATION lpBuffer[buffer_len];
char lpBuffer[buffer_len];
LPDWORD lpBytesReturned;
OVERLAPPED overlapped;

void LpoverlappedCompletionRoutine(
    DWORD dwErrorCode,
    DWORD dwNumberOfBytesTransfered,
    LPOVERLAPPED lpOverlapped)
{

    auto monitor = (filesync::monitor::MONITOR *)overlapped.hEvent;
    std::cout << "triggered" << std::endl;
    auto info = (FILE_NOTIFY_INFORMATION *)lpBuffer;
    while (1)
    {
        auto filename = common::to_cstr(info->FileName, info->FileNameLength);
        auto c = new filesync::monitor::change();
        c->path = std::filesystem::path(monitor->path_to_watch).append(filename).string();
        delete (filename);
        monitor->onchange_cb(c, monitor->obj);
        switch (info->Action)
        {
        case 1:
            filesync::print_debug(common::string_format("added:%s", c->path.c_str()));
            break;
        case 2:
            filesync::print_debug(common::string_format("removed:%s", c->path.c_str()));
            break;

        case 3:
            filesync::print_debug(common::string_format("modified:%s", c->path.c_str()));
            break;
        case 4:
            filesync::print_debug(common::string_format("the old name:%s", c->path.c_str()));
            break;

        case 5:
            filesync::print_debug(common::string_format("the new name:%s", c->path.c_str()));
            break;

        default:
            filesync::print_debug(common::string_format("unrecognized change type:%d filename:%s", info->Action, c->path.c_str()));
            break;
        }
        if (info->NextEntryOffset != 0)
        {
            info = (FILE_NOTIFY_INFORMATION *)((char *)info + info->NextEntryOffset);
        }
        else
        {
            break;
        }
    }
    monitor->ReadDirectoryChangesW();
}

void filesync::monitor::MONITOR::ReadDirectoryChangesW()
{
    ZeroMemory(lpBuffer, buffer_len);
    if (::ReadDirectoryChangesW(h, lpBuffer, buffer_len, true,
                                FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_SIZE, lpBytesReturned, &overlapped, &LpoverlappedCompletionRoutine) == 0)
    {
        filesync::throw_exception(common::GetLastErrorMsg("ReadDirectoryChangesW"));
    }
}
filesync::monitor ::MONITOR::MONITOR(onchange onchange, void *obj, std::string path_to_watch)
{
    this->onchange_cb = onchange;
    this->obj = obj;
    this->path_to_watch = path_to_watch;
    overlapped.hEvent = this;
}
void filesync::monitor::MONITOR::watch()
{
    //get file handle
    h = CreateFileA(path_to_watch.c_str(), FILE_LIST_DIRECTORY, FILE_SHARE_DELETE | FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED | FILE_FLAG_NO_BUFFERING, NULL);
    if (h == INVALID_HANDLE_VALUE)
    {
        filesync::throw_exception(common::GetLastErrorMsg("filesync::monitor::watch()"));
    }
    ReadDirectoryChangesW();
}