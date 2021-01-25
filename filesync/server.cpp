#include <server.h>
namespace common
{
    namespace socket
    {
        char *message::to_json() const
        {
            nlohmann::json j;
            for (auto header : this->headers)
            {
                header.fill_json(j["Header"]);
            }
            j["MsgType"] = this->msg_type;
            j["BodySize"] = this->body_size;
            std::string json_str = j.dump();
            return common::strcpy(json_str.c_str());
        }
        message::operator bool() const
        {
            return this - msg_type > 0;
        }
        message message::parse(std::string json)
        {
            nlohmann::json j = nlohmann::json::parse(json);
            message msg;
            msg.msg_type = j["MsgType"].get<int>();
            msg.body_size = j["BodySize"].get<long>();
            for (auto &header : j["Header"].items())
            {
                nlohmann::json val = header.value();
                if (val.is_number())
                {
                    msg.addHeader({header.key(), static_cast<size_t>(val)});
                }
                else
                {
                    msg.addHeader({header.key(), static_cast<std::string>(val)});
                }
            }
            return msg;
        }
        void message::addHeader(message_header value)
        {
            headers.push_back(value);
        }
        message_header::message_header(std::string name, std::string v)
        {
            this->name = name;
            t = 0;
            this->str_v = v;
        }
        message_header::message_header(std::string name, size_t v)
        {
            this->name = name;
            t = 1;
            this->int_v = v;
        }
        void message_header::fill_json(json &j)
        {
            if (this->t == 0)
                j[this->name] = this->str_v;
            else if (this->t == 1)
                j[this->name] = this->int_v;
        }
    } // namespace socket
} // namespace common
namespace filesync
{
    server::server(short port, std::string file_location, common::http_client &http_client) :ip{"0.0.0.0"}, port{port}, acceptor_{io_context, tcp::endpoint(boost::asio::ip::address::from_string(ip), port)}, file_location{file_location}, http_client{http_client}
    {
    }

    server::~server()
    {
    }
    void server::do_accept()
    {
        acceptor_.async_accept(
            [this](boost::system::error_code ec, tcp::socket socket) {
                if (!ec)
                {
                    filesync::print_info("accepted a new tcp connection.");
                    sessions.push_back(new session{std::move(socket)});
                    receive(sessions.back());
                }
                else
                {
                    filesync::print_info(common::string_format("async_accept failed:%s", ec.message().c_str()));
                }

                do_accept();
            });
    }
    void server::async_receive_file(common::socket::message &msg, session *s)
    {
        std::shared_ptr<int> written{new int{}};
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
                    assert(md5 == expected_md5);
                    //reply
                    auto msg = common::socket::message{};
                    msg.msg_type = static_cast<int>(MsgType::Reply);
                    s->async_send_message(msg, [this, s](bool ok) {
                        assert(ok);
                        receive(s);
                    });
                }
            });
    }
    void server::async_receive_file_v2(common::socket::message &msg, session *s)
    {
        std::shared_ptr<int> written{new int{}};
        std::string block_path = get_block_path(boost::uuids::to_string(boost::uuids::random_generator()()));
        std::shared_ptr<std::ofstream> os{new std::ofstream{block_path, std::ios_base::binary}};
        if (!os.get()->is_open())
        {
            filesync::print_debug(common::string_format("error opening file %s", block_path.c_str()));
            s->close();
            return;
        }
        filesync::print_debug("processing file message uploaded by client.");
        //receive file
        s->async_read_chunk(msg.body_size, [written, os, block_path, msg, this, s](int len, std::string error, bool finished) {
            try
            {
                std::string file_path = msg.getHeaderValue<std::string>("path");
                size_t file_size = msg.getHeaderValue<size_t>("file_size");
                std::string md5 = msg.getHeaderValue<std::string>("md5");
                size_t uploaded_size = msg.getHeaderValue<size_t>("uploaded_size");
                std::string server_file_id = msg.getHeaderValue<std::string>("server_file_id");
                std::string access_token = msg.getHeaderValue<std::string>(TokenHeaderKey);
                *written.get() += len;

                filesync::print_debug(common::string_format("received %d/%d bytes", *written.get(), msg.body_size));
                os.get()->write(s->buf.get(), len);
                if (os.get()->bad())
                {
                    throw common::exception("failed to write file");
                }

                if (!error.empty() || finished)
                {
                    os->close();
                    //save the block record
                    std::string url_path = common::string_format("/api/file-block");
                    http::UrlValues values;
                    values.add("server_file_id", server_file_id.c_str());
                    values.add("name", filesync::file_name(block_path.c_str()));
                    values.add("start", uploaded_size);
                    values.add("end", *written.get() + uploaded_size);
                    this->http_client.POST(url_path, values.str, access_token);
                    if (finished)
                    {
                        //assembly file
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
                        for (auto &item : resp["data"])
                        {
                            std::string block_path = this->get_block_path(item["Path"].get<std::string>());
                            std::ifstream block_is{block_path, std::ios_base::binary};
                            if (!block_is.is_open())
                            {
                                throw common::exception(common::string_format("error opening file %s", block_path.c_str()));
                            }
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
                            std::filesystem::rename(tmp_path, this->get_file_path(file_path));
                            http::UrlValues values;
                            values.add("server_file_id", server_file_id.c_str());
                            this->http_client.PUT("/api/file", values.str, access_token);
                            json resp = json::parse(this->http_client.resp_text);
                            if (!resp["error"].empty())
                            {
                                throw common::exception(common::string_format("Api error:", resp["error"].get<std::string>()));
                            }
                            //reply
                            auto msg = common::socket::message{};
                            msg.msg_type = static_cast<int>(MsgType::Reply);
                            s->async_send_message(msg, [this, s](bool ok) {
                                assert(ok);
                                receive(s);
                            });
                        }
                        else
                        {
                            throw common::exception(common::string_format("Wrong MD5"));
                        }
                    }
                }

                if (!error.empty())
                {
                    throw common::exception(common::string_format("async_receive_file_v2 failed with error:", error));
                }
            }
            catch (const std::exception &e)
            {
                filesync::print_debug(common::string_format("Closing socket,error:%s", e.what()));
                s->close();
            }
        });
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
    void server::receive(session *s)
    {
        s->async_read_message([this, s](std::string error, common::socket::message msg) {
            if (error.empty())
            {
                std::cout << "received a MSG:" << std::unique_ptr<char[]>{msg.to_json()}.get() << std::endl;
                try
                {

                    switch (msg.msg_type)
                    {
                    case static_cast<int>(MsgType::File):
                        async_receive_file(msg, s);
                        break;

                    case static_cast<int>(MsgType::UploadFile):
                        async_receive_file_v2(msg, s);
                        break;
                    case static_cast<int>(MsgType::Download_File):
                    {
                        std::string file_path = this->get_file_path(msg.getHeaderValue<std::string>("path"));
                        if (!std::filesystem::exists(file_path))
                        {
                            filesync::print_info(common::string_format("WARNING:not found a client requested file"));
                            s->close();
                            return;
                        }
                        auto msg = common::socket::message{};
                        msg.msg_type = static_cast<int>(MsgType::Reply);
                        msg.body_size = filesync::file_size(file_path);
                        s->async_send_message(msg, [file_path, this, s](bool ok) {
                            assert(ok);
                            std::shared_ptr<std::istream>
                                fs{new std::ifstream{file_path, std::ios_base::binary}};
                            s->async_send_stream(fs, [this, s](bool ok) {
                                if (ok)
                                {
                                    receive(s);
                                }
                                else
                                {
                                    s->close();
                                }
                            });
                        });
                        break;
                    }
                    default:

                        receive(s);
                        break;
                    }
                }
                catch (const std::exception &e)
                {
                    filesync::print_debug(common::string_format("Closing socket,error:%s", e.what()));
                    s->close();
                }
            }
            else
            {
                filesync::print_debug(error);
                s->close();
            }
        });
    }
    void server::listen()
    {
        this->thread.reset(new std::thread([this]() {
            filesync::print_info(common::string_format("server listening on %s:%d",this->ip.c_str(),this->port));
            do_accept();
            this->io_context.run();
            std::cout << "server listening thread exited" << std::endl;
        }));
    }
    // void session::poll()
    // {
    //     this->io_context->restart();
    //     this->io_context->poll_one();
    // }
    session::session(tcp::socket socket) : socket{std::move(socket)}, has_closed{false}
    {
        char *b = new char[BUF_SIZE];
        memset(b, '\0', BUF_SIZE);
        buf.reset(b);
    }

    /* session::session(session &&s) : socket{std::move(s.socket)}
    {
        this->buf.swap(s.buf);
        this->size_ss = std::move(s.size_ss);
        this->msg_ss = std::move(s.msg_ss);
        this->has_closed = s.has_closed;
    }*/
    session::~session()
    {
        this->close();
    }
    bool session::is_closed()
    {
        return  this->has_closed || !this->socket.is_open();
    }
    template <typename OnReadSize>
    void session::async_read_size(OnReadSize &&onReadSize)
    {
        FILESYNC_HANDLER_TYPE_REQUIREMENTS_ASSERT2(OnReadSize, int(), std::string{})
        boost::asio::async_read(socket, boost::asio::buffer(buf.get(), 1), [this, onReadSize](boost::system::error_code ec, std::size_t length) {
            if (!ec)
            {
                if (buf[0] != '\0')
                {
                    this->size_ss << buf[0];
                    this->async_read_size(onReadSize);
                }
                else
                {
                    size_t size = -1;
                    this->size_ss >> size;
                    if (size == -1 || this->size_ss.str().size() > 3)
                    {
                        filesync::print_debug(common::string_format("error reading size:%s", this->size_ss.str().c_str()));
                        this->async_read_size(onReadSize);
                    }
                    else
                    {
                        onReadSize(size, "");
                    }
                }
            }
            else
            {
                std::string error = common::string_format("async_read_chunk failed:%s", ec.message().c_str());
                filesync::print_info(error);
                onReadSize(0, error);
            }
        });
    }
    /* template <typename OnReadChunk>
    void session::async_read_chunk(std::size_t size, OnReadChunk &&onReadChunk)
    {
        filesync::print_debug(common::string_format("reading %d bytes from upstream", size));
        FILESYNC_HANDLER_TYPE_REQUIREMENTS_ASSERT3(OnReadChunk, int(), std::string{}, bool())
        socket.async_receive(boost::asio::buffer(buf.get(), size > BUF_SIZE ? BUF_SIZE : size), [this, size, onReadChunk](boost::system::error_code ec, std::size_t length) {
            if (!ec)
            {
                //std::cout << "read size:" << length << " [" << this->buf.get() << "]"
                //          << " thread-id:" << std::this_thread::get_id() << std::endl;
                onReadChunk(length, "", length == size);
                if (length != size)
                {
                    async_read_chunk(size - length, onReadChunk);
                }
                else
                {
                    filesync::print_debug("finished async reading chunk");
                }
            }
            else
            {
                std::string error = common::string_format("async_read_chunk failed:%s", ec.message().c_str());
                filesync::print_info(error);
                onReadChunk(length, error, length == size);
            }
        });
    }*/
    bool session::write(const char *data, std::size_t size)
    {
        std::promise<bool> promise;
        std::future<bool> future = promise.get_future();
        async_write(data, size, [&promise](bool ok) {
            promise.set_value(ok);
        });
        return future.get();
    }
    template <typename OnSentMsg>
    void session::async_send_message(const common::socket::message &msg, OnSentMsg &&onSentMsg)
    {
        FILESYNC_HANDLER_TYPE_REQUIREMENTS_ASSERT(OnSentMsg, bool{})
        auto json = msg.to_json();
        int json_len = strlen(json);
        std::string json_len_str = common::string_format("%x", json_len);
        int buf_len = json_len_str.size() + 1 + json_len;
        std::shared_ptr<char[]> buf{new char[buf_len]};
        char *dest = buf.get();
        memcpy(dest, json_len_str.c_str(), json_len_str.size());
        dest += json_len_str.size();
        memcpy(dest++, "\0", 1);
        memcpy(dest, json, strlen(json));
        delete[] json;
        async_write(buf.get(), buf_len, [buf, onSentMsg](bool ok) {
            onSentMsg(ok);
        });
    }
    template <typename OnWrote>
    void session::async_write(const char *data, std::size_t size, OnWrote &&onWrote)
    {
        FILESYNC_HANDLER_TYPE_REQUIREMENTS_ASSERT(OnWrote, bool())
        socket.async_write_some(boost::asio::buffer(data, size), [this, data, size, onWrote](boost::system::error_code ec, std::size_t length) {
            if (!ec)
            {
                if (size == length)
                {
                    onWrote(true);
                }
                else
                {
                    async_write(data + size, size - length, onWrote);
                }
            }
            else
            {
                filesync::print_info(common::string_format("Closing socket because async_write failed:%s", ec.message().c_str()));
                this->close();
                onWrote(false);
            }
        });
        //this->poll();
    }
    template <typename OnSentStream>
    void session::async_send_stream(std::shared_ptr<std::istream> fs, OnSentStream &&onSentStream)
    {
        static const int BUFFER_SIZE = 1024;
        static char buf[BUFFER_SIZE];
        fs->read(buf, BUFFER_SIZE);
        if (fs->rdstate() & (std::ios_base::badbit)) //failed to read bytes
        {
            throw common::exception("failed to read bytes");
        }
        int read_count = fs->gcount();
        async_write(buf, read_count, [this, fs, onSentStream](bool ok) {
            assert(ok);
            if (fs->rdstate() & (std::ios_base::eofbit)) //all bytes has been written
            {
                filesync::print_info(common::string_format("sccessfully sent a stream"));
                onSentStream(true);
            }
            else
            {
                async_send_stream(fs, onSentStream);
            }
        });
    }
    void session::send_file(std::string path, size_t offset)
    {
        std::ifstream fs{path, std::ios_base::binary | std::ios_base::ate};
        size_t file_size = fs.tellg();
        fs.seekg(offset, std::ios_base::beg);
        const int BUFFER_SIZE = 1024;
        std::unique_ptr<char[]> buf{new char[BUFFER_SIZE]};
        long written{};
        while (1)
        {
            fs.read(buf.get(), BUFFER_SIZE);
            if (fs.rdstate() & (std::ios_base::badbit)) //failed to read bytes
            {
                throw common::exception("failed to read bytes");
            }
            int read_count = fs.gcount();
            written += read_count;
            assert(write(buf.get(), read_count));
            if (fs.rdstate() & (std::ios_base::eofbit)) //all bytes has been written
            {
                assert(written + offset == file_size);
                filesync::print_info(common::string_format("sccessfully sent file %s with %d bytes", path.c_str(), written));
                break;
            }
        }
    }
    template <typename OnReadMsg>
    void session::async_read_message(OnReadMsg &&onReadMsg)
    {
        FILESYNC_HANDLER_TYPE_REQUIREMENTS_ASSERT2(OnReadMsg, std::string(), common::socket::message())
        size_ss.clear();
        msg_ss.clear();
        size_ss = std::stringstream{};
        size_ss << std::hex;
        msg_ss = std::stringstream{};
        async_read_size([this, onReadMsg](size_t size, std::string error) {
            if (!error.empty())
            {
                onReadMsg(error, common::socket::message{});
                return;
            }
            std::cout << "read size:" << size << std::endl;
            async_read_chunk(
                size, [this, onReadMsg](int len, std::string error, bool finished) {
                    this->msg_ss.write(this->buf.get(), len);
                    if (!error.empty())
                    {
                        onReadMsg(error, common::socket::message{});
                    }
                    else if (finished)
                    {
                        std::string str = this->msg_ss.str();
                        std::cout << "read message succeed:" << str << std::endl;
                        try
                        {
                            onReadMsg("", common::socket::message::parse(str));
                        }
                        catch (const std::exception &e)
                        {
                            onReadMsg(e.what(), common::socket::message{});
                        }
                    }
                });
        });
    }
    common::socket::message session::read_message()
    {
        std::promise<common::socket::message> promise;
        std::future<common::socket::message> future = promise.get_future();
        async_read_message([&promise](std::string error, common::socket::message msg) {
            promise.set_value(msg);
        });
        return future.get();
    }
    bool session::send_message(common::socket::message &msg)
    {
        std::promise<bool> promise;
        std::future<bool> future = promise.get_future();
        async_send_message(msg, [&promise](bool ok) {
            promise.set_value(ok);
        });
        return future.get();
    }
    void session::close()
    {
        if (this->has_closed)
        {
            filesync::print_info("warning:: repeated invoke socket close function");
            return;
        }
        this->has_closed = true;
        this->socket.close();
        filesync::print_debug("socket closed");
    }
    //begin client
    tcp_client::tcp_client(std::string server_host, std::string server_port) : server_host{server_host}, server_port{server_port}
    {
    }
    tcp_client::~tcp_client()
    {
        work = boost::asio::any_io_executor();
        this->thread->join();
    }
    bool tcp_client::connect()
    {
        filesync::print_info(common::string_format("tcp connecting %s:%s",this->server_host.c_str(),this->server_port.c_str()));
        std::promise<bool> promise;
        std::future<bool> future = promise.get_future();
        this->thread.reset(new std::thread([this, &promise]() {
            try
            {
            
            boost::asio::io_context io_context;
            tcp::socket s(io_context);
            tcp::resolver resolver(io_context);
            boost::asio::connect(s, resolver.resolve(server_host, server_port));
            filesync::print_info("connected succeed");
            this->session_.reset(new session{std::move(s)});
            promise.set_value(true);
            // this->session_->read_message([](filesync::message msg) {

            // });
            //for (int i = 0; i < 100000000; i++)
            /*this->session_->async_send_message(common::socket::message(), [](int) {});
            std::vector<std::string> files;
            common::find_files(path, files, false, 1);
            for (auto f : files)
            {
                filesync::print_info(common::string_format("syncing file %s", f.c_str()));
                this->send_file(f);
            }*/
            work = boost::asio::require(io_context.get_executor(),
                                        boost::asio::execution::outstanding_work.tracked);
            io_context.run();
            this->session_.release();
            this->session_=NULL;
            }
            catch(const std::exception& e)
            {
            filesync::print_debug(e.what());promise.set_value(false);
            }
            filesync::print_debug("client connecting thread exited.");
        }));
       return future.get();
    }
    void tcp_client::send_file(std::string path, size_t offset)
    {
        auto msg = common::socket::message{};
        msg.msg_type = static_cast<int>(filesync::MsgType::File);
        msg.body_size = filesync::file_size(path);
        msg.addHeader({"md5", filesync::file_md5(path.c_str())});
        msg.addHeader({"file_name", filesync::file_name(path.c_str())});
        this->session_->send_message(msg);
        this->session_->send_file(path, offset);
    }
} // namespace filesync