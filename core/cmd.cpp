#include "filesync.h"
#include "CLI11.hpp"
int exit_code = -1;
void begin_sync(std::string account, bool get_all_server_files)
{
    common::print_info("login account:" + account);
    if (get_all_server_files)
    {
        common::print_info("will begin getting all server files in 3 seconds.Be patient...");
        std::this_thread::sleep_for(std::chrono::milliseconds(1000 * 3));
    }
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
        filesync->connect();
        filesync->check_sync_path();
        std::error_code ec;
        if (get_all_server_files && !std::filesystem::remove(filesync->conf.db_path.c_str(), ec))
        {
            delete filesync;
            common::print_info(ec.message());
            return;
        }
        filesync->db.init(filesync->conf.db_path.c_str());
        while (!filesync->get_all_server_files(get_all_server_files))
            ;
        get_all_server_files = true;
        bool process_monitor = false;
        while (1)
        {
            if (!filesync->clear_errs())
            {
                continue;
            }
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
                    local_change->path;
                    std::replace(local_change->path.begin(), local_change->path.end(), '\\', '/');

                    if (!filesync->sync_local_added_or_modified(local_change->path.c_str()))
                    {
                        procoss_monitor_failed = true;
                    }
                    if (!filesync->sync_local_deleted(local_change->path.c_str()))
                    {
                        procoss_monitor_failed = true;
                    }
                    if (procoss_monitor_failed)
                    {
                        filesync->add_local_file_change(local_change);
                    }
                    else
                    {
                        delete (local_change);
                    }
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
                filesync->committer->commit();
            std::this_thread::sleep_for(std::chrono::milliseconds(5000));
        }
    }
    catch (const std::exception &ex)
    {
        common::print_info(ex.what());
    }
    delete (filesync);
}
void begin_listen(std::string listen_port, std::string files_path)
{
    filesync::CONFIG cfg;
    auto err = cfg.load();
    if (err)
    {
        filesync::print_info(err.message());
        return;
    }
    filesync::FileSync *filesync = new filesync::FileSync{common::strcpy("/"), cfg};
    common::http_client http_client{filesync->cfg.server_ip, filesync->cfg.server_port};
#ifdef __linux__
    filesync::server s{(short)std::stoi(listen_port.c_str()), files_path, http_client};
#else
    filesync::server s{(short)std::stoi(listen_port.c_str()), files_path, http_client};
#endif
    s.listen();

    filesync::print_info("exited.");
}
void begin_export(filesync::PATH path, std::string commit_id, std::string max_commit_id, filesync::PATH destination_folder, std::string account)
{
    common::print_info(common::string_format("exporting server directory %s with content since %s until %s to local directory %s", path.string().c_str(), std::string(commit_id) == "/" ? "first commit" : commit_id.c_str(), max_commit_id.c_str(), destination_folder.string().c_str()));
    commit_id = std::string(commit_id) == "/" ? "" : commit_id;
    if (!std::filesystem::exists(destination_folder.string()))
    {
        std::error_code ec;
        std::filesystem::create_directory(destination_folder.string(), ec);
        if (ec)
        {
            common::print_info(ec.message());
            return;
        }
    }
    else
    {
        if (!std::filesystem::is_directory(destination_folder.string()))
        {
            common::print_debug(common::string_format("the path %s already exists, but it's not a directory", destination_folder.string().c_str()));
            return;
        }
    }
    filesync::CONFIG cfg;
    auto err = cfg.load();
    if (err)
    {
        filesync::print_info(err.message());
        return;
    }
    filesync::FileSync *filesync = new filesync::FileSync{common::strcpy("/"), cfg};
    filesync->account = account;
    filesync->connect();
    std::string first_server_path = path.string();
    std::string export_directory_path = destination_folder.string();
    export_directory_path = (std::filesystem::path{export_directory_path} / common::get_file_name(first_server_path)).string();
    common::makedir(export_directory_path);
    std::vector<std::string> failed_paths;
    auto error = filesync->get_server_files(path.string(), commit_id, max_commit_id, [&failed_paths, first_server_path, export_directory_path, filesync](filesync::ServerFile &file)
                                            {
                                                auto relative_path = common::get_relative_path(first_server_path, file.path);
                                                auto exported_path = (std::filesystem::path{export_directory_path} / relative_path).string();
                                                if (file.is_directory)
                                                {
                                                    common::print_info(common::string_format("creating directory:%s", exported_path.c_str()));
                                                    common::makedir(exported_path);
                                                    return;
                                                }
                                                bool has_downloaded = false;
                                                if (std::filesystem::exists(exported_path))
                                                {
                                                    auto md5 = common::file_md5(exported_path.c_str());
                                                    if (filesync::compare_md5(md5.c_str(), file.md5.c_str()))
                                                    {
                                                        has_downloaded = true;
                                                    }
                                                }
                                                if (has_downloaded)
                                                {
                                                    common::print_info(common::string_format("%s already exists", exported_path.c_str()));
                                                    return;
                                                }
                                                common::error err = filesync->download_file(file.path, file.commit_id, exported_path);
                                                if (err)
                                                {
                                                    filesync->destroy_tcp_client();
                                                    common::print_info(err.message());
                                                    failed_paths.push_back(exported_path);
                                                } });
    if (!error.empty())
    {
        common::print_info(error);
        exit(1);
    }
    if (failed_paths.size() == 0)
    {
        common::print_info("Already exported all files.");
        return;
    }
    else
    {
        common::print_info("the following paths are not exported successfully:");
        for (auto i : failed_paths)
        {
            std::cout << "FAIL:" << i << std::endl;
        }
        return;
    }
}
void begin_server_clean(filesync::CMD_SERVER_CLEAN_OPTION opt)
{
    opt.trash_dir = opt.trash_dir.string() + "/trash";
    std::error_code ec;
    std::filesystem::create_directory(opt.trash_dir.string(), ec);
    if (ec)
    {
        common::print_info(common::string_format("%s:%s", opt.trash_dir.string().c_str(), ec.message().c_str()));
        exit(1);
    }
    common::print_info(common::string_format("begin cleaning all unused files in current server:%s", opt.server_id.c_str()));

    filesync::CONFIG cfg;
    auto err = cfg.load();
    if (err)
    {
        filesync::print_info(err.message());
        return;
    }
    filesync::FileSync *filesync = new filesync::FileSync{common::strcpy("/"), cfg};
    std::string url_path = common::string_format("/api/server-file/tobedeleted?id=%s", opt.server_id.c_str());
    common::http_client c{filesync->cfg.server_ip, common::string_format("%d", filesync->cfg.server_port), url_path.c_str(), ""};
    c.GET();
    if (c.error)
    {
        common::print_info(c.error.message());
        exit(1);
    }
    auto j = json::parse(c.resp_text);
    if (!j["error"].is_null())
    {
        common::print_info(common::string_format("failed to parse the json literal string:%s", j["error"].get<std::string>().c_str()));
        exit(1);
    }
    auto data = j["data"];
    std::vector<filesync::server_file> server_files{};
    for (auto item : data)
    {
        filesync::PATH path = opt.files_path.string() + "/" + item["path"].get<std::string>();
        filesync::server_file server_file{};
        server_file.local_path = path;
        server_file.server_file_id = item["id"].get<std::string>();
        server_files.push_back(server_file);
        common::print_info(common::string_format("path:%s id:%s", path.string().c_str(), server_file.server_file_id.c_str()));
    }
    for (auto &file : server_files)
    {
        std::error_code ec;
        std::string tmp_path = opt.trash_dir.string() + "/" + common::get_file_name(file.local_path.string());

        if (!std::filesystem::is_regular_file(file.local_path.string()))
        {
            common::print_info(common::string_format("ERROR:not found the following path or the path isn't a file:%s", file.local_path.string().c_str()));
            char input[1];
            printf("Do you want to abort procedure or ignore the above error?[Y/N]");
            scanf("%s", &input);
            if (input[0] != 'Y' && input[0] != 'y')
            {
                common::print_info("Aborted");
                exit(1);
            }
        }
        else
        {
            common::movebycmd(file.local_path.string(), tmp_path);
            common::print_info(common::string_format("removed file '%s'", file.local_path.string().c_str()));
        }
        http::UrlValues values;
        values.add("id", file.server_file_id.c_str());
        c.POST("/api/reset-server-file", values.str, "");
        if (c.error)
        {
            common::movebycmd(tmp_path, file.local_path.string());
            common::print_info(c.error.message());
            exit(1);
        }
    }

    common::print_info("Finished");
}
void begin_upload(filesync::CMD_UPLOAD_OPTION opt)
{
    common::print_info(common::string_format("md5:%s location:%s", opt.md5.c_str(), opt.location.string().c_str()));
    /*std::this_thread::sleep_for(std::chrono::milliseconds(5000));
    for (std::string line; std::getline(std::cin, line);)
    {
        common::print_info(line);
    }*/
    std::shared_ptr<std::istream> fs;
    if (!opt.path.string().empty())
    {
        if (opt.size != SIZE_MAX)
        {
            common::print_info(std::string("parameter PATH and parameter SIZE can not be used together."));
            return;
        }
        opt.md5 = common::file_md5(opt.path.string().c_str());
        opt.size = common::file_size(opt.path.string().c_str());
        if (opt.filename.empty())
        {
            opt.filename = filesync::file_name(opt.path.string().c_str());
        }
        fs.reset(new std::ifstream(opt.path.string()));
        if (!*fs)
        {
            common::print_info(std::string("Faild to open file ") + opt.path.string());
            return;
        }
    }
    else
    {
        if (opt.size == SIZE_MAX)
        {
            common::print_info(std::string("please specify the parameter SIZE."));
            return;
        }
        if (opt.md5.empty())
        {
            common::print_info(std::string("please specify the parameter MD5 when PATH not specified."));
            return;
        }
        fs = std::shared_ptr<std::istream>{&std::cin, [](void *) {}};
        if (opt.filename.empty())
        {
            common::print_info("missing filename parameter");
            return;
        }
    }
    filesync::CONFIG cfg;
    auto err = cfg.load();
    if (err)
    {
        filesync::print_info(err.message());
        return;
    }
    filesync::FileSync *filesync = new filesync::FileSync{common::strcpy("/"), cfg};
    filesync->account = opt.account;
    if (opt.account.empty() && opt.token.empty() || !opt.account.empty() && !opt.token.empty())
    {
        common::print_info("required a account parameter or a token parameter, and only one of both.");
        return;
    }
    auto token = opt.token.empty() ? filesync->get_token() : opt.token;
    bool upload_ok = filesync->upload_file(fs, opt.md5.c_str(), opt.size, token);
    if (upload_ok)
    {
        filesync::create_file_action *action = new filesync::create_file_action();
        action->is_hidden = false;
        action->location = common::strcpy(opt.location.string().c_str());
        action->md5 = common::strcpy(opt.md5.c_str());
        action->name = common::strcpy(opt.filename.c_str());
        filesync->committer->add_action(action);
        if (filesync->committer->commit(token))
        {
            common::print_info("Finished.");
            exit_code = 0;
        }
    }
}
void CMD_REPORT(CLI::App *parent)
{
    auto opt = std::shared_ptr<filesync::CMD_EXPORT_OPTION>{new filesync::CMD_EXPORT_OPTION};
    auto export_cmd = parent->add_subcommand("export", "export files");
    export_cmd->add_option("--account", opt->account, "your account name")->required();
    export_cmd->add_option("--path", opt->path, "the path to export")->required();
    export_cmd->add_option("--commit_id", opt->commit_id, "the commit id of a path")->required();
    export_cmd->add_option("--max_commit_id", opt->max_commit_id, "the max commit id of subfiles to query")->required();
    export_cmd->add_option("--dest", opt->destination_folder, "the destination folder where save exported files")->required();
    export_cmd->callback([opt]()
                         { begin_export(opt->path, opt->commit_id, opt->max_commit_id, opt->destination_folder, opt->account); });
}
void CMD_SERVER_CLEAN(CLI::App *parent)
{
    auto server_cmd = parent->add_subcommand("server", "server command");
    auto clean_cmd = server_cmd->add_subcommand("clean", "clean all server files which not used");
    auto opt = std::shared_ptr<filesync::CMD_SERVER_CLEAN_OPTION>{new filesync::CMD_SERVER_CLEAN_OPTION};
    clean_cmd->add_option("--server_id", opt->server_id, "server id")->required();
    clean_cmd->add_option("--files_path", opt->files_path, "the path of files repo")->required();
    clean_cmd->add_option("--trash_dir", opt->trash_dir, "the path to save deleted files temporarily")->required();
    clean_cmd->callback([opt]()
                        { begin_server_clean(*opt); });
}
void CMD_UPLOAD(CLI::App *parent)
{
    auto upload_cmd = parent->add_subcommand("upload", "upload a file");
    auto opt = std::shared_ptr<filesync::CMD_UPLOAD_OPTION>{new filesync::CMD_UPLOAD_OPTION};
    opt->size = SIZE_MAX;
    upload_cmd->add_option("--filename", opt->filename, "the filename to save on server");
    upload_cmd->add_option("--md5", opt->md5, "the md5 of file to upload");
    upload_cmd->add_option("--location", opt->location, "the server path where save the uploaded file")->required();
    upload_cmd->add_option("--token", opt->token);
    upload_cmd->add_option("--account", opt->account);
    upload_cmd->add_option("--path", opt->path, "the file to upload");
    upload_cmd->add_option("--size", opt->size);
    upload_cmd->callback([opt]()
                         { begin_upload(*opt); });
}
int filesync::run(int argc, const char *argv[])
{
    setlocale(LC_ALL, "zh_CN.UTF-8");
    CLI::App app("filesync tool");
    auto sync = app.add_subcommand("sync", "syncing everything");
    std::string account;
    bool fa = false;
    sync->add_option("--account", account, "your account name")->required();
    sync->add_option("--fa", fa, "force looking up all server files.this option is recommended for some unusual circumstance.");
    sync->callback([&account, &fa]()
                   { begin_sync(account, fa); });
    std::string listen_port, files_path;

    auto listen = app.add_subcommand("listen", "listen as a server node");
    listen->add_option("--listen_port", listen_port, "the tcp listen port")->required();
    listen->add_option("--files_path", files_path, "the path of files repo")->required();
    listen->callback([&listen_port, &files_path]()
                     { begin_listen(listen_port, files_path); });

    CMD_REPORT(&app);
    CMD_SERVER_CLEAN(&app);
    CMD_UPLOAD(&app);
    CLI11_PARSE(app, argc, argv);
    return exit_code;
}
