#include "server.h"
filesync::SERVER::SERVER(const int &port) : server(port, 2)
{
}
void filesync::SERVER::listen()
{
    server.listen();
}
filesync::CLIENT::~CLIENT()
{
    if (th.joinable())
        th.join();
}
class PrepareFileHandler : public MessageHandler
{
    int Handle(CLIENT *client, CORE::pMSG msg)
    {
        return 0;
    }
};
class PrepareFileFactory : public HandlerFactory
{
    MessageHandler *CreateHandler(CORE::pMSG msg)
    {
        return new PrepareFileHandler();
    }
};
class RecordFileHandler : public MessageHandler
{
    int Handle(CLIENT *client, CORE::pMSG msg)
    {
        return 0;
    }
};
class RecordFileFactory : public HandlerFactory
{
    MessageHandler *CreateHandler(CORE::pMSG msg)
    {
        return new RecordFileHandler();
    }
};
filesync::CLIENT::CLIENT(const std::string &server_host, const int &server_port) : client(server_host, server_port)
{
    client.emplaceHandleFactory(static_cast<::MsgType>(MT_PrepareFile), new PrepareFileFactory());
    client.emplaceHandleFactory(static_cast<::MsgType>(MT_RecordFile), new RecordFileFactory());
}
void filesync::CLIENT::connect()
{
    if (canConnect)
    {
        th = std::thread([this]()
                         {
                             client.connect(); 
                             client.login("xx", "xx"); });
        canConnect = false;
    }
    else
        throw std::runtime_error("Cannot connect to server again");
}