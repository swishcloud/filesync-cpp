#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/asio/connect.hpp>
#include <boost/system/error_code.hpp>
#include <boost/system/system_error.hpp>
#include <filesync.h>
#include <boost/asio/basic_stream_socket.hpp>
#include <iostream>
#include <fstream>
#include <sstream>
#include <nlohmann/json.hpp>
#include <common.h>
#include <thread>
#include <queue>
#include <chrono>
using namespace nlohmann;
using namespace boost::asio;
namespace beast = boost::beast; // from <boost/beast.hpp>
namespace filesync
{
	struct message_header
	{
	private:
		std::string name;
		std::string str_v;
		int int_v;
		int t;

	public:
		message_header(std::string name, std::string v);
		message_header(std::string name, int v);
		void fill_json(json &j);
	};
	class message
	{
	private:
		std::vector<message_header> headers{};

	public:
		int msg_type;
		long body_size;
		char *to_json();
		void addHeader(message_header value);
	};
	class downloader
	{
	public:
		struct buffer
		{
			char *b;
			int size;
			int data_len;
			long pos;
			buffer();
			~buffer();
		};
		downloader(std::ofstream &out);
		~downloader();
		void add_block(buffer *buf);
		buffer *get_block(long pos);
		void flush();

	private:
		std::thread t;
		std::ofstream &out;
		bool finished;
		std::mutex mutex;
		std::mutex save_mutex;
		std::queue<buffer *> buffers;
		const int buffer_count;
		static void save(downloader &downloader);
	};
	class tcp_client;
	void connect(FileSync *filesync);
	ip::tcp::endpoint resolve(std::string host, int port);
	bool download_file(const char *ip, unsigned short port, const char *path, const char *md5, const char *local_md5, filesync::File &file);
	bool upload_file(const char *ip, unsigned short port, std::ifstream &fs, const char *path, const char *md5, long file_size, long uploaded_size, const char *server_file_id);
} // namespace filesync
/*

				outfile.write(buf, socket->read_some(buffer(buf, buf_size)));*/
