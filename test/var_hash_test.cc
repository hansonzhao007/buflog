#include "var_hash.h"

#include <gflags/gflags.h>

#include <iostream>

// ralloc
#include "pptr.hpp"
#include "ralloc.hpp"
#include "test_util.h"

using GFLAGS_NAMESPACE::ParseCommandLineFlags;
using GFLAGS_NAMESPACE::RegisterFlagValidator;
using GFLAGS_NAMESPACE::SetUsageMessage;

using namespace spoton;

DEFINE_uint64 (num, 10 * 1000000LU, "Number of total record");
DEFINE_double (loadfactor, 0.75, "");

#define kNumCacheLinePerBucket 10

size_t PMEM_SPACE = 10 * 1024 * 1024 * 1024LU;  // 10G

struct HashRoot {
    size_t num;
    pptr<char> log_addrs[2];
};

HashRoot* root;

int main (int argc, char* argv[]) {
    for (int i = 0; i < argc; i++) {
        printf ("%s ", argv[i]);
    }
    printf ("\n");
    ParseCommandLineFlags (&argc, &argv, true);

    remove ("/mnt/pmem/hashtest_sb");
    remove ("/mnt/pmem/hashtest_desc");
    remove ("/mnt/pmem/hashtest_basemd");

    bool res = RP_init ("hashtest", PMEM_SPACE * 1.6);
    if (res) {
        root = RP_get_root<HashRoot> (0);
        int recover_res = RP_recover ();
        if (recover_res == 1) {
            printf ("Ralloc Dirty open, recover\n");
        } else {
            printf ("Ralloc Clean restart\n");
        }
    } else {
        printf ("Ralloc Clean create\n");
    }
    void* tmp = RP_malloc (sizeof (HashRoot));
    RP_set_root (tmp, 0);

    size_t log_size = PMEM_SPACE / 2;
    for (int i = 0; i < 2; i++) {
        char* tmp = static_cast<char*> (RP_malloc (log_size));
        root->log_addrs[i] = tmp;
        void* log_base_addr = (root->log_addrs[i]);
        memset (log_base_addr, 0, log_size);
        FLUSH (log_base_addr);
        FLUSHFENCE;
    }

    size_t kKeyPerBucket = HashTable<kNumCacheLinePerBucket>::KeyPerBucket;
    size_t kBucketCount = FLAGS_num / FLAGS_loadfactor / kKeyPerBucket * 1.05;
    size_t kInsertPerBucket = kKeyPerBucket * FLAGS_loadfactor;

    printf ("bucket size: %d\n", sizeof (HashTable<kNumCacheLinePerBucket>::Bucket));
    printf ("key per bucket: %ld\n", kKeyPerBucket);
    printf ("bucket count: %ld\n", kBucketCount);
    printf ("insert per bucket for seq: %d\n", kInsertPerBucket);

    char* addr1 = root->log_addrs[0];
    HashTable<kNumCacheLinePerBucket> hashtable_rnd (kBucketCount, addr1);

    char* addr2 = root->log_addrs[1];
    HashTable<kNumCacheLinePerBucket> hashtable_seq (kBucketCount, addr2);

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