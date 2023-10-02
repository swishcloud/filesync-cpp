#include "cmd.h"
#include "common.h"
#include "filesync.h"
void LoginCMD::reg(CLI::App *parent)
{
    auto cmd = parent->add_subcommand("login", "login to server");
    cmd->add_option("--account", account, "your account name")->required();
    cmd->callback([this]()
                  { callback(); });
}
void LoginCMD::callback()
{
    filesync::CONFIG cfg;
    auto err = cfg.load();
    if (err)
    {
        filesync::print_info(err.message());
        return;
    }
    filesync::FileSync *filesync = new filesync::FileSync{common::strcpy("/"), cfg};
    std::string token = filesync->get_token(account);
    if (!token.empty())
    {
        common::print_info("login success");
    }
    else
    {
        common::print_info("login failed");
    }
    delete filesync;
}