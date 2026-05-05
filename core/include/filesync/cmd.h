#include "filesync/CLI11.hpp"

class CMDBase
{
public:
    virtual ~CMDBase() = default;
    virtual void reg(CLI::App *parent) = 0;

private:
    virtual void callback() = 0;
};
class LoginCMD : public CMDBase
{
public:
    void reg(CLI::App *parent);

private:
    std::string account;
    void callback();
};

class SyncCMD : public CMDBase
{
public:
    void reg(CLI::App *parent);

private:
    std::string account;
    std::string syncPath;
    void callback();
    void addRunCmd(CLI::App *parent);
    void addConfigureCmd(CLI::App *parent);
};

class DownloadCMD : public CMDBase
{
public:
    void reg(CLI::App *parent);

private:
    std::string account;
    std::string path;
    std::string commit_id;
    std::string save_path;
    std::string token;
    void callback();
    void shared_callback();
};