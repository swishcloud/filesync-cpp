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
    char *exec_cmd(const char *command, char **err)
    {
        char *err_str = NULL;
        const int buffer_size = 128;
        char buffer[buffer_size];
        char *result = new char{};
        // Open pipe to file
        FILE *pipe;
        filesync::print_debug(command);
#ifdef __linux__
        pipe = popen(command, "r");
#else
        pipe = _popen(command, "r");
#endif
        if (!pipe)
        {
            err_str = common::strcpy("popen failed!");
            *err = err_str;
            delete result;
            return NULL;
        }
        while (fgets(buffer, buffer_size, pipe) != NULL)
        {
            size_t newsize = strlen(result) + strlen(buffer) + 1;
            char *temp = new char[newsize];
            char *temp2 = temp + strlen(result);
            memcpy(temp, result, strlen(result));
            memcpy(temp2, buffer, strlen(buffer));
            temp[newsize - 1] = '\0';
            delete (result);
            result = temp;
        }
#ifdef __linux__
        pclose(pipe);
#else
        _pclose(pipe);
#endif
        delete (err_str);
        return result;
    } // namespace filesync
    std::string file_md5(const char *filename)
    {
        if (!std::filesystem::exists(filename))
        {
            EXCEPTION("failed to calculate md5 due to the file does not exist.");
        }
        if (std::filesystem::file_size(filename) == 0)
            return "d41d8cd98f00b204e9800998ecf8427e";
        char *err{};
        std::string cmd;
        if (linux_os)
            cmd = common::string_format("md5sum \"%s\"", filename);
        else
            cmd = common::string_format("certutil -hashfile \"%s\" MD5", filename);
        char *cmd_resut = exec_cmd(cmd.c_str(), &err);
        std::cmatch m{};
        std::regex reg;
        if (linux_os)
        {
            reg = "^([a-z\\d]{32})";
        }
        else
        {
            reg = "\\n([a-z\\d]{32})";
        };
        if (!std::regex_search(cmd_resut, m, reg))
        {
            auto e_str = common::string_format("getting uuid failed.%s", cmd_resut);
            delete[](err);
            delete[](cmd_resut);
            EXCEPTION(e_str);
        }
        std::string md5 = m[1].str();
        delete[](err);
        delete[](cmd_resut);
        return md5;
    }
    size_t file_size(std::string path)
    {
        std::ifstream fs{path, std::ios_base::binary | std::ios_base::ate};
        return fs.tellg();
    }
} // namespace filesync