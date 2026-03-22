#ifndef LOGGER_H
#define LOGGER_H
#include <mutex>
#include <common.h>
#include <iostream>
#include "internal.h"
class ILogger
{
public:
    ~ILogger() {

    };
    virtual void error(const char *const msg) = 0;
    virtual void warn(const char *const msg) = 0;
    virtual void info(const char *const msg) = 0;
    virtual void debug(const char *const msg) = 0;
};
class Logger : public ILogger
{
public:
    void error(const char *const msg) override
    {
        print("ERROR", msg);
    }
    void warn(const char *const msg) override
    {
        print("WARN", msg);
    }
    void info(const char *const msg) override
    {
        print("INFO", msg);
    }
    void debug(const char *const msg) override
    {
        print("DEBUG", msg);
    }

private:
    void print(const char *const level, const char *const msg)
    {
        std::lock_guard<std::mutex> guard(filesync::print_mtx);
        std::cout << common::string_format("%s>%s", level, msg) << std::endl;
    }
};
#endif