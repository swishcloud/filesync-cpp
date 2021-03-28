#include <server.h>
namespace common
{
} // namespace common
namespace filesync
{
    void server::receive(XTCP::tcp_session *session)
    {
        XTCP::message msg;
        XTCP::read_message(session, [this, session](common::error error, XTCP::message &msg) {
            if (!error)
            {
                common::print_debug("Received a client message.");
                this->process_message(session, msg, [this, session](std::string error) {
                    if (error.empty())
                    {
                        receive(session);
                    }
                    else
                    {
                        common::print_debug(common::string_format("Error processing message:%s", error.c_str()));
                        this->tcp_server.remove_session(session);
                    }
                });
            }
            else
            {
                common::print_debug(common::string_format("Error reading message:%s", error.message()));
                this->tcp_server.remove_session(session);
            }
        });
    }
    server::server(short port, std::string file_location, common::http_client &http_client) : tcp_server{port}, ip{"0.0.0.0"}, port{port}, file_location{file_location}, http_client{http_client}
    {
        tcp_server.on_accepted = [this](XTCP::tcp_session *session, XTCP::tcp_server *server) {
            filesync::print_info("accepted a client connection.");
            receive(session);
        };
    }
    void server::process_message(XTCP::tcp_session *session, XTCP::message &msg, std::function<void(std::string error)> cb)
    {
        try
        {

            switch (msg.msg_type)
            {
            case static_cast<int>(filesync::tcp::MsgType::UploadFile):
                async_receive_file_v2(msg, session);
                break;
            case static_cast<int>(filesync::tcp::MsgType::Download_File):
                std::string file_path = this->get_file_path(msg.getHeaderValue<std::string>("path"));
                if (!std::filesystem::exists(file_path))
                {
                    throw common::exception(common::string_format("WARNING:not found a client requested file"));
                }
                auto msg = XTCP::message{};
                msg.msg_type = static_cast<int>(filesync::tcp::MsgType::Reply);
                msg.body_size = filesync::file_size(file_path);
                XTCP::send_message(session, msg, [file_path, this, cb, session](bool success) {
                    if (!success)
                    {
                        cb("Faile to send a reply mssage.");
                        return;
                    }
                    std::shared_ptr<std::istream> fs{new std::ifstream{file_path, std::ios_base::binary}};
                    session->send_stream(
                        fs, [this, cb](size_t written_size, XTCP::tcp_session *session, bool completed, common::error error, void *p) {
                            if (completed)
                            {
                                cb(NULL);
                            }
                            else if (error)
                            {
                                cb(common::string_format("Faile to send a stream:%s", error.message()));
                            }
                        },
                        NULL);
                });
            }
        }
        catch (const std::exception &e)
        {
            cb(e.what());
        }
    }
    server::~server()
    {
    }
    void server::do_accept()
    {
        /* acceptor_.async_accept(
            [this](boost::system::error_code ec, tcp::socket socket) {
                if (!ec)
                {
                    filesync::print_info("accepted a new tcp connection.");
                    session *s = new session{std::move(socket)};
                    receive(s);
                }
                else
                {
                    filesync::print_info(common::string_format("async_accept failed:%s", ec.message().c_str()));
                }

                do_accept();
            });*/
    }
    void server::async_receive_file(XTCP::message &msg, XTCP::tcp_session *s)
    {
        /* std::shared_ptr<int> written{new int{}};
        std::string path = this->get_file_path(msg.getHeaderValue<std::string>("file_name"));
        filesync::print_debug(common::string_format("opening file %s", path.c_str()));
        std::shared_ptr<std::ofstream> os{new std::ofstream{path, std::ios_base::binary}};
        if (!os.get()->is_open())
        {
            filesync::print_debug(common::string_format("error opening file %s", path.c_str()));
            s->close();
            return;
        }
        s->async_read_chunk(
            msg.body_size, [written, msg, this, s, os](int len, std::string error, bool finished) {
                *written.get() += len;
                if (!error.empty())
                {
                    std::cout << common::string_format("async_receive_file failed with error:", error) << std::endl;
                    s->close();
                    return;
                }
                std::cout << common::string_format("read %d/%d bytes file content from upstream", *written.get(), msg.body_size) << std::endl;
                os.get()->write(s->buf.get(), len);
                if (os.get()->bad())
                {
                    filesync::print_debug("failed to write file");
                    s->close();
                    return;
                }
                if (finished)
                {
                    os.get()->close();
                    std::cout << common::string_format("received a file,with %d bytes", msg.body_size) << std::endl;
                    std::string md5 = filesync::file_md5(this->get_file_path(msg.getHeaderValue<std::string>("file_name")).c_str());
                    std::string expected_md5 = msg.getHeaderValue<std::string>("md5");
                    if (md5 != expected_md5)
                    {
                        filesync::print_info(common::string_format("MD5 check failed."));
                        s->close();
                        return;
                    }
                    //reply
                    auto reply = common::socket::message{};
                    reply.msg_type = static_cast<int>(MsgType::Reply);
                    s->async_send_message(reply, [this, s, msg](bool ok) {
                        filesync::print_info(common::string_format("%s:%s", ok ? "OK" : "Failed", msg.getHeaderValue<std::string>("file_name")).c_str());
                        receive(s);
                    });
                }
            });*/
    }
    void server::async_receive_file_v2(XTCP::message &msg, XTCP::tcp_session *s)
    {
        std::function<void(std::string err)> cb;
        std::shared_ptr<int> written{new int{}};
        std::string block_path = get_block_path(boost::uuids::to_string(boost::uuids::random_generator()()));
        std::shared_ptr<std::ofstream> os{new std::ofstream{block_path, std::ios_base::binary}};
        if (!os.get()->is_open())
        {
            auto err = common::string_format("error opening file %s", block_path.c_str());
            cb(err);
            return;
        }
        filesync::print_debug("processing file message uploaded by client.");
        //receive file
        s->read(
            msg.body_size, [written, os, block_path, msg, this, s, cb](size_t read_size, XTCP::tcp_session *session, bool completed, common::error error, void *p) {
                try
                {
                    std::string file_path = msg.getHeaderValue<std::string>("path");
                    size_t file_size = msg.getHeaderValue<size_t>("file_size");
                    std::string md5 = msg.getHeaderValue<std::string>("md5");
                    size_t uploaded_size = msg.getHeaderValue<size_t>("uploaded_size");
                    std::string server_file_id = msg.getHeaderValue<std::string>("server_file_id");
                    std::string access_token = msg.getHeaderValue<std::string>(TokenHeaderKey);
                    *written.get() += read_size;

                    filesync::print_debug(common::string_format("received %d/%d bytes", *written.get(), msg.body_size));
                    os.get()->write(s->buffer.get(), read_size);
                    if (os.get()->bad())
                    {
                        throw common::exception("failed to write file");
                    }

                    if (error || completed)
                    {
                        os->close();
                        //save the block record
                        if (*written.get() > 0)
                        {
                            std::string url_path = common::string_format("/api/file-block");
                            http::UrlValues values;
                            values.add("server_file_id", server_file_id.c_str());
                            values.add("name", filesync::file_name(block_path.c_str()));
                            values.add("start", uploaded_size);
                            values.add("end", *written.get() + uploaded_size);
                            this->http_client.POST(url_path, values.str, access_token);
                        }

                        if (error)
                        {
                            throw common::exception(common::string_format("async_receive_file_v2 failed with error:", error));
                        }
                        else if (completed)
                        {
                            //assemble file
                            http::UrlValues values;
                            values.add("server_file_id", server_file_id.c_str());
                            this->http_client.GET("/api/file-block", values.str, access_token);
                            std::cout << this->http_client.resp_text << std::endl;

                            json resp = json::parse(this->http_client.resp_text);
                            if (!resp["error"].empty())
                            {
                                throw common::exception(common::string_format("Api error:", resp["error"].get<std::string>()));
                            }

                            auto tmp_path = (std::filesystem::temp_directory_path() / file_path).string();
                            std::ofstream result_os{tmp_path, std::ios_base::binary};
                            if (!result_os.is_open())
                            {
                                throw common::exception(common::string_format("error opening file %s", tmp_path.c_str()));
                            }
                            for (int i = resp["data"].size() - 1; i >= 0; i--)
                            {
                                auto &item = resp["data"][i];
                                std::string block_path = this->get_block_path(item["Path"].get<std::string>());
                                std::string start_str = item["Start"].get<std::string>();
                                std::string end_str = item["End"].get<std::string>();
                                if (start_str == end_str)
                                {
                                    continue;
                                }
                                size_t start;
                                sscanf(start_str.c_str(), "%zu", &start);
                                std::ifstream block_is{block_path, std::ios_base::binary};
                                if (!block_is.is_open())
                                {
                                    throw common::exception(common::string_format("error opening file %s", block_path.c_str()));
                                }
                                filesync::print_debug(common::string_format("set offset:%d", start));
                                result_os.seekp(start, std::ios_base::beg);
                                filesync::print_debug(common::string_format("offset of intermediate file:%d", result_os.tellp()));
                                result_os << block_is.rdbuf();
                                block_is.close();
                                if (result_os.bad())
                                {
                                    throw common::exception(common::string_format("failed to write file %s", tmp_path.c_str()));
                                }
                            }
                            result_os.close();
                            if (filesync::file_md5(tmp_path.c_str()) == md5)
                            {
                                filesync::movebycmd(tmp_path, this->get_file_path(file_path));
                                http::UrlValues values;
                                values.add("server_file_id", server_file_id.c_str());
                                this->http_client.PUT("/api/file", values.str, access_token);
                                json resp = json::parse(this->http_client.resp_text);
                                if (!resp["error"].empty())
                                {
                                    throw common::exception(common::string_format("Api error:", resp["error"].get<std::string>()));
                                }
                                //reply
                                auto msg = XTCP::message{};
                                msg.msg_type = static_cast<int>(filesync::tcp::MsgType::Reply);
                                XTCP::send_message(s, msg, [this, s, cb](common::error error) {
                                    if (!error)
                                    {
                                        common::print_debug(common::string_format("client uploaded a file successfully"));
                                        cb(NULL);
                                    }
                                    else
                                    {
                                        cb(common::string_format("failed to send reply message after change server file status to completed:%s", error.message()));
                                    }
                                });
                            }
                            else
                            {
                                http::UrlValues values;
                                values.add("id", server_file_id.c_str());
                                this->http_client.POST("/api/reset-server-file", values.str, access_token);
                                throw common::exception(common::string_format("Wrong MD5,client need to attempt to re-upload"));
                            }
                        }
                    }
                }
                catch (const std::exception &e)
                {
                    auto err = common::string_format("Closing socket,error:%s", e.what());
                    cb(err);
                }
            },
            NULL);
    }
    std::string server::get_file_path(std::string name)
    {
        return (std::filesystem::path(this->file_location) / name).string();
    }
    std::string server::get_block_path(std::string name)
    {
        std::filesystem::path p = std::filesystem::path(this->file_location) / "block";
        if (!std::filesystem::exists(p))
            assert(std::filesystem::create_directory(p));
        return (p / name).string();
    }
    void server::listen()
    {
        tcp_server.listen();
    }
    //begin client
    tcp_client::tcp_client(std::string server_host, std::string server_port) : server_host{server_host}, server_port{server_port}, closed{false}
    {
    }
    tcp_client::~tcp_client()
    {
    }
    bool tcp_client::connect()
    {
        filesync::print_info(common::string_format("tcp connecting %s:%s", this->server_host.c_str(), this->server_port.c_str()));
        std::promise<bool> promise;
        std::future<bool> future = promise.get_future();
        xclient.on_connect_success = [&promise](XTCP::tcp_client *client) { promise.set_value(true); };
        xclient.on_connect_fail = [&promise](XTCP::tcp_client *client) { promise.set_value(false); };
        xclient.session.on_closed = [this](XTCP::tcp_session *session) {
            this->closed = true;
        };
        xclient.start(this->server_host.c_str(), this->server_port.c_str());
        return future.get();
    }
    void tcp_client::send_file(std::string path, size_t offset)
    {
        auto msg = XTCP::message{};
        msg.msg_type = static_cast<int>(filesync::tcp::MsgType::File);
        msg.body_size = filesync::file_size(path);
        msg.addHeader({"md5", filesync::file_md5(path.c_str())});
        msg.addHeader({"file_name", filesync::file_name(path.c_str())});
        XTCP::send_message(&this->xclient.session, msg, NULL);
        std::shared_ptr<std::istream> fs{new std::ifstream(path, std::ios::binary)};
        fs->seekg(offset, std::ios_base::beg);
        assert(false); //unfinished;
    }
} // namespace filesync