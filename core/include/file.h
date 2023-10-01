#include <vector>
#include "internal.h"
class FileNode;
class FileNode
{
private:
    /* data */
public:
    filesync::PATH path;
    std::vector<FileNode *> children;
    FileNode(/* args */);
    ~FileNode();
};

class FileChangeManager
{
private:
    FileNode root;

public:
    FileChangeManager();
    void AddChange();
};