#include <libpmemobj.h>
#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>

#include <algorithm>
#include <chrono>
#include <iostream>
#include <random>
#include <thread>

using namespace std;
#include "spoton_log.h"

struct LogRoot {
    size_t num;
    PMEMoid log_addrs[64];
};

void multithreaded (char** argv) {
    std::cout << "multi threaded:" << std::endl;

    std::vector<std::thread> workers;

    const auto processor_count = std::thread::hardware_concurrency ();
    printf ("Core %u\n", processor_count);
    uint16_t log_count = 40;
    size_t kLogSize = 10 * 1024 * 1024 * 1024LU;  // 10G

    std::string filepath = "/mnt/pmem/testlog";
    remove (filepath.c_str ());
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

    uint64_t num = std::atoll (argv[1]);

    printf ("operation,n,Mops/s\n");

    {
        size_t num_per_thread = num / log_count;
        size_t log_size = 33 * num_per_thread * 1.2;
        // create new log and append data to them
        root->num = log_count;
        for (int i = 0; i < log_count; i++) {
            // alloc log space
            int ret = pmemobj_alloc (pop, &root->log_addrs[i], log_size, 0, NULL, NULL);
            if (ret) {
                std::cerr << "alloc error for log " << i << std::endl;
                exit (1);
            }
        }

        auto starttime = std::chrono::system_clock::now ();
        for (int i = 0; i < log_count; i++) {
            workers.emplace_back (std::thread ([&root, log_size, num_per_thread, i] () {
                // register current log address
                void* log_base_addr = pmemobj_direct (root->log_addrs[i]);
                spoton::Log_t::RegisterThreadLocalLog ((char*)(log_base_addr), log_size);

                // Append 10 records
                auto* log_ptr = spoton::Log_t::GetThreadLocalLog ();
                spoton::logptr_t ptr;
                for (size_t r = 0; r < num_per_thread; r++) {
                    ptr = log_ptr->Append (spoton::kLogNodeValid, r, r, ptr);
                }
                log_ptr->CommitTail ();
            }));
        }
        std::for_each (workers.begin (), workers.end (), [] (std::thread& t) { t.join (); });

        auto duration = std::chrono::duration_cast<std::chrono::microseconds> (
            std::chrono::system_clock::now () - starttime);
        printf ("insert,%ld,%f\n", num, (num * 1.0) / duration.count ());
    }
}

int main (int argc, char** argv) {
    if (argc != 2) {
        printf ("usage: %s n\nn: number of keys\n", argv[0]);
        return 1;
    }

    multithreaded (argv);

    return 0;
}