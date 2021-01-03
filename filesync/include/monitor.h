#ifndef MONITOR_H
#define MONITOR_H
#include <iostream>
#include <filesystem>
#include <Windows.h>
namespace filesync
{
    namespace monitor
    {
        enum class change_type
        {
            added,
            removed,
            modified,
            renamed
        };
        struct change
        {
            change_type ct;
            std::string path;
        };
        class MONITOR
        {
        public:
            std::string path_to_watch;
            using onchange = void (*)(change *change, void *obj);
            onchange onchange_cb;
            void *obj;
            MONITOR(onchange onchange, void *obj, std::string path_to_watch);
            void watch();
            void ReadDirectoryChangesW();

        private:
        };
    } // namespace monitor
} // namespace filesync
#endif