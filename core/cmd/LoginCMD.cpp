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
    std::string token = filesync::get_token(account);
    if (!token.empty())
    {
        common::print_info("login success");
    }
    else
    {
        common::print_info("login failed");
    }
}