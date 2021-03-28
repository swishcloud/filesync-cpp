#ifndef SERVER_H
#define SERVER_H
#include <iostream>
#include <boost/asio.hpp>
#include <boost/bind/bind.hpp>
#include <memory>
#include <vector>
#include <internal.h>
#include <thread>
#include <future>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <http.h>
#include "tcp.h"
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
using namespace nlohmann;
using boost::asio::ip::tcp;
#define TokenHeaderKey "access_token"
#define FILESYNC_HANDLER_TYPE_REQUIREMENTS_ASSERT(HANDLER, P) static_assert(std::is_convertible<decltype(std::declval<HANDLER>()(P)), void>::value, "template parameter error");
#define FILESYNC_HANDLER_TYPE_REQUIREMENTS_ASSERT2(HANDLER, P1, P2) static_assert(std::is_convertible<decltype(std::declval<HANDLER>()(P1, P2)), void>::value, "template parameter error");
#define FILESYNC_HANDLER_TYPE_REQUIREMENTS_ASSERT3(HANDLER, P1, P2, P3) static_assert(std::is_convertible<decltype(std::declval<HANDLER>()(P1, P2, P3)), void>::value, "template parameter error");

namespace filesync
{
    class server
    {
    private:
        //private data
        common::http_client &http_client;
        std::string ip;
        short port;
        std::unique_ptr<std::thread> thread;
        std::string file_location;
        //private functions
        void do_accept();
        void async_receive_file(XTCP::message &msg, XTCP::tcp_session *s);
        std::string get_file_path(std::string name);
        std::string get_block_path(std::string name);
        void process_message(XTCP::tcp_session *session, XTCP::message &msg, std::function<void(common::error error)> cb);
        XTCP::tcp_server tcp_server;

    public:
        server(short port, std::string file_location, common::http_client &http_client);
        ~server();
        void listen();
        void receive(XTCP::tcp_session *s);
        void async_receive_file_v2(XTCP::message &msg, XTCP::tcp_session *s);
    };
    class tcp_client
    {
    private:
        std::string server_host;
        std::string server_port;

    public:
        bool closed;
        XTCP::tcp_client xclient;
        tcp_client(std::string server_host, std::string server_port);
        ~tcp_client();
        bool connect();
        void send_file(std::string path, size_t offset = 0);
    };
} // namespace filesync
#endif