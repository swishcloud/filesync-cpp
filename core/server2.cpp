#include "server.h"
#include "webapi.h"
#include "cfg.h"
#include <fstream>
#define CERT_DIRECTORY_PATH "/var/FILESYNC/CERT/"
#define FILE_SAVE_PATH "/var/FILESYNC/FILE/"
#define TEMP_FILE_PATH "/var/FILESYNC/TEMP/"
#define SERVER_ED25519_PUB_KEY_ID "dc4f6be469a84b5c9a9276acaf99ca24"
filesync::SERVER::SERVER(const int &port) : server(port, 2)
{
    server.getPublicKeyCB = GetPublicKeyCB;
}
void filesync::SERVER::listen()
{
    // automatically generate ed22519 key pair for server if not exist, and save to CERT_DIRECTORY_PATH with filename SERVER_ED25519_PUB_KEY_ID(.pub for public key, no suffix for private key)
    Ed25519KeyPair ed25519KeyPair;
    std::string pubKeyFile = common::string_format("%s/%s.pub", CERT_DIRECTORY_PATH, SERVER_ED25519_PUB_KEY_ID);
    std::string priKeyFile = common::string_format("%s/%s", CERT_DIRECTORY_PATH, SERVER_ED25519_PUB_KEY_ID);
    if (!std::filesystem::exists(pubKeyFile) || !std::filesystem::exists(priKeyFile))
    {
        common::print_info("ed25519 key pair not found, generating...");
        RawKey32::Bytes ed_pub = ed25519KeyPair.rawPublicKey();
        RawKey32::Bytes ed_priv = ed25519KeyPair.rawPrivateKey();
        std::error_code ec;
        std::filesystem::create_directories(CERT_DIRECTORY_PATH, ec);
        if (ec)
        {
            common::print_info(ec.message());
            return;
        }
        std::ofstream pubOs(pubKeyFile, std::ios::binary);
        if (!pubOs)
        {
            common::print_info(common::string_format("failed to create public key file:%s", pubKeyFile.c_str()));
            return;
        }
        pubOs.write(reinterpret_cast<const char *>(ed_pub.data()), ed_pub.size());
        if (!pubOs)
        {
            common::print_info(common::string_format("failed to write public key file:%s", pubKeyFile.c_str()));
            return;
        }
        std::ofstream priOs(priKeyFile, std::ios::binary);
        if (!priOs)
        {
            common::print_info(common::string_format("failed to create private key file:%s", priKeyFile.c_str()));
            return;
        }
        priOs.write(reinterpret_cast<const char *>(ed_priv.data()), ed_priv.size());
        if (!priOs)
        {
            common::print_info(common::string_format("failed to write private key file:%s", priKeyFile.c_str()));
            return;
        }
        common::print_info("ed25519 key pair generated successfully");
    }

    // listen on socket and handle messages in callback
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
public:
    MessageHandler *CreateHandler(CORE::pMSG msg)
    {
        return new PrepareFileHandler();
    }
};
class RecordFileHandler : public MessageHandler
{
    int Handle(CLIENT *client, CORE::pMSG msg)
    {
        auto addHyphens = [](const std::string &uuid)
        { if (uuid.size() != 32)
            {
                throw std::invalid_argument("UUID must be 32 characters without hyphens");
            }

            return uuid.substr(0, 8) + "-" +
                   uuid.substr(8, 4) + "-" +
                   uuid.substr(12, 4) + "-" +
                   uuid.substr(16, 4) + "-" +
                   uuid.substr(20, 12); };
        std::cout << "RecordFileHandler entered!!!" << std::endl;
        // parse data
        char targetId[16];                            // 16bytes
        char serverFileId[16];                        // 16bytes
        char filepath[16];                            // 16bytes
        char sha256[32];                              // 32bytes
        char token[msg->datalen - 16 - 16 - 16 - 32]; // variable length
        int index = 0;
        memcpy(targetId, msg->data + index, 16);
        index += 16;
        memcpy(serverFileId, msg->data + index, 16);
        index += 16;
        memcpy(filepath, msg->data + index, 16);
        index += 16;
        memcpy(sha256, msg->data + index, 32);
        index += 32;
        memcpy(token, msg->data + index, msg->datalen - index);

        std::unique_ptr<char[]> sha256Hex(bytesTohex(sha256, 32));
        std::unique_ptr<char[]> filepathHex(bytesTohex(filepath, 16));
        std::unique_ptr<char[]> serverFileIdHex(bytesTohex(serverFileId, 16));
        std::string filepathHyphen = addHyphens(std::string(filepathHex.get(), 32));
        std::string serverFileIdHyphen = addHyphens(std::string(serverFileIdHex.get(), 32));
        const std::string uploadPath = client->pathGenerator->GetUploadPath(std::string(targetId, 16), std::string(sha256Hex.get(), 64), "-");
        std::cout << "server file id:" << serverFileIdHyphen << std::endl;
        // If the final file already exists, it means the client has already uploaded the file, signal a warning and return false
        std::string finalPath = common::string_format("%s/%s", FILE_SAVE_PATH, filepathHyphen.c_str());
        if (std::filesystem::exists(finalPath))
        {
            common::print_info("warning: file already exists");
            return 0;
        }
        // move uploaded file to final location FILE_SAVE_PATH
        std::error_code ec;
        std::filesystem::create_directories(FILE_SAVE_PATH, ec);
        if (ec)
        {
            common::print_info(ec.message());
            return 0;
        }
        std::filesystem::rename(uploadPath, finalPath, ec);
        if (ec)
        {
            common::print_info(ec.message());
            return 0;
        }
        // verify sha256
        std::string fileSha256 = (*client->sha256Generator)(finalPath.c_str());
        std::transform(fileSha256.begin(), fileSha256.end(), fileSha256.begin(), ::toupper);
        if (fileSha256 != std::string(sha256Hex.get(), 64))
        {
            common::print_info("sha256 mismatch, deleting file");
            std::filesystem::remove(finalPath);
            return 0;
        }
        // update file record in database to mark the file as uploaded
        filesync::CONFIG cfg;
        auto err = cfg.load();
        if (err)
        {
            filesync::print_info(err.message());
            return 0;
        }
        IWebAPI *api = new WebAPI(cfg.server_ip, common::string_format("%d", cfg.server_port), std::string(token, sizeof(token)), "");
        int res = api->complete_server_file(serverFileIdHyphen);
        delete api;
        if (!res)
        {
            common::print_info("failed to complete server file");
            return 0;
        }
        return 1;
    }
};
class RecordFileFactory : public HandlerFactory
{
    MessageHandler *CreateHandler(CORE::pMSG msg)
    {
        return new RecordFileHandler();
    }
};
class FileSyncPathGenerator : public IPathGenerator
{
public:
    std::string GetUploadPath(const std::string targetId, const std::string sha256, const std::string filename)
    {
        std::unique_ptr<char[]> targetID(bytesTohex(targetId.c_str(), 16));
        // choose TEMP_FILE_PATH as destination directory, and concatenate targetId, sha256 and filename to generate the final path
        std::string path = common::string_format("%s/%s/%s/%s", TEMP_FILE_PATH, std::string(targetID.get(), 32).c_str(), sha256.c_str(), filename.c_str());
        // create parent directories if not exist
        std::error_code ec;
        std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
        if (ec)
        {
            common::print_info(ec.message());
            throw std::runtime_error("failed to create directories for upload path");
        }
        return path;
    }
    std::string GetSharedPath(const std::string filename)
    {
        throw std::runtime_error("shared path is not supported in FileSyncPathGenerator");
    }
};
filesync::CLIENT::CLIENT(const std::string &server_host, const int &server_port) : client(server_host, server_port)
{
    delete client.sha256Generator;
    client.sha256Generator = new filesync::SHA256Generator();
    client.getPrivateKeyCB = GetPrivateKeyCB;
    client.edPriId = SERVER_ED25519_PUB_KEY_ID;
    delete client.pathGenerator;
    client.pathGenerator = new FileSyncPathGenerator();
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
int filesync::SERVER::GetPublicKeyCB(const char *id, RawKey32::Bytes &key)
{
    if (std::string(id, 32) != SERVER_ED25519_PUB_KEY_ID)
    {
        std::cout << "invalid public key id:" << id << std::endl;
        return 0;
    }
    std::string filename = common::string_format("%s/%s.pub", CERT_DIRECTORY_PATH, SERVER_ED25519_PUB_KEY_ID);
    std::ifstream is(filename, std::ios::binary);
    if (!is)
    {
        std::cout << "failed to open public key file:" << filename << std::endl;
        return 0;
        if (std::string(id, 32) != SERVER_ED25519_PUB_KEY_ID)
        {
            std::cout << "invalid public key id:" << id << std::endl;
            return 0;
        }
        std::string filename = common::string_format("%s/%s.pub", CERT_DIRECTORY_PATH, SERVER_ED25519_PUB_KEY_ID);
        std::ifstream is(filename, std::ios::binary);
        if (!is)
        {
            std::cout << "failed to open public key file:" << filename << std::endl;
            return 0;
        }
        is.read(reinterpret_cast<char *>(key.data()), key.size());
        if (!is)
        {
            std::cout << "failed to read public key file:" << filename << std::endl;
            return 0;
        }
        return 1;
    }
    is.read(reinterpret_cast<char *>(key.data()), key.size());
    if (!is)
    {
        std::cout << "failed to read public key file:" << filename << std::endl;
        return 0;
    }
    return 1;
}
int filesync::CLIENT::GetPrivateKeyCB(const char *id, RawKey32::Bytes &key)
{

    if (std::string(id, 32) != SERVER_ED25519_PUB_KEY_ID)
    {
        std::cout << "invalid public key id:" << id << std::endl;
        return 0;
    }
    std::string filename = common::string_format("%s/%s", CERT_DIRECTORY_PATH, SERVER_ED25519_PUB_KEY_ID);
    std::ifstream is(filename, std::ios::binary);
    if (!is)
    {
        std::cout << "failed to open private key file:" << filename << std::endl;
        return 0;
    }
    is.read(reinterpret_cast<char *>(key.data()), key.size());
    if (!is)
    {
        std::cout << "failed to read public key file:" << filename << std::endl;
        return 0;
    }
    return 1;
}
