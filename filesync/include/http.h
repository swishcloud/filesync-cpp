#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/version.hpp>
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/error.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <boost/iostreams/code_converter.hpp>
#include <boost/locale.hpp>
#include <cstdlib>
#include <iostream>
#include <string>
namespace beast = boost::beast;		// from <boost/beast.hpp>
namespace boost_http = beast::http; // from <boost/beast/http.hpp>
namespace net = boost::asio;		// from <boost/asio.hpp>
namespace ssl = net::ssl;			// from <boost/asio/ssl.hpp>
using tcp = net::ip::tcp;			// from <boost/asio/ip/tcp.hpp>
namespace filesync
{
} // namespace filesync

namespace http
{
	class value
	{
	private:
		const int vt{};
		int integer_v{};
		char *str_v{};

	public:
		value(const value &a);
		value(int v);
		value(const char *v);
		~value();

	public:
		const char *str();
	};
	struct data_block
	{
		const char *name;
		value v;
		data_block(const char *name, value value);
	};
	class http_client
	{
	public:
		http_client();
		bool post(const char *host, const char *port, const char *target, std::vector<http::data_block> data, const char *token, std::string &resp_text);
	};
} // namespace http
std::string url_encode(const char *str);
namespace common
{
	class http_client
	{

	public:
		http_client(std::string host, std::string port, std::string target, std::string token);
		~http_client() noexcept(false);

		void GET();
		void POST(std::vector<http::data_block> data);
		std::string resp_text;
		std::string error;

	private:
		HANDLE handle;
		std::string host;
		std::string port;
		std::string target;
		std::string token;
		std::unique_ptr<beast::ssl_stream<beast::tcp_stream>> stream;
		boost_http::request<boost_http::string_body> req;
		beast::flat_buffer buffer;
		boost_http::response<boost_http::dynamic_body> res;
		net::io_context ioc;

		void connect();
		void handshake();

		void send_request();

		void receive_response(std::size_t length);
	};
} // namespace common