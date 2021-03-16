#ifndef TCP_Ha
#define TCP_Ha
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/asio/connect.hpp>
#include <boost/system/error_code.hpp>
#include <boost/system/system_error.hpp>
#include <boost/asio/basic_stream_socket.hpp>
#include <iostream>
#include <fstream>
#include <sstream>
#include <nlohmann/json.hpp>
#include <common.h>
#include <thread>
#include <queue>
#include <chrono>
#include <TCP/tcp.h>
using namespace nlohmann;
using namespace boost::asio;
namespace beast = boost::beast; // from <boost/beast.hpp>
namespace filesync
{
	namespace tcp
	{
		enum class MsgType
		{
			File = 1000,
			Reply = 1001,
			UploadFile = 1002,
			Download_File = 8
		};
		struct message_header
		{
		private:
			std::string str_v;
			size_t int_v;
			int t;

		public:
			std::string name;
			message_header(std::string name, std::string v);
			message_header(std::string name, size_t v);
			void fill_json(json &j);
			template <class T>
			T getValue() const
			{
				if (std::is_integral<T>::value)
				{
					return static_cast<T>(*(T *)(&this->int_v));
				}
				else
				{
					return static_cast<T>(*(T *)(&this->str_v));
				}
			}
		};
		class message
		{
		private:
			std::vector<message_header> headers{};

		public:
			//message();
			//message(message &&msg);
			int msg_type{0};
			size_t body_size{0};
			char *to_json() const;
			operator bool() const;
			static message parse(std::string json);
			void addHeader(message_header value);

			template <typename T>
			T getHeaderValue(std::string name) const
			{
				for (auto &h : this->headers)
				{
					if (strcmp(h.name.c_str(), name.c_str()) == 0)
					{
						return h.getValue<T>();
					}
				}
				return T();
			}
		};

		void send_message(XTCP::tcp_session *session, message &msg, std::function<void(bool success)> on_sent);
		void read_message(XTCP::tcp_session *session, message &msg, std::function<void(bool success, message &msg)> on_read);
	}
} // namespace filesync
/*

				outfile.write(buf, socket->read_some(buffer(buf, buf_size)));*/
#endif