#include "CLI11.hpp"

class CMDBase
{
public:
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