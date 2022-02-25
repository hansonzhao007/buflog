#include "var_hash.h"

#include <gflags/gflags.h>
#include <libpmemobj.h>

#include <iostream>

#include "test_util.h"

using GFLAGS_NAMESPACE::ParseCommandLineFlags;
using GFLAGS_NAMESPACE::RegisterFlagValidator;
using GFLAGS_NAMESPACE::SetUsageMessage;

using namespace spoton;

DEFINE_uint64 (num, 10 * 1000000LU, "Number of total record");
DEFINE_double (loadfactor, 0.75, "");

#define kNumCacheLinePerBucket 8

size_t PMEM_SPACE = 10 * 1024 * 1024 * 1024LU;  // 10G

struct HashRoot {
    size_t num;
    PMEMoid log_addrs[2];
};

HashRoot* root;
PMEMobjpool* log_pop;

int main (int argc, char* argv[]) {
    for (int i = 0; i < argc; i++) {
        printf ("%s ", argv[i]);
    }
    printf ("\n");
    ParseCommandLineFlags (&argc, &argv, true);

    std::string filepath = "/mnt/pmem/testhash";
    remove (filepath.c_str ());
    log_pop = pmemobj_create (filepath.c_str (), POBJ_LAYOUT_NAME (hash), PMEM_SPACE * 1.6, 0666);
    if (log_pop == nullptr) {
        std::cerr << "create log file fail. " << filepath << std::endl;
        exit (1);
    }

    PMEMoid g_root = pmemobj_root (log_pop, sizeof (HashRoot));
    root = reinterpret_cast<HashRoot*> (pmemobj_direct (g_root));
    size_t log_size = PMEM_SPACE / 2;
    for (int i = 0; i < 2; i++) {
        int ret = pmemobj_alloc (log_pop, &root->log_addrs[i], log_size, 0, NULL, NULL);
        if (ret) {
            printf ("alloc error for log");
            exit (1);
        }
        void* log_base_addr = pmemobj_direct (root->log_addrs[i]);
        pmemobj_memset_persist (log_pop, log_base_addr, 0, log_size);
        _mm_sfence ();
    }

    size_t kKeyPerBucket = HashTable<kNumCacheLinePerBucket>::KeyPerBucket;
    size_t kBucketCount = FLAGS_num / FLAGS_loadfactor / kKeyPerBucket * 1.05;
    size_t kInsertPerBucket = kKeyPerBucket;

    printf ("bucket size: %d\n", sizeof (HashTable<kNumCacheLinePerBucket>::Bucket));
    printf ("key per bucket: %ld\n", kKeyPerBucket);
    printf ("bucket count: %ld\n", kBucketCount);
    printf ("insert per bucket for seq: %d\n", kInsertPerBucket);

    HashTable<kNumCacheLinePerBucket> hashtable_rnd (kBucketCount,
                                                     (char*)pmemobj_direct (root->log_addrs[0]));

    HashTable<kNumCacheLinePerBucket> hashtable_seq (kBucketCount,
                                                     (char*)pmemobj_direct (root->log_addrs[1]));

    {
        util::IPMWatcher watcher ("random_insert");
        auto starttime = std::chrono::system_clock::now ();
        for (size_t i = 0; i < FLAGS_num; i++) {
            bool res = hashtable_rnd.insert (i + 1, i);
            if (!res) {
                printf ("Hash rnd overflow. inserted: %lu, load factor: %f\n", i,
                        (double)i / hashtable_rnd.getCapacity ());
                exit (1);
            }
        }
        auto duration = std::chrono::duration_cast<std::chrono::microseconds> (
            std::chrono::system_clock::now () - starttime);
        printf ("throughput: %f Mops/s (load factor: %f, ops: %lu)\n",
                (double)(FLAGS_num) / duration.count (),
                (double)FLAGS_num / hashtable_rnd.getCapacity (), FLAGS_num);
    }

    {
        util::IPMWatcher watcher ("seq_insert");
        auto starttime = std::chrono::system_clock::now ();
        size_t i = 0;

        std::vector<size_t> buckets (kBucketCount);
        for (int i = 0; i < kBucketCount; i++) {
            buckets[i] = i;
        }
        std::random_shuffle (buckets.begin (), buckets.end ());

        size_t bucketi = 0;
        while (i < FLAGS_num) {
            for (int r = 0; r < kInsertPerBucket; r++) {
                bool res = hashtable_seq.insert (i + 1, i++, buckets[bucketi]);
                if (!res) {
                    printf ("Hash seq overflow. inserted: %lu, load factor: %f\n", i,
                            (double)i / hashtable_seq.getCapacity ());
                    exit (1);
                }
            }
            bucketi++;
        }
        auto duration = std::chrono::duration_cast<std::chrono::microseconds> (
            std::chrono::system_clock::now () - starttime);
        printf ("throughput: %f Mops/s (load factor: %f, ops: %lu)\n",
                (double)(FLAGS_num) / duration.count (),
                (double)FLAGS_num / hashtable_rnd.getCapacity (), FLAGS_num);
    }

    return 0;
}