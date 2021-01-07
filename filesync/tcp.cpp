#include <tcp.h>
#define TokenHeaderKey "access_token"
using namespace nlohmann;
using namespace boost::asio;
int read_size(ip::tcp::socket *socket)
{
	const int max_size_str_len = 5;
	char *size_str = new char[max_size_str_len + 1]{};
	for (int i = 0; i < max_size_str_len; i++)
	{
		char *data = new char[1]{};
		int size = socket->receive(boost::asio::buffer(data, 1));
		if (size != 1)
		{
			filesync::EXCEPTION("unknown exception.");
		}
		size_str[i] = data[0];
		if (size_str[i] == '\0')
		{
			break;
		}
		delete (data);
	}
	int size;
	std::stringstream str;
	str << size_str;
	str >> size;
	delete (size_str);
	return size;
}
char *read_size_n(ip::tcp::socket *socket, int size)
{
	char *data = new char[size + 1];
	int read = 0;
	while (1)
	{
		boost::asio::mutable_buffer buffer(data + read, size - read);
		int size = socket->receive(buffer);
		read += size;
		data[read] = '\0';
		std::cout << data << std::endl;
		if (read == size)
		{
			break;
		}
	}
	return data;
}
void send_message(ip::tcp::socket *socket, filesync::message *msg)
{
	try
	{
		//send the size portion
		auto json = msg->to_json();
		int json_len = strlen(json);
		std::string json_len_str = common::string_format("%d", json_len);
		int size = socket->write_some(const_buffer{json_len_str.c_str(), json_len_str.size()});
		assert(size = json_len_str.size());
		//send NULL to separate size portion and header portion.
		size = socket->write_some(const_buffer{"\0", 1});
		assert(size == 1);
		//send the json portion.
		size = socket->write_some(const_buffer{json, (size_t)json_len});
		delete (json);
		assert(size == json_len);
	}
	catch (const std::exception &e)
	{
		std::cout << e.what() << std::endl;
	}
}
void receive_message(ip::tcp::socket *socket, filesync::PartitionConf &conf)
{
	int size = read_size(socket);
	char *header = read_size_n(socket, size);
	auto j = json::parse(header);
	delete (header);
	header = NULL;
	try
	{
		conf.max_commit_id = j["Header"]["max_commit_id"].get<std::string>();
		conf.first_commit_id = j["Header"]["first_commit_id"].get<std::string>();
		conf.partition_id = j["Header"]["partition_id"].get<std::string>();
		;
	}
	catch (const std::exception &e)
	{
		std::cout << e.what() << std::endl;
	}
}
ip::tcp::endpoint filesync::resolve(std::string host, int port)
{
	boost::asio::io_service io_service;
	boost::asio::ip::tcp::resolver resolver(io_service);
	boost::asio::ip::tcp::resolver::query query(host, common::string_format("%d", port));
	boost::asio::ip::tcp::resolver::iterator iter = resolver.resolve(query);
	return iter->endpoint();
}
void filesync::connect(FileSync *filesync)
{
	try
	{
		io_context my_io_context{};
		ip::tcp::socket *socket = new ip::tcp::socket{my_io_context};
		boost::system::error_code err;
		//beast::get_lowest_layer(*socket).expire_after(std::chrono::seconds(10));
		socket->connect(resolve(filesync->cfg.server_ip, filesync->cfg.server_tcp_port), err);
		if (err.failed())
		{
			socket->close();
			delete (socket);
			filesync::EXCEPTION(err.message());
		}
		auto msg = new message();
		char *token = get_token();
		msg->addHeader({"token", token});
		delete (token);
		auto json = msg->to_json();
		send_message(socket, msg);
		receive_message(socket, filesync->conf);
		filesync->cfg.save();
		filesync->conf.init();
		socket->close();
		delete (socket);
		socket = NULL;
		delete (msg);
		msg = NULL;
	}
	catch (const std::exception &e)
	{
		std::cout << e.what() << std::endl;
		int delay = 10000;
		std::cout << "reconnect in " << delay / 1000 << "s" << std::endl;
		std::this_thread::sleep_for(std::chrono::milliseconds(delay));
		connect(filesync);
	}
}
const int MT_Download_File = 8;
const int MT_FILE = 3;
bool filesync::download_file(const char *ip, unsigned short port, const char *path, const char *md5, const char *local_md5, File &file)
{
	if (common::file_exist(file.full_path.c_str()) != 0)
	{
		auto f_md5 = file_md5(file.full_path.c_str());
		if (filesync::compare_md5(f_md5.c_str(), md5))
		{
			return true;
		}
		else if (filesync::compare_md5(f_md5.c_str(), local_md5))
		{
			file.status = FileStatus::Conflict;
			std::cout << "file confict:" << file.full_path << std::endl;
			return false;
		}
	}

	io_context my_io_context{};
	ip::tcp::socket *socket = new ip::tcp::socket{my_io_context};
	boost::system::error_code err;
	socket->connect(ip::tcp::endpoint{ip::address::from_string(ip), port}, err);
	if (err.failed())
	{
		std::cout << err.value() << " " << err.message() << std::endl;
		return false;
	}
	auto msg = new message();
	msg->msg_type = MT_Download_File;
	msg->addHeader({"path", common::strncpy(path)});
	auto json = msg->to_json();
	send_message(socket, msg);

	//begin receiving
	int size = read_size(socket);
	char *header = read_size_n(socket, size);
	auto j = json::parse(header);
	delete (header);
	header = NULL;
	long body_size = j["BodySize"].get<long>();
	auto parent = get_parent_dir(file.full_path.c_str());
	if (!std::filesystem::exists(parent))
		if (!std::filesystem::create_directories(parent))
			std::cout << "Failed to create directory " << parent << std::endl;
	std::ofstream outfile{file.full_path, std::ios::binary};
	//int buf_size = 1024 * 1024 * 1.5;//1.5MB
	bool downloaded = false;
	std::cout << "downloading " << file.full_path << std::endl;
	downloader downloader{outfile};
	long read = 0;
	try
	{
		if (body_size > 0)
			while (1)
			{
				auto block = new filesync::downloader::buffer();
				assert(block);
				block->pos = read;
				block->data_len = socket->read_some(buffer(block->b, block->size));
				read += block->data_len;
				if (outfile.bad())
				{
					filesync::EXCEPTION("writing the file failed.");
				}
				downloader.add_block(block);
				float percent = ((float)read / body_size) * 100;
				std::cout << "\r" << common::string_format("%d/%d bytes %.2F%%", (long)read, body_size, percent);
				if (read == body_size)
				{
					break;
				}
			}
		std::cout << std::endl;
		downloader.flush();
		downloaded = true;
	}
	catch (const std::exception &e)
	{
		std::cout << std::endl
				  << e.what() << std::endl;
	}
	if (downloaded && !compare_md5(md5, file_md5(file.full_path.c_str()).c_str()))
	{
		std::cout << "the MD5 of the downloaded file is inconsistent with the MD5 of the file on the server,deleting the downloaded file." << std::endl;
		if (!std::filesystem::remove(file.full_path))
		{
			filesync::EXCEPTION("deleting the file failed.");
		}
	}
	else
	{
		return true;
	}
	return false;
}
bool filesync::upload_file(const char *ip, unsigned short port, std::ifstream &fs, const char *path, const char *md5, long file_size, long uploaded_size, const char *server_file_id)
{
	io_context my_io_context{};
	ip::tcp::socket *socket = new ip::tcp::socket{my_io_context};
	boost::system::error_code err;
	socket->connect(ip::tcp::endpoint{ip::address::from_string(ip), port}, err);
	if (err.failed())
	{
		std::cout << err.value() << " " << err.message() << std::endl;
		return false;
	}
	auto msg = new message();
	msg->msg_type = MT_FILE;
	msg->addHeader({"path", path});
	msg->addHeader({"md5", md5});
	msg->addHeader({"file_size", file_size});
	msg->addHeader({"uploaded_size", uploaded_size});
	msg->addHeader({"server_file_id", server_file_id});
	auto token = get_token();
	msg->addHeader({TokenHeaderKey, token});
	delete (token);
	msg->body_size = file_size - uploaded_size;
	auto json = msg->to_json();
	send_message(socket, msg);
	const int BUFFER_SIZE = 1024;
	char *buf = new char[BUFFER_SIZE];
	long written{};
	fs.seekg(uploaded_size, std::ios_base::beg);
	while (1)
	{
		auto read = msg->body_size - written;
		if (read > BUFFER_SIZE)
		{
			read = BUFFER_SIZE;
		}
		fs.read(buf, read);
		written += socket->write_some(boost::asio::buffer(buf, read), err);
		if (err.failed())
		{
			std::cout << err.value() << " " << err.message() << std::endl;
			return false;
		}
		assert(written <= msg->body_size);
		if (written == msg->body_size)
		{
			break;
		}
	}
	int size = read_size(socket);
	char *header = read_size_n(socket, size);
	auto j = json::parse(header);
	delete (header);
	header = NULL;
	std::cout << "upload file succeed:" << j << std::endl;
	return true;
}
char *filesync::message::to_json()
{
	nlohmann::json j;
	for (auto header : this->headers)
	{
		header.fill_json(j["Header"]);
	}
	j["MsgType"] = this->msg_type;
	j["BodySize"] = this->body_size;
	std::string json_str = j.dump();
	return common::strncpy(json_str.c_str());
}
void filesync::message::addHeader(filesync::message_header value)
{
	headers.push_back(value);
}
filesync::message_header::message_header(std::string name, std::string v)
{
	this->name = name;
	t = 0;
	this->str_v = v;
}
filesync::message_header::message_header(std::string name, int v)
{
	this->name = name;
	t = 1;
	this->int_v = v;
}
void filesync::message_header::fill_json(json &j)
{
	if (this->t == 0)
		j[this->name] = this->str_v;
	else if (this->t == 1)
		j[this->name] = this->int_v;
}
void filesync::downloader::add_block(filesync::downloader::buffer *buf)
{
	std::lock_guard<std::mutex> guard(mutex);
	this->buffers.push(buf);
}
filesync::downloader::buffer *filesync::downloader::get_block(long pos)
{
	std::lock_guard<std::mutex> guard(mutex);
	if (this->buffers.empty())
	{
		return NULL;
	}
	auto buf = this->buffers.front();
	this->buffers.pop();
	return buf;
}
filesync::downloader::buffer::buffer()
{
	this->size = 1024 * 1024 * 1;
	this->data_len = 0;
	this->pos = -1;
	this->b = new char[1024 * 1024 * 1];
}
filesync::downloader::buffer::~buffer()
{
	delete (this->b);
}
filesync::downloader::downloader(std::ofstream &out) : buffer_count{1000}, out{out}, finished{false}
{
	//run a thread
	t = std::thread(save, std::ref(*this));
}
filesync::downloader::~downloader()
{
}
void filesync::downloader::flush()
{
	{
		std::lock_guard<std::mutex> guard(this->save_mutex);
		this->finished = true;
	}
	this->t.join();
}
void filesync::downloader::save(downloader &downloader)
{
	long written = 0;
	bool wait = true;
	while (1)
	{
		auto block = downloader.get_block(written);
		if (block)
		{
			downloader.out.write(block->b, block->data_len);
			if (downloader.out.bad())
			{
				throw_exception("failed to write file");
			}
			written += block->data_len;
			delete (block);
		}
		else if (!wait)
		{
			downloader.out.flush();
			downloader.out.close();
			return;
		}
		else
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
			std::lock_guard<std::mutex> guard(downloader.save_mutex);
			wait = !downloader.finished;
		}
	}
}