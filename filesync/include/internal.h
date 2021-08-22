#ifndef INTERNAL_H
#define INTERNAL_H
#include <fstream>
#include <common.h>
#include <regex>
#include <nlohmann/json.hpp>
#ifdef __linux__
#define linux_os true
#else
#define linux_os false
#endif
using namespace nlohmann;
namespace filesync
{
    struct PATH
    {
    private:
        std::string str;

    public:
        std::string string();
        size_t size();
        PATH(){};
        PATH(std::string str, bool trimEnd = false);
    };
    inline const char *DIRECTORY_MD5 = R"(00000000000000000000000000000000)";
    char *get_token();
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
} // namespace filesync
#endif