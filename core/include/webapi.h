#ifndef FILESYNC_WEBAPI_H
#define FILESYNC_WEBAPI_H
#include <vector>
#include "internal.h"
#include <common.h>
#include <http.h>

class IWebAPI
{
public:
    virtual ~IWebAPI() = default;
    virtual std::vector<filesync::File> get_file_list(const std::string &path, const std::string &revision) = 0;
    virtual int get_file_info(const std::string &md5, const size_t &size, filesync::ServerFile &sf) = 0;
    virtual int complete_server_file(const std::string &server_file_id) = 0;
};
class WebAPI : public IWebAPI
{
private:
    const std::string serverIP;
    const std::string port;
    const std::string token;
    const std::string maxid;

public:
    WebAPI(const std::string &serverIP, const std::string &port, const std::string &token, const std::string &maxid) : serverIP(serverIP), port(port), token(token), maxid(maxid)
    {
    }
    std::vector<filesync::File> get_file_list(const std::string &path, const std::string &revision)
    {

        std::vector<filesync::File> files;
        std::cout << "get_file_list:" << path << std::endl;
        std::string url_path = common::string_format("/api/files?path=%s&commit_id=%s&max=%s", common::url_encode(path.c_str()).c_str(), revision.c_str(), maxid.c_str());

        std::cout << "construct http_client" << std::endl;
        common::http_client c{serverIP, port, url_path, token};
        std::cout << "Send get request" << std::endl;
        c.GET();
        if (c.error)
        {
            throw std::runtime_error(common::string_format("api request error:", c.error.message()));
        }
        // std::cout << c.resp_text << std::endl;
        auto j = json::parse(c.resp_text);
        auto err = j["error"];
        if (!err.is_null())
        {
            throw std::runtime_error("server error:" + err.get<std::string>());
        }
        auto data = j["data"];
        for (auto item : data)
        {
            std::string file_server_path = common::string_format("%s%s%s", path.c_str(), path == "/" ? "" : "/", item["name"].get<std::string>().c_str());
            filesync::File f;
            f.is_directory = strcmp(item["type"].get<std::string>().c_str(), "2") == 0;
            f.commit_id = item["commit_id"];
            f.name = item["name"];
            f.id = item["id"];
            f.server_path = file_server_path;
            if (!f.is_directory)
            {
                f.size = std::stoull(item["size"].get<std::string>().c_str());
                f.md5 = item["md5"].get<std::string>().substr(0, 32);
            }
            files.push_back(f);
        }
        return files;
    }
    int get_file_info(const std::string &md5, const size_t &size, filesync::ServerFile &sf)
    {
        std::string url_path = common::string_format("/api/file-info?md5=%s&size=%zu", md5.c_str(), size);
        common::http_client c{serverIP, port, url_path.c_str(), token};
        c.GET();
        if (c.error)
        {
            common::print_info(c.error.message());
            return 0;
        }
        auto j = json::parse(c.resp_text);
        auto data = j["data"];
        if (!j["error"].is_null())
        {
            common::print_info(common::string_format("Failed to get file info from web server:%s", j["error"].get<std::string>().c_str()));
            return 0;
        }
        sf.md5 = data["md5"].get<std::string>();
        sf.is_completed = data["is_completed"].get<std::string>() == "true";
        sf.path = data["path"].get<std::string>();
        sf.size = std::stoul(data["size"].get<std::string>());
        sf.uploaded_size = std::stoul(data["uploaded_size"].get<std::string>());
        sf.server_file_id = data["server_file_id"].get<std::string>();
        sf.ip = data["ip"].get<std::string>();
        sf.port = std::stoi(data["port"].get<std::string>());
        return 1;
    }
    int complete_server_file(const std::string &server_file_id)
    {
        common::http_client c{serverIP, std::stoi(port.c_str())};
        http::UrlValues values;
        values.add("server_file_id", server_file_id.c_str());
        c.PUT("/api/file", values.str, token);
        if (c.error)
        {
            common::print_info(c.error.message());
            return 0;
        }
        json resp = json::parse(c.resp_text);
        if (!resp["error"].empty())
        {
            common::print_info(common::string_format("Api error:", resp["error"].get<std::string>().c_str()));
            return 0;
        }
        return 1;
    }
};
#endif