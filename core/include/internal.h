#ifndef INTERNAL_H
#define INTERNAL_H
#include <fstream>
#include <common.h>
#include <regex>
#include <commotion/core/core.h>
#include <nlohmann/json.hpp>
#include "file_repository.h"
#ifdef __linux__
#define linux_os true
#else
#define linux_os false
#endif
using namespace nlohmann;
namespace filesync
{
    enum class FileStatus
    {
        None = 0X00,
        Unknown = 0X01,
        Conflict = 0X02,
        Modified = 0X4,
        Added = 0X8,
        Online = 0X10,
        Synced = 0X20,
        OutOfDate
    };
    extern std::mutex print_mtx;
    struct PATH
    {
    private:
        std::string str;

    public:
        std::string string();
        size_t size();
        PATH() {};
        PATH(std::string str, bool trimEnd = false);
    };
    inline const char *DIRECTORY_MD5 = R"(00000000000000000000000000000000)";
    std::string get_parent_dir(const char *filename);
    std::string format_path(std::string str);
    bool compare_md5(const char *a, const char *b);
    char *file_name(const char *path);
    void throw_exception(std::string err);
    void print_info(std::string str);
    void print_debug(std::string str);
    class exception : public std::exception
    {
    public:
        std::string err;
        const char *what() const noexcept override;
    };
    void EXCEPTION(std::string err);
    void PLATFORM_NOT_SUPPORTED();
    class UuidGenerator : public IUuidGenerator
    {
    public:
        std::string newUuid()
        {
            return common::uuid();
        }
    };

    typedef enum
    {
        MT_PrepareFile = 50 + 1,
        MT_RecordFile
    } MsgType;

    class SHA256Generator : public ISHA256Generator
    {
    public:
        std::string operator()(const char *const filepath) const
        {
            std::string md5 = common::file_md5(filepath);
            md5 += md5;
            return md5;
        }
    };

    struct File
    {
    private:
    public:
        FileStatus status;
        std::string full_path;
        std::string relative_path;
        std::string server_path;
        std::string id;
        std::string commit_id;
        std::string name;
        std::string md5;
        size_t size;
        bool is_directory;
    };
    class ServerFile
    {
    private:
    public:
        std::string name;
        bool is_directory;
        std::string md5;
        std::size_t size;
        std::size_t uploaded_size;
        std::string commit_id;
        std::string path;
        std::string server_file_id;
        std::string ip;
        int port;
        bool is_completed;
    };
} // namespace filesync
#endif