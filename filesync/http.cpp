#include <http.h>
#define USER_AGENT "BY_ALLEN"
#include <common.h>
//#include <Shlwapi.h>
#include <ctype.h>
#include <filesync.h>
#define TIMEOUT 120
using std::placeholders::_1;
using std::placeholders::_2;
#define a bad
enum
{
	max_length = 1024
};
std::string url_encode(const char *str)
{
	int len = strlen(str);
	std::string buf{};
	for (int i = 0; i < len; i++)
	{
		unsigned char c = str[i];
		if (isalnum(c) || c == '-' || c == '.' || c == '_' || c == '~')
		{
			buf.push_back(c);
		}
		else
		{
			buf.append(common::string_format("%%%x", c));
		}
	}
	return buf;
}
void http::UrlValues::add(std::string name, http::value value)
{
	if (!str.empty())
	{
		str.append("&");
	}
	str.append(name).append("=").append(url_encode(value.str()));
}

http::value::value(const value &a) : vt{a.vt}
{
	this->integer_v = a.integer_v;
	this->str_v = common::strcpy(a.str_v);
}

http::value::value(int v) : vt{1}, integer_v{v}
{
}

http::value::value(const char *v) : vt{2}, str_v{common::strcpy(v)}
{
}
http::value::~value()
{
	delete[](this->str_v);
	this->str_v = NULL;
}
const char *http::value::str()
{
	if (this->vt == 1)
	{
		return common::strcpy(common::string_format("%d", this->integer_v).c_str());
	}
	else if (this->vt == 2)
	{
		return this->str_v;
	}
	else
	{
		throw common::exception("invalid vt value.");
	}
}

http::data_block::data_block(const char *name, value value) : name{name}, v{value}
{
}
namespace common
{
	http_client::http_client(std::string host, std::string port, std::string target, std::string token)
		: host{host}, port{port}, target{target}, token{token}
	{
	}
	http_client::http_client(std::string host, int port) : host{host}, port{common::string_format("%d", port)}
	{
	}
	http_client::~http_client() noexcept(false)
	{
	}

	void http_client::GET()
	{
		this->req = {boost_http::verb::get, target, 11};
		this->req.set(boost_http::field::host, host);
		this->req.set(boost_http::field::user_agent, USER_AGENT);
		this->req.set(boost_http::field::content_type, "text/html; charset=utf-8");
		this->req.set(boost_http::field::accept, "application/json");
		this->req.set(boost_http::field::authorization, common::string_format("Bearer %s", token.c_str()));
		connect();
	}
	void http_client::GET(std::string target, std::string parameters, std::string token)
	{
		this->req = {boost_http::verb::get, target + "?" + parameters, 11};
		this->req.set(boost_http::field::host, host);
		this->req.set(boost_http::field::user_agent, USER_AGENT);
		this->req.set(boost_http::field::content_type, "application/x-www-form-urlencoded");
		this->req.set(boost_http::field::accept, "application/json");
		this->req.set(boost_http::field::authorization, common::string_format("Bearer %s", token.c_str()));
		connect();
	}
	void http_client::PUT(std::string target, std::string parameters, std::string token)
	{
		this->req = {boost_http::verb::put, target + "?" + parameters, 11};
		this->req.set(boost_http::field::host, host);
		this->req.set(boost_http::field::user_agent, USER_AGENT);
		this->req.set(boost_http::field::content_type, "application/x-www-form-urlencoded");
		this->req.set(boost_http::field::accept, "application/json");
		this->req.set(boost_http::field::authorization, common::string_format("Bearer %s", token.c_str()));
		connect();
	}
	void http_client::POST(std::vector<http::data_block> data)
	{
		const char *boundary = "boundary";
		std::string multipart;
		for (auto block : data)
		{
			auto part = common::string_format("--%s\nContent-Disposition:form-data;name=\"%s\"\n\n"
											  "%s\n",
											  boundary, block.name, block.v.str());
			//std::cout << part << std::endl;
			multipart.append(part);
		}
		multipart.append(common::string_format("--%s--", boundary));
		this->req = {boost_http::verb::post, target, 11};
		this->req.set(boost_http::field::host, host);
		this->req.set(boost_http::field::user_agent, USER_AGENT);
		this->req.set(boost_http::field::content_type, common::string_format("multipart/form-data;boundary=\"%s\"", boundary));
		this->req.set(boost_http::field::accept, "application/json");
		this->req.set(boost_http::field::content_length, common::string_format("%d", multipart.size()));
		this->req.set(boost_http::field::authorization, common::string_format("Bearer %s", token.c_str()));
		this->req.body() = multipart;
		connect();
	}
	void http_client::POST(std::string target, std::string parameters, std::string token)
	{
		this->req = {boost_http::verb::post, target, 11};
		this->req.set(boost_http::field::host, host);
		this->req.set(boost_http::field::user_agent, USER_AGENT);
		this->req.set(boost_http::field::content_type, "application/x-www-form-urlencoded");
		this->req.set(boost_http::field::accept, "application/json");
		this->req.set(boost_http::field::content_length, common::string_format("%d", parameters.size()));
		this->req.set(boost_http::field::authorization, common::string_format("Bearer %s", token.c_str()));
		this->req.body() = parameters;
		connect();
	}
	void http_client::connect()
	{
		tcp::resolver resolve(ioc);
		ssl::context ctx(ssl::context::tlsv12_client);
		//load_root_certificates(ctx);
		ctx.set_verify_mode(ssl::verify_none);
		auto const results = resolve.resolve(host, port);
		stream.reset(new beast::ssl_stream<beast::tcp_stream>(ioc, ctx));
		beast::get_lowest_layer(*stream.get()).expires_after(std::chrono::seconds(TIMEOUT));
		beast::get_lowest_layer(*stream.get()).async_connect(results, [this](const boost::system::error_code &error, const tcp::endpoint & /*endpoint*/) {
			if (!error)
			{
				//filesync::print_debug("async connection succeed.");
				handshake();
			}
			else
			{
				this->error = common::string_format("async connect failed with error:%s", error.message().c_str());
				filesync::print_debug(this->error);
			}
		});
		this->error = "";
		this->resp_text = "";
		ioc.restart();
		ioc.run();
		delete stream.get();
		stream.release();
	}
	void http_client::handshake()
	{
		(*stream.get()).async_handshake(boost::asio::ssl::stream_base::client, [this](const boost::system::error_code &error) {
			if (!error)
			{
				//filesync::print_debug("async handshake succeed.");
				send_request();
			}
			else
			{
				this->error = common::string_format("async handshake failed with error:%s", error.message().c_str());
				filesync::print_debug(this->error);
			}
		});
	}

	void http_client::send_request()
	{
		boost_http::async_write(*stream.get(), this->req,
								[this](const boost::system::error_code &error, std::size_t length) {
									if (!error)
									{
										//filesync::print_debug("async write succeed.");
										receive_response(length);
									}
									else
									{
										this->error = common::string_format("async write failed with error:%d", error.message().c_str());
										filesync::print_debug(this->error);
									}
								});
	}

	void http_client::receive_response(std::size_t length)
	{
		this->buffer.clear();
		boost_http::response<boost_http::dynamic_body> res;
		this->res = res;
		boost_http::async_read(*stream.get(), this->buffer, this->res, [this](boost::system::error_code const &error, // result of operation
																			  std::size_t bytes_transferred) {
			if (!error)
			{
				//filesync::print_debug("async read succeed.");
				this->resp_text = beast::buffers_to_string(this->res.body().data());
				//filesync::print_debug(common::string_format("HTTP RESPONSE:[%s]", this->resp_text.c_str()));
			}
			else
			{
				this->error = common::string_format("async read failed with error:%s", error.message().c_str());
				filesync::print_debug(this->error);
			}
		});
	}
}; // namespace common