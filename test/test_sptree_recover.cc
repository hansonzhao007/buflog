// #include "FAST_FAIR/btree_pmem_buflog.h"
#include <libpmem.h>

#include <iostream>

#include "gflags/gflags.h"
#include "spoton-tree/sptree.h"
#include "test_util.h"
// ralloc
#include "pptr.hpp"
#include "ralloc.hpp"
using GFLAGS_NAMESPACE::ParseCommandLineFlags;
using GFLAGS_NAMESPACE::RegisterFlagValidator;
using GFLAGS_NAMESPACE::SetUsageMessage;

util::RandomGenerator gen;
using namespace spoton;

// For hash table
DEFINE_uint32 (num, 10000, "number of input");
DEFINE_string (mode, "insert", "insert, recover");
DEFINE_uint32 (valuesize, 32, "value size");
DEFINE_bool (print, true, "print tree or not");
DEFINE_int32 (key, 52, "");
int main (int argc, char* argv[]) {
    ParseCommandLineFlags (&argc, &argv, true);

    struct timespec start, end;

    util::RandomKeyTrace trace (FLAGS_num, true);

    SPTree* tree = nullptr;

    if (FLAGS_mode == "insert") {
        SPTree::DistroySPTree ();
        tree = SPTree::CreateSPTree (false);

        {
            util::IPMWatcher watcher ("sptreeInsert");
            clock_gettime (CLOCK_MONOTONIC, &start);
            auto key_iter = trace.Begin ();
            for (int i = 0; i < (int)FLAGS_num; ++i) {
                size_t key = key_iter.Next ();
                tree->insert (key, key);
            }
            clock_gettime (CLOCK_MONOTONIC, &end);
            long long elapsedTime =
                (end.tv_sec - start.tv_sec) * 1000000000 + (end.tv_nsec - start.tv_nsec);
            std::cout << "Insert " << FLAGS_num << " keys. time(uses) : " << elapsedTime / 1000
                      << std::endl;
        }

        {
            util::IPMWatcher watcher ("sptreeRread");
            clock_gettime (CLOCK_MONOTONIC, &start);
            size_t res = 0;
            int found = 0;
            auto key_iter2 = trace.Begin ();
            for (int i = 0; i < (int)FLAGS_num; ++i) {
                size_t key = key_iter2.Next ();
                res = tree->lookup (key);
                if (res != 0) {
                    found++;
                } else {
                    printf ("%d th key not found\n", i);
                }
            }
            clock_gettime (CLOCK_MONOTONIC, &end);
            long long elapsedTime =
                (end.tv_sec - start.tv_sec) * 1000000000 + (end.tv_nsec - start.tv_nsec);
            std::cout << "Search " << FLAGS_num << " keys. Found " << found
                      << ". time(uses) : " << elapsedTime / 1000 << std::endl;
        }
        if (FLAGS_print) {
            printf ("%s\n", tree->ToStats ().c_str ());
        }
    } else if (FLAGS_mode == "recover") {
        auto starttime = std::chrono::system_clock::now ();
        tree = SPTree::RecoverSPTree ();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds> (
            std::chrono::system_clock::now () - starttime);
        printf ("recover duration %f s.\n", duration.count () / 1000000.0);

        if (FLAGS_print) {
            printf ("%s\n", tree->ToStats ().c_str ());
        }

        size_t res = 0;
        int found = 0;
        auto key_iter = trace.Begin ();
        clock_gettime (CLOCK_MONOTONIC, &start);
        for (int i = 0; i < (int)FLAGS_num; ++i) {
            size_t key = key_iter.Next ();
            res = tree->lookup (key);
            if (res != 0) {
                found++;
            }
        }
        clock_gettime (CLOCK_MONOTONIC, &end);
        auto elapsedTime = (end.tv_sec - start.tv_sec) * 1000000000 + (end.tv_nsec - start.tv_nsec);
        std::cout << "Recover Search " << FLAGS_num << " keys. Found " << found
                  << ". time(uses) : " << elapsedTime / 1000 << std::endl;
    }
    return 0;
}
