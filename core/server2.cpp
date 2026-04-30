#include "server.h"
#include "webapi.h"
#include "cfg.h"
#include <fstream>
#define CERT_DIRECTORY_PATH "/var/FILESYNC/CERT/"
#define FILE_SAVE_PATH "/var/FILESYNC/FILE/"
#define TEMP_FILE_PATH "/var/FILESYNC/TEMP/"
#define SERVER_ED25519_PUB_KEY_ID "dc4f6be469a84b5c9a9276acaf99ca24"
#define DOWNLOAD_NON_FULL_FILE true
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
    filesync::SERVER *server;

public:
    PrepareFileHandler(filesync::SERVER *server) : server(server) {}
    int Handle(CLIENT *client, CORE::pMSG msg)
    {
        // parse a MT_PrepareFile msg, the data contains targetId(16bytes), file type(1 byte),  commit_id(32bytes), server_path_length(1byte), server_path(variable length), token_length(1byte), token(variable length)
        char targetId[16];
        char commitId[16];
        int serverPathLen;
        char serverPath[256];
        int tokenLen;
        char token[256];
        int index = 0;
        memcpy(targetId, msg->data + index, 16);
        index += 16;
        int file_type = msg->data[index]; // 0 for share file, 1 for normal file
        index += 1;
        memcpy(commitId, msg->data + index, 16);
        index += 16;
        serverPathLen = msg->data[index];
        if (serverPathLen <= 0 || serverPathLen > 255)
        {
            common::print_info(common::string_format("invalid server path length: %d", serverPathLen));
            return 0;
        }
        serverPath[serverPathLen] = 0;
        index += 1;
        memcpy(serverPath, msg->data + index, serverPathLen);
        index += serverPathLen;
        tokenLen = msg->data[index];
        if (tokenLen <= 0 || tokenLen > 255)
        {
            common::print_info(common::string_format("invalid token length: %d", tokenLen));
            return 0;
        }
        token[tokenLen] = 0;
        index += 1;
        memcpy(token, msg->data + index, tokenLen);
        // check if the file is ready for downloading, if not ready, return false
        std::unique_ptr<char[]> commitIdHex(bytesTohex(commitId, 16));
        const std::string commitIdStr = filesync::addHyphens(std::string(commitIdHex.get(), 32));

        filesync::CONFIG cfg;
        auto err = cfg.load();
        if (err)
        {
            filesync::print_info(err.message());
            return 0;
        }
        IWebAPI *api = new WebAPI(cfg.server_ip, common::string_format("%d", cfg.server_port), std::string(token, sizeof(token)), "");
        filesync::ServerFile sf;
        int res = 0;
        if (file_type)
        {
            res = api->get_file(serverPath, commitIdStr, sf);
        }
        else
        {
            res = api->get_share(serverPath, sf);
        }
        delete api;
        if (!res)
        {
            common::print_debug("Failed to get file info");
            return 0;
        }
        if (!sf.is_completed)
        {
            common::print_debug("File is not ready for downloading");
            return 0;
        }
        if (!DOWNLOAD_NON_FULL_FILE && sf.uploaded_size != sf.size)
        {
            common::print_debug("ERROR:File is not fully uploaded, uploaded size:" + std::to_string(sf.uploaded_size) + ", total size:" + std::to_string(sf.size));
            return 0;
        }
        // associate a download id with the client, and save the mapping of download id and file info in memory for later use.
        const std::string downloadId = filesync::stripHyphen(server->addDownloadingFile(sf));

        // send back the download id to client to notify the client to start downloading file
        std::string sha256 = sf.md5 + sf.md5;
        std::cout << "download id:" << downloadId << ", sha256:" << sha256 << std::endl;
        std::unique_ptr<char[]> sha256Bytes(hexToBytes(sha256.c_str())); // 32 bytes
        char respData[64];                                               // 16 bytes targetId + 16 bytes downloadId+ 32 bytes sha256
        int respIndex = 0;
        memcpy(respData + respIndex, targetId, 16);
        respIndex += 16;
        std::unique_ptr<char[]> downloadIdBytes(hexToBytes(downloadId.c_str())); // 16 bytes
        memcpy(respData + respIndex, downloadIdBytes.get(), 16);
        respIndex += 16;
        memcpy(respData + respIndex, sha256Bytes.get(), 32);
        respIndex += 32;
        client->sendMessage(static_cast<::MsgType>(filesync::MT_PrepareFile), respData, respIndex);
        return 1;
    }
};
class PrepareFileFactory : public HandlerFactory
{
private:
    filesync::SERVER *server;

public:
    PrepareFileFactory(filesync::SERVER *server) : server(server) {}
    MessageHandler *CreateHandler(CORE::pMSG msg)
    {
        return new PrepareFileHandler(server);
    }
};
class RecordFileHandler : public MessageHandler
{

private:
    filesync::SERVER *server;

public:
    RecordFileHandler(filesync::SERVER *server) : server(server) {}
    int Handle(CLIENT *client, CORE::pMSG msg)
    {
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
        std::string filepathHyphen = filesync::addHyphens(std::string(filepathHex.get(), 32));
        std::string serverFileIdHyphen = filesync::addHyphens(std::string(serverFileIdHex.get(), 32));
        const std::string uploadPath = client->pathGenerator->GetUploadPath(std::string(targetId, 16), std::string(sha256Hex.get(), 64), "-");
        const size_t file_size = std::filesystem::file_size(uploadPath);
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
        std::string block_name = common::uuid(); // fake name for block record, since the file is already uploaded, the block record is not important and won't be used.
        int res = api->add_block(serverFileIdHyphen, block_name, 0, file_size);
        if (res)
            res = api->complete_server_file(serverFileIdHyphen);
        delete api;
        if (!res)
        {
            common::print_info("failed to add block or complete server file");
            return 0;
        }
        return 1;
    }
};
class RecordFileFactory : public HandlerFactory
{
private:
    filesync::SERVER *server;

public:
    RecordFileFactory(filesync::SERVER *server) : server(server) {}
    MessageHandler *CreateHandler(CORE::pMSG msg)
    {
        return new RecordFileHandler(server);
    }
};
class FileSyncPathGenerator : public IPathGenerator
{
private:
    filesync::SERVER &server;

public:
    FileSyncPathGenerator(filesync::SERVER &server) : server(server) {}
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
    std::string GetSharedPath(const std::string name)
    {
        std::string downloadId = name;
        std::transform(downloadId.begin(), downloadId.end(), downloadId.begin(), ::tolower);
        downloadId = filesync::addHyphens(downloadId);
        filesync::ServerFile sf;
        if (!server.getDownloadingFile(downloadId, sf))
            return "";
        std::string serverFileName = sf.path;
        std::string path = common::string_format("%s/%s", FILE_SAVE_PATH, serverFileName.c_str());
        if (!common::file_exist(path.c_str()))
        {
            std::transform(serverFileName.begin(), serverFileName.end(), serverFileName.begin(), ::toupper);
            path = common::string_format("%s/%s", FILE_SAVE_PATH, serverFileName.c_str());
        }
        return path;
    }
};
filesync::CLIENT::CLIENT(const std::string &server_host, const int &server_port, filesync::SERVER &server) : client(server_host, server_port), server(server)
{
    delete client.sha256Generator;
    client.sha256Generator = new filesync::SHA256Generator();
    client.getPrivateKeyCB = GetPrivateKeyCB;
    client.edPriId = SERVER_ED25519_PUB_KEY_ID;
    delete client.pathGenerator;
    client.pathGenerator = new FileSyncPathGenerator(server);
    client.emplaceHandleFactory(static_cast<::MsgType>(MT_PrepareFile), new PrepareFileFactory(&server));
    client.emplaceHandleFactory(static_cast<::MsgType>(MT_RecordFile), new RecordFileFactory(&server));
}
void filesync::CLIENT::connect()
{
    if (canConnect)
    {
        th = std::thread([this]()
                         {
                             client.connect(); 
                             client.login("filesync", "supersecret");
                            client.heartbeat(); });
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
