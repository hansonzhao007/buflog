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
DEFINE_double (loadfactor, 0.7, "");

#ifdef CACHELINE1
#define kNumCacheLinePerBucket 1
#elif defined(CACHELINE2)
#define kNumCacheLinePerBucket 2
#elif defined(CACHELINE3)
#define kNumCacheLinePerBucket 3
#elif defined(CACHELINE4)
#define kNumCacheLinePerBucket 4
#elif defined(CACHELINE5)
#define kNumCacheLinePerBucket 5
#elif defined(CACHELINE6)
#define kNumCacheLinePerBucket 6
#elif defined(CACHELINE7)
#define kNumCacheLinePerBucket 7
#elif defined(CACHELINE8)
#define kNumCacheLinePerBucket 8
#endif

size_t PMEM_SPACE = 32 * 1024 * 1024 * 1024LU;  // 10G

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
    log_pop = pmemobj_create (filepath.c_str (), POBJ_LAYOUT_NAME (hash), PMEM_SPACE * 1.2, 0666);
    if (log_pop == nullptr) {
        std::cerr << "create log file fail. " << filepath << std::endl;
        exit (1);
    }

    PMEMoid g_root = pmemobj_root (log_pop, sizeof (HashRoot));
    root = reinterpret_cast<HashRoot*> (pmemobj_direct (g_root));
    size_t log_size = 10LU * 1024 * 1024 * 1024;
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
    size_t kBucketCount = FLAGS_num / FLAGS_loadfactor / kKeyPerBucket * 1.2;
    size_t kInsertPerBucket = kKeyPerBucket;

    printf ("bucket size: %d\n", sizeof (HashTable<kNumCacheLinePerBucket>::Bucket));
    printf ("key per bucket: %ld\n", kKeyPerBucket);
    printf ("bucket count: %ld\n", kBucketCount);
    printf ("insert per bucket for seq: %d\n", kInsertPerBucket);

    char* log_base_addr = reinterpret_cast<char*> ((size_t) (pmemobj_direct (root->log_addrs[0])) &
                                                   (~0xFFLU));  // aligned to 256 byte
    HashTable<kNumCacheLinePerBucket> hashtable_rnd (kBucketCount, log_base_addr);

    log_base_addr = reinterpret_cast<char*> ((size_t) (pmemobj_direct (root->log_addrs[1])) &
                                             (~0xFFLU));  // aligned to 256 byte
    HashTable<kNumCacheLinePerBucket> hashtable_seq (kBucketCount, log_base_addr);

    pmem_drain ();

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
        pmem_drain ();
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

        std::random_device rd;
        std::mt19937 g (rd ());
        std::shuffle (buckets.begin (), buckets.end (), g);

        size_t bi = 0;
        size_t fail = 0;
        while (i < FLAGS_num) {
            for (int r = 0; r < kInsertPerBucket; r++) {
                bool res = hashtable_seq.insert (i + 1, i++, buckets[bi]);
                if (!res) {
                    fail++;
                }
            }
            bi++;
        }
        pmem_drain ();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds> (
            std::chrono::system_clock::now () - starttime);
        printf ("throughput: %f Mops/s (load factor: %f, ops: %lu. fail: %lu)\n",
                (double)(i) / duration.count (), (double)i / hashtable_seq.getCapacity (), i, fail);
    }

    {
        void* log_base_addr = pmemobj_direct (root->log_addrs[1]);
        pmemobj_memset_persist (log_pop, log_base_addr, 0, log_size);
        pmem_drain ();

        util::IPMWatcher watcher ("seq_insert_batch");
        auto starttime = std::chrono::system_clock::now ();
        size_t i = 0;

        std::vector<size_t> buckets (kBucketCount);
        for (int i = 0; i < kBucketCount; i++) {
            buckets[i] = i;
        }

        std::random_device rd;
        std::mt19937 g (rd ());
        std::shuffle (buckets.begin (), buckets.end (), g);

        size_t bi = 0;
        while (i < FLAGS_num) {
            spoton::HashTable<kNumCacheLinePerBucket>::Bucket tmp;
            memcpy ((void*)&tmp, (void*)hashtable_seq.getBucket (buckets[bi]),
                    sizeof (spoton::HashTable<kNumCacheLinePerBucket>::Bucket));
            for (int r = 0; r < kInsertPerBucket; r++) {
                tmp.insert (i + 1, i++);
            }
            hashtable_seq.insert (tmp, buckets[bi]);
            bi++;
        }
        pmem_drain ();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds> (
            std::chrono::system_clock::now () - starttime);
        printf ("throughput: %f Mops/s (load factor: %f, ops: %lu)\n",
                (double)(i) / duration.count (), (double)i / hashtable_seq.getCapacity (), i);
    }

    return 0;
}