#ifndef SERVER_H
#define SERVER_H
#include <iostream>
#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <memory>
#include <vector>
#include <internal.h>
#include <thread>
#include <future>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <http.h>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
using namespace nlohmann;
using boost::asio::ip::tcp;
#define TokenHeaderKey "access_token"
#define FILESYNC_HANDLER_TYPE_REQUIREMENTS_ASSERT(HANDLER, P) static_assert(std::is_convertible<decltype(std::declval<HANDLER>()(P)), void>::value, "template parameter error");
#define FILESYNC_HANDLER_TYPE_REQUIREMENTS_ASSERT2(HANDLER, P1, P2) static_assert(std::is_convertible<decltype(std::declval<HANDLER>()(P1, P2)), void>::value, "template parameter error");
#define FILESYNC_HANDLER_TYPE_REQUIREMENTS_ASSERT3(HANDLER, P1, P2, P3) static_assert(std::is_convertible<decltype(std::declval<HANDLER>()(P1, P2, P3)), void>::value, "template parameter error");
namespace common
{
    namespace socket
    {

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
                return 0;
            }
        };

    } // namespace socket
} // namespace common
namespace filesync
{
    enum class MsgType
    {
        File = 1000,
        Reply = 1001,
        UploadFile = 1002,
        Download_File = 8
    };

    class session
    {

    private:
        //typedef void (*OnReadSize)(size_t sizes);
        //typedef void (*OnReadChunk)(std::ostream &os, size_t read);
        tcp::socket socket;
        const size_t BUF_SIZE = 1024;
        std::stringstream size_ss;
        std::stringstream msg_ss;
        //private functions

    public:
        std::unique_ptr<char[]> buf{};
        session(tcp::socket socket);
        ~session();
        //session(session &&s);
        bool has_closed;
        bool is_closed();
        template <typename OnReadSize>
        void async_read_size(OnReadSize &&onReadSize);
        template <typename OnReadChunk>
        void async_read_chunk(std::size_t size, OnReadChunk &&onReadChunk)
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
        };
        template <typename OnSentMsg>
        void async_send_message(const common::socket::message &msg, OnSentMsg &&onSentMsg);
        template <typename OnReadMsg>
        void async_read_message(OnReadMsg &&onReadMsg);
        template <typename OnWrote>
        void async_write(const char *data, std::size_t size, OnWrote &&onSentMsg);
        template <typename OnSentStream>
        void async_send_stream(std::shared_ptr<std::istream> fs, OnSentStream &&onSentStream);
        void send_file(std::string path, size_t offset);
        bool write(const char *data, std::size_t size);
        common::socket::message read_message();
        bool send_message(common::socket::message &msg);
        void close();
        //void poll();
    };
    class server
    {
    private:
        //private data
        common::http_client &http_client;
        short port;
        boost::asio::io_context io_context;
        tcp::acceptor acceptor_;
        std::vector<session*> sessions;
        std::unique_ptr<std::thread> thread;
        std::string file_location;
        //private functions
        void do_accept();
        void async_receive_file(common::socket::message &msg, session *s);
        std::string get_file_path(std::string name);
        std::string get_block_path(std::string name);

    public:
        server(short port, std::string file_location, common::http_client &http_client);
        ~server();
        void listen();
        void receive(session *s);
        void async_receive_file_v2(common::socket::message &msg, session *s);
    };
    class tcp_client
    {
    private:
        std::string server_host;
        std::string server_port;
        std::unique_ptr<std::thread> thread;
        boost::asio::any_io_executor work;

    public:
        std::unique_ptr<session> session_;
        tcp_client(std::string server_host, std::string server_port);
        ~tcp_client();
        void connect();
        void send_file(std::string path, size_t offset = 0);
    };
} // namespace filesync
#endif