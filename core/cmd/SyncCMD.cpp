#include "cmd.h"
#include "common.h"
#include "filesync.h"
void SyncCMD::reg(CLI::App *parent)
{
    auto sync = parent->add_subcommand("sync", "syncing everything")->require_subcommand();
    sync->add_option("--account", account, "your account name")->required();
    sync->callback([this]()
                   { callback(); });
    addRunCmd(sync);
    addConfigureCmd(sync);
}

void SyncCMD::callback()
{
    common::print_debug("CMD END");
}
void SyncCMD::addRunCmd(CLI::App *parent)
{
    auto run = parent->add_subcommand("run", "run as a service");
    bool fa = false;
    run->callback([this]()
                  {

    common::print_info("login account:" + account);
    filesync::CONFIG cfg;
    auto err = cfg.load();
    if (err)
    {
        filesync::print_info(err.message());
        return;
    }
    filesync::FileSync *filesync = new filesync::FileSync{common::strcpy("/"), cfg};
    filesync->account = account;
    try
    {
        // conect to tcp server
        filesync::FSConnectResult connect_res = filesync->connect(cfg.server_ip, common::string_format("%d", cfg.server_tcp_port));
        // configuration
        filesync::PartitionConf conf = filesync::PartitionConf::create(cfg.debug_mode, connect_res.partition_id, true);
        if(conf.commit_id.empty())
        {
            conf.commit_id = connect_res.first_commit_id;
        }
        conf.first_commit_id = connect_res.first_commit_id;
        conf.max_commit_id = connect_res.max_commit_id;
        filesync->conf = conf;

        common::error err=filesync->check_sync_path();
        if(err){
            common::print_info(err.message());
            return;
        }
        filesync->db.init(filesync->conf.db_path.c_str());
        if(!filesync->get_all_server_files(filesync::GET_SERVER_FILES_RETRY_MAX_TIMES))
        {
            filesync::print_info("errors while getting server files");
            return;
        }
        bool process_monitor = false;
        while (1)
        {
            if (!filesync->get_file_changes())
            {
                continue;
            }
            if (!filesync->sync_server())
            {
                continue;
            }

            bool procoss_monitor_failed = false;
            if (process_monitor)
            {
                while (auto local_change = filesync->get_local_file_change())
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    auto changedPath = filesync::PATH(local_change->path,true);
                    if (!filesync->sync_local_added_or_modified(changedPath.string().c_str()))
                    {
                        procoss_monitor_failed = true;
                    }
                    if (!filesync->sync_local_deleted(changedPath.string().c_str()))
                    {
                        procoss_monitor_failed = true;
                    }
                    delete local_change;
                }
            }
            else
            {
                if (!filesync->sync_local_added_or_modified(filesync->conf.sync_path.string().c_str()))
                {
                    continue;
                }
                if (!filesync->sync_local_deleted(NULL))
                {
                    continue;
                }
                process_monitor = true;
                filesync::print_info("begin processing  monitor...");
            }
            if (!procoss_monitor_failed)
            {
                if (!filesync->committer->commit())
                    process_monitor = false;
            }
            else
            {

                process_monitor = false;
                filesync->committer->clear();
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5000));
        }
    }
    catch (const std::exception &ex)
    {
        common::print_info(ex.what());
    }
    delete (filesync); });
}
void SyncCMD::addConfigureCmd(CLI::App *parent)
{
    auto configure = parent->add_subcommand("configure", "configure arguments");
    configure->add_option("--sync_path", syncPath, "configure the path to sync")->required();
    configure->callback([this]()
                        { 
    filesync::CONFIG cfg;
    auto err = cfg.load();
    if (err)
    {
        filesync::print_info(err.message());
        return;
    }
    filesync::FileSync *filesync = new filesync::FileSync{common::strcpy("/"), cfg};
    filesync->account = account;
    // conect to tcp server
    filesync::FSConnectResult connect_res = filesync->connect(cfg.server_ip, common::string_format("%d", cfg.server_tcp_port));
    filesync::PartitionConf conf = filesync::PartitionConf::create(cfg.debug_mode, connect_res.partition_id, true);
    if(!conf.sync_path.string().empty()){
                           common::print_info("Error:the file sync path is already configured.");
                            return;
        }
        conf.sync_path=syncPath;
        conf.save(); });
}