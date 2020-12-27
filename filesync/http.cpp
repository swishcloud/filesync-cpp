#include <http.h>
#define USER_AGENT "BY_ALLEN"
#include <common.h>
//#include <Shlwapi.h>
#include <ctype.h>
#include <filesync.h>
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
class http_request
{
public:
	boost_http::request<boost_http::string_body> req;
	http_request(const char *host, const char *port, const char *target)
	{
		req = {boost_http::verb::get, target, 11};
		req.set(boost_http::field::host, host);
		req.set(boost_http::field::user_agent, USER_AGENT);
		req.set(boost_http::field::content_type, "text/html; charset=utf-8");
		req.set(boost_http::field::accept, "application/json");
	}
};

//http::data_block::data_block(const char* name, const int value) :name{ name }, integer_v{ value }, vt{ 1 }
//{
//}
//http::data_block::data_block(const char* name, const char* value) : name{ name }, str_v{ value }, vt{ 2 }
//{
//}
http::http_client::http_client()
{
}
void http::http_client::get(const char *host, const char *port, const char *target)
{
}
bool http::http_client::post(const char *host, const char *port, const char *target, std::vector<http::data_block> data, const char *token, std::string &resp_text)
{
	boost::system::error_code err;
	net::io_context ioc;
	tcp::resolver resolve(ioc);
	ssl::context ctx(ssl::context::tlsv12_client);
	//load_root_certificates(ctx);
	ctx.set_verify_mode(ssl::verify_none);
	auto const results = resolve.resolve(host, port);
	beast::ssl_stream<beast::tcp_stream> stream(ioc, ctx);
	beast::get_lowest_layer(stream).connect(results, err);
	if (err.failed())
	{
		std::cout << err.value() << " " << err.message() << std::endl;
		return false;
	}
	stream.handshake(ssl::stream_base::client);
	char *boundary = "boundary";
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
	boost_http::request<boost_http::string_body> req{boost_http::verb::post, target, 11};
	req.set(boost_http::field::host, host);
	req.set(boost_http::field::user_agent, USER_AGENT);
	req.set(boost_http::field::content_type, common::string_format("multipart/form-data;boundary=\"%s\"", boundary));
	req.set(boost_http::field::accept, "application/json");
	req.set(boost_http::field::content_length, common::string_format("%d", multipart.size()));
	if (token)
		req.set(boost_http::field::authorization, common::string_format("Bearer %s", token));
	boost_http::write(stream, req, err);
	if (err.failed())
	{
		std::cout << err.value() << " " << err.message() << std::endl;
		return false;
	}
	int written = stream.write_some(boost::asio::const_buffer(multipart.c_str(), multipart.size()), err);
	assert(written = multipart.size());
	if (err.failed())
	{
		std::cout << err.value() << " " << err.message() << std::endl;
		return false;
	}
	beast::flat_buffer buffer;
	boost_http::response<boost_http::dynamic_body> res;
	boost_http::read(stream, buffer, res, err);
	if (err.failed())
	{
		std::cout << err.value() << " " << err.message() << std::endl;
		return false;
	}
	stream.shutdown(err);
	// not_connected happens sometimes, so don't bother reporting it.
	auto t = err.message();
	if (err && err != beast::errc::not_connected && err != boost::asio::ssl::error::stream_truncated)
		throw beast::system_error{err};
	resp_text = beast::buffers_to_string(res.body().data());
	std::cout << resp_text << std::endl;
	return true;
}
void _setcomm_h(const boost_http::request<boost_http::string_body> *req)
{
}
bool filesync::http_get(const char *host, const char *port, const char *target, const char *token, std::string &resp_text)
{
	try
	{
		net::io_context ioc;
		tcp::resolver resolve(ioc);
		ssl::context ctx(ssl::context::tlsv12_client);
		//load_root_certificates(ctx);
		ctx.set_verify_mode(ssl::verify_none);
		auto const results = resolve.resolve(host, port);
		beast::ssl_stream<beast::tcp_stream> stream(ioc, ctx);
		beast::get_lowest_layer(stream).connect(results);
		stream.handshake(ssl::stream_base::client);
		boost_http::request<boost_http::string_body> req{boost_http::verb::get, target, 11};
		req.set(boost_http::field::host, host);
		req.set(boost_http::field::user_agent, USER_AGENT);
		req.set(boost_http::field::content_type, "text/html; charset=utf-8");
		req.set(boost_http::field::accept, "application/json");
		if (token)
			req.set(boost_http::field::authorization, common::string_format("Bearer %s", token));
		boost_http::write(stream, req);
		beast::flat_buffer buffer;
		boost_http::response<boost_http::dynamic_body> res;
		boost_http::read(stream, buffer, res);
		beast::error_code ec;
		//stream.shutdown(ec);
		// not_connected happens sometimes, so don't bother reporting it.
		if (ec && ec != beast::errc::not_connected)
			throw beast::system_error{ec};
		resp_text = beast::buffers_to_string(res.body().data());
		return true;
	}
	catch (const std::exception &e)
	{
		std::cerr << "ERROR:" << e.what() << std::endl;
	}
	return false;
}
void http_post()
{
}

http::value::value(const value &a) : vt{a.vt}
{
	this->integer_v = a.integer_v;
	this->str_v = common::strncpy(a.str_v);
}

http::value::value(int v) : vt{1}, integer_v{v}
{
}

http::value::value(const char *v) : vt{2}, str_v{common::strncpy(v)}
{
}
http::value::~value()
{
	delete (this->str_v);
	this->str_v = NULL;
}
const char *http::value::str()
{
	if (this->vt == 1)
	{
		return common::strncpy(common::string_format("%d", this->integer_v).c_str());
	}
	else if (this->vt == 2)
	{
		return this->str_v;
	}
	else
	{
		filesync::EXCEPTION("invalid vt value.");
	}
}

http::data_block::data_block(const char *name, value value) : name{name}, v{value}
{
}
