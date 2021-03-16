#include <tcp.h>
namespace filesync
{
    namespace tcp
    {
        void receive_size(XTCP::tcp_session *tcp_session, std::function<void(bool success, message &msg)> on_read);
        void receive_message(XTCP::tcp_session *tcp_session, size_t size, std::function<void(bool success, message &msg)> on_read);

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
            return this->msg_type > 0;
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
        void send_message(XTCP::tcp_session *session, message &msg, std::function<void(bool success)> on_sent)
        {
            auto json = std::unique_ptr<char[]>{msg.to_json()};
            int json_len = strlen(json.get());
            std::string json_len_str = common::string_format("%x", json_len);
            int buf_len = json_len_str.size() + 1 + json_len;
            std::shared_ptr<char[]> buf{new char[buf_len]};
            char *dest = buf.get();
            memcpy(dest, json_len_str.c_str(), json_len_str.size());
            dest += json_len_str.size();
            memcpy(dest++, "\0", 1);
            memcpy(dest, json.get(), json_len);

            std::promise<std::string> promise;
            session->write(
                buf.get(), buf_len, [buf, on_sent, &promise](size_t read_size, XTCP::tcp_session *session, bool completed, const char *error, void *p) {
                    if (error)
                    {
                        session->close();
                    }
                    if ((completed || error))
                    {
                        if (on_sent)
                            on_sent(!error);
                        else
                            promise.set_value(error ? error : "");
                    }
                },
                NULL);
            if (!on_sent)
            {
                auto err = promise.get_future().get();
                if (!err.empty())
                {
                    throw common::exception(err);
                }
            }
        }
        struct tcp_session_data
        {
            std::stringstream size_ss;
            std::stringstream msg_ss;
            std::promise<message> promise;
        };
        void read_message(XTCP::tcp_session *session, message &msg, std::function<void(bool success, message &msg)> on_read)
        {
            assert(session->data == NULL);
            auto tsd = new filesync::tcp::tcp_session_data{};
            tsd->size_ss << std::hex;
            session->data = tsd;
            receive_size(session, [on_read, session, tsd](bool success, message &msg) {
                if (!success)
                {
                    session->close();
                }
                tsd->promise.set_value(msg);
                if (on_read)
                {
                    on_read(success, msg);
                    delete tsd;
                    session->data = NULL;
                }
            });
            if (!on_read)
            {
                msg = tsd->promise.get_future().get();
                delete tsd;
                session->data = NULL;
            }
        }
        void receive_size(XTCP::tcp_session *tcp_session, std::function<void(bool success, message &msg)> on_read)
        {
            tcp_session->read(
                1, [on_read](size_t read_size, XTCP::tcp_session *session, bool completed, const char *error, void *p) {
                    tcp_session_data *tsd = (tcp_session_data *)p;
                    if (error)
                    {
                        filesync::tcp::message msg;
                        on_read(false, msg);
                        return;
                    }
                    tsd->size_ss << session->buffer.get()[0];
                    if (session->buffer.get()[0] == '\0')
                    {
                        int size;
                        tsd->size_ss >> size;
                        common::print_debug(common::string_format("read message SIZE:%d", size));
                        receive_message(session, size, on_read);
                        return;
                    }
                    else
                    {
                        assert(completed); //just one byte.
                        receive_size(session, on_read);
                    }
                },
                tcp_session->data);
        }
        void receive_message(XTCP::tcp_session *tcp_session, size_t size, std::function<void(bool success, message &msg)> on_read)
        {
            tcp_session->read(
                size, [on_read](size_t read_size, XTCP::tcp_session *session, bool completed, const char *error, void *p) {
                    tcp_session_data *tsd = (tcp_session_data *)p;
                    std::unique_ptr<char[]> msg_content = std::unique_ptr<char[]>{common::strcpy(session->buffer.get(), read_size)};
                    tsd->msg_ss << msg_content.get();

                    if (completed)
                    {
                        common::print_info(common::string_format("read message:%s", tsd->msg_ss.str().c_str()));
                    }
                    filesync::tcp::message msg = completed ? tcp::message::parse(msg_content.get()) : tcp::message{};
                    if (error || completed)
                    {
                        if (on_read)
                            on_read(!error, msg);
                    }
                },
                tcp_session->data);
        }
    }
}
// #define TokenHeaderKey "access_token"
// using namespace nlohmann;
// using namespace boost::asio;
// int read_size(ip::tcp::socket *socket)
// {
// 	const int max_size_str_len = 5;
// 	char *size_str = new char[max_size_str_len + 1]{};
// 	for (int i = 0; i < max_size_str_len; i++)
// 	{
// 		std::unique_ptr<char[]> data{new char[1]{}};
// 		int size = socket->receive(boost::asio::buffer(data.get(), 1));
// 		if (size != 1)
// 		{
// 			filesync::EXCEPTION("unknown exception.");
// 		}
// 		size_str[i] = data[0];
// 		if (size_str[i] == '\0')
// 		{
// 			break;
// 		}
// 	}
// 	int size;
// 	std::stringstream str;
// 	str << size_str;
// 	str >> size;
// 	delete[](size_str);
// 	return size;
// }
// char *read_size_n(ip::tcp::socket *socket, int size)
// {
// 	char *data = new char[size + 1];
// 	int read = 0;
// 	while (1)
// 	{
// 		boost::asio::mutable_buffer buffer(data + read, size - read);
// 		int size = socket->receive(buffer);
// 		read += size;
// 		data[read] = '\0';
// 		std::cout << data << std::endl;
// 		if (read == size)
// 		{
// 			break;
// 		}
// 	}
// 	return data;
// }
// void send_message(ip::tcp::socket *socket, filesync::message *msg)
// {
// 	try
// 	{
// 		//send the size portion
// 		auto json = msg->to_json();
// 		int json_len = strlen(json);
// 		std::string json_len_str = common::string_format("%d", json_len);
// 		int size = socket->write_some(const_buffer{json_len_str.c_str(), json_len_str.size()});
// 		assert(size = json_len_str.size());
// 		//send NULL to separate size portion and header portion.
// 		size = socket->write_some(const_buffer{"\0", 1});
// 		assert(size == 1);
// 		//send the json portion.
// 		size = socket->write_some(const_buffer{json, (size_t)json_len});
// 		delete[](json);
// 		assert(size == json_len);
// 	}
// 	catch (const std::exception &e)
// 	{
// 		std::cout << e.what() << std::endl;
// 	}
// }
// void receive_message(ip::tcp::socket *socket, filesync::PartitionConf &conf)
// {
// 	int size = read_size(socket);
// 	char *header = read_size_n(socket, size);
// 	auto j = json::parse(header);
// 	delete[](header);
// 	header = NULL;
// 	try
// 	{
// 		conf.max_commit_id = j["Header"]["max_commit_id"].get<std::string>();
// 		conf.first_commit_id = j["Header"]["first_commit_id"].get<std::string>();
// 		conf.partition_id = j["Header"]["partition_id"].get<std::string>();
// 		;
// 	}
// 	catch (const std::exception &e)
// 	{
// 		std::cout << e.what() << std::endl;
// 	}
// }
// ip::tcp::endpoint filesync::resolve(std::string host, int port)
// {
// 	boost::asio::io_service io_service;
// 	boost::asio::ip::tcp::resolver resolver(io_service);
// 	boost::asio::ip::tcp::resolver::query query(host, common::string_format("%d", port));
// 	boost::asio::ip::tcp::resolver::iterator iter = resolver.resolve(query);
// 	return iter->endpoint();
// }
// void filesync::connect(FileSync *filesync)
// {
// 	try
// 	{
// 		io_context my_io_context{};
// 		ip::tcp::socket *socket = new ip::tcp::socket{my_io_context};
// 		boost::system::error_code err;
// 		//beast::get_lowest_layer(*socket).expire_after(std::chrono::seconds(10));
// 		socket->connect(resolve(filesync->cfg.server_ip, filesync->cfg.server_tcp_port), err);
// 		if (err.failed())
// 		{
// 			socket->close();
// 			delete (socket);
// 			filesync::EXCEPTION(err.message());
// 		}
// 		auto msg = new message();
// 		char *token = get_token();
// 		msg->addHeader({"token", token});
// 		delete[](token);
// 		send_message(socket, msg);
// 		receive_message(socket, filesync->conf);
// 		filesync->cfg.save();
// 		filesync->conf.init(filesync->cfg.debug_mode);
// 		socket->close();
// 		delete (socket);
// 		socket = NULL;
// 		delete (msg);
// 		msg = NULL;

// 		//create the tcp client

// 		filesync->tcp_client = new filesync::tcp_client{"localhost", "4000"};
// 		filesync->tcp_client->connect();
// 	}
// 	catch (const std::exception &e)
// 	{
// 		std::cout << e.what() << std::endl;
// 		int delay = 10000;
// 		std::cout << "reconnect in " << delay / 1000 << "s" << std::endl;
// 		std::this_thread::sleep_for(std::chrono::milliseconds(delay));
// 		connect(filesync);
// 	}
// }
// const int MT_Download_File = 8;
// const int MT_FILE = 3;
// bool filesync::download_file(const char *ip, unsigned short port, const char *path, const char *md5, const char *local_md5, File &file)
// {
// 	if (common::file_exist(file.full_path.c_str()) != 0)
// 	{
// 		auto f_md5 = file_md5(file.full_path.c_str());
// 		if (filesync::compare_md5(f_md5.c_str(), md5))
// 		{
// 			return true;
// 		}
// 		else if (filesync::compare_md5(f_md5.c_str(), local_md5))
// 		{
// 			file.status = FileStatus::Conflict;
// 			std::cout << "file confict:" << file.full_path << std::endl;
// 			return false;
// 		}
// 	}

// 	io_context my_io_context{};
// 	std::unique_ptr<ip::tcp::socket> socket{new ip::tcp::socket{my_io_context}};
// 	boost::system::error_code err;
// 	socket->connect(ip::tcp::endpoint{ip::address::from_string(ip), port}, err);
// 	if (err.failed())
// 	{
// 		std::cout << err.value() << " " << err.message() << std::endl;
// 		return false;
// 	}
// 	auto msg = new message();
// 	msg->msg_type = MT_Download_File;
// 	msg->addHeader({"path", path});
// 	send_message(socket.get(), msg);
// 	delete msg;

// 	//begin receiving
// 	int size = read_size(socket.get());
// 	char *header = read_size_n(socket.get(), size);
// 	auto j = json::parse(header);
// 	delete[](header);
// 	header = NULL;
// 	long body_size = j["BodySize"].get<long>();
// 	auto parent = get_parent_dir(file.full_path.c_str());
// 	if (!std::filesystem::exists(parent))
// 		if (!std::filesystem::create_directories(parent))
// 			std::cout << "Failed to create directory " << parent << std::endl;
// 	std::string download_path = (std::filesystem::temp_directory_path() / md5).string();
// 	std::ofstream outfile{download_path, std::ios::binary};
// 	//int buf_size = 1024 * 1024 * 1.5;//1.5MB
// 	bool downloaded = false;
// 	std::cout << "downloading " << download_path << std::endl;
// 	downloader downloader{outfile};
// 	long read = 0;
// 	try
// 	{
// 		if (body_size > 0)
// 			while (1)
// 			{
// 				auto block = new filesync::downloader::buffer();
// 				assert(block);
// 				block->pos = read;
// 				block->data_len = socket->read_some(buffer(block->b, block->size));
// 				read += block->data_len;
// 				if (outfile.bad())
// 				{
// 					filesync::EXCEPTION("writing the file failed.");
// 				}
// 				downloader.add_block(block);
// 				float percent = ((float)read / body_size) * 100;
// 				std::cout << "\r" << common::string_format("%d/%d bytes %.2F%%", (long)read, body_size, percent);
// 				if (read == body_size)
// 				{
// 					break;
// 				}
// 			}
// 		std::cout << std::endl;
// 		downloader.flush();
// 		downloaded = true;
// 	}
// 	catch (const std::exception &e)
// 	{
// 		std::cout << std::endl
// 				  << e.what() << std::endl;
// 	}
// 	if (downloaded && !compare_md5(md5, file_md5(download_path.c_str()).c_str()))
// 	{
// 		std::cout << "the MD5 of the downloaded file is inconsistent with the MD5 of the file on the server,deleting the downloaded file." << std::endl;
// 		if (!std::filesystem::remove(download_path))
// 		{
// 			filesync::EXCEPTION("deleting the file failed.");
// 		}
// 	}
// 	else
// 	{
// 		std::filesystem::rename(download_path, file.full_path);
// 		return true;
// 	}
// 	return false;
// }
// bool filesync::upload_file(const char *ip, unsigned short port, std::ifstream &fs, const char *path, const char *md5, long file_size, long uploaded_size, const char *server_file_id)
// {
// 	io_context my_io_context{};
// 	ip::tcp::socket *socket = new ip::tcp::socket{my_io_context};
// 	boost::system::error_code err;
// 	socket->connect(ip::tcp::endpoint{ip::address::from_string(ip), port}, err);
// 	if (err.failed())
// 	{
// 		std::cout << err.value() << " " << err.message() << std::endl;
// 		return false;
// 	}
// 	auto msg = new message();
// 	msg->msg_type = MT_FILE;
// 	msg->addHeader({"path", path});
// 	msg->addHeader({"md5", md5});
// 	msg->addHeader({"file_size", file_size});
// 	msg->addHeader({"uploaded_size", uploaded_size});
// 	msg->addHeader({"server_file_id", server_file_id});
// 	auto token = get_token();
// 	msg->addHeader({TokenHeaderKey, token});
// 	delete[](token);
// 	msg->body_size = file_size - uploaded_size;
// 	send_message(socket, msg);
// 	const int BUFFER_SIZE = 1024;
// 	std::unique_ptr<char[]> buf{new char[BUFFER_SIZE]};
// 	long written{};
// 	fs.seekg(uploaded_size, std::ios_base::beg);
// 	while (1)
// 	{
// 		auto read = msg->body_size - written;
// 		if (read > BUFFER_SIZE)
// 		{
// 			read = BUFFER_SIZE;
// 		}
// 		fs.read(buf.get(), read);
// 		written += socket->write_some(boost::asio::buffer(buf.get(), read), err);
// 		if (err.failed())
// 		{
// 			std::cout << err.value() << " " << err.message() << std::endl;
// 			return false;
// 		}
// 		float percent = ((float)written / msg->body_size) * 100;
// 		std::cout << "\r" << common::string_format("uploaded %d/%d bytes %.2F%%", (long)written, msg->body_size, percent);
// 		assert(written <= msg->body_size);
// 		if (written == msg->body_size)
// 		{
// 			break;
// 		}
// 	}
// 	int size = read_size(socket);
// 	char *header = read_size_n(socket, size);
// 	auto j = json::parse(header);
// 	delete[](header);
// 	header = NULL;
// 	std::cout << "upload file succeed:" << j << std::endl;
// 	return true;
// }
// //message impl
// /*filesync::message::message()
// {
// }
// ilesync::message::message(message &&msg)
// {
// 	this->headers = std::move(msg.headers);
// 	this->body_size = msg.body_size;
// 	this->msg_type = msg.msg_type;
// }*/
// char *filesync::message::to_json() const
// {
// 	nlohmann::json j;
// 	for (auto header : this->headers)
// 	{
// 		header.fill_json(j["Header"]);
// 	}
// 	j["MsgType"] = this->msg_type;
// 	j["BodySize"] = this->body_size;
// 	std::string json_str = j.dump();
// 	return common::strcpy(json_str.c_str());
// }
// filesync::message filesync::message::parse(std::string json)
// {
// 	nlohmann::json j = nlohmann::json::parse(json);
// 	filesync::message msg;
// 	msg.msg_type = j["MsgType"].get<int>();
// 	msg.body_size = j["BodySize"].get<long>();
// 	for (auto &header : j["Header"].items())
// 	{
// 		nlohmann::json val = header.value();
// 		if (val.is_number())
// 		{
// 			msg.addHeader({header.key(), static_cast<int>(val)});
// 		}
// 		else
// 		{
// 			msg.addHeader({header.key(), static_cast<std::string>(val)});
// 		}
// 	}
// 	return msg;
// }
// void filesync::message::addHeader(filesync::message_header value)
// {
// 	headers.push_back(value);
// }
// filesync::message_header::message_header(std::string name, std::string v)
// {
// 	this->name = name;
// 	t = 0;
// 	this->str_v = v;
// }
// filesync::message_header::message_header(std::string name, int v)
// {
// 	this->name = name;
// 	t = 1;
// 	this->int_v = v;
// }
// void filesync::message_header::fill_json(json &j)
// {
// 	if (this->t == 0)
// 		j[this->name] = this->str_v;
// 	else if (this->t == 1)
// 		j[this->name] = this->int_v;
// }
// void filesync::downloader::add_block(filesync::downloader::buffer *buf)
// {
// 	std::lock_guard<std::mutex> guard(mutex);
// 	this->buffers.push(buf);
// }
// filesync::downloader::buffer *filesync::downloader::get_block(long pos)
// {
// 	std::lock_guard<std::mutex> guard(mutex);
// 	if (this->buffers.empty())
// 	{
// 		return NULL;
// 	}
// 	auto buf = this->buffers.front();
// 	this->buffers.pop();
// 	return buf;
// }
// filesync::downloader::buffer::buffer()
// {
// 	this->size = 1024 * 1024 * 1;
// 	this->data_len = 0;
// 	this->pos = -1;
// 	this->b = new char[1024 * 1024 * 1];
// }
// filesync::downloader::buffer::~buffer()
// {
// 	delete[](this->b);
// }
// filesync::downloader::downloader(std::ofstream &out) : buffer_count{1000}, out{out}, finished{false}
// {
// 	//run a thread
// 	t = std::thread(save, std::ref(*this));
// }
// filesync::downloader::~downloader()
// {
// }
// void filesync::downloader::flush()
// {
// 	{
// 		std::lock_guard<std::mutex> guard(this->save_mutex);
// 		this->finished = true;
// 	}
// 	this->t.join();
// }
// void filesync::downloader::save(downloader &downloader)
// {
// 	long written = 0;
// 	bool wait = true;
// 	while (1)
// 	{
// 		auto block = downloader.get_block(written);
// 		if (block)
// 		{
// 			downloader.out.write(block->b, block->data_len);
// 			if (downloader.out.bad())
// 			{
// 				throw_exception("failed to write file");
// 			}
// 			written += block->data_len;
// 			delete (block);
// 		}
// 		else if (!wait)
// 		{
// 			downloader.out.flush();
// 			downloader.out.close();
// 			return;
// 		}
// 		else
// 		{
// 			std::this_thread::sleep_for(std::chrono::milliseconds(100));
// 			std::lock_guard<std::mutex> guard(downloader.save_mutex);
// 			wait = !downloader.finished;
// 		}
// 	}
// }
