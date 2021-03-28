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

} // namespace filesync