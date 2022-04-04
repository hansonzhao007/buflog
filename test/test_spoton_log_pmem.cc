#include <fcntl.h>
#include <libpmemobj.h>
#include <stdio.h>
#include <unistd.h>

#include <algorithm>
#include <iostream>
#include <thread>
#include <vector>

#include "spoton_log.h"

struct LogRoot {
    size_t num;
    PMEMoid log_addrs[16];
};

int main (int argc, char** argv) {
    if (argc != 2) {
        printf (
            "usage: %s 0|1 \n0:write to records to new logs. 1: read records from existing logs\n",
            argv[0]);
        exit (1);
    }

    std::vector<std::thread> workers;

    uint64_t mode = std::atoll (argv[1]);
    uint16_t log_count = 8;
    size_t kLogSize = 10 * 1024 * 1024 * 1024LU;  // 10G

    std::string filepath = "/mnt/pmem/testlog";
    PMEMobjpool* pop;

    if (access (filepath.c_str (), F_OK) != 0) {
        // the file dost not exist
        pop = pmemobj_create (filepath.c_str (), POBJ_LAYOUT_NAME (log), kLogSize, 0666);
        if (pop == nullptr) {
            std::cerr << "create log file fail. " << filepath << std::endl;
            exit (1);
        }
    } else {
        pop = pmemobj_open (filepath.c_str (), POBJ_LAYOUT_NAME (log));
        if (pop == nullptr) {
            std::cerr << "open error. " << filepath << std::endl;
            exit (1);
        }
    }
    PMEMoid g_root = pmemobj_root (pop, sizeof (LogRoot));
    auto* root = reinterpret_cast<LogRoot*> (pmemobj_direct (g_root));

    if (mode == 0) {
        // create new log and append data to them
        root->num = log_count;
        for (int i = 0; i < log_count; i++) {
            workers.emplace_back (std::thread ([&pop, &root, i] () {
                // alloc 10 MB log space
                size_t log_size = 16 * 1024 * 1024;
                int ret = pmemobj_alloc (pop, &root->log_addrs[i], log_size, 0, NULL, NULL);
                if (ret) {
                    std::cerr << "alloc error for log " << i << std::endl;
                    exit (1);
                }

                // register current log address
                void* log_base_addr = pmemobj_direct (root->log_addrs[i]);
                spoton::Log_t::RegisterThreadLocalLog ((char*)(log_base_addr), log_size);

                // Append 10 records
                auto* log_ptr = spoton::Log_t::GetThreadLocalLog ();
                spoton::logptr_t ptr;
                for (int r = 0; r < 10; r++) {
                    ptr = log_ptr->Append (spoton::kLogNodeValid,
                                           "log data test" + std::to_string (r), ptr);
                }
                log_ptr->CommitTail ();
            }));
        }
        std::for_each (workers.begin (), workers.end (), [] (std::thread& t) { t.join (); });
    } else if (mode == 1) {
        log_count = root->num;
        std::cout << "read " << log_count << " logs" << std::endl;

        for (int i = 0; i < log_count; i++) {
            workers.push_back (std::thread ([&root, i] () {
                // open current log address
                void* log_base_addr = pmemobj_direct (root->log_addrs[i]);
                spoton::Log_t::OpenThreadLocalLog (i, (char*)(log_base_addr));

                // read log records
                auto* log_ptr = spoton::Log_t::GetThreadLocalLog ();

                auto iter = log_ptr->begin ();
                int r = 0;
                while (iter->Valid ()) {
                    printf ("log %d: iter entry %d: %s\n", i, r++, iter->ToString ().c_str ());
                    ++iter;
                }

                // test log reverse iter
                auto riter = log_ptr->rbegin ();
                r = 0;
                while (riter->Valid ()) {
                    printf ("log %d: riter entry %d: %s\n", i, r++, riter->ToString ().c_str ());
                    --riter;
                }

                // test logptr_t Next
                spoton::LogNode_t* log_node = &(*log_ptr->begin ());
                r = 0;
                while (log_node && log_node->Valid ()) {
                    printf ("log %d: next entry %d: %s\n", i, r++, log_node->ToString ().c_str ());
                    log_node = log_node->Next ();
                }

                // test logptr_t ListNext
                log_node = &(*log_ptr->rbegin ());
                r = 0;
                while (log_node && log_node->Valid ()) {
                    printf ("log %d: ListNext %d: %s\n", i, r++, log_node->ToString ().c_str ());
                    log_node = log_node->ListNext ();
                }
            }));
        }
        std::for_each (workers.begin (), workers.end (), [] (std::thread& t) { t.join (); });
    }

    return 0;
}