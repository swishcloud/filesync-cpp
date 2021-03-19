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

	}
} // namespace filesync
#endif