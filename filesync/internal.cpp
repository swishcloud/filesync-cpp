#include <internal.h>
#include <mutex>
namespace filesync
{
    std::mutex print_mtx;
    void print_debug(std::string str)
    {
        std::lock_guard<std::mutex> guard(print_mtx);
        auto level = "DEBUG";
        std::cout << common::string_format("%s>%s", level, str.c_str()) << std::endl;
    }
    void print_info(std::string str)
    {
        std::lock_guard<std::mutex> guard(print_mtx);
        auto level = "INFO";
        std::cout << common::string_format("%s>%s", level, str.c_str()) << std::endl;
    }
    const char *exception::what() const noexcept
    {
        return this->err.c_str();
    }
    void EXCEPTION(std::string err)
    {
        exception e;
        e.err = err;
        throw e;
    }
    void PLATFORM_NOT_SUPPORTED()
    {
        exception e;
        e.err = "PLATFORM_NOT_SUPPORTED";
        throw e;
    }
    PATH::PATH(std::string str, bool trimEnd)
    {
        this->str = str;
        common::formalize_path(this->str);
        auto c = this->str[this->str.size() - 1];
        if (('\\' == c || '/' == c) && str.size() > 1)
        {
            if (!trimEnd)
                throw "PATH except '/' can not end with \\ or /.";
            this->str = this->str.substr(0, str.size() - 1);
        }
        std::regex regex{"\\\\"};
        this->str = std::regex_replace(this->str, regex, "/");
    }
    std::string PATH::string()
    {
        return this->str;
    }
    size_t PATH::size()
    {
        return this->str.size();
    }
} // namespace filesync